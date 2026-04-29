#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_FILE="$ROOT_DIR/test/checker/out.txt"

mkdir -p "$BUILD_DIR"

bison -d -o "$BUILD_DIR/parser.tab.cc" "$ROOT_DIR/src/parser.y"
flex --c++ -o "$BUILD_DIR/lex.yy.cc" "$ROOT_DIR/src/lexer.l"
CXX=${CXX:-g++}
"$CXX" -std=c++17 -I"$ROOT_DIR/include" \
  "$BUILD_DIR/parser.tab.cc" \
  "$BUILD_DIR/lex.yy.cc" \
  "$ROOT_DIR/src/ast/ast.cpp" \
  "$ROOT_DIR/src/checker/checker.cpp" \
  "$ROOT_DIR/src/checker/errReporter.cpp" \
  -o "$BUILD_DIR/parser"

: > "$OUTPUT_FILE"
for file in "$ROOT_DIR"/test/checker/cases/*.sy; do
  [ -f "$file" ] || continue
  echo "==> $(basename "$file")" | tee -a "$OUTPUT_FILE"
  if "$BUILD_DIR/parser" "$file" | tee -a "$OUTPUT_FILE"; then
    :
  fi
done
