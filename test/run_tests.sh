#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$SCRIPT_DIR/.."
BUILD_DIR="${BUILD_DIR_OVERRIDE:-$PROJ_DIR/build}"
RESULT_DIR="$PROJ_DIR/test/results"
LIB_DIR="$PROJ_DIR/lib"
CACHE_DIR="$SCRIPT_DIR/tmp/performance"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULT_DIR/result_all_$TIMESTAMP.txt"
FUNCTIONAL_SUITES="${FUNCTIONAL_SUITES:-functional h_functional}"
PERFORMANCE_SUITE="${PERFORMANCE_SUITE:-performance}"
OPT_LEVELS=(0 1)
PERFORMANCE_FLAGS=(-O1)
COMPILE_TIMEOUT_SECONDS=300
RUNTIME_TIMEOUT_SECONDS=300

mkdir -p "$RESULT_DIR" "$CACHE_DIR"
COMPILER="$BUILD_DIR/compiler"
if [ ! -x "$COMPILER" ]; then
    echo "Error: compiler not found or not executable at $COMPILER"
    exit 1
fi

RED=''; GREEN=''; RESET=''
if [ -t 1 ]; then
    RED=$(tput setaf 1 2>/dev/null || true)
    GREEN=$(tput setaf 2 2>/dev/null || true)
    RESET=$(tput sgr0 2>/dev/null || true)
fi

: > "$RESULT_FILE"
PASS=0; FAIL=0; CRASH=0; TOTAL=0

run_functional() {
    local suite dir sy base expfile infile exp num name opt tmpbase result exitcode
    local outtext norm_result norm_exp captured compile_us start_ns end_ns compile_status
    read -r -a suites <<< "$FUNCTIONAL_SUITES"
    for suite in "${suites[@]}"; do
        dir="$PROJ_DIR/test/$suite"
        [ -d "$dir" ] || { echo "Error: test suite not found at $dir"; return 1; }
    done
    for opt in "${OPT_LEVELS[@]}"; do
        for suite in "${suites[@]}"; do
            dir="$PROJ_DIR/test/$suite"
            echo "========== O$opt $suite =========="
            for sy in "$dir"/*.sy; do
                [ -f "$sy" ] || continue
                base=$(basename "$sy" .sy); expfile="$dir/$base.out"
                [ -f "$expfile" ] || continue
                TOTAL=$((TOTAL + 1)); exp=$(cat "$expfile")
                num=$(printf '%s\n' "$base" | grep -oE '^[0-9]+' || printf '%s\n' "$base")
                name=$(printf '%s\n' "$base" | sed 's/^[0-9]*_//')
                infile="$dir/$base.in"; tmpbase="$base.O$opt"
                start_ns=$(date +%s%N)
                if [ "$opt" -eq 0 ]; then
                    timeout 60s "$COMPILER" -S "-O$opt" "$sy" -o "/tmp/$tmpbase.s" 2>/dev/null
                else
                    "$COMPILER" -S "-O$opt" "$sy" -o "/tmp/$tmpbase.s" 2>/dev/null
                fi
                compile_status=$?
                end_ns=$(date +%s%N); compile_us=$(( (end_ns - start_ns) / 1000 ))
                if [ "$compile_status" -ne 0 ]; then
                    echo "${RED}CE${RESET} [O$opt][$suite] $num $name"; printf '%s\n' "[O$opt][$suite] $base : CE" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue
                fi
                gcc "/tmp/$tmpbase.s" -o "/tmp/$tmpbase.elf" -L "$LIB_DIR" -lsysy -static 2>/dev/null
                if [ $? -ne 0 ]; then
                    echo "${RED}LE${RESET} [O$opt][$suite] $num $name"; printf '%s\n' "[O$opt][$suite] $base : LE" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue
                fi
                if [ -f "$infile" ]; then timeout 60s "/tmp/$tmpbase.elf" < "$infile" > "/tmp/$tmpbase.stdout" 2>/dev/null; else timeout 60s "/tmp/$tmpbase.elf" > "/tmp/$tmpbase.stdout" 2>/dev/null; fi
                exitcode=$?; captured=$(cat "/tmp/$tmpbase.stdout"; printf '\034'); outtext=${captured%$'\034'}
                if [ -z "$outtext" ]; then result="$exitcode"; elif [[ "$outtext" == *$'\n' ]]; then result="${outtext}${exitcode}"; else result=$(printf "%s\n%d" "$outtext" "$exitcode"); fi
                norm_result=$(printf '%s' "$result" | sed -E 's/[[:space:]]+$//'); norm_exp=$(printf '%s' "$exp" | sed -E 's/[[:space:]]+$//')
                if [ "$norm_result" = "$norm_exp" ]; then
                    echo "${GREEN}PASS${RESET} [O$opt][$suite] $num $name"; printf '%s\n' "[O$opt][$suite] $base : AC" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; PASS=$((PASS + 1))
                elif [ $exitcode -ge 128 ]; then
                    echo "${RED}SIG$((exitcode - 128))${RESET} [O$opt][$suite] $num $name"; printf '%s\n' "[O$opt][$suite] $base : SIG$((exitcode - 128))" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; CRASH=$((CRASH + 1))
                else
                    echo "${RED}FAIL${RESET} [O$opt][$suite] $num $name"; printf '%s\n' "[O$opt][$suite] $base : WA" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1))
                fi
                rm -f "/tmp/$tmpbase.s" "/tmp/$tmpbase.elf" "/tmp/$tmpbase.stdout"
            done
        done
    done
}

run_performance() {
    local dir="$PROJ_DIR/test/$PERFORMANCE_SUITE" sy base expfile infile exp num name
    local cached_asm candidate_asm cached_time stderr_file stdout_file elf compile_status elapsed exitcode outtext result norm_result norm_exp
    [ -d "$dir" ] || { echo "Error: test suite not found at $dir"; return 1; }
    echo "========== $PERFORMANCE_SUITE Tests =========="
    for sy in "$dir"/*.sy; do
        [ -f "$sy" ] || continue
        base=$(basename "$sy" .sy); expfile="$dir/$base.out"; infile="$dir/$base.in"; [ -f "$expfile" ] || continue
        TOTAL=$((TOTAL + 1)); exp=$(cat "$expfile"); num=$(printf '%s\n' "$base" | grep -oE '^[0-9]+' || printf '%s\n' "$base"); name=$(printf '%s\n' "$base" | sed 's/^[0-9]*_//')
        cached_asm="$CACHE_DIR/$base.s"; candidate_asm="$CACHE_DIR/$base.candidate.s"; cached_time="$CACHE_DIR/$base.time"; stderr_file="$CACHE_DIR/$base.stderr"; stdout_file="$CACHE_DIR/$base.stdout"; elf="/tmp/$base.$$.elf"
        rm -f "$candidate_asm" "$stderr_file" "$stdout_file" "$elf"
        start_ns=$(date +%s%N)
        timeout --signal=TERM --kill-after=1s "$COMPILE_TIMEOUT_SECONDS" "$COMPILER" -S "$sy" -o "$candidate_asm" "${PERFORMANCE_FLAGS[@]}" 2>/dev/null
        compile_status=$?; end_ns=$(date +%s%N); compile_us=$(( (end_ns - start_ns) / 1000 ))
        if [ "$compile_status" -eq 124 ] || [ "$compile_status" -eq 137 ]; then echo "${RED}TLE${RESET} $num $name (compile > ${COMPILE_TIMEOUT_SECONDS}s)"; echo "$base : TLE" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue; fi
        if [ "$compile_status" -ne 0 ]; then echo "${RED}CE${RESET} $num $name"; echo "$base : CE" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue; fi
        elapsed=""
        if [ -f "$cached_asm" ] && cmp -s "$candidate_asm" "$cached_asm" && [ -f "$cached_time" ]; then elapsed=$(sed -nE 's/^([0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us)$/\1/p' "$cached_time" | tail -n 1); fi
        if [ -n "$elapsed" ]; then rm -f "$candidate_asm"; echo "${GREEN}AC${RESET} $num $name  $elapsed (From Cache)"; printf '%s\n' "$base : AC" "Compile time : ${compile_us}us" "Total time : $elapsed" >> "$RESULT_FILE"; PASS=$((PASS + 1)); continue; fi
        if [ ! -f "$cached_asm" ] || ! cmp -s "$candidate_asm" "$cached_asm"; then mv -f "$candidate_asm" "$cached_asm"; rm -f "$cached_time"; else rm -f "$candidate_asm"; fi
        gcc "$cached_asm" -o "$elf" -L "$LIB_DIR" -lsysy -static 2>/dev/null
        if [ $? -ne 0 ]; then echo "${RED}LE${RESET} $num $name"; echo "$base : LE" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue; fi
        if [ -f "$infile" ]; then timeout --signal=TERM --kill-after=1s "$RUNTIME_TIMEOUT_SECONDS" "$elf" < "$infile" > "$stdout_file" 2> "$stderr_file"; else timeout --signal=TERM --kill-after=1s "$RUNTIME_TIMEOUT_SECONDS" "$elf" > "$stdout_file" 2> "$stderr_file"; fi
        exitcode=$?; outtext=$(cat "$stdout_file"); elapsed=$(sed -nE 's/^TOTAL: ([0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us)$/\1/p' "$stderr_file" | tail -n 1); rm -f "$stderr_file" "$stdout_file" "$elf"
        if [ "$exitcode" -eq 124 ] || [ "$exitcode" -eq 137 ]; then echo "${RED}TLE${RESET} $num $name (run > ${RUNTIME_TIMEOUT_SECONDS}s)"; echo "$base : TLE" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); continue; fi
        if [ -z "$outtext" ]; then result="$exitcode"; elif [[ "$outtext" == *$'\n' ]]; then result="${outtext}${exitcode}"; else result=$(printf "%s\n%d" "$outtext" "$exitcode"); fi
        norm_result=$(printf '%s' "$result" | sed -E 's/[[:space:]]+$//'); norm_exp=$(printf '%s' "$exp" | sed -E 's/[[:space:]]+$//')
        if [ "$norm_result" = "$norm_exp" ] && [ -n "$elapsed" ]; then printf '%s\n' "$elapsed" > "$cached_time"; echo "${GREEN}AC${RESET} $num $name  $elapsed"; printf '%s\n' "$base : AC" "Compile time : ${compile_us}us" "Total time : $elapsed" >> "$RESULT_FILE"; PASS=$((PASS + 1)); elif [ "$norm_result" = "$norm_exp" ]; then echo "${RED}TE${RESET} $num $name (missing runtime TOTAL)"; printf '%s\n' "$base : TE" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); elif [ "$exitcode" -ge 128 ]; then echo "${RED}SIG$((exitcode - 128))${RESET} $num $name"; printf '%s\n' "$base : SIG$((exitcode - 128))" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); else echo "${RED}WA${RESET} $num $name"; printf '%s\n' "$base : WA" "Compile time : ${compile_us}us" >> "$RESULT_FILE"; FAIL=$((FAIL + 1)); fi
    done
}

run_functional
run_performance
echo
echo "========================================="
echo "  TOTAL: $TOTAL  ${GREEN}PASS${RESET}: $PASS  ${RED}FAIL${RESET}: $FAIL  ${RED}CRASH${RESET}: $CRASH"
echo "========================================="
{
    echo
    echo "TOTAL: $TOTAL  PASS: $PASS  FAIL: $FAIL  CRASH: $CRASH"
} >> "$RESULT_FILE"
echo "Results written to: $RESULT_FILE"
