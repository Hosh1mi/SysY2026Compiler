#pragma once
#include "machine.hpp"
#include "../../mid/ir/ir.hpp"
#include <map>
#include <ostream>
#include <set>
#include <utility>
#include <vector>

class Arm64CodeGen;

class Arm64FuncContext {
public:
    Arm64FuncContext(Function *f, std::ostream &os, bool enableRegAlloc = true);
    Arm64FuncContext(Function *f, MachineEmitter &emitter, bool enableRegAlloc = true);
    void generate();

private:
    void emitMachineLine(const std::string &line);
    void emitMachineInstr(MachineInstr inst);

    void emitPrologue();
    void emitEpilogue();
    void emitBlock(BasicBlock *bb);
    void emitInstruction(Instruction *inst);

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

    // store from register to slot
    void storeInt(Value *v, const std::string &reg);
    void storeFloat(Value *v, const std::string &reg);
    void storeAddr(Value *v, const std::string &reg);
    void storeVector(Value *v, const std::string &reg);

    // emit constant into register
    void emitIntConst(int val, const std::string &reg);
    void emitFloatConst(float val, const std::string &reg);

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
    std::ostream &os_;
    MachineEmitter *machineEmitter_ = nullptr;
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
    bool needsFrame_ = true;  // false when localSize==0 (no stack, no callee-saved regs)
    std::set<BasicBlock*> branchTargets_;  // blocks referenced by branch instructions
};
