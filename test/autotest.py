#!/usr/bin/env python3
"""
性能测试脚本：对 performance/ 目录下的测试用例进行编译、运行和结果比对，
并将结果写入同目录下的 result.txt。
"""

import os
import subprocess
import sys
from pathlib import Path


def main():
    # 切换到脚本所在目录，保证后续相对路径有效
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    perf_dir = Path("performance")
    if not perf_dir.is_dir():
        print("错误：找不到 performance/ 目录", file=sys.stderr)
        sys.exit(1)

    results = []
    sy_files = sorted(perf_dir.glob("*.sy"))

    for sy_file in sy_files:
        stem = sy_file.stem
        out_file = perf_dir / f"{stem}.out"
        in_file = perf_dir / f"{stem}.in"

        # 读取期望输出（.out 文件的全部行，最后一行是预期返回值）
        with open(out_file, "r", encoding="utf-8") as f:
            expected_lines = [line.rstrip("\n") for line in f]

        # 1. 编译：../build/compiler <filename>.sy -> out.s
        try:
            with open("out.s", "w") as out_f:
                comp = subprocess.run(
                    ["../build/compiler", str(sy_file)],
                    stdout=out_f,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            if comp.returncode != 0:
                results.append(f"{stem} : RE")
                continue
        except Exception:
            results.append(f"{stem} : RE")
            continue

        # 2. 链接：g++ out.s ../lib/libsysy.a -o out -O1
        link = subprocess.run(
            ["g++", "out.s", "../lib/libsysy.a", "-o", "out", "-O1"],
            capture_output=True,
            text=True,
        )
        if link.returncode != 0:
            results.append(f"{stem} : RE")
            continue

        # 3. 运行，如有 .in 则提供标准输入
        stdin_content = None
        if in_file.exists():
            with open(in_file, "r", encoding="utf-8") as f:
                stdin_content = f.read() 

        try:
            # 可选超时（如 30 秒），可根据需要调整
            exec_proc = subprocess.run(
                ["./out"],
                input=stdin_content,
                capture_output=True,
                text=True,
                timeout=30,
            )
        except subprocess.TimeoutExpired:
            results.append(f"{stem} : RE")
            continue
        except Exception:
            results.append(f"{stem} : RE")
            continue

        # 崩溃（信号终止）视为 RE
        if exec_proc.returncode < 0:
            results.append(f"{stem} : RE")
            continue

        # 构建实际输出：标准输出的各行 + 最后一行是程序返回值（转为字符串）
        actual_stdout_lines = exec_proc.stdout.splitlines()
        actual_retcode = str(exec_proc.returncode)
        actual_combined = actual_stdout_lines + [actual_retcode]

        # 提取总时间 TOTAL: ...（来自标准错误输出）
        total_time = None
        for line in exec_proc.stderr.splitlines():
            if line.startswith("TOTAL:"):
                total_time = line[len("TOTAL:"):].strip()
        time_str = f"Total time : {total_time}" if total_time else "Total time : N/A"

        # 4. 结果判定
        if actual_combined == expected_lines:
            results.append(f"{stem} : AC\n{time_str}")
        else:
            # 寻找第一个不匹配的位置
            mismatch_msg = ""
            max_lines = max(len(actual_combined), len(expected_lines))
            for i in range(max_lines):
                exp = expected_lines[i] if i < len(expected_lines) else None
                act = actual_combined[i] if i < len(actual_combined) else None
                if exp != act:
                    if exp is None:
                        mismatch_msg = f"Line {i+1}: expected nothing, got '{act}'"
                    elif act is None:
                        mismatch_msg = f"Line {i+1}: expected '{exp}', got nothing"
                    else:
                        mismatch_msg = f"Line {i+1}: expected '{exp}', got '{act}'"
                    break
            results.append(f"{stem} : WA,\nFirst mismatch : {mismatch_msg}\n{time_str}")

    # 写入最终结果
    with open("result.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")


if __name__ == "__main__":
    main()