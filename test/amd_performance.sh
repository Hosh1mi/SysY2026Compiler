#!/bin/bash
# amd_performance.sh — 交叉编译环境 (aarch64-linux-gnu-gcc + qemu), 测试 performance (含计时)
# 输出: test/results/result_performance.txt

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJ_DIR/build"
RESULT_DIR="$PROJ_DIR/test/results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$PROJ_DIR/test/results/result_performance_${TIMESTAMP}.txt"
LIB_DIR="$PROJ_DIR/lib"
TEST_DIR="$PROJ_DIR/test/performance"

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

format_time() {
    local total_us=$1
    local us=$((total_us % 1000000))
    local total_s=$((total_us / 1000000))
    local s=$((total_s % 60))
    local total_m=$((total_s / 60))
    local m=$((total_m % 60))
    local h=$((total_m / 60))
    printf "%dH-%dM-%dS-%dus" $h $m $s $us
}

> "$RESULT_FILE"

PASS=0; FAIL=0; TOTAL=0

echo "========== Performance Tests =========="

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
        echo "${RED}CE${RESET} ${num} ${name}"
        echo "$base : CE" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
        continue
    fi

    # 2. Cross-assemble + static link
    aarch64-linux-gnu-gcc "/tmp/${base}.s" -o "/tmp/${base}.elf" \
        -L "$LIB_DIR" -lsysy -static 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "${RED}LE${RESET} ${num} ${name}"
        echo "$base : LE" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
        continue
    fi

    # 3. Run with qemu + timing
    start_ns=$(date +%s%N 2>/dev/null)
    if [ -f "$infile" ]; then
        outtext=$(qemu-aarch64 "/tmp/${base}.elf" < "$infile" 2>/dev/null)
    else
        outtext=$(qemu-aarch64 "/tmp/${base}.elf" 2>/dev/null)
    fi
    exitcode=$?
    end_ns=$(date +%s%N 2>/dev/null)
    elapsed_us=$(((end_ns - start_ns) / 1000))

    # 4. Format result
    if [ -z "$outtext" ]; then
        result="$exitcode"
    else
        result=$(printf "%s\n%d" "$outtext" $exitcode)
    fi

    # 5. Compare
    if [ "$result" = "$exp" ]; then
        echo "${GREEN}AC${RESET} ${num} ${name}  $(format_time $elapsed_us)"
        echo "$base : AC" >> "$RESULT_FILE"
        echo "Total time : $(format_time $elapsed_us)" >> "$RESULT_FILE"
        PASS=$((PASS + 1))
    elif [ $exitcode -ge 128 ]; then
        sig=$((exitcode - 128))
        echo "${RED}SIG${sig}${RESET} ${num} ${name}"
        echo "$base : SIG$sig" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
    else
        echo "${RED}WA${RESET} ${num} ${name}"
        echo "  expected: '$(echo "$exp" | tr '\n' '|')'"
        echo "  got:      '$(echo "$result" | tr '\n' '|')'"
        echo "$base : WA" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
    fi

    rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
done

echo ""
echo "========================================="
echo "  TOTAL: $TOTAL  ${GREEN}AC${RESET}: $PASS  ${RED}FAIL${RESET}: $FAIL"
echo "========================================="
echo "" >> "$RESULT_FILE"
echo "TOTAL: $TOTAL  AC: $PASS  FAIL: $FAIL" >> "$RESULT_FILE"
echo "Results written to: $RESULT_FILE"
