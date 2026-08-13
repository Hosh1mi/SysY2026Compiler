// This file declares generated AArch64 selection tables.
// Generated from aarch64.td by generate.py.
// Edit the target description or generator, not this file.
#pragma once

#include "backend/selection_dag.hpp"

#include <array>
#include <cstdint>

namespace backend::aarch64::generated {

enum class SelectionMode : std::uint8_t { Ignore, Generated, Custom, Hybrid };
enum class CustomSelector : std::uint8_t {
    None,
    Argument,
    Constant,
    FPConstant,
    GlobalAddress,
    Add,
    Sub,
    Mul,
    SDiv,
    SRem,
    UDiv,
    URem,
    FAdd,
    FSub,
    FMul,
    FDiv,
    Shl,
    LShr,
    AShr,
    And,
    Or,
    Xor,
    ICmp,
    FCmp,
    Select,
    GEP,
    InsertElement,
    ExtractElement,
    ShuffleVector,
    Phi,
    Call,
    TailCall,
    BranchCond,
    Return,
    VectorReduceAdd,
    SMin,
    SMax,
    MulMod,
    Count,
};
enum class PatternOperandKind : std::uint8_t {
    Definition,
    Use,
    OperandInteger,
    NodeInteger,
    FrameIndex,
    NodeGlobal,
    Block,
    OperandGlobal,
    Zero,
};
enum class PatternMemoryAction : std::uint8_t { None, Load, Store };

struct PatternOperand {
    PatternOperandKind kind;
    std::uint8_t index;
    std::int8_t tiedTo;
    bool earlyClobber;
};

struct SelectionPattern {
    const char *name;
    Opcode opcode;
    std::array<PatternOperand, 4> operands;
    std::uint8_t operandCount;
    PatternMemoryAction memory;
};

SelectionMode selectionMode(SDOpcode opcode);
CustomSelector customSelector(SDOpcode opcode);
int dagAddressOperand(SDOpcode opcode);
const SelectionPattern *matchPattern(const SDNode &node, bool directGlobal);
CondCode integerCondition(int predicate);
CondCode floatingCondition(int predicate);

} // namespace backend::aarch64::generated
