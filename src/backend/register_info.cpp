// This file centralizes physical-register classes, ABI roles, names, and
// allocation orders shared by instruction selection, register allocation,
// frame lowering, and assembly emission.
#include "../include/backend/target.hpp"

#include <array>

namespace backend::aarch64 {
namespace {

constexpr unsigned regNumber(PhysReg reg) { return static_cast<unsigned>(reg); }

bool isX(PhysReg reg) { return reg >= PhysReg::X0 && reg <= PhysReg::X30; }

bool isV(PhysReg reg) { return reg >= PhysReg::V0 && reg <= PhysReg::V31; }

} // namespace

RegClass RegisterInfo::classForType(ValueType type) {
	switch (type) {
	case ValueType::I1:
	case ValueType::I32:
		return RegClass::GPR32;
	case ValueType::I64:
	case ValueType::Ptr:
		return RegClass::GPR64;
	case ValueType::F32:
		return RegClass::FPR32;
	case ValueType::V4I32:
	case ValueType::V4F32:
		return RegClass::NEON128;
	case ValueType::Flags:
		return RegClass::CCR;
	default:
		return RegClass::Invalid;
	}
}

bool RegisterInfo::isGPR(PhysReg reg) {
	return isX(reg) || reg == PhysReg::SP || reg == PhysReg::XZR;
}

bool RegisterInfo::isVector(PhysReg reg) { return isV(reg); }

bool RegisterInfo::aliases(PhysReg lhs, PhysReg rhs) {
	return lhs != PhysReg::NoReg && lhs == rhs;
}

bool RegisterInfo::isReserved(PhysReg reg) {
	return reg == PhysReg::NoReg || reg == PhysReg::SP || reg == PhysReg::XZR ||
	       reg == PhysReg::X18 || reg == PhysReg::X29 || reg == PhysReg::X30 ||
	       reg == PhysReg::NZCV;
}

bool RegisterInfo::isCallerSaved(PhysReg reg) {
	if (reg >= PhysReg::X0 && reg <= PhysReg::X17)
		return reg != PhysReg::X18;
	return reg >= PhysReg::V0 && reg <= PhysReg::V7 ||
	       reg >= PhysReg::V16 && reg <= PhysReg::V31;
}

bool RegisterInfo::isCalleeSaved(PhysReg reg) {
	return reg >= PhysReg::X19 && reg <= PhysReg::X28 ||
	       reg >= PhysReg::V8 && reg <= PhysReg::V15;
}

bool RegisterInfo::isArgumentRegister(PhysReg reg) {
	return (reg >= PhysReg::X0 && reg <= PhysReg::X7) ||
	       (reg >= PhysReg::V0 && reg <= PhysReg::V7);
}

bool RegisterInfo::isReturnRegister(PhysReg reg) {
	return reg == PhysReg::X0 || reg == PhysReg::V0;
}

PhysReg RegisterInfo::integerArgumentRegister(unsigned index) {
	return static_cast<PhysReg>(static_cast<unsigned>(PhysReg::X0) + index);
}

PhysReg RegisterInfo::vectorArgumentRegister(unsigned index) {
	return static_cast<PhysReg>(static_cast<unsigned>(PhysReg::V0) + index);
}

std::string_view RegisterInfo::name(PhysReg reg, RegClass view) {
	static const std::array<std::string_view, 31> xNames = {
	    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
	    "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
	    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
	    "x24", "x25", "x26", "x27", "x28", "x29", "x30"};
	static const std::array<std::string_view, 31> wNames = {
	    "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",
	    "w8",  "w9",  "w10", "w11", "w12", "w13", "w14", "w15",
	    "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
	    "w24", "w25", "w26", "w27", "w28", "w29", "w30"};
	static const std::array<std::string_view, 32> vNames = {
	    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
	    "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
	    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
	    "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"};
	static const std::array<std::string_view, 32> sNames = {
	    "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
	    "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15",
	    "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
	    "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31"};

	if (isX(reg)) {
		unsigned index = regNumber(reg) - regNumber(PhysReg::X0);
		return view == RegClass::GPR32 ? wNames[index] : xNames[index];
	}
	if (isV(reg)) {
		unsigned index = regNumber(reg) - regNumber(PhysReg::V0);
		return view == RegClass::FPR32 ? sNames[index] : vNames[index];
	}
	if (reg == PhysReg::SP)
		return view == RegClass::GPR32 ? "wsp" : "sp";
	if (reg == PhysReg::XZR)
		return view == RegClass::GPR32 ? "wzr" : "xzr";
	if (reg == PhysReg::NZCV)
		return "nzcv";
	return "<noreg>";
}

unsigned RegisterInfo::spillSize(RegClass regClass) {
	return regClass == RegClass::NEON128 ? 16
	       : regClass == RegClass::GPR64 ? 8
	                                     : 4;
}

unsigned RegisterInfo::spillAlignment(RegClass regClass) {
	return spillSize(regClass);
}

const std::vector<PhysReg> &
RegisterInfo::allocationOrder(RegClass regClass, bool preferCallerSaved) {
	static const std::vector<PhysReg> volatileFirstGprs = {
	    PhysReg::X9,  PhysReg::X10, PhysReg::X11, PhysReg::X12, PhysReg::X13,
	    PhysReg::X14, PhysReg::X15, PhysReg::X16, PhysReg::X17, PhysReg::X8,
	    PhysReg::X7,  PhysReg::X6,  PhysReg::X5,  PhysReg::X4,  PhysReg::X3,
	    PhysReg::X2,  PhysReg::X1,  PhysReg::X0,  PhysReg::X19, PhysReg::X20,
	    PhysReg::X21, PhysReg::X22, PhysReg::X23, PhysReg::X24, PhysReg::X25,
	    PhysReg::X26, PhysReg::X27, PhysReg::X28};
	static const std::vector<PhysReg> preservedFirstGprs = {
	    PhysReg::X9,  PhysReg::X10, PhysReg::X11, PhysReg::X12, PhysReg::X13,
	    PhysReg::X14, PhysReg::X15, PhysReg::X16, PhysReg::X17, PhysReg::X19,
	    PhysReg::X20, PhysReg::X21, PhysReg::X22, PhysReg::X23, PhysReg::X24,
	    PhysReg::X25, PhysReg::X26, PhysReg::X27, PhysReg::X28, PhysReg::X8,
	    PhysReg::X7,  PhysReg::X6,  PhysReg::X5,  PhysReg::X4,  PhysReg::X3,
	    PhysReg::X2,  PhysReg::X1,  PhysReg::X0};
	static const std::vector<PhysReg> volatileFirstVectors = {
	    PhysReg::V16, PhysReg::V17, PhysReg::V18, PhysReg::V19, PhysReg::V20,
	    PhysReg::V21, PhysReg::V22, PhysReg::V23, PhysReg::V24, PhysReg::V25,
	    PhysReg::V26, PhysReg::V27, PhysReg::V28, PhysReg::V29, PhysReg::V30,
	    PhysReg::V31, PhysReg::V7,  PhysReg::V6,  PhysReg::V5,  PhysReg::V4,
	    PhysReg::V3,  PhysReg::V2,  PhysReg::V1,  PhysReg::V0,  PhysReg::V8,
	    PhysReg::V9,  PhysReg::V10, PhysReg::V11, PhysReg::V12, PhysReg::V13,
	    PhysReg::V14, PhysReg::V15};
	static const std::vector<PhysReg> preservedFirstVectors = {
	    PhysReg::V16, PhysReg::V17, PhysReg::V18, PhysReg::V19, PhysReg::V20,
	    PhysReg::V21, PhysReg::V22, PhysReg::V23, PhysReg::V24, PhysReg::V25,
	    PhysReg::V26, PhysReg::V27, PhysReg::V28, PhysReg::V29, PhysReg::V30,
	    PhysReg::V31, PhysReg::V8,  PhysReg::V9,  PhysReg::V10, PhysReg::V11,
	    PhysReg::V12, PhysReg::V13, PhysReg::V14, PhysReg::V15, PhysReg::V7,
	    PhysReg::V6,  PhysReg::V5,  PhysReg::V4,  PhysReg::V3,  PhysReg::V2,
	    PhysReg::V1,  PhysReg::V0};
	static const std::vector<PhysReg> none;
	if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
		return preferCallerSaved ? volatileFirstGprs : preservedFirstGprs;
	if (regClass == RegClass::FPR32 || regClass == RegClass::NEON128)
		return preferCallerSaved ? volatileFirstVectors : preservedFirstVectors;
	return none;
}

const std::vector<PhysReg> &RegisterInfo::calleeSaved(RegClass regClass) {
	static const std::vector<PhysReg> gprs = {
	    PhysReg::X19, PhysReg::X20, PhysReg::X21, PhysReg::X22, PhysReg::X23,
	    PhysReg::X24, PhysReg::X25, PhysReg::X26, PhysReg::X27, PhysReg::X28};
	static const std::vector<PhysReg> vectors = {
	    PhysReg::V8,  PhysReg::V9,  PhysReg::V10, PhysReg::V11,
	    PhysReg::V12, PhysReg::V13, PhysReg::V14, PhysReg::V15};
	static const std::vector<PhysReg> none;
	if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
		return gprs;
	if (regClass == RegClass::FPR32 || regClass == RegClass::NEON128)
		return vectors;
	return none;
}

} // namespace backend::aarch64
