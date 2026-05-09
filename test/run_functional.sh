#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJ_DIR/build"
FUNC_DIR="$PROJ_DIR/test/functional"
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

FPASS=0; FFAIL=0; FCRASH=0; FTOTAL=0

for sy in "$FUNC_DIR"/*.sy; do
    base=$(basename "$sy" .sy)
    infile="$FUNC_DIR/${base}.in"
    expfile="$FUNC_DIR/${base}.out"
    [ ! -f "$expfile" ] && continue
    FTOTAL=$((FTOTAL + 1))

    exp=$(cat "$expfile")
    num=$(echo "$base" | grep -oE '^[0-9]+' || echo "$base")
    name=$(echo "$base" | sed 's/^[0-9]*_//')

    # 1. SysY -> ARM64 asm
    ./compiler -S "$sy" -o "/tmp/${base}.s" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "${RED}CE${RESET} ${num} ${name}"
        FFAIL=$((FFAIL + 1))
        continue
    fi

    # 2. Cross-assemble + static link
    aarch64-linux-gnu-gcc "/tmp/${base}.s" -o "/tmp/${base}.elf" \
        -L "$LIB_DIR" -lsysy -static 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "${RED}LE${RESET} ${num} ${name}"
        FFAIL=$((FFAIL + 1))
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
        echo "${GREEN}PASS${RESET} ${num} ${name}"
        FPASS=$((FPASS + 1))
    elif [ $exitcode -ge 128 ]; then
        sig=$((exitcode - 128))
        echo "${RED}SIG${sig}${RESET} ${num} ${name}"
        FCRASH=$((FCRASH + 1))
    else
        echo "${RED}FAIL${RESET} ${num} ${name}"
        echo "  expected: '$(echo "$exp" | tr '\n' '|')'"
        echo "  got:      '$(echo "$result" | tr '\n' '|')'"
        FFAIL=$((FFAIL + 1))
    fi

    rm -f "/tmp/${base}.s" "/tmp/${base}.elf"
done

echo "========================================="
echo "  TOTAL: $FTOTAL  ${GREEN}PASS${RESET}: $FPASS  ${RED}FAIL${RESET}: $FFAIL  ${RED}CRASH${RESET}: $FCRASH"
echo "========================================="
