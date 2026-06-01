#pragma once
#include <string>

/// Apply peephole optimizations to a single function's ARM64 assembly text.
/// Uses a sliding window of 4 consecutive instruction lines.
/// Returns the optimized assembly; the input is left unchanged.
std::string peepholeOptimize(const std::string &asmText);
