#!/usr/bin/env python3
import os
import subprocess
import glob
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
COMPILER = os.path.join(SCRIPT_DIR, "../build/compiler")
TEST_DIR = os.path.join(SCRIPT_DIR, "../test/h_functional")
CHECKLIST = os.path.join(SCRIPT_DIR, "../checklist.md")
TEST_S = os.path.join(SCRIPT_DIR, "test.s")
TEST_LIB = os.path.join(SCRIPT_DIR, "../lib/libsysy.a")
TEST_BIN = os.path.join(SCRIPT_DIR, "test")
TEST_OUT = os.path.join(SCRIPT_DIR, "test.out")
TEST_IN = os.path.join(SCRIPT_DIR, "test.in")

def get_test_num(filename):
    """Extract the numeric prefix from a test filename like 00_hello.sy -> '00'"""
    basename = os.path.basename(filename)
    parts = basename.split("_", 1)
    if parts[0].isdigit():
        return parts[0].zfill(2)
    return None

def run(cmd, **kwargs):
    result = subprocess.run(cmd, shell=True, capture_output=True, text=False, **kwargs)
    result.stdout = result.stdout.decode('utf-8', errors='replace')
    result.stderr = result.stderr.decode('utf-8', errors='replace')
    return result

def main():
    sy_files = sorted(glob.glob(os.path.join(TEST_DIR, "*.sy")))
    if not sy_files:
        print(f"No .sy files found in {TEST_DIR}")
        # sys.exit(1)

    results = []

    for sy_file in sy_files:
        num = get_test_num(sy_file)
        if num is None:
            print(f"Skipping {sy_file}: cannot determine test number")
            continue

        # Step 1: Compile
        compile_cmd = f"{COMPILER} {sy_file} > {TEST_S}"
        comp_result = run(compile_cmd)

        if comp_result.returncode != 0:
            msg = f"task {num} : CE"
            print(msg)
            results.append(msg)
            # Write partial checklist and stop
            write_checklist(results)
            # sys.exit(1)

        # Step 2: Assemble & link
        build_cmd = f"g++ {TEST_S} {TEST_LIB} -o {TEST_BIN}"
        build_result = run(build_cmd)

        if build_result.returncode != 0:
            msg = f"task {num} : CE"
            print(msg)
            results.append(msg)
            write_checklist(results)
            # sys.exit(1)

        # Step 3: Run and capture output + exit code
        input_file = os.path.splitext(sy_file)[0] + ".in"
        test_input = ""
        if os.path.exists(input_file):
            with open(input_file, "r") as f:
                test_input = f.read()
        with open(TEST_IN, "w") as f:
            f.write(test_input)

        try:
            exec_result = run(TEST_BIN, input=test_input.encode('utf-8'), timeout=5)
        except subprocess.TimeoutExpired:
            msg = f"task {num} : TLE (timeout)"
            print(msg)
            results.append(msg)
            write_checklist(results)
            # sys.exit(1)
        output_lines = exec_result.stdout
        exit_code = exec_result.returncode

        # Write test.out: stdout + exit code as last line
        with open(TEST_OUT, "w") as f:
            f.write(output_lines)
            if not output_lines.endswith("\n") and output_lines:
                f.write("\n")
            f.write(f"{exit_code}\n")

        # Step 4: Compare with expected .out
        expected_out = os.path.splitext(sy_file)[0] + ".out"
        if not os.path.exists(expected_out):
            print(f"task {num} : SKIP (no expected .out file)")
            results.append(f"task {num} : SKIP")
            continue

        with open(expected_out, "r") as f:
            expected = f.read()

        with open(TEST_OUT, "r") as f:
            actual = f.read()

        # 按行分割，保留每行内容（包括空行），不自动去除空白
        expected_lines = expected.splitlines()
        actual_lines = actual.splitlines()

        # 去除末尾的空行（如果存在），便于比较
        while expected_lines and expected_lines[-1] == "":
            expected_lines.pop()
        while actual_lines and actual_lines[-1] == "":
            actual_lines.pop()

        if expected_lines == actual_lines:
            msg = f"task {num} : completed"
            print(msg)
            results.append(msg)
        else:
            # 输出详细信息帮助调试
            msg = f"task {num} : WA"
            print(msg)
            print(f"  Expected lines: {expected_lines}")
            print(f"  Actual lines:   {actual_lines}")
            results.append(msg)
            write_checklist(results)
            # sys.exit(1)

    write_checklist(results)
    print(f"\nAll tests done. Results written to {CHECKLIST}")

def write_checklist(results):
    with open(CHECKLIST, "w") as f:
        f.write("# Test Checklist\n\n")
        for line in results:
            f.write(f"- {line}\n")

if __name__ == "__main__":
    main()