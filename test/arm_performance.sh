#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJ_DIR/build"
RESULT_DIR="$PROJ_DIR/test/results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$PROJ_DIR/test/results/result_${TIMESTAMP}.txt"
LIB_DIR="$PROJ_DIR/lib"
TEST_DIR="$PROJ_DIR/test/performance"
CACHE_DIR="$PROJ_DIR/tmp"

mkdir -p "$RESULT_DIR" "$CACHE_DIR"

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

PASS=0; FAIL=0; TOTAL=0

echo "========== Performance Final Tests =========="

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

    cached_asm="$CACHE_DIR/${base}.s"
    candidate_asm="$CACHE_DIR/${base}.candidate.s"
    cached_time="$CACHE_DIR/${base}.time"
    runtime_stderr="$CACHE_DIR/${base}.stderr"
    elf="/tmp/${base}.elf"

    # 1. Always compile, then compare the complete assembly with the previous
    #    successful run.  A cached time is usable only when its assembly is
    #    byte-identical and the runtime-reported time has a valid format.
    ./compiler -S "$sy" -o "$candidate_asm" -O1 2>/dev/null
    if [ $? -ne 0 ]; then
        rm -f "$candidate_asm"
        echo "${RED}CE${RESET} ${num} ${name}"
        echo "$base : CE" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
        continue
    fi

    cache_hit=0
    elapsed=""
    if [ -f "$cached_asm" ] && cmp -s "$candidate_asm" "$cached_asm" &&
       [ -f "$cached_time" ]; then
        elapsed=$(sed -nE 's/^([0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us)$/\1/p' "$cached_time" | tail -n 1)
        if [ -n "$elapsed" ]; then
            cache_hit=1
        fi
    fi

    if [ "$cache_hit" -eq 1 ]; then
        rm -f "$candidate_asm"
        echo "${GREEN}AC${RESET} ${num} ${name}  ${elapsed} (From Cache)"
        echo "$base : AC" >> "$RESULT_FILE"
        echo "Total time : $elapsed" >> "$RESULT_FILE"
        PASS=$((PASS + 1))
        continue
    fi

    if [ ! -f "$cached_asm" ] || ! cmp -s "$candidate_asm" "$cached_asm"; then
        mv -f "$candidate_asm" "$cached_asm"
        # The old time belongs to different code and must never survive a
        # failed link or run of the replacement assembly.
        rm -f "$cached_time"
    else
        rm -f "$candidate_asm"
    fi

    # 2. Native gcc assemble + link
    gcc "$cached_asm" -o "$elf" \
        -L "$LIB_DIR" -lsysy -static 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "${RED}LE${RESET} ${num} ${name}"
        echo "$base : LE" >> "$RESULT_FILE"
        FAIL=$((FAIL + 1))
        continue
    fi

    # 3. Run natively.  The SysY runtime reports the measured region on
    #    stderr as `TOTAL: <H>-<M>-<S>-<us>`; that is the authoritative time.
    if [ -f "$infile" ]; then
        outtext=$("$elf" < "$infile" 2>"$runtime_stderr")
    else
        outtext=$("$elf" 2>"$runtime_stderr")
    fi
    exitcode=$?
    elapsed=$(sed -nE 's/^TOTAL: ([0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us)$/\1/p' "$runtime_stderr" | tail -n 1)
    rm -f "$runtime_stderr"

    # 4. Format result
    if [ -z "$outtext" ]; then
        result="$exitcode"
    else
        result=$(printf "%s\n%d" "$outtext" $exitcode)
    fi

    # 5. Compare
    norm_result=$(printf '%s' "$result" | sed -E 's/[[:space:]]+$//')
    norm_exp=$(printf '%s' "$exp" | sed -E 's/[[:space:]]+$//')
    if [ "$norm_result" = "$norm_exp" ]; then
        if [ -n "$elapsed" ]; then
            printf '%s\n' "$elapsed" > "$cached_time"
            echo "${GREEN}AC${RESET} ${num} ${name}  $elapsed"
            echo "$base : AC" >> "$RESULT_FILE"
            echo "Total time : $elapsed" >> "$RESULT_FILE"
            PASS=$((PASS + 1))
        else
            echo "${RED}TE${RESET} ${num} ${name} (missing runtime TOTAL)"
            echo "$base : TE" >> "$RESULT_FILE"
            FAIL=$((FAIL + 1))
        fi
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

    rm -f "$elf"
done

echo ""
echo "========================================="
echo "  TOTAL: $TOTAL  ${GREEN}AC${RESET}: $PASS  ${RED}FAIL${RESET}: $FAIL"
echo "========================================="
echo "" >> "$RESULT_FILE"
echo "TOTAL: $TOTAL  AC: $PASS  FAIL: $FAIL" >> "$RESULT_FILE"
echo "Results written to: $RESULT_FILE"
