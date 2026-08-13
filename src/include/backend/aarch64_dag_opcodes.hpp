// This file defines SelectionDAG opcodes and their names.
// Generated from aarch64.td by generate.py.
// Edit the target description or generator, not this file.
#pragma once

#include <cstdint>

namespace backend::aarch64 {

enum class SDOpcode : std::uint16_t {
    Invalid,
    EntryToken,
    Argument,
    Constant,
    FPConstant,
    GlobalAddress,
    FrameIndex,
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
    FNeg,
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
    Load,
    Store,
    ZExt,
    SExt,
    Trunc,
    FPToSI,
    SIToFP,
    Bitcast,
    Clz,
    Splat,
    InsertElement,
    ExtractElement,
    ShuffleVector,
    Phi,
    Call,
    TailCall,
    Branch,
    BranchCond,
    Return,
    MAdd,
    MSub,
    FMAdd,
    FMSub,
    VectorReduceAdd,
    SMin,
    SMax,
    MulMod,
    Count,
};

constexpr const char *sdOpcodeName(SDOpcode opcode) {
    switch (opcode) {
    case SDOpcode::Invalid: return "Invalid";
    case SDOpcode::EntryToken: return "EntryToken";
    case SDOpcode::Argument: return "Argument";
    case SDOpcode::Constant: return "Constant";
    case SDOpcode::FPConstant: return "FPConstant";
    case SDOpcode::GlobalAddress: return "GlobalAddress";
    case SDOpcode::FrameIndex: return "FrameIndex";
    case SDOpcode::Add: return "Add";
    case SDOpcode::Sub: return "Sub";
    case SDOpcode::Mul: return "Mul";
    case SDOpcode::SDiv: return "SDiv";
    case SDOpcode::SRem: return "SRem";
    case SDOpcode::UDiv: return "UDiv";
    case SDOpcode::URem: return "URem";
    case SDOpcode::FAdd: return "FAdd";
    case SDOpcode::FSub: return "FSub";
    case SDOpcode::FMul: return "FMul";
    case SDOpcode::FDiv: return "FDiv";
    case SDOpcode::FNeg: return "FNeg";
    case SDOpcode::Shl: return "Shl";
    case SDOpcode::LShr: return "LShr";
    case SDOpcode::AShr: return "AShr";
    case SDOpcode::And: return "And";
    case SDOpcode::Or: return "Or";
    case SDOpcode::Xor: return "Xor";
    case SDOpcode::ICmp: return "ICmp";
    case SDOpcode::FCmp: return "FCmp";
    case SDOpcode::Select: return "Select";
    case SDOpcode::GEP: return "GEP";
    case SDOpcode::Load: return "Load";
    case SDOpcode::Store: return "Store";
    case SDOpcode::ZExt: return "ZExt";
    case SDOpcode::SExt: return "SExt";
    case SDOpcode::Trunc: return "Trunc";
    case SDOpcode::FPToSI: return "FPToSI";
    case SDOpcode::SIToFP: return "SIToFP";
    case SDOpcode::Bitcast: return "Bitcast";
    case SDOpcode::Clz: return "Clz";
    case SDOpcode::Splat: return "Splat";
    case SDOpcode::InsertElement: return "InsertElement";
    case SDOpcode::ExtractElement: return "ExtractElement";
    case SDOpcode::ShuffleVector: return "ShuffleVector";
    case SDOpcode::Phi: return "Phi";
    case SDOpcode::Call: return "Call";
    case SDOpcode::TailCall: return "TailCall";
    case SDOpcode::Branch: return "Branch";
    case SDOpcode::BranchCond: return "BranchCond";
    case SDOpcode::Return: return "Return";
    case SDOpcode::MAdd: return "MAdd";
    case SDOpcode::MSub: return "MSub";
    case SDOpcode::FMAdd: return "FMAdd";
    case SDOpcode::FMSub: return "FMSub";
    case SDOpcode::VectorReduceAdd: return "VectorReduceAdd";
    case SDOpcode::SMin: return "SMin";
    case SDOpcode::SMax: return "SMax";
    case SDOpcode::MulMod: return "MulMod";
    case SDOpcode::Count: break;
    }
    return "Invalid";
}

} // namespace backend::aarch64
