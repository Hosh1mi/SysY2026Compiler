import os
import subprocess
import sys
import re
from pathlib import Path

def parse_total_time(time_str):
    if not time_str:
        return 0.0
    try:
        match = re.match(r'(\d+)H-(\d+)M-(\d+)S-(\d+)us', time_str)
        if match:
            hours = int(match.group(1))
            minutes = int(match.group(2))
            seconds = int(match.group(3))
            microseconds = int(match.group(4))
            return hours * 3600 + minutes * 60 + seconds + microseconds / 1_000_000
    except (ValueError, AttributeError):
        pass
    return 0.0

def main():
    script_dir = Path(__file__).resolve().parent
    os.chdir(script_dir)

    perf_dir = Path("performance")
    if not perf_dir.is_dir():
        print("错误：找不到 performance/ 目录", file=sys.stderr)
        sys.exit(1)

    results = []
    sy_files = sorted(perf_dir.glob("*.sy"))

    total_ac = 0
    total_wa = 0
    total_tle = 0
    total_re = 0
    total_time = 0.0  

    for sy_file in sy_files:
        stem = sy_file.stem
        out_file = perf_dir / f"{stem}.out"
        in_file = perf_dir / f"{stem}.in"

        with open(out_file, "r", encoding="utf-8") as f:
            expected_lines = [line.rstrip("\n") for line in f]

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
                total_re += 1
                continue
        except Exception:
            results.append(f"{stem} : RE")
            total_re += 1
            continue

        link = subprocess.run(
            ["g++", "out.s", "../lib/libsysy.a", "-o", "out", "-O1"],
            capture_output=True,
            text=True,
        )
        if link.returncode != 0:
            results.append(f"{stem} : RE")
            total_re += 1
            continue

        stdin_content = None
        if in_file.exists():
            with open(in_file, "r", encoding="utf-8") as f:
                stdin_content = f.read()

        try:
            exec_proc = subprocess.run(
                ["./out"],
                input=stdin_content,
                capture_output=True,
                text=True,
                timeout=30,
            )
        except subprocess.TimeoutExpired:
            results.append(f"{stem} : TLE")
            total_tle += 1
            total_time += 60.0
            continue
        except Exception:
            results.append(f"{stem} : RE")
            total_re += 1
            continue

        if exec_proc.returncode < 0:
            results.append(f"{stem} : RE")
            total_re += 1
            continue

        actual_stdout_lines = exec_proc.stdout.splitlines()
        actual_retcode = str(exec_proc.returncode)
        actual_combined = actual_stdout_lines + [actual_retcode]

        total_time_str = None
        # ---- 修改开始 ----
        for line in exec_proc.stderr.splitlines():
            line_stripped = line.strip()
            # 不区分大小写匹配 "total" 开头的行，例如 "TOTAL:" 或 "Total time:"
            if line_stripped.lower().startswith("total"):
                if ":" in line_stripped:
                    total_time_str = line_stripped.split(":", 1)[1].strip()
                break
        # ---- 修改结束 ----

        case_time = parse_total_time(total_time_str)

        time_str = f"Total time: {total_time_str}" if total_time_str else "Total time: N/A"

        if actual_combined == expected_lines:
            results.append(f"{stem} : AC\n{time_str}")
            total_ac += 1
            total_time += case_time
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
            total_wa += 1
            total_time += case_time

    total_tests = total_ac + total_wa + total_tle + total_re
    summary = [
        "--- Summary ---",
        f"Total tests: {total_tests}",
        f"AC: {total_ac}",
        f"WA: {total_wa}",
        f"TLE: {total_tle}",
        f"RE: {total_re}",
        f"Total run time: {total_time:.6f} s",
    ]
    results.extend(summary)

    with open("result.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")

if __name__ == "__main__":
    main()