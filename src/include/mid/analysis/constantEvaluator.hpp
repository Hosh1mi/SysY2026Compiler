#pragma once

#include "../ir/instruction.hpp"

#include <cstdint>
#include <limits>

namespace ConstantEvaluator {

inline bool foldIntegerBinary(Instruction::OpID op, int lhs, int rhs,
                              int &result) {
    switch (op) {
    case Instruction::Add:
        result = static_cast<int32_t>(static_cast<uint32_t>(lhs) +
                                      static_cast<uint32_t>(rhs));
        return true;
    case Instruction::Sub:
        result = static_cast<int32_t>(static_cast<uint32_t>(lhs) -
                                      static_cast<uint32_t>(rhs));
        return true;
    case Instruction::Mul:
        result = static_cast<int32_t>(static_cast<uint32_t>(lhs) *
                                      static_cast<uint32_t>(rhs));
        return true;
    case Instruction::SDiv:
        if (rhs == 0 ||
            (lhs == std::numeric_limits<int32_t>::min() && rhs == -1))
            return false;
        result = lhs / rhs;
        return true;
    case Instruction::SRem:
        if (rhs == 0 ||
            (lhs == std::numeric_limits<int32_t>::min() && rhs == -1))
            return false;
        result = lhs % rhs;
        return true;
    default:
        return false;
    }
}

inline bool foldFloatBinary(Instruction::OpID op, float lhs, float rhs,
                            float &result) {
    switch (op) {
    case Instruction::FAdd: result = lhs + rhs; return true;
    case Instruction::FSub: result = lhs - rhs; return true;
    case Instruction::FMul: result = lhs * rhs; return true;
    case Instruction::FDiv:
        if (rhs == 0.0f) return false;
        result = lhs / rhs;
        return true;
    default:
        return false;
    }
}

} // namespace ConstantEvaluator
