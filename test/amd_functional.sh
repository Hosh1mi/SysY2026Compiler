#!/bin/bash
# amd_functional.sh — 交叉编译环境 (aarch64-linux-gnu-gcc + qemu), 测试 functional + h_functional
# 输出: test/results/result_functional.txt

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJ_DIR/build"
RESULT_DIR="$PROJ_DIR/test/results"
RESULT_FILE="$RESULT_DIR/result_functional.txt"
LIB_DIR="$PROJ_DIR/lib"
TEST_DIRS=("$PROJ_DIR/test/functional" "$PROJ_DIR/test/h_functional")

mkdir -p "$RESULT_DIR"

if [ ! -f "$BUILD_DIR/compiler" ]; then
    echo "Error: compiler not found at $BUILD_DIR/compiler"
    echo "Run 'cd build && cmake .. && make -j4' first."
    exit 1
fi

cd "$BUILD_DIR"

RED=''; GREEN=''; RESET=''
if [ -t 1 ]; then
    RED=$(tput setaf 1 2>/dev/null || true)
    GREEN=$(tput setaf 2 2>/dev/null || true)
    RESET=$(tput sgr0 2>/dev/null || true)
fi

> "$RESULT_FILE"

PASS=0; FAIL=0; CRASH=0; TOTAL=0

for TEST_DIR in "${TEST_DIRS[@]}"; do
    dir_name=$(basename "$TEST_DIR")
    echo "========== $dir_name =========="

    for sy in "$TEST_DIR"/*.sy; do
        [ ! -f "$sy" ] && continue
        base=$(basename "$sy" .sy)
        expfile="$TEST_DIR/${base}.out"
        [ ! -f "$expfile" ] && continue
        TOTAL=$((TOTAL + 1))

        infile="$TEST_DIR/${base}.in"
        exp=$(cat "$expfile")
        num=$(echo "$base" | grep -oE '^[0-9]+' || echo "$base")
        name=$(echo "$base" | sed 's/^[0-9]*_//')

        # 1. SysY -> ARM64 asm
        ./compiler -S "$sy" -o "/tmp/${base}.s" -O1 2>/dev/null
        if [ $? -ne 0 ]; then
            echo "${RED}CE${RESET} [$dir_name] ${num} ${name}"
            echo "[$dir_name] $base : CE" >> "$RESULT_FILE"
            FAIL=$((FAIL + 1))
            continue
        fi

        # 2. Cross-assemble + static link
        aarch64-linux-gnu-gcc "/tmp/${base}.s" -o "/tmp/${base}.elf" \
            -L "$LIB_DIR" -lsysy -static 2>/dev/null
        if [ $? -ne 0 ]; then
            echo "${RED}LE${RESET} [$dir_name] ${num} ${name}"
            echo "[$dir_name] $base : LE" >> "$RESULT_FILE"
            FAIL=$((FAIL + 1))
            continue
        fi

        # 3. Run with qemu
        if [ -f "$infile" ]; then
            outtext=$(qemu-aarch64 "/tmp/${base}.elf" < "$infile" 2>/dev/null)
        else
            outtext=$(qemu-aarch64 "/tmp/${base}.elf" 2>/dev/null)
        fi
        exitcode=$?

        # 4. Format result
        if [ -z "$outtext" ]; then
            result="$exitcode"
        else
            result=$(printf "%s\n%d" "$outtext" $exitcode)
        fi

        # 5. Compare
        if [ "$result" = "$exp" ]; then
            echo "${GREEN}PASS${RESET} [$dir_name] ${num} ${name}"
            echo "[$dir_name] $base : AC" >> "$RESULT_FILE"
            PASS=$((PASS + 1))
        elif [ $exitcode -ge 128 ]; then
            sig=$((exitcode - 128))
            echo "${RED}SIG${sig}${RESET} [$dir_name] ${num} ${name}"
            echo "[$dir_name] $base : SIG$sig" >> "$RESULT_FILE"
            CRASH=$((CRASH + 1))
        else
            echo "${RED}FAIL${RESET} [$dir_name] ${num} ${name}"
            echo "  expected: '$(echo "$exp" | tr '\n' '|')'"
            echo "  got:      '$(echo "$result" | tr '\n' '|')'"
            echo "[$dir_name] $base : WA" >> "$RESULT_FILE"
            FAIL=$((FAIL + 1))
        fi

        rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
    done
done

echo ""
echo "========================================="
echo "  TOTAL: $TOTAL  ${GREEN}PASS${RESET}: $PASS  ${RED}FAIL${RESET}: $FAIL  ${RED}CRASH${RESET}: $CRASH"
echo "========================================="
echo "" >> "$RESULT_FILE"
echo "TOTAL: $TOTAL  PASS: $PASS  FAIL: $FAIL  CRASH: $CRASH" >> "$RESULT_FILE"
echo "Results written to: $RESULT_FILE"
