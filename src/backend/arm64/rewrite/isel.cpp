#include "../../../include/backend/arm64/rewrite/isel.hpp"
#include "../../../include/backend/arm64/rewrite/constant_division.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

bool isPowerOfTwo(unsigned value) {
    return value && (value & (value - 1)) == 0;
}

unsigned log2Exact(unsigned value) {
    unsigned result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

CondCode integerCondition(int predicate) {
    switch (static_cast<ICmpInst::ICmpOp>(predicate)) {
    case ICmpInst::ICMP_EQ: return CondCode::EQ;
    case ICmpInst::ICMP_NE: return CondCode::NE;
    case ICmpInst::ICMP_UGT: return CondCode::HI;
    case ICmpInst::ICMP_UGE: return CondCode::HS;
    case ICmpInst::ICMP_ULT: return CondCode::LO;
    case ICmpInst::ICMP_ULE: return CondCode::LS;
    case ICmpInst::ICMP_SGT: return CondCode::GT;
    case ICmpInst::ICMP_SGE: return CondCode::GE;
    case ICmpInst::ICMP_SLT: return CondCode::LT;
    case ICmpInst::ICMP_SLE: return CondCode::LE;
    }
    throw std::logic_error("unknown integer predicate");
}

CondCode floatingCondition(int predicate) {
    switch (static_cast<FCmpInst::FCmpOp>(predicate)) {
    case FCmpInst::FCMP_OEQ: return CondCode::EQ;
    case FCmpInst::FCMP_OGT: return CondCode::GT;
    case FCmpInst::FCMP_OGE: return CondCode::GE;
    case FCmpInst::FCMP_OLT: return CondCode::MI;
    case FCmpInst::FCMP_OLE: return CondCode::LS;
    case FCmpInst::FCMP_ONE: return CondCode::NE;
    case FCmpInst::FCMP_UEQ: return CondCode::EQ;
    case FCmpInst::FCMP_UGT: return CondCode::HI;
    case FCmpInst::FCMP_UGE: return CondCode::PL;
    case FCmpInst::FCMP_ULT: return CondCode::LT;
    case FCmpInst::FCMP_ULE: return CondCode::LE;
    case FCmpInst::FCMP_UNE: return CondCode::NE;
    case FCmpInst::FCMP_ORD: return CondCode::VC;
    case FCmpInst::FCMP_UNO: return CondCode::VS;
    case FCmpInst::FCMP_FALSE: return CondCode::AL;
    case FCmpInst::FCMP_TRUE: return CondCode::AL;
    }
    throw std::logic_error("unknown floating predicate");
}

RegisterMask callPreservedMask() {
    RegisterMask mask;
    mask.setPreserved(PhysReg::SP);
    mask.setPreserved(PhysReg::X18);
    mask.setPreserved(PhysReg::X19);
    mask.setPreserved(PhysReg::X20);
    mask.setPreserved(PhysReg::X21);
    mask.setPreserved(PhysReg::X22);
    mask.setPreserved(PhysReg::X23);
    mask.setPreserved(PhysReg::X24);
    mask.setPreserved(PhysReg::X25);
    mask.setPreserved(PhysReg::X26);
    mask.setPreserved(PhysReg::X27);
    mask.setPreserved(PhysReg::X28);
    mask.setPreserved(PhysReg::X29);
    for (PhysReg reg : RegisterInfo::calleeSaved(RegClass::NEON128))
        mask.setPreserved(reg);
    return mask;
}

PhysReg integerArgumentRegister(unsigned index) {
    return static_cast<PhysReg>(
        static_cast<unsigned>(PhysReg::X0) + index);
}

PhysReg vectorArgumentRegister(unsigned index) {
    return static_cast<PhysReg>(
        static_cast<unsigned>(PhysReg::V0) + index);
}

} // namespace

std::unique_ptr<MachineFunction>
AArch64InstructionSelector::select(FunctionDAG &functionDAG) const {
    auto machineFunction =
        std::make_unique<MachineFunction>(functionDAG.function->name_);
    machineFunction->setProperty(MachineProperty::IsSSA);
    machineFunction->setProperty(MachineProperty::HasPHIs);
    machineFunction->setProperty(MachineProperty::Legalized);
    machineFunction->setProperty(MachineProperty::Selected);

    std::unordered_map<BasicBlock *, MachineBasicBlock *> blocks;
    for (BasicBlock *block : functionDAG.blockOrder)
        blocks.emplace(
            block,
            &machineFunction->createBlock(
                block->name_.empty() ? "bb" : block->name_));
    for (BasicBlock *block : functionDAG.blockOrder)
        for (BasicBlock *successor : block->succ_bbs_)
            blocks.at(block)->addSuccessor(blocks.at(successor));

    auto &registerInfo = machineFunction->registerInfo();
    std::unordered_map<SDNode *, VReg> results;
    std::unordered_map<SDNode *, bool> directGlobalMemory;
    for (BasicBlock *block : functionDAG.blockOrder)
        for (const auto &owned : functionDAG.blocks.at(block)->nodes())
            if (owned->opcode() == SDOpcode::GlobalAddress)
                directGlobalMemory.emplace(owned.get(), true);
    for (BasicBlock *block : functionDAG.blockOrder) {
        for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
            SDNode &user = *owned;
            for (unsigned index = 0; index < user.operands().size();
                 ++index) {
                SDNode *operand = user.operands()[index].node;
                auto global = directGlobalMemory.find(operand);
                if (global == directGlobalMemory.end())
                    continue;
                bool directLoad =
                    user.opcode() == SDOpcode::Load && index == 1;
                bool directStore =
                    user.opcode() == SDOpcode::Store && index == 2;
                if (!directLoad && !directStore)
                    global->second = false;
            }
        }
    }
    // Allocate result identities before emission so loop PHIs can reference
    // backedge definitions that are emitted later.
    for (BasicBlock *block : functionDAG.blockOrder) {
        for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
            SDNode &node = *owned;
            if (node.opcode() == SDOpcode::EntryToken ||
                node.opcode() == SDOpcode::Invalid ||
                node.resultTypes().empty() ||
                node.resultTypes().front() == ValueType::Invalid)
                continue;
            ValueType type = node.resultTypes().front();
            RegClass regClass = RegisterInfo::classForType(type);
            if (regClass == RegClass::Invalid)
                throw std::logic_error("selected value has no register class");
            results.emplace(
                &node, registerInfo.createVirtualRegister(regClass, type));
        }
    }

    auto resultReg = [&](SDValue value) -> VReg {
        auto found = results.find(value.node);
        if (found == results.end())
            throw std::logic_error("DAG operand has no selected register");
        return found->second;
    };
    auto valueClass = [&](SDValue value) {
        return registerInfo.get(resultReg(value)).regClass;
    };
    auto use = [&](SDValue value) {
        VReg reg = resultReg(value);
        return MachineOperand::vreg(reg, registerInfo.get(reg).regClass);
    };
    auto define = [&](SDNode &node) {
        VReg reg = results.at(&node);
        return MachineOperand::vreg(reg, registerInfo.get(reg).regClass, true);
    };
    auto append = [&](MachineBasicBlock &block, MachineInstr instruction,
                      SDNode *definition = nullptr) -> MachineInstr & {
        MachineInstr &inserted = block.append(std::move(instruction));
        if (definition && results.count(definition))
            registerInfo.setDefinition(results.at(definition), &inserted);
        return inserted;
    };
    auto createTemporary = [&](ValueType type) {
        return registerInfo.createVirtualRegister(
            RegisterInfo::classForType(type), type);
    };
    auto emitSignedConstantDivision =
        [&](MachineBasicBlock &block, MachineOperand destination,
            MachineOperand numerator, std::int32_t divisor,
            SDNode *definition = nullptr) -> bool {
        division::SignedDivisorInfo info =
            division::analyzeSignedDivisor(divisor);
        if (!info.reducible)
            return false;

        auto appendTemporary =
            [&](MachineInstr instruction, VReg reg) -> MachineOperand {
            MachineInstr &inserted =
                append(block, std::move(instruction));
            registerInfo.setDefinition(reg, &inserted);
            return MachineOperand::vreg(
                reg, registerInfo.get(reg).regClass);
        };
        auto temporaryUnary =
            [&](Opcode opcode, ValueType type, MachineOperand source,
                std::optional<std::int64_t> immediate =
                    std::nullopt) -> MachineOperand {
            VReg reg = createTemporary(type);
            MachineInstr instruction(opcode);
            instruction
                .addOperand(MachineOperand::vreg(
                    reg, registerInfo.get(reg).regClass, true))
                .addOperand(std::move(source));
            if (immediate)
                instruction.addOperand(
                    MachineOperand::immediate(*immediate));
            return appendTemporary(std::move(instruction), reg);
        };
        auto temporaryBinary =
            [&](Opcode opcode, ValueType type, MachineOperand lhs,
                MachineOperand rhs) -> MachineOperand {
            VReg reg = createTemporary(type);
            MachineInstr instruction(opcode);
            instruction
                .addOperand(MachineOperand::vreg(
                    reg, registerInfo.get(reg).regClass, true))
                .addOperand(std::move(lhs))
                .addOperand(std::move(rhs));
            return appendTemporary(std::move(instruction), reg);
        };
        auto appendFinal = [&](MachineInstr instruction) {
            MachineInstr &inserted =
                append(block, std::move(instruction), definition);
            if (!definition && destination.isVirtualRegister())
                registerInfo.setDefinition(
                    destination.virtualRegister(), &inserted);
        };

        if (divisor == 1) {
            MachineInstr instruction(Opcode::COPY);
            instruction.addOperand(destination).addOperand(numerator);
            appendFinal(std::move(instruction));
        } else if (divisor == -1) {
            MachineInstr instruction(Opcode::NEGW);
            instruction.addOperand(destination).addOperand(numerator);
            appendFinal(std::move(instruction));
        } else if (info.powerOfTwo) {
            MachineOperand adjusted;
            if (info.shift == 1) {
                VReg adjustedReg =
                    createTemporary(ValueType::I32);
                MachineInstr addBias(Opcode::ADDWrs);
                addBias
                    .addOperand(MachineOperand::vreg(
                        adjustedReg, RegClass::GPR32, true))
                    .addOperand(numerator)
                    .addOperand(numerator)
                    .addOperand(MachineOperand::immediate(31));
                adjusted = appendTemporary(
                    std::move(addBias), adjustedReg);
            } else {
                MachineOperand sign = temporaryUnary(
                    Opcode::ASRWri, ValueType::I32, numerator, 31);
                MachineOperand bias = temporaryUnary(
                    Opcode::LSRWri, ValueType::I32, sign,
                    32 - info.shift);
                adjusted = temporaryBinary(
                    Opcode::ADDWrr, ValueType::I32, numerator, bias);
            }
            if (divisor < 0) {
                MachineOperand quotient = temporaryUnary(
                    Opcode::ASRWri, ValueType::I32, adjusted,
                    info.shift);
                MachineInstr negate(Opcode::NEGW);
                negate.addOperand(destination).addOperand(quotient);
                appendFinal(std::move(negate));
            } else {
                MachineInstr shift(Opcode::ASRWri);
                shift.addOperand(destination)
                    .addOperand(adjusted)
                    .addOperand(
                        MachineOperand::immediate(info.shift));
                appendFinal(std::move(shift));
            }
        } else {
            division::MagicNumber magic =
                division::computeSignedMagic(divisor);
            VReg multiplierReg = createTemporary(ValueType::I32);
            MachineInstr materialize(Opcode::MOVi32);
            materialize
                .addOperand(MachineOperand::vreg(
                    multiplierReg, RegClass::GPR32, true))
                .addOperand(
                    MachineOperand::immediate(magic.multiplier));
            MachineOperand multiplier = appendTemporary(
                std::move(materialize), multiplierReg);

            MachineOperand product = temporaryBinary(
                Opcode::SMULLXrr, ValueType::Ptr, numerator,
                multiplier);
            if (magic.strategy ==
                division::MagicStrategy::MultiplyShift) {
                MachineOperand shifted = temporaryUnary(
                    Opcode::ASRXri, ValueType::Ptr, product,
                    32 + magic.shift);
                MachineInstr add(Opcode::ADDWrsX);
                add.addOperand(destination)
                    .addOperand(shifted)
                    .addOperand(shifted)
                    .addOperand(MachineOperand::immediate(31));
                appendFinal(std::move(add));
                return true;
            }
            MachineOperand highX = temporaryUnary(
                Opcode::ASRXri, ValueType::Ptr, product, 32);
            MachineOperand high = temporaryUnary(
                Opcode::COPYXtoW, ValueType::I32, highX);
            if (magic.strategy ==
                division::MagicStrategy::MultiplyAddShift)
                high = temporaryBinary(
                    Opcode::ADDWrr, ValueType::I32, high,
                    numerator);
            else
                high = temporaryBinary(
                    Opcode::SUBWrr, ValueType::I32, high,
                    numerator);
            if (magic.shift)
                high = temporaryUnary(
                    Opcode::ASRWri, ValueType::I32, high,
                    magic.shift);
            // The final round-toward-zero correction depends on the sign of
            // the approximate quotient, not the dividend.  They have the
            // same sign for positive divisors, which can hide this
            // distinction; for a negative divisor the quotient sign is
            // reversed.
            MachineInstr add(Opcode::ADDWrs);
            add.addOperand(destination)
                .addOperand(high)
                .addOperand(high)
                .addOperand(MachineOperand::immediate(31));
            appendFinal(std::move(add));
        }
        return true;
    };
    auto emitVectorConstant =
        [&](MachineBasicBlock &block, MachineOperand destination,
            const std::array<std::uint32_t, 4> &lanes,
            SDNode *definition = nullptr) {
        std::vector<unsigned> nonzeroLanes;
        for (unsigned lane = 0; lane < lanes.size(); ++lane)
            if (lanes[lane])
                nonzeroLanes.push_back(lane);

        if (nonzeroLanes.empty()) {
            MachineInstr zero(Opcode::MOVIv4Zero);
            zero.addOperand(destination);
            MachineInstr &inserted =
                append(block, std::move(zero), definition);
            if (!definition && destination.isVirtualRegister())
                registerInfo.setDefinition(
                    destination.virtualRegister(), &inserted);
            return;
        }

        VReg current = createTemporary(ValueType::V4I32);
        MachineInstr zero(Opcode::MOVIv4Zero);
        zero.addOperand(MachineOperand::vreg(
            current, RegClass::NEON128, true));
        MachineInstr &zeroDefinition = append(block, std::move(zero));
        registerInfo.setDefinition(current, &zeroDefinition);

        for (std::size_t index = 0;
             index < nonzeroLanes.size(); ++index) {
            unsigned lane = nonzeroLanes[index];
            VReg scalar = createTemporary(ValueType::I32);
            MachineInstr materialize(Opcode::MOVi32);
            materialize
                .addOperand(MachineOperand::vreg(
                    scalar, RegClass::GPR32, true))
                .addOperand(MachineOperand::immediate(lanes[lane]));
            MachineInstr &scalarDefinition =
                append(block, std::move(materialize));
            registerInfo.setDefinition(scalar, &scalarDefinition);

            bool last = index + 1 == nonzeroLanes.size();
            VReg next = last && destination.isVirtualRegister()
                            ? destination.virtualRegister()
                            : createTemporary(ValueType::V4I32);
            MachineInstr insert(Opcode::INSv4i32);
            insert
                .addOperand(MachineOperand::vreg(
                    next, RegClass::NEON128, true))
                .addOperand(MachineOperand::vreg(
                    current, RegClass::NEON128))
                .addOperand(MachineOperand::vreg(
                    scalar, RegClass::GPR32))
                .addOperand(MachineOperand::immediate(lane));
            insert.operands()[1].tiedTo = 0;
            MachineInstr &inserted = append(
                block, std::move(insert),
                last ? definition : nullptr);
            if (!last || !definition)
                registerInfo.setDefinition(next, &inserted);
            current = next;
        }
    };
    unsigned nextParallelCopyGroup = 1;
    const unsigned entryArgumentCopyGroup = nextParallelCopyGroup++;

    // FrameIndex nodes carry stable indices assigned by the DAG builder.
    std::vector<SDNode *> frameNodes;
    for (BasicBlock *block : functionDAG.blockOrder)
        for (const auto &node : functionDAG.blocks.at(block)->nodes())
            if (node->opcode() == SDOpcode::FrameIndex)
                frameNodes.push_back(node.get());
    std::sort(frameNodes.begin(), frameNodes.end(),
              [](SDNode *lhs, SDNode *rhs) { return lhs->index < rhs->index; });
    for (SDNode *node : frameNodes) {
        int index = machineFunction->frameInfo().createStackObject(
            node->memorySize, node->alignment, false);
        if (index != static_cast<int>(node->index))
            throw std::logic_error("DAG frame-index numbering diverged");
    }

    // Source argument indices use independent GPR and SIMD register banks.
    std::vector<unsigned> argumentBankIndex(
        functionDAG.function->arguments_.size());
    std::vector<bool> argumentIsFloat(
        functionDAG.function->arguments_.size());
    std::vector<int> argumentStackOffset(
        functionDAG.function->arguments_.size(), -1);
    unsigned integerArguments = 0;
    unsigned floatArguments = 0;
    unsigned incomingStackSize = 0;
    for (Argument *argument : functionDAG.function->arguments_) {
        ValueType type = SelectionDAGBuilder::valueType(argument->type_);
        bool isFloat = type == ValueType::F32 ||
                       type == ValueType::V4F32 ||
                       type == ValueType::V4I32;
        argumentIsFloat[argument->arg_no_] = isFloat;
        argumentBankIndex[argument->arg_no_] =
            isFloat ? floatArguments++ : integerArguments++;
        if (argumentBankIndex[argument->arg_no_] >= 8) {
            unsigned alignment =
                type == ValueType::V4I32 || type == ValueType::V4F32
                    ? 16U : 8U;
            incomingStackSize =
                (incomingStackSize + alignment - 1) / alignment * alignment;
            argumentStackOffset[argument->arg_no_] =
                static_cast<int>(incomingStackSize);
            incomingStackSize += alignment;
        }
    }

    for (BasicBlock *sourceBlock : functionDAG.blockOrder) {
        MachineBasicBlock &block = *blocks.at(sourceBlock);
        for (const auto &owned :
             functionDAG.blocks.at(sourceBlock)->nodes()) {
            SDNode &node = *owned;
            switch (node.opcode()) {
            case SDOpcode::Invalid:
            case SDOpcode::EntryToken:
                break;
            case SDOpcode::Argument: {
                ValueType type = node.resultTypes().front();
                RegClass regClass = RegisterInfo::classForType(type);
                unsigned bank = argumentBankIndex.at(node.index);
                if (bank < 8) {
                    PhysReg phys = argumentIsFloat.at(node.index)
                                       ? vectorArgumentRegister(bank)
                                       : integerArgumentRegister(bank);
                    MachineInstr copy(Opcode::COPY);
                    copy.parallelCopyGroup = entryArgumentCopyGroup;
                    copy.addOperand(define(node))
                        .addOperand(MachineOperand::physReg(phys, regClass));
                    append(block, std::move(copy), &node);
                } else {
                    int fixed = machineFunction->frameInfo().createFixedObject(
                        regClass == RegClass::NEON128 ? 16 : 8,
                        argumentStackOffset.at(node.index),
                        regClass == RegClass::NEON128 ? 16 : 8);
                    // Preserve the fixed incoming-frame reference through
                    // register allocation.  Frame lowering can then select
                    // the x29-relative scaled addressing mode directly,
                    // instead of materializing an address for every stack
                    // argument before allocation.
                    MachineInstr load(Opcode::SPILL_LOAD);
                    load.addOperand(define(node))
                        .addOperand(MachineOperand::frameIndex(fixed));
                    load.addMemoryOperand(MachineMemOperand{
                        MachineMemOperand::Access::Load,
                        regClass == RegClass::NEON128 ? 16U
                            : regClass == RegClass::GPR64 ? 8U : 4U,
                        regClass == RegClass::NEON128 ? 16U
                            : regClass == RegClass::GPR64 ? 8U : 4U,
                        nullptr, fixed, 0, false});
                    append(block, std::move(load), &node);
                }
                break;
            }
            case SDOpcode::Constant: {
                ValueType type = node.resultTypes().front();
                if (type == ValueType::V4I32 ||
                    type == ValueType::V4F32) {
                    std::array<std::uint32_t, 4> lanes{};
                    for (unsigned lane = 0; lane < lanes.size(); ++lane)
                        if (lane < node.shuffleMask.size())
                            lanes[lane] = static_cast<std::uint32_t>(
                                node.shuffleMask[lane]);
                    emitVectorConstant(
                        block, define(node), lanes, &node);
                    break;
                }
                Opcode opcode = (type == ValueType::Ptr ||
                                 type == ValueType::I64)
                                    ? Opcode::MOVi64
                                    : Opcode::MOVi32;
                MachineInstr materialize(opcode);
                materialize.addOperand(define(node))
                    .addOperand(
                        MachineOperand::immediate(node.integer));
                append(block, std::move(materialize), &node);
                registerInfo.get(results.at(&node)).rematerializable = true;
                break;
            }
            case SDOpcode::FPConstant: {
                VReg bits = createTemporary(ValueType::I32);
                MachineInstr materialize(Opcode::MOVi32);
                materialize
                    .addOperand(MachineOperand::vreg(
                        bits, RegClass::GPR32, true))
                    .addOperand(MachineOperand::immediate(
                        node.floatingBits));
                MachineInstr &bitsDefinition =
                    append(block, std::move(materialize));
                registerInfo.setDefinition(bits, &bitsDefinition);
                MachineInstr move(Opcode::FMOVSW);
                move.addOperand(define(node))
                    .addOperand(MachineOperand::vreg(
                        bits, RegClass::GPR32));
                append(block, std::move(move), &node);
                break;
            }
            case SDOpcode::GlobalAddress: {
                if (directGlobalMemory.at(&node)) {
                    MachineInstr adrp(Opcode::ADRP);
                    adrp.addOperand(define(node))
                        .addOperand(
                            MachineOperand::global(node.symbol));
                    append(block, std::move(adrp), &node);
                    break;
                }
                VReg page = createTemporary(ValueType::Ptr);
                MachineInstr adrp(Opcode::ADRP);
                adrp.addOperand(MachineOperand::vreg(
                                    page, RegClass::GPR64, true))
                    .addOperand(MachineOperand::global(node.symbol));
                MachineInstr &pageDef = append(block, std::move(adrp));
                registerInfo.setDefinition(page, &pageDef);
                MachineInstr add(Opcode::ADDlow);
                add.addOperand(define(node))
                    .addOperand(MachineOperand::vreg(page, RegClass::GPR64))
                    .addOperand(MachineOperand::global(node.symbol));
                append(block, std::move(add), &node);
                registerInfo.get(results.at(&node)).rematerializable = true;
                break;
            }
            case SDOpcode::FrameIndex: {
                MachineInstr lea(Opcode::LEA_FRAME);
                lea.addOperand(define(node))
                    .addOperand(MachineOperand::frameIndex(node.index));
                append(block, std::move(lea), &node);
                break;
            }
            case SDOpcode::Add:
            case SDOpcode::Sub:
            case SDOpcode::Mul:
            case SDOpcode::SDiv:
            case SDOpcode::UDiv:
            case SDOpcode::And:
            case SDOpcode::Or:
            case SDOpcode::Xor:
            case SDOpcode::Shl:
            case SDOpcode::LShr:
            case SDOpcode::AShr:
            case SDOpcode::FAdd:
            case SDOpcode::FSub:
            case SDOpcode::FMul:
            case SDOpcode::FDiv: {
                ValueType resultType = node.resultTypes().front();
                bool integerVector = resultType == ValueType::V4I32;
                bool floatingVector = resultType == ValueType::V4F32;
                bool integer64 = resultType == ValueType::I64;
                Opcode opcode = Opcode::Invalid;
                if (integerVector) {
                    switch (node.opcode()) {
                    case SDOpcode::Add: opcode = Opcode::ADDv4i32; break;
                    case SDOpcode::Sub: opcode = Opcode::SUBv4i32; break;
                    case SDOpcode::Mul: opcode = Opcode::MULv4i32; break;
                    case SDOpcode::And: opcode = Opcode::ANDv16i8; break;
                    case SDOpcode::Or: opcode = Opcode::ORRv16i8; break;
                    case SDOpcode::Xor: opcode = Opcode::EORv16i8; break;
                    case SDOpcode::Shl: opcode = Opcode::SSHLv4i32; break;
                    case SDOpcode::LShr: opcode = Opcode::USHLv4i32; break;
                    case SDOpcode::AShr: opcode = Opcode::SSHLv4i32; break;
                    default: break;
                    }
                } else if (floatingVector) {
                    switch (node.opcode()) {
                    case SDOpcode::FAdd: opcode = Opcode::ADDv4f32; break;
                    case SDOpcode::FSub: opcode = Opcode::SUBv4f32; break;
                    case SDOpcode::FMul: opcode = Opcode::MULv4f32; break;
                    case SDOpcode::FDiv: opcode = Opcode::DIVv4f32; break;
                    default: break;
                    }
                } else {
                    switch (node.opcode()) {
                    case SDOpcode::Add: opcode = integer64 ? Opcode::ADDXrr : Opcode::ADDWrr; break;
                    case SDOpcode::Sub: opcode = integer64 ? Opcode::SUBXrr : Opcode::SUBWrr; break;
                    case SDOpcode::Mul: opcode = integer64 ? Opcode::MULXrr : Opcode::MULWrr; break;
                    case SDOpcode::SDiv: opcode = integer64 ? Opcode::SDIVXrr : Opcode::SDIVWrr; break;
                    case SDOpcode::UDiv: opcode = integer64 ? Opcode::UDIVXrr : Opcode::UDIVWrr; break;
                    case SDOpcode::And: opcode = integer64 ? Opcode::ANDXrr : Opcode::ANDWrr; break;
                    case SDOpcode::Or: opcode = integer64 ? Opcode::ORRXrr : Opcode::ORRWrr; break;
                    case SDOpcode::Xor: opcode = integer64 ? Opcode::EORXrr : Opcode::EORWrr; break;
                    case SDOpcode::Shl: opcode = integer64 ? Opcode::LSLXrr : Opcode::LSLWrr; break;
                    case SDOpcode::LShr: opcode = integer64 ? Opcode::LSRXrr : Opcode::LSRWrr; break;
                    case SDOpcode::AShr: opcode = integer64 ? Opcode::ASRXrr : Opcode::ASRWrr; break;
                    case SDOpcode::FAdd: opcode = Opcode::FADDS; break;
                    case SDOpcode::FSub: opcode = Opcode::FSUBS; break;
                    case SDOpcode::FMul: opcode = Opcode::FMULS; break;
                    case SDOpcode::FDiv: opcode = Opcode::FDIVS; break;
                    default: break;
                    }
                }
                if (opcode == Opcode::Invalid)
                    throw std::logic_error(
                        "unsupported typed binary operation");

                SDNode *rhs = node.operands()[1].node;
                SDNode *lhs = node.operands()[0].node;
                if (!integerVector && !floatingVector && !integer64 &&
                    node.opcode() == SDOpcode::Mul) {
                    SDNode *constant = nullptr;
                    SDValue variable;
                    if (rhs &&
                        rhs->opcode() == SDOpcode::Constant) {
                        constant = rhs;
                        variable = node.operands()[0];
                    } else if (lhs &&
                               lhs->opcode() ==
                                   SDOpcode::Constant) {
                        constant = lhs;
                        variable = node.operands()[1];
                    }
                    if (constant) {
                        std::int32_t factor =
                            static_cast<std::int32_t>(
                                constant->integer);
                        std::uint32_t magnitude =
                            factor < 0
                                ? 0U -
                                      static_cast<std::uint32_t>(
                                          factor)
                                : static_cast<std::uint32_t>(
                                      factor);
                        Opcode reduced = Opcode::Invalid;
                        unsigned shift = 0;
                        if (factor == 0)
                            reduced = Opcode::MOVi32;
                        else if (magnitude == 1)
                            reduced = factor < 0
                                          ? Opcode::NEGW
                                          : Opcode::COPY;
                        else if (isPowerOfTwo(magnitude)) {
                            reduced = Opcode::LSLWri;
                            shift = log2Exact(magnitude);
                        } else if (
                            magnitude > 1 &&
                            isPowerOfTwo(magnitude - 1)) {
                            reduced = Opcode::ADDWlsl;
                            shift = log2Exact(
                                magnitude - 1);
                        }

                        if (reduced != Opcode::Invalid) {
                            bool needsNegate =
                                factor < 0 &&
                                magnitude != 1;
                            MachineOperand destination =
                                define(node);
                            VReg temporary = 0;
                            if (needsNegate) {
                                temporary =
                                    createTemporary(
                                        ValueType::I32);
                                destination =
                                    MachineOperand::vreg(
                                        temporary,
                                        RegClass::GPR32,
                                        true);
                            }
                            MachineInstr instruction(reduced);
                            instruction.addOperand(
                                destination);
                            if (factor == 0)
                                instruction.addOperand(
                                    MachineOperand::immediate(
                                        0));
                            else {
                                instruction.addOperand(
                                    use(variable));
                                if (reduced ==
                                    Opcode::LSLWri)
                                    instruction.addOperand(
                                        MachineOperand::
                                            immediate(shift));
                                else if (
                                    reduced ==
                                    Opcode::ADDWlsl)
                                    instruction
                                        .addOperand(
                                            use(variable))
                                        .addOperand(
                                            MachineOperand::
                                                immediate(
                                                    shift));
                            }
                            MachineInstr &selected =
                                append(
                                    block,
                                    std::move(instruction),
                                    needsNegate ? nullptr
                                                : &node);
                            if (needsNegate) {
                                registerInfo.setDefinition(
                                    temporary, &selected);
                                MachineInstr negate(
                                    Opcode::NEGW);
                                negate
                                    .addOperand(
                                        define(node))
                                    .addOperand(
                                        MachineOperand::vreg(
                                            temporary,
                                            RegClass::GPR32));
                                append(block,
                                       std::move(negate),
                                       &node);
                            }
                            break;
                        }
                    }
                }
                if (!integerVector && !floatingVector && !integer64 &&
                    node.opcode() == SDOpcode::SDiv && rhs &&
                    rhs->opcode() == SDOpcode::Constant &&
                    emitSignedConstantDivision(
                        block, define(node), use(node.operands()[0]),
                        static_cast<std::int32_t>(rhs->integer), &node))
                    break;
                Opcode immediateOpcode = Opcode::Invalid;
                if (!integerVector && !floatingVector &&
                    node.opcode() == SDOpcode::Add)
                    immediateOpcode = integer64 ? Opcode::ADDXri
                                                : Opcode::ADDWri;
                else if (!integerVector && !floatingVector &&
                         node.opcode() == SDOpcode::Sub)
                    immediateOpcode = integer64 ? Opcode::SUBXri
                                                : Opcode::SUBWri;
                else if (!integerVector && !floatingVector && !integer64 &&
                         node.opcode() == SDOpcode::And)
                    immediateOpcode = Opcode::ANDWri;
                else if (!integerVector && !floatingVector &&
                         node.opcode() == SDOpcode::Shl)
                    immediateOpcode = integer64 ? Opcode::LSLXri
                                                : Opcode::LSLWri;
                else if (!integerVector && !floatingVector &&
                         node.opcode() == SDOpcode::LShr)
                    immediateOpcode = integer64 ? Opcode::LSRXri
                                                : Opcode::LSRWri;
                else if (!integerVector && !floatingVector &&
                         node.opcode() == SDOpcode::AShr)
                    immediateOpcode = integer64 ? Opcode::ASRXri
                                                : Opcode::ASRWri;

                MachineInstr instruction(opcode);
                instruction.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                if (integerVector &&
                    (node.opcode() == SDOpcode::LShr ||
                     node.opcode() == SDOpcode::AShr)) {
                    VReg negated = createTemporary(ValueType::V4I32);
                    MachineInstr negate(Opcode::NEGv4i32);
                    negate.addOperand(MachineOperand::vreg(
                                          negated, RegClass::NEON128,
                                          true))
                        .addOperand(use(node.operands()[1]));
                    MachineInstr &negateDef =
                        append(block, std::move(negate));
                    registerInfo.setDefinition(negated, &negateDef);
                    instruction.addOperand(MachineOperand::vreg(
                        negated, RegClass::NEON128));
                    append(block, std::move(instruction), &node);
                    break;
                }
                if (rhs && rhs->opcode() == SDOpcode::Constant &&
                    immediateOpcode != Opcode::Invalid &&
                    InstrInfo::acceptsImmediate(immediateOpcode,
                                                rhs->integer)) {
                    instruction.setOpcode(immediateOpcode);
                    instruction.addOperand(
                        MachineOperand::immediate(rhs->integer));
                } else {
                    instruction.addOperand(use(node.operands()[1]));
                }
                append(block, std::move(instruction), &node);
                break;
            }
            case SDOpcode::SMin:
            case SDOpcode::SMax: {
                ValueType resultType = node.resultTypes().front();
                if (resultType == ValueType::V4I32) {
                    MachineInstr instruction(
                        node.opcode() == SDOpcode::SMin ? Opcode::SMINv4i32
                                                        : Opcode::SMAXv4i32);
                    instruction.addOperand(define(node))
                        .addOperand(use(node.operands()[0]))
                        .addOperand(use(node.operands()[1]));
                    append(block, std::move(instruction), &node);
                    break;
                }
                if (resultType != ValueType::I32)
                    throw std::logic_error("unsupported signed min/max type");

                MachineInstr compare(Opcode::CMPWrr);
                compare.addOperand(use(node.operands()[0]))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, true, true));
                append(block, std::move(compare));

                MachineInstr select(Opcode::CSELW);
                select.addOperand(define(node))
                    .addOperand(use(node.operands()[0]))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(MachineOperand::condition(
                        node.opcode() == SDOpcode::SMin ? CondCode::LT
                                                        : CondCode::GT))
                    .addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, false, true));
                append(block, std::move(select), &node);
                break;
            }
            case SDOpcode::SRem:
            case SDOpcode::URem: {
                bool integer64 =
                    node.resultTypes().front() == ValueType::I64;
                SDNode *rhs = node.operands()[1].node;
                if (!integer64 && node.opcode() == SDOpcode::SRem && rhs &&
                    rhs->opcode() == SDOpcode::Constant) {
                    auto divisor =
                        static_cast<std::int32_t>(rhs->integer);
                    division::SignedDivisorInfo info =
                        division::analyzeSignedDivisor(divisor);
                    if (info.reducible && info.magnitude == 1) {
                        MachineInstr zero(Opcode::MOVi32);
                        zero.addOperand(define(node))
                            .addOperand(MachineOperand::immediate(0));
                        append(block, std::move(zero), &node);
                        break;
                    }
                    if (info.reducible && info.powerOfTwo) {
                        MachineOperand numerator =
                            use(node.operands()[0]);
                        MachineInstr compare(Opcode::CMPWri);
                        compare.addOperand(numerator)
                            .addOperand(MachineOperand::immediate(0))
                            .addOperand(MachineOperand::physReg(
                                PhysReg::NZCV, RegClass::CCR,
                                true, true));
                        append(block, std::move(compare));

                        MachineOperand magnitude = numerator;
                        if (info.shift != 1) {
                            VReg absolute =
                                createTemporary(ValueType::I32);
                            MachineInstr negate(Opcode::CNEGW);
                            negate
                                .addOperand(MachineOperand::vreg(
                                    absolute, RegClass::GPR32,
                                    true))
                                .addOperand(numerator)
                                .addOperand(MachineOperand::condition(
                                    CondCode::MI))
                                .addOperand(MachineOperand::physReg(
                                    PhysReg::NZCV, RegClass::CCR,
                                    false, true));
                            MachineInstr &absoluteDefinition =
                                append(block, std::move(negate));
                            registerInfo.setDefinition(
                                absolute, &absoluteDefinition);
                            magnitude = MachineOperand::vreg(
                                absolute, RegClass::GPR32);
                        }

                        VReg masked =
                            createTemporary(ValueType::I32);
                        MachineInstr mask(Opcode::ANDWri);
                        mask.addOperand(MachineOperand::vreg(
                                            masked, RegClass::GPR32,
                                            true))
                            .addOperand(magnitude)
                            .addOperand(MachineOperand::immediate(
                                static_cast<std::uint32_t>(
                                    info.magnitude - 1)));
                        MachineInstr &maskedDefinition =
                            append(block, std::move(mask));
                        registerInfo.setDefinition(
                            masked, &maskedDefinition);

                        MachineInstr restoreSign(Opcode::CNEGW);
                        restoreSign.addOperand(define(node))
                            .addOperand(MachineOperand::vreg(
                                masked, RegClass::GPR32))
                            .addOperand(MachineOperand::condition(
                                CondCode::MI))
                            .addOperand(MachineOperand::physReg(
                                PhysReg::NZCV, RegClass::CCR,
                                false, true));
                        append(block, std::move(restoreSign), &node);
                        break;
                    }
                }
                ValueType quotientType = integer64 ? ValueType::I64
                                                   : ValueType::I32;
                RegClass quotientClass = integer64 ? RegClass::GPR64
                                                   : RegClass::GPR32;
                VReg quotient = createTemporary(quotientType);
                MachineOperand quotientDef = MachineOperand::vreg(
                    quotient, quotientClass, true);
                bool reduced =
                    !integer64 && node.opcode() == SDOpcode::SRem && rhs &&
                    rhs->opcode() == SDOpcode::Constant &&
                    emitSignedConstantDivision(
                        block, quotientDef, use(node.operands()[0]),
                        static_cast<std::int32_t>(rhs->integer));
                if (!reduced) {
                    MachineInstr divide(
                        node.opcode() == SDOpcode::SRem
                            ? (integer64 ? Opcode::SDIVXrr : Opcode::SDIVWrr)
                            : (integer64 ? Opcode::UDIVXrr : Opcode::UDIVWrr));
                    divide.addOperand(quotientDef)
                        .addOperand(use(node.operands()[0]))
                        .addOperand(use(node.operands()[1]));
                    MachineInstr &quotientDefinition =
                        append(block, std::move(divide));
                    registerInfo.setDefinition(quotient,
                                               &quotientDefinition);
                }
                MachineInstr remainder(integer64 ? Opcode::MSUBXrrr
                                                  : Opcode::MSUBWrrr);
                remainder.addOperand(define(node))
                    .addOperand(MachineOperand::vreg(
                        quotient, quotientClass))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(remainder), &node);
                break;
            }
            case SDOpcode::MAdd:
            case SDOpcode::MSub: {
                MachineInstr fused(node.opcode() == SDOpcode::MAdd
                                       ? Opcode::MADDWrrr
                                       : Opcode::MSUBWrrr);
                fused.addOperand(define(node))
                    .addOperand(use(node.operands()[0]))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(use(node.operands()[2]));
                append(block, std::move(fused), &node);
                break;
            }
            case SDOpcode::FNeg: {
                MachineInstr instruction(
                    node.resultTypes().front() == ValueType::V4F32
                        ? Opcode::NEGv4f32
                        : Opcode::FNEGS);
                instruction.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(instruction), &node);
                break;
            }
            case SDOpcode::ICmp:
            case SDOpcode::FCmp: {
                bool floating = node.opcode() == SDOpcode::FCmp;
                bool integer64 = !floating && node.operands()[0].node &&
                    node.operands()[0].node->resultTypes().front() ==
                        ValueType::I64;
                auto floatingPredicate =
                    static_cast<FCmpInst::FCmpOp>(node.predicate);
                if (floating &&
                    (floatingPredicate == FCmpInst::FCMP_FALSE ||
                     floatingPredicate == FCmpInst::FCMP_TRUE)) {
                    MachineInstr constant(Opcode::MOVi32);
                    constant.addOperand(define(node))
                        .addOperand(MachineOperand::immediate(
                            floatingPredicate == FCmpInst::FCMP_TRUE));
                    append(block, std::move(constant), &node);
                    break;
                }
                SDNode *rhs = node.operands()[1].node;
                bool integerImmediate =
                    !floating && rhs &&
                    rhs->opcode() == SDOpcode::Constant &&
                    InstrInfo::acceptsImmediate(
                        integer64 ? Opcode::CMPXri : Opcode::CMPWri,
                        rhs->integer);
                bool floatingZero =
                    floating && rhs &&
                    rhs->opcode() == SDOpcode::FPConstant &&
                    rhs->floatingBits == 0;
                MachineInstr compare(
                    integerImmediate ? (integer64 ? Opcode::CMPXri
                                                  : Opcode::CMPWri)
                    : floatingZero ? Opcode::FCMPZS
                    : floating ? Opcode::FCMPSrr
                               : (integer64 ? Opcode::CMPXrr
                                            : Opcode::CMPWrr));
                compare.addOperand(use(node.operands()[0]));
                if (integerImmediate)
                    compare.addOperand(
                        MachineOperand::immediate(rhs->integer));
                else if (!floatingZero)
                    compare.addOperand(use(node.operands()[1]));
                compare.addOperand(MachineOperand::physReg(
                    PhysReg::NZCV, RegClass::CCR, true, true));
                append(block, std::move(compare));
                if (floating &&
                    (floatingPredicate == FCmpInst::FCMP_ONE ||
                     floatingPredicate == FCmpInst::FCMP_UEQ)) {
                    VReg first = createTemporary(ValueType::I1);
                    MachineInstr firstSet(Opcode::CSETW);
                    firstSet
                        .addOperand(MachineOperand::vreg(
                            first, RegClass::GPR32, true))
                        .addOperand(MachineOperand::condition(
                            floatingPredicate == FCmpInst::FCMP_ONE
                                ? CondCode::NE
                                : CondCode::EQ))
                        .addOperand(MachineOperand::physReg(
                            PhysReg::NZCV, RegClass::CCR, false, true));
                    MachineInstr &firstDefinition =
                        append(block, std::move(firstSet));
                    registerInfo.setDefinition(first, &firstDefinition);
                    VReg second = createTemporary(ValueType::I1);
                    MachineInstr secondSet(Opcode::CSETW);
                    secondSet
                        .addOperand(MachineOperand::vreg(
                            second, RegClass::GPR32, true))
                        .addOperand(MachineOperand::condition(
                            floatingPredicate == FCmpInst::FCMP_ONE
                                ? CondCode::VC
                                : CondCode::VS))
                        .addOperand(MachineOperand::physReg(
                            PhysReg::NZCV, RegClass::CCR, false, true));
                    MachineInstr &secondDefinition =
                        append(block, std::move(secondSet));
                    registerInfo.setDefinition(second, &secondDefinition);
                    MachineInstr combine(
                        floatingPredicate == FCmpInst::FCMP_ONE
                            ? Opcode::ANDWrr
                            : Opcode::ORRWrr);
                    combine.addOperand(define(node))
                        .addOperand(MachineOperand::vreg(
                            first, RegClass::GPR32))
                        .addOperand(MachineOperand::vreg(
                            second, RegClass::GPR32));
                    append(block, std::move(combine), &node);
                    break;
                }
                MachineInstr set(Opcode::CSETW);
                set.addOperand(define(node))
                    .addOperand(MachineOperand::condition(
                        floating ? floatingCondition(node.predicate)
                                 : integerCondition(node.predicate)))
                    .addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, false, true));
                append(block, std::move(set), &node);
                break;
            }
            case SDOpcode::Select: {
                MachineInstr compare(Opcode::CMPWri);
                compare.addOperand(use(node.operands()[0]))
                    .addOperand(MachineOperand::immediate(0))
                    .addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, true, true));
                append(block, std::move(compare));
                RegClass selectedClass = valueClass(node.operands()[1]);
                Opcode opcode =
                    selectedClass == RegClass::FPR32 ? Opcode::FCSELS
                    : selectedClass == RegClass::GPR64 ? Opcode::CSELX
                                                       : Opcode::CSELW;
                MachineInstr select(opcode);
                select.addOperand(define(node))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(use(node.operands()[2]))
                    .addOperand(MachineOperand::condition(CondCode::NE))
                    .addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, false, true));
                append(block, std::move(select), &node);
                break;
            }
            case SDOpcode::GEP: {
                VReg current = resultReg(node.operands()[0]);
                for (unsigned i = 1; i < node.operands().size(); ++i) {
                    SDNode *indexNode = node.operands()[i].node;
                    unsigned scale = node.gepScales.at(i - 1);
                    bool last = i + 1 == node.operands().size();
                    VReg destination =
                        last ? results.at(&node)
                             : createTemporary(ValueType::Ptr);
                    MachineInstr address;
                    if (indexNode &&
                        indexNode->opcode() == SDOpcode::Constant) {
                        std::int64_t offset = indexNode->integer * scale;
                        if (offset >= 0 && offset <= 4095) {
                            address.setOpcode(Opcode::ADDXri);
                            address.addOperand(MachineOperand::vreg(
                                                   destination,
                                                   RegClass::GPR64, true))
                                .addOperand(MachineOperand::vreg(
                                    current, RegClass::GPR64))
                                .addOperand(
                                    MachineOperand::immediate(offset));
                        } else {
                            VReg offsetReg = createTemporary(ValueType::Ptr);
                            MachineInstr materialize(Opcode::MOVi64);
                            materialize
                                .addOperand(MachineOperand::vreg(
                                    offsetReg, RegClass::GPR64, true))
                                .addOperand(
                                    MachineOperand::immediate(offset));
                            MachineInstr &offsetDef =
                                append(block, std::move(materialize));
                            registerInfo.setDefinition(offsetReg, &offsetDef);
                            address.setOpcode(Opcode::ADDXrr);
                            address.addOperand(MachineOperand::vreg(
                                                   destination,
                                                   RegClass::GPR64, true))
                                .addOperand(MachineOperand::vreg(
                                    current, RegClass::GPR64))
                                .addOperand(MachineOperand::vreg(
                                    offsetReg, RegClass::GPR64));
                        }
                    } else if (isPowerOfTwo(scale) && scale <= 8) {
                        address.setOpcode(Opcode::ADDXrs);
                        address.addOperand(MachineOperand::vreg(
                                               destination,
                                               RegClass::GPR64, true))
                            .addOperand(MachineOperand::vreg(
                                current, RegClass::GPR64))
                            .addOperand(use(node.operands()[i]))
                            .addOperand(MachineOperand::immediate(
                                log2Exact(scale)))
                            .addOperand(MachineOperand::immediate(1));
                    } else if (isPowerOfTwo(scale)) {
                        VReg extended = createTemporary(ValueType::Ptr);
                        MachineInstr extend(Opcode::SXTW);
                        extend.addOperand(MachineOperand::vreg(
                                              extended,
                                              RegClass::GPR64, true))
                            .addOperand(use(node.operands()[i]));
                        MachineInstr &extendDef =
                            append(block, std::move(extend));
                        registerInfo.setDefinition(extended, &extendDef);
                        address.setOpcode(Opcode::ADDXrs);
                        address.addOperand(MachineOperand::vreg(
                                               destination,
                                               RegClass::GPR64, true))
                            .addOperand(MachineOperand::vreg(
                                current, RegClass::GPR64))
                            .addOperand(MachineOperand::vreg(
                                extended, RegClass::GPR64))
                            .addOperand(MachineOperand::immediate(
                                log2Exact(scale)))
                            .addOperand(MachineOperand::immediate(2));
                    } else {
                        VReg scaleReg = createTemporary(ValueType::I32);
                        MachineInstr materialize(Opcode::MOVi32);
                        materialize
                            .addOperand(MachineOperand::vreg(
                                scaleReg, RegClass::GPR32, true))
                            .addOperand(MachineOperand::immediate(scale));
                        MachineInstr &scaleDef =
                            append(block, std::move(materialize));
                        registerInfo.setDefinition(scaleReg, &scaleDef);
                        // AArch64 has a widening signed multiply-add that
                        // exactly represents base + sext(index) * scale.
                        // Select it here while all temporaries are virtual;
                        // neither RA nor the printer may invent an address
                        // scratch register later.
                        address.setOpcode(Opcode::SMADDLXrrr);
                        address
                            .addOperand(MachineOperand::vreg(
                                destination, RegClass::GPR64, true))
                            .addOperand(use(node.operands()[i]))
                            .addOperand(MachineOperand::vreg(
                                scaleReg, RegClass::GPR32))
                            .addOperand(MachineOperand::vreg(
                                current, RegClass::GPR64));
                    }
                    MachineInstr &addressDef =
                        append(block, std::move(address),
                               last ? &node : nullptr);
                    if (!last)
                        registerInfo.setDefinition(destination, &addressDef);
                    current = destination;
                }
                if (node.operands().size() == 1) {
                    MachineInstr copy(Opcode::COPY);
                    copy.addOperand(define(node))
                        .addOperand(use(node.operands()[0]));
                    append(block, std::move(copy), &node);
                }
                break;
            }
            case SDOpcode::Load: {
                RegClass resultClass =
                    registerInfo.get(results.at(&node)).regClass;
                SDNode *address = node.operands()[1].node;
                bool directGlobal =
                    address &&
                    address->opcode() == SDOpcode::GlobalAddress &&
                    directGlobalMemory.at(address);
                Opcode opcode =
                    resultClass == RegClass::FPR32
                        ? (directGlobal ? Opcode::LDRSlo
                                        : Opcode::LDRSui)
                    : resultClass == RegClass::NEON128
                        ? (directGlobal ? Opcode::LDRQlo
                                        : Opcode::LDRQui)
                    : resultClass == RegClass::GPR64
                        ? (directGlobal ? Opcode::LDRXlo
                                        : Opcode::LDRXui)
                        : (directGlobal ? Opcode::LDRWlo
                                        : Opcode::LDRWui);
                MachineInstr load(opcode);
                load.addOperand(define(node))
                    .addOperand(use(node.operands()[1]));
                if (directGlobal)
                    load.addOperand(
                        MachineOperand::global(address->symbol));
                else
                    load.addOperand(MachineOperand::immediate(0));
                load.addMemoryOperand(MachineMemOperand{
                    MachineMemOperand::Access::Load, node.memorySize,
                    node.alignment, node.origin, std::nullopt, 0, false});
                append(block, std::move(load), &node);
                break;
            }
            case SDOpcode::Store: {
                RegClass storedClass = valueClass(node.operands()[1]);
                SDNode *address = node.operands()[2].node;
                bool directGlobal =
                    address &&
                    address->opcode() == SDOpcode::GlobalAddress &&
                    directGlobalMemory.at(address);
                Opcode opcode =
                    storedClass == RegClass::FPR32
                        ? (directGlobal ? Opcode::STRSlo
                                        : Opcode::STRSui)
                    : storedClass == RegClass::NEON128
                        ? (directGlobal ? Opcode::STRQlo
                                        : Opcode::STRQui)
                    : storedClass == RegClass::GPR64
                        ? (directGlobal ? Opcode::STRXlo
                                        : Opcode::STRXui)
                        : (directGlobal ? Opcode::STRWlo
                                        : Opcode::STRWui);
                MachineInstr store(opcode);
                store.addOperand(use(node.operands()[1]))
                    .addOperand(use(node.operands()[2]));
                if (directGlobal)
                    store.addOperand(
                        MachineOperand::global(address->symbol));
                else
                    store.addOperand(MachineOperand::immediate(0));
                store.addMemoryOperand(MachineMemOperand{
                    MachineMemOperand::Access::Store, node.memorySize,
                    node.alignment, node.origin, std::nullopt, 0, false});
                append(block, std::move(store));
                break;
            }
            case SDOpcode::ZExt:
            case SDOpcode::SExt:
            case SDOpcode::Trunc:
            case SDOpcode::Bitcast: {
                ValueType resultType = node.resultTypes().front();
                ValueType sourceType =
                    node.operands()[0].node->resultTypes().front();
                if (resultType == ValueType::I64 &&
                    (sourceType == ValueType::I32 ||
                     sourceType == ValueType::I1)) {
                    MachineInstr extend(node.opcode() == SDOpcode::SExt
                                            ? Opcode::SXTW
                                            : Opcode::UXTW);
                    extend.addOperand(define(node))
                        .addOperand(use(node.operands()[0]));
                    append(block, std::move(extend), &node);
                    break;
                }
                if ((resultType == ValueType::I32 ||
                     resultType == ValueType::I1) &&
                    sourceType == ValueType::I64) {
                    MachineInstr truncate(Opcode::COPYXtoW);
                    truncate.addOperand(define(node))
                        .addOperand(use(node.operands()[0]));
                    append(block, std::move(truncate), &node);
                    break;
                }
                MachineInstr copy(Opcode::COPY);
                copy.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(copy), &node);
                break;
            }
            case SDOpcode::FPToSI:
            case SDOpcode::SIToFP: {
                MachineInstr convert(node.opcode() == SDOpcode::FPToSI
                                         ? Opcode::FCVTZSW
                                         : Opcode::SCVTFWS);
                convert.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(convert), &node);
                break;
            }
            case SDOpcode::Clz: {
                MachineInstr clz(Opcode::CLZW);
                clz.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(clz), &node);
                break;
            }
            case SDOpcode::Splat: {
                MachineInstr duplicate(
                    node.resultTypes().front() == ValueType::V4F32
                        ? Opcode::DUPv4f32
                        : Opcode::DUPv4i32);
                duplicate.addOperand(define(node))
                    .addOperand(use(node.operands()[0]));
                append(block, std::move(duplicate), &node);
                break;
            }
            case SDOpcode::InsertElement: {
                SDNode *index = node.operands()[2].node;
                if (!index || index->opcode() != SDOpcode::Constant ||
                    index->integer < 0 || index->integer >= 4)
                    throw std::logic_error(
                        "AArch64 vector insert requires a constant lane");
                MachineInstr insert(
                    node.resultTypes().front() == ValueType::V4F32
                        ? Opcode::INSv4f32
                        : Opcode::INSv4i32);
                insert.addOperand(define(node))
                    .addOperand(use(node.operands()[0]))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(
                        MachineOperand::immediate(index->integer));
                insert.operands()[1].tiedTo = 0;
                append(block, std::move(insert), &node);
                break;
            }
            case SDOpcode::ExtractElement: {
                SDNode *index = node.operands()[1].node;
                if (!index || index->opcode() != SDOpcode::Constant ||
                    index->integer < 0 || index->integer >= 4)
                    throw std::logic_error(
                        "AArch64 vector extract requires a constant lane");
                MachineInstr extract(
                    node.resultTypes().front() == ValueType::F32
                        ? Opcode::EXTRACTv4f32
                        : Opcode::EXTRACTv4i32);
                extract.addOperand(define(node))
                    .addOperand(use(node.operands()[0]))
                    .addOperand(
                        MachineOperand::immediate(index->integer));
                append(block, std::move(extract), &node);
                break;
            }
            case SDOpcode::ShuffleVector: {
                std::array<std::uint32_t, 4> packedMask{};
                for (unsigned lane = 0; lane < packedMask.size(); ++lane) {
                    int sourceLane =
                        lane < node.shuffleMask.size()
                            ? node.shuffleMask[lane]
                            : 0;
                    for (unsigned byte = 0; byte < 4; ++byte)
                        packedMask[lane] |=
                            static_cast<std::uint32_t>(
                                sourceLane * 4 + byte)
                            << (byte * 8);
                }
                VReg mask = createTemporary(ValueType::V4I32);
                emitVectorConstant(
                    block,
                    MachineOperand::vreg(
                        mask, RegClass::NEON128, true),
                    packedMask);
                MachineInstr shuffle(Opcode::SHUFFLEv16i8);
                shuffle.addOperand(define(node))
                    .addOperand(use(node.operands()[0]))
                    .addOperand(use(node.operands()[1]))
                    .addOperand(MachineOperand::vreg(
                        mask, RegClass::NEON128));
                append(block, std::move(shuffle), &node);
                break;
            }
            case SDOpcode::VectorReduceAdd: {
                VReg reduced = createTemporary(ValueType::F32);
                MachineInstr reduce(Opcode::ADDVv4i32);
                reduce
                    .addOperand(MachineOperand::vreg(
                        reduced, RegClass::FPR32, true))
                    .addOperand(use(node.operands()[0]));
                MachineInstr &reduceDef =
                    append(block, std::move(reduce));
                registerInfo.setDefinition(reduced, &reduceDef);
                MachineInstr move(Opcode::FMOVWS);
                move.addOperand(define(node))
                    .addOperand(MachineOperand::vreg(
                        reduced, RegClass::FPR32));
                append(block, std::move(move), &node);
                break;
            }
            case SDOpcode::Phi: {
                MachineInstr phi(Opcode::PHI);
                phi.addOperand(define(node));
                for (unsigned i = 0; i < node.operands().size(); ++i) {
                    phi.addOperand(use(node.operands()[i]))
                        .addOperand(MachineOperand::block(
                            blocks.at(node.incomingBlocks.at(i))));
                }
                append(block, std::move(phi), &node);
                break;
            }
            case SDOpcode::Call: {
                unsigned integerIndex = 0;
                unsigned floatIndex = 0;
                unsigned outgoingStackSize = 0;
                struct StackArgument {
                    SDValue value;
                    unsigned offset;
                    unsigned size;
                    unsigned alignment;
                };
                std::vector<StackArgument> stackArguments;
                for (unsigned i = 1; i < node.operands().size(); ++i) {
                    RegClass regClass = valueClass(node.operands()[i]);
                    bool vectorBank =
                        regClass == RegClass::FPR32 ||
                        regClass == RegClass::NEON128;
                    unsigned &index = vectorBank ? floatIndex : integerIndex;
                    if (index >= 8) {
                        unsigned alignment =
                            regClass == RegClass::NEON128 ? 16U : 8U;
                        outgoingStackSize =
                            (outgoingStackSize + alignment - 1) /
                            alignment * alignment;
                        stackArguments.push_back(StackArgument{
                            node.operands()[i], outgoingStackSize,
                            regClass == RegClass::NEON128 ? 16U
                                : regClass == RegClass::GPR64 ? 8U : 4U,
                            alignment});
                        outgoingStackSize += alignment;
                    }
                    ++index;
                }
                outgoingStackSize =
                    (outgoingStackSize + 15) / 16 * 16;
                machineFunction->frameInfo().maxCallFrameSize =
                    std::max(
                        machineFunction->frameInfo().maxCallFrameSize,
                        outgoingStackSize);
                if (outgoingStackSize) {
                    MachineInstr down(Opcode::ADJCALLSTACKDOWN);
                    down.addOperand(
                        MachineOperand::immediate(outgoingStackSize));
                    append(block, std::move(down));
                }
                for (const StackArgument &argument : stackArguments) {
                    RegClass regClass = valueClass(argument.value);
                    unsigned width =
                        regClass == RegClass::NEON128 ? 16U
                            : regClass == RegClass::GPR64 ? 8U : 4U;
                    bool encodable =
                        argument.offset % width == 0 &&
                        argument.offset / width <= 4095;
                    MachineOperand address = MachineOperand::physReg(
                        PhysReg::SP, RegClass::GPR64);
                    std::int64_t memoryOffset = argument.offset;
                    if (!encodable) {
                        VReg offset = createTemporary(ValueType::Ptr);
                        MachineInstr materialize(Opcode::MOVi64);
                        materialize
                            .addOperand(MachineOperand::vreg(
                                offset, RegClass::GPR64, true))
                            .addOperand(MachineOperand::immediate(
                                argument.offset));
                        MachineInstr &offsetDefinition =
                            append(block, std::move(materialize));
                        registerInfo.setDefinition(
                            offset, &offsetDefinition);

                        VReg computedAddress =
                            createTemporary(ValueType::Ptr);
                        MachineInstr add(Opcode::ADDXrr);
                        add.addOperand(MachineOperand::vreg(
                                           computedAddress,
                                           RegClass::GPR64, true))
                            .addOperand(MachineOperand::physReg(
                                PhysReg::SP, RegClass::GPR64))
                            .addOperand(MachineOperand::vreg(
                                offset, RegClass::GPR64));
                        MachineInstr &addressDefinition =
                            append(block, std::move(add));
                        registerInfo.setDefinition(
                            computedAddress, &addressDefinition);
                        address = MachineOperand::vreg(
                            computedAddress, RegClass::GPR64);
                        memoryOffset = 0;
                    }
                    Opcode storeOpcode =
                        regClass == RegClass::FPR32 ? Opcode::STRSui
                        : regClass == RegClass::NEON128 ? Opcode::STRQui
                        : regClass == RegClass::GPR64 ? Opcode::STRXui
                                                       : Opcode::STRWui;
                    MachineInstr store(storeOpcode);
                    store.addOperand(use(argument.value))
                        .addOperand(address)
                        .addOperand(MachineOperand::immediate(
                            memoryOffset));
                    store.addMemoryOperand(MachineMemOperand{
                        MachineMemOperand::Access::Store, argument.size,
                        argument.alignment, node.origin, std::nullopt,
                        argument.offset, false});
                    append(block, std::move(store));
                }
                integerIndex = 0;
                floatIndex = 0;
                const unsigned callCopyGroup = nextParallelCopyGroup++;
                for (unsigned i = 1; i < node.operands().size(); ++i) {
                    RegClass regClass = valueClass(node.operands()[i]);
                    bool vectorBank =
                        regClass == RegClass::FPR32 ||
                        regClass == RegClass::NEON128;
                    unsigned &index = vectorBank ? floatIndex : integerIndex;
                    if (index < 8) {
                        PhysReg destination =
                            vectorBank ? vectorArgumentRegister(index)
                                       : integerArgumentRegister(index);
                        MachineInstr copy(Opcode::COPY);
                        copy.parallelCopyGroup = callCopyGroup;
                        copy.addOperand(MachineOperand::physReg(
                                            destination, regClass, true))
                            .addOperand(use(node.operands()[i]));
                        append(block, std::move(copy));
                    }
                    ++index;
                }
                MachineInstr call(Opcode::CALL);
                call.addOperand(MachineOperand::external(node.symbol))
                    .addOperand(MachineOperand::registerMask(
                        callPreservedMask()));
                append(block, std::move(call));
                machineFunction->frameInfo().hasCalls = true;
                if (outgoingStackSize) {
                    MachineInstr up(Opcode::ADJCALLSTACKUP);
                    up.addOperand(
                        MachineOperand::immediate(outgoingStackSize));
                    append(block, std::move(up));
                }

                if (!node.resultTypes().empty() &&
                    node.resultTypes().front() != ValueType::Invalid) {
                    RegClass resultClass =
                        registerInfo.get(results.at(&node)).regClass;
                    PhysReg source =
                        resultClass == RegClass::FPR32 ||
                                resultClass == RegClass::NEON128
                            ? PhysReg::V0
                            : PhysReg::X0;
                    MachineInstr copy(Opcode::COPY);
                    copy.addOperand(define(node))
                        .addOperand(MachineOperand::physReg(
                            source, resultClass));
                    append(block, std::move(copy), &node);
                }
                break;
            }
            case SDOpcode::TailCall: {
                // Register-only sibling/general TCO.  Does not set hasCalls so
                // a function whose only calls are tail calls can stay frameless.
                unsigned integerIndex = 0;
                unsigned floatIndex = 0;
                const unsigned callCopyGroup = nextParallelCopyGroup++;
                for (unsigned i = 1; i < node.operands().size(); ++i) {
                    RegClass regClass = valueClass(node.operands()[i]);
                    bool vectorBank =
                        regClass == RegClass::FPR32 ||
                        regClass == RegClass::NEON128;
                    unsigned &index = vectorBank ? floatIndex : integerIndex;
                    if (index >= 8)
                        throw std::logic_error(
                            "TailCall selected with stack-passed arguments");
                    PhysReg destination =
                        vectorBank ? vectorArgumentRegister(index)
                                   : integerArgumentRegister(index);
                    MachineInstr copy(Opcode::COPY);
                    copy.parallelCopyGroup = callCopyGroup;
                    copy.addOperand(MachineOperand::physReg(
                                        destination, regClass, true))
                        .addOperand(use(node.operands()[i]));
                    append(block, std::move(copy));
                    ++index;
                }
                MachineInstr call(Opcode::TAILCALL);
                call.addOperand(MachineOperand::external(node.symbol))
                    .addOperand(MachineOperand::registerMask(
                        callPreservedMask()));
                append(block, std::move(call));
                break;
            }
            case SDOpcode::Branch: {
                MachineInstr branch(Opcode::B);
                branch.addOperand(MachineOperand::block(
                    blocks.at(node.incomingBlocks.at(0))));
                append(block, std::move(branch));
                break;
            }
            case SDOpcode::BranchCond: {
                MachineInstr conditional(Opcode::CBNZ);
                conditional.addOperand(use(node.operands()[1]))
                    .addOperand(MachineOperand::block(
                        blocks.at(node.incomingBlocks.at(0))));
                append(block, std::move(conditional));
                MachineInstr fallback(Opcode::B);
                fallback.addOperand(MachineOperand::block(
                    blocks.at(node.incomingBlocks.at(1))));
                append(block, std::move(fallback));
                break;
            }
            case SDOpcode::Return: {
                if (node.operands().size() > 1) {
                    RegClass resultClass = valueClass(node.operands()[1]);
                    PhysReg destination =
                        resultClass == RegClass::FPR32 ||
                                resultClass == RegClass::NEON128
                            ? PhysReg::V0
                            : PhysReg::X0;
                    MachineInstr copy(Opcode::COPY);
                    copy.addOperand(MachineOperand::physReg(
                                        destination, resultClass, true))
                        .addOperand(use(node.operands()[1]));
                    append(block, std::move(copy));
                }
                append(block, MachineInstr(Opcode::RET));
                break;
            }
            }
        }
    }
    return machineFunction;
}

} // namespace backend::aarch64
