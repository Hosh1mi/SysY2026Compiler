#pragma once

#include "target.hpp"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace backend::aarch64 {

using VReg = std::uint32_t;

class MachineBasicBlock;

struct RegisterMask {
    static constexpr std::size_t kWords = 2;
    std::array<std::uint64_t, kWords> preserved{};

    bool preserves(PhysReg reg) const;
    void setPreserved(PhysReg reg);
};

class MachineOperand {
public:
    enum class Kind : std::uint8_t {
        Invalid,
        VirtualRegister,
        PhysicalRegister,
        Immediate,
        FloatingBits,
        BasicBlock,
        GlobalSymbol,
        ExternalSymbol,
        FrameIndex,
        RegisterMask,
        ConditionCode,
    };

    static MachineOperand vreg(VReg reg, RegClass regClass, bool isDef = false);
    static MachineOperand physReg(PhysReg reg, RegClass regClass,
                                  bool isDef = false, bool implicit = false);
    static MachineOperand immediate(std::int64_t value);
    static MachineOperand floatingBits(std::uint32_t bits);
    static MachineOperand block(MachineBasicBlock *block);
    static MachineOperand global(std::string symbol);
    static MachineOperand external(std::string symbol);
    static MachineOperand frameIndex(int index);
    static MachineOperand registerMask(RegisterMask mask);
    static MachineOperand condition(CondCode condition);

    Kind kind() const { return kind_; }
    bool isRegister() const;
    bool isSameRegisterAs(const MachineOperand &other) const;
    bool isVirtualRegister() const { return kind_ == Kind::VirtualRegister; }
    bool isPhysicalRegister() const { return kind_ == Kind::PhysicalRegister; }

    VReg virtualRegister() const { return vreg_; }
    PhysReg physicalRegister() const { return physReg_; }
    RegClass regClass() const { return regClass_; }
    std::int64_t immediate() const { return immediate_; }
    std::uint32_t floatingBits() const { return floatingBits_; }
    MachineBasicBlock *basicBlock() const { return block_; }
    const std::string &symbol() const { return symbol_; }
    int frameIndex() const { return frameIndex_; }
    const RegisterMask &registerMask() const { return regMask_; }
    CondCode condition() const { return condition_; }

    bool isDef = false;
    bool isImplicit = false;
    bool isKill = false;
    bool isDead = false;
    bool isUndef = false;
    bool isEarlyClobber = false;
    bool isRenamable = true;
    int tiedTo = -1;

private:
    Kind kind_ = Kind::Invalid;
    VReg vreg_ = 0;
    PhysReg physReg_ = PhysReg::NoReg;
    RegClass regClass_ = RegClass::Invalid;
    std::int64_t immediate_ = 0;
    std::uint32_t floatingBits_ = 0;
    MachineBasicBlock *block_ = nullptr;
    std::string symbol_;
    int frameIndex_ = -1;
    RegisterMask regMask_;
    CondCode condition_ = CondCode::AL;
};

struct MachineMemOperand {
    enum class Access : std::uint8_t { Load, Store };

    Access access = Access::Load;
    unsigned size = 0;
    unsigned alignment = 1;
    const void *irValue = nullptr;
    std::optional<int> frameIndex;
    std::optional<std::int64_t> offset;
    bool isVolatile = false;
};

class MachineInstr {
public:
    explicit MachineInstr(Opcode opcode = Opcode::Invalid) : opcode_(opcode) {}

    Opcode opcode() const { return opcode_; }
    void setOpcode(Opcode opcode) { opcode_ = opcode; }

    std::vector<MachineOperand> &operands() { return operands_; }
    const std::vector<MachineOperand> &operands() const { return operands_; }
    MachineInstr &addOperand(MachineOperand operand);

    std::vector<MachineMemOperand> &memoryOperands() { return memoryOperands_; }
    const std::vector<MachineMemOperand> &memoryOperands() const {
        return memoryOperands_;
    }
    MachineInstr &addMemoryOperand(MachineMemOperand operand);

    bool isTerminator() const;
    bool isBranch() const;
    bool isCall() const;
    bool mayLoad() const;
    bool mayStore() const;
    bool hasSideEffects() const;
    bool isPseudo() const;
    bool readsRegister(PhysReg reg) const;
    bool definesRegister(PhysReg reg) const;

    unsigned debugLine = 0;
    unsigned slotIndex = 0;
    unsigned parallelCopyGroup = 0;

private:
    Opcode opcode_;
    std::vector<MachineOperand> operands_;
    std::vector<MachineMemOperand> memoryOperands_;
};

class MachineBasicBlock {
public:
    using InstrList = std::list<MachineInstr>;

    MachineBasicBlock(unsigned number, std::string name)
        : number_(number), name_(std::move(name)) {}

    unsigned number() const { return number_; }
    const std::string &name() const { return name_; }
    InstrList &instructions() { return instructions_; }
    const InstrList &instructions() const { return instructions_; }
    std::vector<MachineBasicBlock *> &predecessors() { return predecessors_; }
    const std::vector<MachineBasicBlock *> &predecessors() const {
        return predecessors_;
    }
    std::vector<MachineBasicBlock *> &successors() { return successors_; }
    const std::vector<MachineBasicBlock *> &successors() const {
        return successors_;
    }

    MachineInstr &append(MachineInstr instruction);
    void addSuccessor(MachineBasicBlock *successor);
    void removeSuccessor(MachineBasicBlock *successor);

    double frequency = 1.0;
    unsigned loopDepth = 0;

private:
    unsigned number_;
    std::string name_;
    InstrList instructions_;
    std::vector<MachineBasicBlock *> predecessors_;
    std::vector<MachineBasicBlock *> successors_;
};

struct VRegInfo {
    RegClass regClass = RegClass::Invalid;
    ValueType valueType = ValueType::Invalid;
    MachineInstr *definition = nullptr;
    bool spillTemporary = false;
    unsigned splitGeneration = 0;
};

class MachineRegisterInfo {
public:
    VReg createVirtualRegister(RegClass regClass, ValueType valueType);
    bool contains(VReg reg) const;
    VRegInfo &get(VReg reg);
    const VRegInfo &get(VReg reg) const;
    void setDefinition(VReg reg, MachineInstr *instruction);
    void eraseVirtualRegister(VReg reg);
    const std::unordered_map<VReg, VRegInfo> &virtualRegisters() const {
        return virtualRegisters_;
    }

private:
    VReg nextVirtualRegister_ = 1;
    std::unordered_map<VReg, VRegInfo> virtualRegisters_;
};

struct StackObject {
    int index = -1;
    unsigned size = 0;
    unsigned alignment = 1;
    int offset = 0;
    bool spill = false;
    bool fixed = false;
};

class MachineFrameInfo {
public:
    int createStackObject(unsigned size, unsigned alignment, bool spill);
    int createFixedObject(unsigned size, int offset, unsigned alignment);
    StackObject &getObject(int index);
    const StackObject &getObject(int index) const;
    std::vector<StackObject> &objects() { return objects_; }
    const std::vector<StackObject> &objects() const { return objects_; }

    unsigned stackSize = 0;
    unsigned maxCallFrameSize = 0;
    unsigned maxAlignment = 16;
    bool hasCalls = false;
    bool usesFramePointer = false;
    std::vector<PhysReg> savedRegisters;
    std::unordered_map<PhysReg, int> savedRegisterOffsets;

private:
    std::vector<StackObject> objects_;
};

enum class MachineProperty : std::uint32_t {
    None = 0,
    IsSSA = 1U << 0,
    HasPHIs = 1U << 1,
    TracksLiveness = 1U << 2,
    Legalized = 1U << 3,
    Selected = 1U << 4,
    NoVRegs = 1U << 5,
    FrameFinalized = 1U << 6,
    BranchesRelaxed = 1U << 7,
};

constexpr MachineProperty operator|(MachineProperty lhs, MachineProperty rhs) {
    return static_cast<MachineProperty>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr MachineProperty operator&(MachineProperty lhs, MachineProperty rhs) {
    return static_cast<MachineProperty>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

class MachineFunction {
public:
    struct VectorConstantPoolEntry {
        std::array<std::uint32_t, 4> lanes{};
        std::string label;
    };

    explicit MachineFunction(std::string name) : name_(std::move(name)) {}

    const std::string &name() const { return name_; }
    MachineBasicBlock &createBlock(std::string name);
    std::vector<std::unique_ptr<MachineBasicBlock>> &blocks() { return blocks_; }
    const std::vector<std::unique_ptr<MachineBasicBlock>> &blocks() const {
        return blocks_;
    }
    MachineBasicBlock *entryBlock();
    const MachineBasicBlock *entryBlock() const;

    MachineRegisterInfo &registerInfo() { return registerInfo_; }
    const MachineRegisterInfo &registerInfo() const { return registerInfo_; }
    MachineFrameInfo &frameInfo() { return frameInfo_; }
    const MachineFrameInfo &frameInfo() const { return frameInfo_; }

    // Deduplicate 128-bit vector immediates into per-function .rodata labels.
    const std::string &getOrCreateVectorConstant(
        const std::array<std::uint32_t, 4> &lanes);
    const std::vector<VectorConstantPoolEntry> &vectorConstantPool() const {
        return vectorConstantPool_;
    }

    bool hasProperty(MachineProperty property) const;
    bool hasAllProperties(MachineProperty properties) const;
    void setProperty(MachineProperty property);
    void clearProperty(MachineProperty property);

private:
    std::string name_;
    std::vector<std::unique_ptr<MachineBasicBlock>> blocks_;
    MachineRegisterInfo registerInfo_;
    MachineFrameInfo frameInfo_;
    std::vector<VectorConstantPoolEntry> vectorConstantPool_;
    std::uint32_t properties_ = 0;
};

std::string printMachineIR(const MachineFunction &function);
void printMachineIR(const MachineFunction &function, std::ostream &output);

} // namespace backend::aarch64
