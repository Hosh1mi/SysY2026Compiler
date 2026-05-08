#pragma once
#include "../../mid/ir/ir.hpp"
#include <map>
#include <ostream>
#include <set>
#include <utility>
#include <vector>

class Arm64CodeGen;

class Arm64FuncContext {
public:
    Arm64FuncContext(Function *f, std::ostream &os);
    void generate();

private:
    void emitPrologue();
    void emitEpilogue();
    void emitBlock(BasicBlock *bb);
    void emitInstruction(Instruction *inst);

    // slot management
    int getSlot(Value *v);
    bool hasSlot(Value *v) const;

    // linear-scan register allocation for SSA values
    void allocateLinearScanRegisters();
    bool canAssignRegister(Value *v) const;
    bool hasAssignedReg(Value *v) const;
    std::string assignedReg(Value *v, bool asAddress = false) const;

    // scratch register pool
    std::string allocIntReg();
    std::string allocFloatReg();
    std::string allocAddrReg();
    void resetRegs();
    void freeAddrReg(const std::string& reg);
    void freeIntReg(const std::string &reg);

    // load from slot/constant/global to scratch register
    std::string loadInt(Value *v);
    std::string loadFloat(Value *v);
    std::string loadAddr(Value *v);

    // store from register to slot
    void storeInt(Value *v, const std::string &reg);
    void storeFloat(Value *v, const std::string &reg);
    void storeAddr(Value *v, const std::string &reg);

    // emit constant into register
    void emitIntConst(int val, const std::string &reg);
    void emitFloatConst(float val, const std::string &reg);

    // emit global address
    void emitGlobalAddr(GlobalVariable *gv, const std::string &reg);

    // PHI resolution
    void preparePhi();
    void emitPhiCopies(BasicBlock *bb);

    void getLiveRegsAtCall(Instruction *call,
        std::vector<int> &liveIntRegs,
        std::vector<int> &liveFloatRegs);

    const char *icmpCond(ICmpInst::ICmpOp op);
    const char *fcmpCond(FCmpInst::FCmpOp op);

    Function *func_;
    std::ostream &os_;

    std::map<Value*, int> slots_;    // Value* → SP offset (negative)
    int frameSize_ = 0;
    int slotCount_ = 0;

    std::set<int> usedIntRegs_;
    std::set<int> usedFloatRegs_;

    std::map<Value*, std::string> assignedRegs_; // linear-scan Value* → physical reg name

    // (predecessor_BB, result_slot_offset) → source Value*
    std::vector<std::pair<BasicBlock*, std::pair<Value*, int>>> phiCopies_;

    BasicBlock *epilogueBB_ = nullptr;
};
