#pragma once
#include "machine.hpp"
#include "../../mid/ir/ir.hpp"
#include <map>
#include <set>
#include <utility>
#include <vector>

class Arm64CodeGen;

class Arm64FuncContext {
public:
    Arm64FuncContext(Function *f, MachineEmitter &emitter, bool enableRegAlloc = true);
    void generate();

private:
    void emitMachineLine(const std::string &line);
    void emitMachineInstr(MachineInstr inst);
    void emitMachineInstrLine(const std::string &line, MOpcode opcode,
                              std::initializer_list<std::string> defs = {},
                              std::initializer_list<std::string> uses = {},
                              int latency = 1,
                              bool setsFlags = false,
                              bool usesFlags = false,
                              bool isBarrier = false);
    void emitLoadRegMachine(const std::string &reg, int off);
    void emitStoreRegMachine(const std::string &reg, int off);
    void emitLoadPairMachine(const std::string &r1, const std::string &r2, int off);
    void emitStorePairMachine(const std::string &r1, const std::string &r2, int off);
    void emitLoadRegSPMachine(const std::string &reg, int off);
    void emitStoreRegSPMachine(const std::string &reg, int off);
    void emitLoadPairSPMachine(const std::string &r1, const std::string &r2, int off);
    void emitStorePairSPMachine(const std::string &r1, const std::string &r2, int off);
    void emitLoadMemMachine(const std::string &reg, const std::string &addrText,
                            std::initializer_list<std::string> uses,
                            MOpcode opcode = MOpcode::Load, int latency = 4);
    void emitStoreMemMachine(const std::string &reg, const std::string &addrText,
                             std::initializer_list<std::string> uses,
                             MOpcode opcode = MOpcode::Store);
    void emitMoveMachine(const std::string &dst, const std::string &src,
                         const std::string &opcode = "mov");
    void emitUnaryMachine(const std::string &opcode, const std::string &dst,
                          const std::string &src, MOpcode mop = MOpcode::Alu,
                          int latency = 1);
    void emitBinaryMachine(const std::string &opcode, const std::string &dst,
                           const std::string &lhs, const std::string &rhs,
                           MOpcode mop = MOpcode::Alu, int latency = 1);
    void emitRawAluMachine(const std::string &line, const std::string &dst,
                           std::initializer_list<std::string> uses,
                           MOpcode mop = MOpcode::Alu, int latency = 1);
    void emitBranchMachine(const std::string &line,
                           std::initializer_list<std::string> uses = {},
                           bool usesFlags = false);
    void emitCallMachine(const std::string &callee, CallInst *call);
    void emitRetMachine();
    void emitStackAdjustMachine(const std::string &opcode, int bytes);
    void emitStackAdjustMachine(const std::string &opcode, const std::string &reg);
    void emitFramePushMachine();
    void emitFramePopMachine();

    void emitPrologue();
    void emitEpilogue();
    void emitBlock(BasicBlock *bb);
    void emitInstruction(Instruction *inst);

    // ── constant strength reduction (constStrengthReduce.cpp) ──────────────
    // Each tryEmit* lowers a constant-operand integer op to a cheaper machine
    // sequence and returns true when it emitted (false → caller uses the
    // generic path).  emitGepIndexStep lowers one GEP index level's address
    // arithmetic, mutating the running address register `addr`.
    bool tryEmitMulConst(Instruction *inst, Value *v1, Value *v2);
    bool tryEmitSDivConst(Instruction *inst, Value *v1, Value *v2);
    bool tryEmitSRemConst(Instruction *inst, Value *v1, Value *v2);
    bool tryEmitAddSubImm(Instruction *inst, Value *v1, Value *v2,
                          const std::string &rd);
    bool tryEmitLogicalConst(Instruction *inst, Value *v1, Value *v2);
    void emitGepIndexStep(std::string &addr, Value *idx, int elemSize,
                          Instruction *inst);
    void emitLivePromotedConstsFrom(BasicBlock *bb, Instruction *after);
    bool promotedConstReachableBeforeClobber(BasicBlock *bb, Instruction *after,
                                             int val,
                                             std::set<BasicBlock*> &visited) const;
    bool instructionUsesPromotedConst(Instruction *inst, int val) const;
    bool callClobbersPromotedConst(int val) const;

    // slot management
    int getSlot(Value *v);
    bool hasSlot(Value *v) const;

    // graph-coloring register allocation result accessors
    bool hasAssignedReg(Value *v) const;
    std::string assignedReg(Value *v, bool asAddress = false) const;

    // scratch register pool
    std::string allocIntReg();
    std::string allocFloatReg();
    std::string allocAddrReg();
    void resetRegs();
    void freeAddrReg(const std::string& reg);
    void freeIntReg(const std::string &reg);

    // NEON scratch register pool
    std::string allocNEONReg();
    void freeNEONReg(const std::string &reg);
    void resetNEONRegs();

    // load from slot/constant/global to scratch register
    std::string loadInt(Value *v);
    std::string loadFloat(Value *v);
    std::string loadAddr(Value *v);
    std::string loadVector(Value *v);

    // Materialize a value in a register required by the ABI or instruction
    // encoding. Unlike load*(), these helpers never allocate an intermediate
    // scratch register.
    void materializeIntInReg(Value *v, const std::string &dst);
    void materializeFloatInReg(Value *v, const std::string &dst);
    void materializeAddrInReg(Value *v, const std::string &dst);
    std::string residentReg(Value *v, bool asAddress = false) const;
    void bindValueToReg(Value *v, const std::string &reg);

    // store from register to slot
    void storeInt(Value *v, const std::string &reg);
    void storeFloat(Value *v, const std::string &reg);
    void storeAddr(Value *v, const std::string &reg);
    void storeVector(Value *v, const std::string &reg);

    // emit constant into register
    void emitIntConst(int val, const std::string &reg);
    void emitFloatConst(float val, const std::string &reg);
    // 取一个持有常量 val 的只读寄存器：优先用提升常量寄存器，否则物化进 scratch
    std::string intConstReg(int val);

    // emit global address
    void emitGlobalAddr(GlobalVariable *gv, const std::string &reg);

    // PHI resolution
    void preparePhi();
    void emitPhiCopies(BasicBlock *pred, BasicBlock *succ);
    bool tryEmitCSel(ICmpInst *icmp, BranchInst *br);
    bool tryEmitCCmpCSel(ICmpInst *icmp, BranchInst *br);
    void emitFusedCmpBranch(ICmpInst *icmp, BranchInst *br);

    // Block layout: reorder basic blocks to maximize fallthrough
    void reorderBlocks();

    const char *icmpCond(ICmpInst::ICmpOp op);
    const char *fcmpCond(FCmpInst::FCmpOp op);

    Function *func_;
    MachineEmitter &machineEmitter_;
    bool enableRegAlloc_ = true;

    std::map<Value*, int> slots_;    // Value* → SP offset (negative)
    int frameSize_ = 0;
    int prologueFrameSize_ = 0;   // snapshot at prologue start, used by epilogue
    int slotCount_ = 0;

    std::set<int> usedIntRegs_;
    std::set<int> usedFloatRegs_;
    std::set<int> usedNEONRegs_;    // NEON scratch: v0-v7, v16-v31
    std::set<int> reservedIntRegs_;
    std::set<int> reservedFloatRegs_;
    std::set<int> reservedNEONRegs_;
    std::set<BasicBlock*> blockSkipped_;  // blocks handled by csel, don't emit
    std::set<std::pair<BasicBlock*, Value*>> cselHandled_; // (pred, phi) pairs already handled by csel

    std::map<Value*, std::string> assignedRegs_; // Value* → physical reg name (graph coloring)
    // Values deliberately produced in an ABI-constrained register during
    // lowering. This is separate from graph coloring because the constraint
    // belongs to instruction selection.
    std::map<Value*, std::string> fixedValueRegs_;
    // 循环内提升的大常量：常量值 → 专属寄存器（入口/call 后按可达使用选择性物化）
    std::map<int, std::string> promotedConsts_;

    struct PhiCopy {
        BasicBlock *pred;
        BasicBlock *succ;
        Value *src;
        int dstSlot;
        Value *phi;
    };
    std::vector<PhiCopy> phiCopies_;

    BasicBlock *epilogueBB_ = nullptr;
    int edgeCounter_ = 0;
    bool needsFrame_ = true;     // false when localSize==0 && no calls (no epilogue needed)
    bool needsFramePtr_ = true;  // false for leaf functions with no stack args (skip x29/x30)
    bool needsLinkSave_ = false; // true when x30 is saved in the local SP frame
    std::set<BasicBlock*> branchTargets_;  // blocks referenced by branch instructions
};
