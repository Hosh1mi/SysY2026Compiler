#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_FILE="$ROOT_DIR/test/parser/out.txt"

mkdir -p "$BUILD_DIR"

bison -d -o "$BUILD_DIR/parser.tab.cc" "$ROOT_DIR/src/parser.y"
flex --c++ -o "$BUILD_DIR/lex.yy.cc" "$ROOT_DIR/src/lexer.l"
CXX=${CXX:-g++}
"$CXX" -std=c++17 -DDISABLE_SEMANTIC_CHECK -I"$ROOT_DIR/include" \
  "$BUILD_DIR/parser.tab.cc" \
  "$BUILD_DIR/lex.yy.cc" \
  "$ROOT_DIR/src/ast/ast.cpp" \
  -o "$BUILD_DIR/parser"

: > "$OUTPUT_FILE"
for file in "$ROOT_DIR"/test/parser/*.sy; do
  [ -f "$file" ] || continue
  echo "==> $(basename "$file")" | tee -a "$OUTPUT_FILE"
  "$BUILD_DIR/parser" "$file" | tee -a "$OUTPUT_FILE"
done
