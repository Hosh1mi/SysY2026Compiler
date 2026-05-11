#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJ_DIR/build"
FUNC_DIR="$PROJ_DIR/test/functional"
PERF_DIR="$PROJ_DIR/test/performance"
RESULT_FILE="$PROJ_DIR/test/result.txt"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_ARCHIVE="$PROJ_DIR/test/results/result_${TIMESTAMP}.txt"
mkdir -p "$PROJ_DIR/test/results"
LIB_DIR="$PROJ_DIR/lib"

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

run_test() {
    local sy=$1
    local base=$2
    local dir=$3
    local timing=$4

    local infile="$dir/${base}.in"
    local expfile="$dir/${base}.out"

    [ ! -f "$expfile" ] && return 1

    local exp=$(cat "$expfile")
    local num=$(echo "$base" | grep -oE '^[0-9]+' || echo "$base")
    local name=$(echo "$base" | sed 's/^[0-9]*_//')

    # 1. SysY -> native asm
    local asm_msg=$(./compiler -S "$sy" -o "/tmp/${base}.s" -O1 2>&1)
    local compile_rc=$?
    if [ $compile_rc -ne 0 ]; then
        echo "${RED}CE${RESET} ${num} ${name}"
        [ -n "$timing" ] && echo "$base : CE" >> "$RESULT_FILE"
        return 2
    fi

    # 2. 本机 gcc 汇编并链接（保留静态链接，可选）
    local link_err=$(gcc "/tmp/${base}.s" -o "/tmp/${base}.elf" \
        -L "$LIB_DIR" -lsysy -static 2>&1)
    if [ -n "$link_err" ]; then
        echo "${RED}LE${RESET} ${num} ${name}"
        [ -n "$timing" ] && echo "$base : LE" >> "$RESULT_FILE"
        return 2
    fi

    # 3. 直接运行（无 QEMU）
    local start_ns end_ns elapsed_us outtext exitcode
    if [ -n "$timing" ]; then
        start_ns=$(date +%s%N 2>/dev/null)
        if [ -f "$infile" ]; then
            outtext=$(/tmp/${base}.elf < "$infile" 2>/dev/null)
        else
            outtext=$(/tmp/${base}.elf 2>/dev/null)
        fi
        exitcode=$?
        end_ns=$(date +%s%N 2>/dev/null)
        elapsed_us=$(((end_ns - start_ns) / 1000))
    else
        if [ -f "$infile" ]; then
            outtext=$(/tmp/${base}.elf < "$infile" 2>/dev/null)
        else
            outtext=$(/tmp/${base}.elf 2>/dev/null)
        fi
        exitcode=$?
    fi

    # 4. 格式化结果
    local result
    if [ -z "$outtext" ]; then
        result="$exitcode"
    else
        result=$(printf "%s\n%d" "$outtext" $exitcode)
    fi

    # 5. 比较并报告
    if [ "$result" = "$exp" ]; then
        echo "${GREEN}PASS${RESET} ${num} ${name}"
        if [ -n "$timing" ]; then
            echo "$base : AC" >> "$RESULT_FILE"
            echo "Total time : $(format_time $elapsed_us)" >> "$RESULT_FILE"
        fi
        rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
        return 0
    elif [ $exitcode -ge 128 ]; then
        local sig=$((exitcode - 128))
        echo "${RED}RE${RESET}  ${num} ${name} (signal $sig)"
        if [ -n "$timing" ]; then
            echo "$base : RE" >> "$RESULT_FILE"
        fi
        rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
        return 3
    else
        echo "${RED}WA${RESET}  ${num} ${name}"
        echo "  expected: '$(echo "$exp" | tr '\n' '|')'"
        echo "  got:      '$(echo "$result" | tr '\n' '|')'"
        if [ -n "$timing" ]; then
            echo "$base : WA" >> "$RESULT_FILE"
        fi
        rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
        return 3
    fi
}

# ===================== functional tests =====================
# 取消注释以启用功能测试
# echo "========== Functional Tests =========="
# FPASS=0; FFAIL=0; FCRASH=0; FTOTAL=0
#
# for sy in "$FUNC_DIR"/*.sy; do
#     base=$(basename "$sy" .sy)
#     expfile="$FUNC_DIR/${base}.out"
#     [ ! -f "$expfile" ] && continue
#     FTOTAL=$((FTOTAL + 1))
#
#     run_test "$sy" "$base" "$FUNC_DIR" ""
#     rc=$?
#     case $rc in
#         0) FPASS=$((FPASS + 1)) ;;
#         3) FCRASH=$((FCRASH + 1)) ;;
#         *) FFAIL=$((FFAIL + 1)) ;;
#     esac
# done
#
# echo "  TOTAL: $FTOTAL  ${GREEN}PASS${RESET}: $FPASS  ${RED}FAIL${RESET}: $FFAIL  ${RED}CRASH${RESET}: $FCRASH"

# ===================== performance tests =====================
echo ""
echo "========== Performance Tests =========="
PPASS=0; PFAIL=0; PTOTAL=0

> "$RESULT_FILE"

for sy in "$PERF_DIR"/*.sy; do
    base=$(basename "$sy" .sy)
    expfile="$PERF_DIR/${base}.out"
    [ ! -f "$expfile" ] && continue
    PTOTAL=$((PTOTAL + 1))

    run_test "$sy" "$base" "$PERF_DIR" "time"
    rc=$?
    case $rc in
        0) PPASS=$((PPASS + 1)) ;;
        *) PFAIL=$((PFAIL + 1)) ;;
    esac
done

echo ""
echo "  TOTAL: $PTOTAL  ${GREEN}AC${RESET}: $PPASS  ${RED}FAIL${RESET}: $PFAIL"
cp "$RESULT_FILE" "$RESULT_ARCHIVE"
echo "  Results written to: $RESULT_FILE"
echo "  Archived to: $RESULT_ARCHIVE"