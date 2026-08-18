// This file defines generated AArch64 selection tables and matchers.
// Generated from aarch64.td by generate.py.
// Edit the target description or generator, not this file.
#include "../include/backend/aarch64_isel.hpp"

#include "../include/mid/ir/instruction.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>

namespace backend::aarch64::generated {
namespace {

constexpr std::size_t kDAGOpcodeCount =
    static_cast<std::size_t>(SDOpcode::Count);

constexpr std::array<SelectionMode, kDAGOpcodeCount> kSelectionModes{{
    SelectionMode::Ignore,    // Invalid
    SelectionMode::Ignore,    // EntryToken
    SelectionMode::Custom,    // Argument
    SelectionMode::Hybrid,    // Constant
    SelectionMode::Custom,    // FPConstant
    SelectionMode::Hybrid,    // GlobalAddress
    SelectionMode::Generated, // FrameIndex
    SelectionMode::Hybrid,    // Add
    SelectionMode::Hybrid,    // Sub
    SelectionMode::Hybrid,    // Mul
    SelectionMode::Hybrid,    // SDiv
    SelectionMode::Custom,    // SRem
    SelectionMode::Hybrid,    // UDiv
    SelectionMode::Custom,    // URem
    SelectionMode::Hybrid,    // FAdd
    SelectionMode::Hybrid,    // FSub
    SelectionMode::Hybrid,    // FMul
    SelectionMode::Hybrid,    // FDiv
    SelectionMode::Generated, // FNeg
    SelectionMode::Hybrid,    // Shl
    SelectionMode::Hybrid,    // LShr
    SelectionMode::Hybrid,    // AShr
    SelectionMode::Hybrid,    // And
    SelectionMode::Hybrid,    // Or
    SelectionMode::Hybrid,    // Xor
    SelectionMode::Custom,    // ICmp
    SelectionMode::Custom,    // FCmp
    SelectionMode::Custom,    // Select
    SelectionMode::Custom,    // GEP
    SelectionMode::Generated, // Load
    SelectionMode::Generated, // Store
    SelectionMode::Generated, // ZExt
    SelectionMode::Generated, // SExt
    SelectionMode::Generated, // Trunc
    SelectionMode::Generated, // FPToSI
    SelectionMode::Generated, // SIToFP
    SelectionMode::Generated, // Bitcast
    SelectionMode::Generated, // Clz
    SelectionMode::Generated, // Splat
    SelectionMode::Hybrid,    // InsertElement
    SelectionMode::Hybrid,    // ExtractElement
    SelectionMode::Custom,    // ShuffleVector
    SelectionMode::Custom,    // Phi
    SelectionMode::Custom,    // Call
    SelectionMode::Custom,    // TailCall
    SelectionMode::Generated, // Branch
    SelectionMode::Custom,    // BranchCond
    SelectionMode::Hybrid,    // Return
    SelectionMode::Generated, // MAdd
    SelectionMode::Generated, // MSub
    SelectionMode::Generated, // FMAdd
    SelectionMode::Generated, // FMSub
    SelectionMode::Custom,    // VectorReduceAdd
    SelectionMode::Hybrid,    // SMin
    SelectionMode::Hybrid,    // SMax
    SelectionMode::Custom,    // MulMod
}};

constexpr std::array<CustomSelector, kDAGOpcodeCount> kCustomSelectors{{
    CustomSelector::None,            // Invalid
    CustomSelector::None,            // EntryToken
    CustomSelector::Argument,        // Argument
    CustomSelector::Constant,        // Constant
    CustomSelector::FPConstant,      // FPConstant
    CustomSelector::GlobalAddress,   // GlobalAddress
    CustomSelector::None,            // FrameIndex
    CustomSelector::Add,             // Add
    CustomSelector::Sub,             // Sub
    CustomSelector::Mul,             // Mul
    CustomSelector::SDiv,            // SDiv
    CustomSelector::SRem,            // SRem
    CustomSelector::UDiv,            // UDiv
    CustomSelector::URem,            // URem
    CustomSelector::FAdd,            // FAdd
    CustomSelector::FSub,            // FSub
    CustomSelector::FMul,            // FMul
    CustomSelector::FDiv,            // FDiv
    CustomSelector::None,            // FNeg
    CustomSelector::Shl,             // Shl
    CustomSelector::LShr,            // LShr
    CustomSelector::AShr,            // AShr
    CustomSelector::And,             // And
    CustomSelector::Or,              // Or
    CustomSelector::Xor,             // Xor
    CustomSelector::ICmp,            // ICmp
    CustomSelector::FCmp,            // FCmp
    CustomSelector::Select,          // Select
    CustomSelector::GEP,             // GEP
    CustomSelector::None,            // Load
    CustomSelector::None,            // Store
    CustomSelector::None,            // ZExt
    CustomSelector::None,            // SExt
    CustomSelector::None,            // Trunc
    CustomSelector::None,            // FPToSI
    CustomSelector::None,            // SIToFP
    CustomSelector::None,            // Bitcast
    CustomSelector::None,            // Clz
    CustomSelector::None,            // Splat
    CustomSelector::InsertElement,   // InsertElement
    CustomSelector::ExtractElement,  // ExtractElement
    CustomSelector::ShuffleVector,   // ShuffleVector
    CustomSelector::Phi,             // Phi
    CustomSelector::Call,            // Call
    CustomSelector::TailCall,        // TailCall
    CustomSelector::None,            // Branch
    CustomSelector::BranchCond,      // BranchCond
    CustomSelector::Return,          // Return
    CustomSelector::None,            // MAdd
    CustomSelector::None,            // MSub
    CustomSelector::None,            // FMAdd
    CustomSelector::None,            // FMSub
    CustomSelector::VectorReduceAdd, // VectorReduceAdd
    CustomSelector::SMin,            // SMin
    CustomSelector::SMax,            // SMax
    CustomSelector::MulMod,          // MulMod
}};

constexpr std::array<int, kDAGOpcodeCount> kAddressOperands{{
    -1, // Invalid
    -1, // EntryToken
    -1, // Argument
    -1, // Constant
    -1, // FPConstant
    -1, // GlobalAddress
    -1, // FrameIndex
    -1, // Add
    -1, // Sub
    -1, // Mul
    -1, // SDiv
    -1, // SRem
    -1, // UDiv
    -1, // URem
    -1, // FAdd
    -1, // FSub
    -1, // FMul
    -1, // FDiv
    -1, // FNeg
    -1, // Shl
    -1, // LShr
    -1, // AShr
    -1, // And
    -1, // Or
    -1, // Xor
    -1, // ICmp
    -1, // FCmp
    -1, // Select
    -1, // GEP
    1,  // Load
    2,  // Store
    -1, // ZExt
    -1, // SExt
    -1, // Trunc
    -1, // FPToSI
    -1, // SIToFP
    -1, // Bitcast
    -1, // Clz
    -1, // Splat
    -1, // InsertElement
    -1, // ExtractElement
    -1, // ShuffleVector
    -1, // Phi
    -1, // Call
    -1, // TailCall
    -1, // Branch
    -1, // BranchCond
    -1, // Return
    -1, // MAdd
    -1, // MSub
    -1, // FMAdd
    -1, // FMSub
    -1, // VectorReduceAdd
    -1, // SMin
    -1, // SMax
    -1, // MulMod
}};

constexpr std::array<SelectionPattern, 141> kPatterns{{
    SelectionPattern{
        "ConstantI1",
        Opcode::MOVi32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::NodeInteger, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ConstantI32",
        Opcode::MOVi32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::NodeInteger, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ConstantI64",
        Opcode::MOVi64,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::NodeInteger, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ConstantPtr",
        Opcode::MOVi64,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::NodeInteger, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "GlobalAddressDirect",
        Opcode::ADRP,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::NodeGlobal, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "FrameIndexPtr",
        Opcode::LEA_FRAME,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::FrameIndex, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI1Imm",
        Opcode::ADDWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI32Imm",
        Opcode::ADDWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI64Imm",
        Opcode::ADDXri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI1",
        Opcode::ADDWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI32",
        Opcode::ADDWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddI64",
        Opcode::ADDXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AddV4I32",
        Opcode::ADDv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI1Imm",
        Opcode::SUBWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI32Imm",
        Opcode::SUBWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI64Imm",
        Opcode::SUBXri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI1",
        Opcode::SUBWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI32",
        Opcode::SUBWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubI64",
        Opcode::SUBXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SubV4I32",
        Opcode::SUBv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "MulI1",
        Opcode::MULWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "MulI32",
        Opcode::MULWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "MulI64",
        Opcode::MULXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "MulV4I32",
        Opcode::MULv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SDivI1",
        Opcode::SDIVWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SDivI32",
        Opcode::SDIVWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SDivI64",
        Opcode::SDIVXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "UDivI1",
        Opcode::UDIVWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "UDivI32",
        Opcode::UDIVWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "UDivI64",
        Opcode::UDIVXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FAddF32",
        Opcode::FADDS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FAddV4F32",
        Opcode::ADDv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FSubF32",
        Opcode::FSUBS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FSubV4F32",
        Opcode::SUBv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FMulF32",
        Opcode::FMULS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FMulV4F32",
        Opcode::MULv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FDivF32",
        Opcode::FDIVS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FDivV4F32",
        Opcode::DIVv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "FNegF32",
        Opcode::FNEGS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "FNegV4F32",
        Opcode::NEGv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI1Imm",
        Opcode::LSLWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI32Imm",
        Opcode::LSLWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI64Imm",
        Opcode::LSLXri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI1",
        Opcode::LSLWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI32",
        Opcode::LSLWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlI64",
        Opcode::LSLXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ShlV4I32",
        Opcode::SSHLv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI1Imm",
        Opcode::LSRWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI32Imm",
        Opcode::LSRWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI64Imm",
        Opcode::LSRXri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI1",
        Opcode::LSRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI32",
        Opcode::LSRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrI64",
        Opcode::LSRXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LShrV4I32",
        Opcode::USHLv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI1Imm",
        Opcode::ASRWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI32Imm",
        Opcode::ASRWri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI64Imm",
        Opcode::ASRXri,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI1",
        Opcode::ASRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI32",
        Opcode::ASRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrI64",
        Opcode::ASRXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AShrV4I32",
        Opcode::SSHLv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AndI1",
        Opcode::ANDWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AndI32",
        Opcode::ANDWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AndI64",
        Opcode::ANDXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "AndV4I32",
        Opcode::ANDv16i8,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "OrI1",
        Opcode::ORRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "OrI32",
        Opcode::ORRWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "OrI64",
        Opcode::ORRXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "OrV4I32",
        Opcode::ORRv16i8,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "XorI1",
        Opcode::EORWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "XorI32",
        Opcode::EORWrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "XorI64",
        Opcode::EORXrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "XorV4I32",
        Opcode::EORv16i8,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "LoadF32Global",
        Opcode::LDRSlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI1Global",
        Opcode::LDRWlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI32Global",
        Opcode::LDRWlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI64Global",
        Opcode::LDRXlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadPtrGlobal",
        Opcode::LDRXlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadV4F32Global",
        Opcode::LDRQlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadV4I32Global",
        Opcode::LDRQlo,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadF32",
        Opcode::LDRSui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI1",
        Opcode::LDRWui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI32",
        Opcode::LDRWui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadI64",
        Opcode::LDRXui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadPtr",
        Opcode::LDRXui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadV4F32",
        Opcode::LDRQui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "LoadV4I32",
        Opcode::LDRQui,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Load},
    SelectionPattern{
        "StoreF32Global",
        Opcode::STRSlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI1Global",
        Opcode::STRWlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI32Global",
        Opcode::STRWlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI64Global",
        Opcode::STRXlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StorePtrGlobal",
        Opcode::STRXlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreV4F32Global",
        Opcode::STRQlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreV4I32Global",
        Opcode::STRQlo,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::OperandGlobal, 2, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreF32",
        Opcode::STRSui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI1",
        Opcode::STRWui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI32",
        Opcode::STRWui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreI64",
        Opcode::STRXui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StorePtr",
        Opcode::STRXui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreV4F32",
        Opcode::STRQui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "StoreV4I32",
        Opcode::STRQui,
        {PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false},
         PatternOperand{PatternOperandKind::Zero, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::Store},
    SelectionPattern{
        "ZExtI1FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ZExtI32FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ZExtI64I1",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ZExtI64I32",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ZExtCopy",
        Opcode::COPY,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SExtI1FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SExtI32FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SExtI64I1",
        Opcode::SXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SExtI64I32",
        Opcode::SXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SExtCopy",
        Opcode::COPY,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "TruncI1FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "TruncI32FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "TruncI64I1",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "TruncI64I32",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "TruncCopy",
        Opcode::COPY,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "FPToSII32",
        Opcode::FCVTZSW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SIToFPF32",
        Opcode::SCVTFWS,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "BitcastI1FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "BitcastI32FromI64",
        Opcode::COPYXtoW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "BitcastI64I1",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "BitcastI64I32",
        Opcode::UXTW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "BitcastCopy",
        Opcode::COPY,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ClzI1",
        Opcode::CLZW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "ClzI32",
        Opcode::CLZW,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SplatV4F32",
        Opcode::DUPv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "SplatV4I32",
        Opcode::DUPv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        2,
        PatternMemoryAction::None},
    SelectionPattern{
        "InsertV4F32Lane",
        Opcode::INSv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, 0, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 2, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "InsertV4I32Lane",
        Opcode::INSv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, 0, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 2, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "ExtractF32Lane",
        Opcode::EXTRACTv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "ExtractI32Lane",
        Opcode::EXTRACTv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::OperandInteger, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "BranchBlock",
        Opcode::B,
        {PatternOperand{PatternOperandKind::Block, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        1,
        PatternMemoryAction::None},
    SelectionPattern{
        "ReturnVoid",
        Opcode::RET,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        0,
        PatternMemoryAction::None},
    SelectionPattern{
        "MAddI32",
        Opcode::MADDWrrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "MAddV4I32",
        Opcode::MLAv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, true},
         PatternOperand{PatternOperandKind::Use, 2, 0, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "MSubI32",
        Opcode::MSUBWrrr,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "MSubV4I32",
        Opcode::MLSv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, true},
         PatternOperand{PatternOperandKind::Use, 2, 0, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "FMAddV4F32",
        Opcode::FMLAv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, 0, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "FMSubV4F32",
        Opcode::FMLSv4f32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 2, 0, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false}},
        4,
        PatternMemoryAction::None},
    SelectionPattern{
        "SMinV4I32",
        Opcode::SMINv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
    SelectionPattern{
        "SMaxV4I32",
        Opcode::SMAXv4i32,
        {PatternOperand{PatternOperandKind::Definition, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 0, -1, false},
         PatternOperand{PatternOperandKind::Use, 1, -1, false},
         PatternOperand{PatternOperandKind::Definition, 0, -1, false}},
        3,
        PatternMemoryAction::None},
}};

ValueType resultType(const SDNode &node) {
	return node.resultTypes().empty() ? ValueType::Invalid
	                                  : node.resultTypes().front();
}

ValueType operandType(const SDNode &node, unsigned index) {
	if (index >= node.operands().size() || !node.operands()[index].node)
		return ValueType::Invalid;
	const SDValue source = node.operands()[index];
	return source.result < source.node->resultTypes().size()
	           ? source.node->resultTypes()[source.result]
	           : ValueType::Invalid;
}

const SDNode *constantOperand(const SDNode &node, unsigned index) {
	if (index >= node.operands().size())
		return nullptr;
	const SDNode *operand = node.operands()[index].node;
	return operand && operand->opcode() == SDOpcode::Constant ? operand
	                                                          : nullptr;
}

bool matchesTargetImmediate(const SDNode &node, unsigned index, Opcode opcode) {
	const SDNode *constant = constantOperand(node, index);
	return constant && InstrInfo::acceptsImmediate(opcode, constant->integer);
}

bool matchesLane(const SDNode &node, unsigned index) {
	const SDNode *constant = constantOperand(node, index);
	return constant && constant->integer >= 0 && constant->integer < 4;
}

} // namespace

SelectionMode selectionMode(SDOpcode opcode) {
	const auto index = static_cast<std::size_t>(opcode);
	return index < kDAGOpcodeCount ? kSelectionModes[index]
	                               : SelectionMode::Custom;
}

CustomSelector customSelector(SDOpcode opcode) {
	const auto index = static_cast<std::size_t>(opcode);
	return index < kDAGOpcodeCount ? kCustomSelectors[index]
	                               : CustomSelector::None;
}

int dagAddressOperand(SDOpcode opcode) {
	const auto index = static_cast<std::size_t>(opcode);
	return index < kDAGOpcodeCount ? kAddressOperands[index] : -1;
}

const SelectionPattern *matchPattern(const SDNode &node, bool directGlobal) {
	const ValueType resultType = generated::resultType(node);
	switch (node.opcode()) {
	case SDOpcode::Constant:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[0];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[1];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[2];
		if (resultType == ValueType::Ptr && true && true)
			return &kPatterns[3];
		break;
	case SDOpcode::GlobalAddress:
		if (resultType == ValueType::Ptr && true && directGlobal)
			return &kPatterns[4];
		break;
	case SDOpcode::FrameIndex:
		if (resultType == ValueType::Ptr && true && true)
			return &kPatterns[5];
		break;
	case SDOpcode::Add:
		if (resultType == ValueType::I1 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ADDWri))
			return &kPatterns[6];
		if (resultType == ValueType::I32 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ADDWri))
			return &kPatterns[7];
		if (resultType == ValueType::I64 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ADDXri))
			return &kPatterns[8];
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[9];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[10];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[11];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[12];
		break;
	case SDOpcode::Sub:
		if (resultType == ValueType::I1 && true &&
		    matchesTargetImmediate(node, 1, Opcode::SUBWri))
			return &kPatterns[13];
		if (resultType == ValueType::I32 && true &&
		    matchesTargetImmediate(node, 1, Opcode::SUBWri))
			return &kPatterns[14];
		if (resultType == ValueType::I64 && true &&
		    matchesTargetImmediate(node, 1, Opcode::SUBXri))
			return &kPatterns[15];
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[16];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[17];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[18];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[19];
		break;
	case SDOpcode::Mul:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[20];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[21];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[22];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[23];
		break;
	case SDOpcode::SDiv:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[24];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[25];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[26];
		break;
	case SDOpcode::UDiv:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[27];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[28];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[29];
		break;
	case SDOpcode::FAdd:
		if (resultType == ValueType::F32 && true && true)
			return &kPatterns[30];
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[31];
		break;
	case SDOpcode::FSub:
		if (resultType == ValueType::F32 && true && true)
			return &kPatterns[32];
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[33];
		break;
	case SDOpcode::FMul:
		if (resultType == ValueType::F32 && true && true)
			return &kPatterns[34];
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[35];
		break;
	case SDOpcode::FDiv:
		if (resultType == ValueType::F32 && true && true)
			return &kPatterns[36];
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[37];
		break;
	case SDOpcode::FNeg:
		if (resultType == ValueType::F32 && true && true)
			return &kPatterns[38];
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[39];
		break;
	case SDOpcode::Shl:
		if (resultType == ValueType::I1 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSLWri))
			return &kPatterns[40];
		if (resultType == ValueType::I32 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSLWri))
			return &kPatterns[41];
		if (resultType == ValueType::I64 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSLXri))
			return &kPatterns[42];
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[43];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[44];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[45];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[46];
		break;
	case SDOpcode::LShr:
		if (resultType == ValueType::I1 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSRWri))
			return &kPatterns[47];
		if (resultType == ValueType::I32 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSRWri))
			return &kPatterns[48];
		if (resultType == ValueType::I64 && true &&
		    matchesTargetImmediate(node, 1, Opcode::LSRXri))
			return &kPatterns[49];
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[50];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[51];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[52];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[53];
		break;
	case SDOpcode::AShr:
		if (resultType == ValueType::I1 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ASRWri))
			return &kPatterns[54];
		if (resultType == ValueType::I32 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ASRWri))
			return &kPatterns[55];
		if (resultType == ValueType::I64 && true &&
		    matchesTargetImmediate(node, 1, Opcode::ASRXri))
			return &kPatterns[56];
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[57];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[58];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[59];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[60];
		break;
	case SDOpcode::And:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[61];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[62];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[63];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[64];
		break;
	case SDOpcode::Or:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[65];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[66];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[67];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[68];
		break;
	case SDOpcode::Xor:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[69];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[70];
		if (resultType == ValueType::I64 && true && true)
			return &kPatterns[71];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[72];
		break;
	case SDOpcode::Load:
		if (resultType == ValueType::F32 && true && directGlobal)
			return &kPatterns[73];
		if (resultType == ValueType::I1 && true && directGlobal)
			return &kPatterns[74];
		if (resultType == ValueType::I32 && true && directGlobal)
			return &kPatterns[75];
		if (resultType == ValueType::I64 && true && directGlobal)
			return &kPatterns[76];
		if (resultType == ValueType::Ptr && true && directGlobal)
			return &kPatterns[77];
		if (resultType == ValueType::V4F32 && true && directGlobal)
			return &kPatterns[78];
		if (resultType == ValueType::V4I32 && true && directGlobal)
			return &kPatterns[79];
		if (resultType == ValueType::F32 && true && !directGlobal)
			return &kPatterns[80];
		if (resultType == ValueType::I1 && true && !directGlobal)
			return &kPatterns[81];
		if (resultType == ValueType::I32 && true && !directGlobal)
			return &kPatterns[82];
		if (resultType == ValueType::I64 && true && !directGlobal)
			return &kPatterns[83];
		if (resultType == ValueType::Ptr && true && !directGlobal)
			return &kPatterns[84];
		if (resultType == ValueType::V4F32 && true && !directGlobal)
			return &kPatterns[85];
		if (resultType == ValueType::V4I32 && true && !directGlobal)
			return &kPatterns[86];
		break;
	case SDOpcode::Store:
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::F32 && directGlobal)
			return &kPatterns[87];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I1 && directGlobal)
			return &kPatterns[88];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I32 && directGlobal)
			return &kPatterns[89];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I64 && directGlobal)
			return &kPatterns[90];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::Ptr && directGlobal)
			return &kPatterns[91];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::V4F32 && directGlobal)
			return &kPatterns[92];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::V4I32 && directGlobal)
			return &kPatterns[93];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::F32 && !directGlobal)
			return &kPatterns[94];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I1 && !directGlobal)
			return &kPatterns[95];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I32 && !directGlobal)
			return &kPatterns[96];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::I64 && !directGlobal)
			return &kPatterns[97];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::Ptr && !directGlobal)
			return &kPatterns[98];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::V4F32 && !directGlobal)
			return &kPatterns[99];
		if (resultType == ValueType::Invalid &&
		    operandType(node, 1) == ValueType::V4I32 && !directGlobal)
			return &kPatterns[100];
		break;
	case SDOpcode::ZExt:
		if (resultType == ValueType::I1 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[101];
		if (resultType == ValueType::I32 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[102];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I1 && true)
			return &kPatterns[103];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I32 && true)
			return &kPatterns[104];
		if (true && true && true)
			return &kPatterns[105];
		break;
	case SDOpcode::SExt:
		if (resultType == ValueType::I1 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[106];
		if (resultType == ValueType::I32 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[107];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I1 && true)
			return &kPatterns[108];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I32 && true)
			return &kPatterns[109];
		if (true && true && true)
			return &kPatterns[110];
		break;
	case SDOpcode::Trunc:
		if (resultType == ValueType::I1 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[111];
		if (resultType == ValueType::I32 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[112];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I1 && true)
			return &kPatterns[113];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I32 && true)
			return &kPatterns[114];
		if (true && true && true)
			return &kPatterns[115];
		break;
	case SDOpcode::FPToSI:
		if (resultType == ValueType::I32 &&
		    operandType(node, 0) == ValueType::F32 && true)
			return &kPatterns[116];
		break;
	case SDOpcode::SIToFP:
		if (resultType == ValueType::F32 &&
		    operandType(node, 0) == ValueType::I32 && true)
			return &kPatterns[117];
		break;
	case SDOpcode::Bitcast:
		if (resultType == ValueType::I1 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[118];
		if (resultType == ValueType::I32 &&
		    operandType(node, 0) == ValueType::I64 && true)
			return &kPatterns[119];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I1 && true)
			return &kPatterns[120];
		if (resultType == ValueType::I64 &&
		    operandType(node, 0) == ValueType::I32 && true)
			return &kPatterns[121];
		if (true && true && true)
			return &kPatterns[122];
		break;
	case SDOpcode::Clz:
		if (resultType == ValueType::I1 && true && true)
			return &kPatterns[123];
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[124];
		break;
	case SDOpcode::Splat:
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[125];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[126];
		break;
	case SDOpcode::InsertElement:
		if (resultType == ValueType::V4F32 && true && matchesLane(node, 2))
			return &kPatterns[127];
		if (resultType == ValueType::V4I32 && true && matchesLane(node, 2))
			return &kPatterns[128];
		break;
	case SDOpcode::ExtractElement:
		if (resultType == ValueType::F32 && true && matchesLane(node, 1))
			return &kPatterns[129];
		if (resultType == ValueType::I32 && true && matchesLane(node, 1))
			return &kPatterns[130];
		break;
	case SDOpcode::Branch:
		if (resultType == ValueType::Invalid && true && true)
			return &kPatterns[131];
		break;
	case SDOpcode::Return:
		if (resultType == ValueType::Invalid && true &&
		    node.operands().size() == 1)
			return &kPatterns[132];
		break;
	case SDOpcode::MAdd:
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[133];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[134];
		break;
	case SDOpcode::MSub:
		if (resultType == ValueType::I32 && true && true)
			return &kPatterns[135];
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[136];
		break;
	case SDOpcode::FMAdd:
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[137];
		break;
	case SDOpcode::FMSub:
		if (resultType == ValueType::V4F32 && true && true)
			return &kPatterns[138];
		break;
	case SDOpcode::SMin:
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[139];
		break;
	case SDOpcode::SMax:
		if (resultType == ValueType::V4I32 && true && true)
			return &kPatterns[140];
		break;
	case SDOpcode::Count:
		break;
	}
	return nullptr;
}

CondCode integerCondition(int predicate) {
	switch (static_cast<ICmpInst::ICmpOp>(predicate)) {
	case ICmpInst::ICMP_EQ:
		return CondCode::EQ;
	case ICmpInst::ICMP_NE:
		return CondCode::NE;
	case ICmpInst::ICMP_UGT:
		return CondCode::HI;
	case ICmpInst::ICMP_UGE:
		return CondCode::HS;
	case ICmpInst::ICMP_ULT:
		return CondCode::LO;
	case ICmpInst::ICMP_ULE:
		return CondCode::LS;
	case ICmpInst::ICMP_SGT:
		return CondCode::GT;
	case ICmpInst::ICMP_SGE:
		return CondCode::GE;
	case ICmpInst::ICMP_SLT:
		return CondCode::LT;
	case ICmpInst::ICMP_SLE:
		return CondCode::LE;
	}
	std::abort();
}

CondCode floatingCondition(int predicate) {
	switch (static_cast<FCmpInst::FCmpOp>(predicate)) {
	case FCmpInst::FCMP_FALSE:
		return CondCode::AL;
	case FCmpInst::FCMP_OEQ:
		return CondCode::EQ;
	case FCmpInst::FCMP_OGT:
		return CondCode::GT;
	case FCmpInst::FCMP_OGE:
		return CondCode::GE;
	case FCmpInst::FCMP_OLT:
		return CondCode::MI;
	case FCmpInst::FCMP_OLE:
		return CondCode::LS;
	case FCmpInst::FCMP_ONE:
		return CondCode::NE;
	case FCmpInst::FCMP_ORD:
		return CondCode::VC;
	case FCmpInst::FCMP_UNO:
		return CondCode::VS;
	case FCmpInst::FCMP_UEQ:
		return CondCode::EQ;
	case FCmpInst::FCMP_UGT:
		return CondCode::HI;
	case FCmpInst::FCMP_UGE:
		return CondCode::PL;
	case FCmpInst::FCMP_ULT:
		return CondCode::LT;
	case FCmpInst::FCMP_ULE:
		return CondCode::LE;
	case FCmpInst::FCMP_UNE:
		return CondCode::NE;
	case FCmpInst::FCMP_TRUE:
		return CondCode::AL;
	}
	std::abort();
}

} // namespace backend::aarch64::generated
