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
    Arm64FuncContext(Function *f, Arm64CodeGen &parent, std::ostream &os);
    void generate();

private:
    void emitPrologue();
    void emitEpilogue();
    void emitBlock(BasicBlock *bb);
    void emitInstruction(Instruction *inst);

    // slot management
    int getSlot(Value *v);
    bool hasSlot(Value *v) const;

    // scratch register pool
    std::string allocIntReg();
    std::string allocFloatReg();
    std::string allocAddrReg();
    void resetRegs();

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

    const char *icmpCond(ICmpInst::ICmpOp op);
    const char *fcmpCond(FCmpInst::FCmpOp op);

    Function *func_;
    Arm64CodeGen &parent_;
    std::ostream &os_;

    std::map<Value*, int> slots_;    // Value* → SP offset (negative)
    int frameSize_ = 0;
    int slotCount_ = 0;

    std::set<int> usedIntRegs_;
    std::set<int> usedFloatRegs_;

    // (predecessor_BB, result_slot_offset) → source Value*
    std::vector<std::pair<BasicBlock*, std::pair<Value*, int>>> phiCopies_;

    BasicBlock *epilogueBB_ = nullptr;
};
