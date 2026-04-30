#pragma once

#include "arm_context.hpp"
#include <cstdint>
#include <string>

class ArmBuilder {
public:
    ArmBuilder() = default;

    std::string buildArm(Module *module);

private:
    enum class InstrKind {
        Ret,
        Br,
        BinOp,
        ICmp,
        FCmp,
        Call,
        Load,
        Store,
        Alloca,
        Zext,
        FpToSi,
        SiToFp,
        Bitcast,
        Gep,
        Phi,
        Unknown,
    };

    std::string emitModule(Module *module);
    std::string emitData(Module *module);
    std::string emitFunction(Function *func);
    std::string emitRuntimeHelpers();

    void analyzeFunction(ArmFuncContext &ctx);
    void assignStackSlots(ArmFuncContext &ctx);
    void collectPhiMoves(ArmFuncContext &ctx);
    void emitFunctionBody(ArmFuncContext &ctx);
    void emitBasicBlock(ArmFuncContext &ctx, BasicBlock *bb);
    void emitInstruction(ArmFuncContext &ctx, Instruction *inst);
    InstrKind classifyInstruction(Instruction *inst) const;
    void emitPhiCopiesForSuccessor(ArmFuncContext &ctx, BasicBlock *succ);
    void emitParallelMoves(ArmFuncContext &ctx, const std::vector<ArmFuncContext::PhiMove> &moves);

    void emitPrologue(ArmFuncContext &ctx);
    void emitEpilogue(ArmFuncContext &ctx);

    void emitValueToReg(ArmFuncContext &ctx, Value *v, const std::string &reg, bool asFloat = false);
    void emitStoreFromReg(ArmFuncContext &ctx, Value *dst, const std::string &reg, bool asFloat = false);
    void emitLoadValue(ArmFuncContext &ctx, Value *v, const std::string &reg, bool asFloat = false);
    void emitAddrOf(ArmFuncContext &ctx, Value *v, const std::string &reg);

    static bool isFloatTy(Type *ty);
    static bool isIntTy(Type *ty);
    static bool isPointerLike(Type *ty);
    static bool isImm12(int64_t v);
    static std::string globalName(Value *v);
    static std::string escapeLabel(const std::string &name);
    static std::string typeSuffix(Type *ty);
    static int64_t constInt(Value *v);
    static float constFloat(Value *v);
    static std::string floatLiteral(float f);
    static uint32_t floatBits(float f);
    static size_t typeSize(Type *ty);

    int allocateSlot(ArmFuncContext &ctx, Value *v, size_t size, size_t align = 8);
    int alignUp(int x, int align) const;
    std::string uniqueLabel(const std::string &base);

    int temp_counter_ = 0;
};
