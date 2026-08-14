// This file implements target queries that require executable logic.  Static
// instruction facts live in target_desc/aarch64.td and are generated at build
// time.
#include "backend/target.hpp"

#include "backend/aarch64_instruction_info.hpp"

namespace backend::aarch64 {
namespace {

bool isLogicalImmediate32(std::uint32_t value) {
	if (value == 0 || value == UINT32_MAX)
		return false;

	for (unsigned elementBits = 2; elementBits <= 32; elementBits *= 2) {
		const std::uint32_t elementMask =
		    elementBits == 32 ? UINT32_MAX
		                      : (std::uint32_t{1} << elementBits) - 1;
		const std::uint32_t element = value & elementMask;
		std::uint32_t replicated = 0;
		for (unsigned offset = 0; offset < 32; offset += elementBits)
			replicated |= element << offset;
		if (replicated != value)
			continue;

		for (unsigned ones = 1; ones < elementBits; ++ones) {
			const std::uint32_t run = (std::uint32_t{1} << ones) - 1;
			for (unsigned rotate = 0; rotate < elementBits; ++rotate) {
				const std::uint32_t rotated =
				    rotate == 0
				        ? run
				        : ((run >> rotate) | (run << (elementBits - rotate))) &
				              elementMask;
				if (rotated == element)
					return true;
			}
		}
	}
	return false;
}

} // namespace

const InstrDesc &InstrInfo::get(Opcode opcode) {
	return generated::instructionDescriptor(opcode);
}

bool InstrInfo::acceptsImmediate(Opcode opcode, std::int64_t immediate) {
	using generated::ImmediateConstraint;
	switch (generated::immediateConstraint(opcode)) {
	case ImmediateConstraint::AddSub12:
		return immediate >= 0 &&
		       (immediate <= 4095 ||
		        (immediate % 4096 == 0 && immediate / 4096 <= 4095));
	case ImmediateConstraint::Shift32:
		return immediate >= 0 && immediate <= 31;
	case ImmediateConstraint::Shift64:
		return immediate >= 0 && immediate <= 63;
	case ImmediateConstraint::Logical32:
		return immediate >= 0 && immediate <= UINT32_MAX &&
		       isLogicalImmediate32(static_cast<std::uint32_t>(immediate));
	case ImmediateConstraint::None:
		return false;
	}
	return false;
}

bool InstrInfo::isCommutable(Opcode opcode) {
	return generated::isCommutable(opcode);
}

unsigned InstrInfo::rematerializationCost(Opcode opcode) {
	return generated::rematerializationCost(opcode);
}

CondCode InstrInfo::inverseCondition(CondCode condition) {
	switch (condition) {
	case CondCode::EQ:
		return CondCode::NE;
	case CondCode::NE:
		return CondCode::EQ;
	case CondCode::HS:
		return CondCode::LO;
	case CondCode::LO:
		return CondCode::HS;
	case CondCode::MI:
		return CondCode::PL;
	case CondCode::PL:
		return CondCode::MI;
	case CondCode::VS:
		return CondCode::VC;
	case CondCode::VC:
		return CondCode::VS;
	case CondCode::HI:
		return CondCode::LS;
	case CondCode::LS:
		return CondCode::HI;
	case CondCode::GE:
		return CondCode::LT;
	case CondCode::LT:
		return CondCode::GE;
	case CondCode::GT:
		return CondCode::LE;
	case CondCode::LE:
		return CondCode::GT;
	case CondCode::AL:
		return CondCode::AL;
	}
	return CondCode::AL;
}

} // namespace backend::aarch64
