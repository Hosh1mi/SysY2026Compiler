#include "../../../include/backend/arm64/rewrite/machine_ir.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace backend::aarch64 {

bool RegisterMask::preserves(PhysReg reg) const {
    unsigned bit = static_cast<unsigned>(reg);
    unsigned word = bit / 64;
    return word < preserved.size() && (preserved[word] & (1ULL << (bit % 64)));
}

void RegisterMask::setPreserved(PhysReg reg) {
    unsigned bit = static_cast<unsigned>(reg);
    unsigned word = bit / 64;
    if (word < preserved.size())
        preserved[word] |= 1ULL << (bit % 64);
}

MachineOperand MachineOperand::vreg(VReg reg, RegClass regClass, bool isDef) {
    MachineOperand operand;
    operand.kind_ = Kind::VirtualRegister;
    operand.vreg_ = reg;
    operand.regClass_ = regClass;
    operand.isDef = isDef;
    return operand;
}

MachineOperand MachineOperand::physReg(PhysReg reg, RegClass regClass,
                                       bool isDef, bool implicit) {
    MachineOperand operand;
    operand.kind_ = Kind::PhysicalRegister;
    operand.physReg_ = reg;
    operand.regClass_ = regClass;
    operand.isDef = isDef;
    operand.isImplicit = implicit;
    return operand;
}

MachineOperand MachineOperand::immediate(std::int64_t value) {
    MachineOperand operand;
    operand.kind_ = Kind::Immediate;
    operand.immediate_ = value;
    return operand;
}

MachineOperand MachineOperand::floatingBits(std::uint32_t bits) {
    MachineOperand operand;
    operand.kind_ = Kind::FloatingBits;
    operand.floatingBits_ = bits;
    return operand;
}

MachineOperand MachineOperand::block(MachineBasicBlock *block) {
    MachineOperand operand;
    operand.kind_ = Kind::BasicBlock;
    operand.block_ = block;
    return operand;
}

MachineOperand MachineOperand::global(std::string symbol) {
    MachineOperand operand;
    operand.kind_ = Kind::GlobalSymbol;
    operand.symbol_ = std::move(symbol);
    return operand;
}

MachineOperand MachineOperand::external(std::string symbol) {
    MachineOperand operand;
    operand.kind_ = Kind::ExternalSymbol;
    operand.symbol_ = std::move(symbol);
    return operand;
}

MachineOperand MachineOperand::frameIndex(int index) {
    MachineOperand operand;
    operand.kind_ = Kind::FrameIndex;
    operand.frameIndex_ = index;
    return operand;
}

MachineOperand MachineOperand::registerMask(RegisterMask mask) {
    MachineOperand operand;
    operand.kind_ = Kind::RegisterMask;
    operand.regMask_ = mask;
    return operand;
}

MachineOperand MachineOperand::condition(CondCode condition) {
    MachineOperand operand;
    operand.kind_ = Kind::ConditionCode;
    operand.condition_ = condition;
    return operand;
}

bool MachineOperand::isRegister() const {
    return isVirtualRegister() || isPhysicalRegister();
}

MachineInstr &MachineInstr::addOperand(MachineOperand operand) {
    operands_.push_back(std::move(operand));
    return *this;
}

MachineInstr &MachineInstr::addMemoryOperand(MachineMemOperand operand) {
    memoryOperands_.push_back(std::move(operand));
    return *this;
}

bool MachineInstr::isTerminator() const {
    return InstrInfo::get(opcode_).terminator;
}

bool MachineInstr::isBranch() const {
    return InstrInfo::get(opcode_).branch;
}

bool MachineInstr::isCall() const {
    return InstrInfo::get(opcode_).call;
}

bool MachineInstr::mayLoad() const {
    return InstrInfo::get(opcode_).mayLoad;
}

bool MachineInstr::mayStore() const {
    return InstrInfo::get(opcode_).mayStore;
}

bool MachineInstr::hasSideEffects() const {
    return InstrInfo::get(opcode_).hasSideEffects;
}

bool MachineInstr::isPseudo() const {
    return InstrInfo::get(opcode_).pseudo;
}

MachineInstr &MachineBasicBlock::append(MachineInstr instruction) {
    instructions_.push_back(std::move(instruction));
    return instructions_.back();
}

void MachineBasicBlock::addSuccessor(MachineBasicBlock *successor) {
    if (!successor)
        return;
    if (std::find(successors_.begin(), successors_.end(), successor) ==
        successors_.end())
        successors_.push_back(successor);
    if (std::find(successor->predecessors_.begin(),
                  successor->predecessors_.end(), this) ==
        successor->predecessors_.end())
        successor->predecessors_.push_back(this);
}

void MachineBasicBlock::removeSuccessor(MachineBasicBlock *successor) {
    successors_.erase(std::remove(successors_.begin(), successors_.end(),
                                  successor), successors_.end());
    if (successor)
        successor->predecessors_.erase(
            std::remove(successor->predecessors_.begin(),
                        successor->predecessors_.end(), this),
            successor->predecessors_.end());
}

VReg MachineRegisterInfo::createVirtualRegister(RegClass regClass,
                                                ValueType valueType) {
    VReg reg = nextVirtualRegister_++;
    virtualRegisters_.emplace(
        reg, VRegInfo{regClass, valueType, nullptr, false, false});
    return reg;
}

bool MachineRegisterInfo::contains(VReg reg) const {
    return virtualRegisters_.count(reg) != 0;
}

VRegInfo &MachineRegisterInfo::get(VReg reg) {
    auto it = virtualRegisters_.find(reg);
    if (it == virtualRegisters_.end())
        throw std::out_of_range("unknown virtual register");
    return it->second;
}

const VRegInfo &MachineRegisterInfo::get(VReg reg) const {
    auto it = virtualRegisters_.find(reg);
    if (it == virtualRegisters_.end())
        throw std::out_of_range("unknown virtual register");
    return it->second;
}

void MachineRegisterInfo::setDefinition(VReg reg, MachineInstr *instruction) {
    get(reg).definition = instruction;
}

void MachineRegisterInfo::eraseVirtualRegister(VReg reg) {
    virtualRegisters_.erase(reg);
}

int MachineFrameInfo::createStackObject(unsigned size, unsigned alignment,
                                        bool spill) {
    int index = static_cast<int>(objects_.size());
    objects_.push_back(StackObject{index, size, alignment, 0, spill, false});
    maxAlignment = std::max(maxAlignment, alignment);
    return index;
}

int MachineFrameInfo::createFixedObject(unsigned size, int offset,
                                        unsigned alignment) {
    int index = static_cast<int>(objects_.size());
    objects_.push_back(StackObject{index, size, alignment, offset, false, true});
    maxAlignment = std::max(maxAlignment, alignment);
    return index;
}

StackObject &MachineFrameInfo::getObject(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= objects_.size())
        throw std::out_of_range("unknown stack object");
    return objects_[index];
}

const StackObject &MachineFrameInfo::getObject(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= objects_.size())
        throw std::out_of_range("unknown stack object");
    return objects_[index];
}

MachineBasicBlock &MachineFunction::createBlock(std::string name) {
    unsigned number = static_cast<unsigned>(blocks_.size());
    blocks_.push_back(
        std::make_unique<MachineBasicBlock>(number, std::move(name)));
    return *blocks_.back();
}

MachineBasicBlock *MachineFunction::entryBlock() {
    return blocks_.empty() ? nullptr : blocks_.front().get();
}

const MachineBasicBlock *MachineFunction::entryBlock() const {
    return blocks_.empty() ? nullptr : blocks_.front().get();
}

bool MachineFunction::hasProperty(MachineProperty property) const {
    return properties_ & static_cast<std::uint32_t>(property);
}

void MachineFunction::setProperty(MachineProperty property) {
    properties_ |= static_cast<std::uint32_t>(property);
}

void MachineFunction::clearProperty(MachineProperty property) {
    properties_ &= ~static_cast<std::uint32_t>(property);
}

namespace {

const char *regClassName(RegClass regClass) {
    switch (regClass) {
    case RegClass::GPR32: return "gpr32";
    case RegClass::GPR64: return "gpr64";
    case RegClass::FPR32: return "fpr32";
    case RegClass::NEON128: return "neon128";
    case RegClass::CCR: return "ccr";
    default: return "invalid";
    }
}

std::string operandText(const MachineOperand &operand) {
    std::ostringstream output;
    if (operand.isDef)
        output << "def ";
    if (operand.isImplicit)
        output << "implicit ";
    switch (operand.kind()) {
    case MachineOperand::Kind::VirtualRegister:
        output << '%' << operand.virtualRegister() << ':'
               << regClassName(operand.regClass());
        break;
    case MachineOperand::Kind::PhysicalRegister:
        output << '$' << RegisterInfo::name(operand.physicalRegister(),
                                            operand.regClass());
        break;
    case MachineOperand::Kind::Immediate:
        output << operand.immediate();
        break;
    case MachineOperand::Kind::FloatingBits:
        output << "fpbits(0x" << std::hex << operand.floatingBits() << ')';
        break;
    case MachineOperand::Kind::BasicBlock:
        output << "%bb." << (operand.basicBlock()
                                 ? std::to_string(operand.basicBlock()->number())
                                 : std::string("<null>"));
        break;
    case MachineOperand::Kind::GlobalSymbol:
        output << '@' << operand.symbol();
        break;
    case MachineOperand::Kind::ExternalSymbol:
        output << '&' << operand.symbol();
        break;
    case MachineOperand::Kind::FrameIndex:
        output << "%stack." << operand.frameIndex();
        break;
    case MachineOperand::Kind::RegisterMask:
        output << "<regmask>";
        break;
    case MachineOperand::Kind::ConditionCode:
        output << "cc(" << static_cast<unsigned>(operand.condition()) << ')';
        break;
    default:
        output << "<invalid>";
        break;
    }
    if (operand.tiedTo >= 0)
        output << "(tied-def " << operand.tiedTo << ')';
    return output.str();
}

} // namespace

void printMachineIR(const MachineFunction &function, std::ostream &output) {
    output << "machine-function " << function.name() << " {\n";
    for (const auto &block : function.blocks()) {
        output << "  bb." << block->number() << ' ' << block->name() << ':';
        if (!block->successors().empty()) {
            output << " successors";
            for (const auto *successor : block->successors())
                output << " %bb." << successor->number();
        }
        output << '\n';
        for (const auto &instruction : block->instructions()) {
            const auto &descriptor = InstrInfo::get(instruction.opcode());
            output << "    " << descriptor.mnemonic;
            bool first = true;
            for (const auto &operand : instruction.operands()) {
                output << (first ? " " : ", ") << operandText(operand);
                first = false;
            }
            output << '\n';
        }
    }
    output << "}\n";
}

std::string printMachineIR(const MachineFunction &function) {
    std::ostringstream output;
    printMachineIR(function, output);
    return output.str();
}

} // namespace backend::aarch64
