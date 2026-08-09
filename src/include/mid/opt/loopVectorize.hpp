#pragma once
// LoopVectorize —— 将规则的标量循环转换为 SIMD 循环。
//
// 目前主要处理两类循环：标量归约和逐元素数组计算。
//
// 归约支持的典型形式包括：
//   sum:      s += a[i]
//   dot:      s += a[i] * b[i]
//   min/max:  s = min(s, a[i])
//   chained:  s -= a[i] * b[i]
//
// 逐元素计算例如：
//   C[i] = A[i] + B[i] * k
//   A[i] = A[i] << 1
//
// 内存访问需要是连续访问、规则的递增指针，或循环不变量；必要时可以通过
// runtime alias check 保护无法静态证明互不重叠的数组访问。
//
// 成功后生成固定宽度的 vector main loop，并保留原标量循环处理 remainder。
// 当前主要面向结构规则的 canonical loops；复杂控制流和无法保证 memory
// dependence 安全的循环不会进行向量化。
//
// 本 Pass 做 loop-level vectorization；单个 basic block 内的独立标量操作
// 组合由 SLPVectorize 处理。

#include "../analysis/loopVectorizationAnalysis.hpp"
#include "pass.hpp"

#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

class LoopVectorize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopVectorize"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    struct InductionVar {
        PhiInst *phi = nullptr;
        Value *initVal = nullptr;
        int stride = 0;
        bool isAdd = true;
        Instruction *updateInst = nullptr;
    };

    struct PackedOperand {
        enum Kind { CONTIGUOUS, IV_STEP, GATHER, INVARIANT };
        Kind kind;
        Type *scalarTy;
        Value *source;
        int laneStride;
    };

    struct ReductionGroup {
        enum Kind { Add, Sub, SMin, SMax };
        PhiInst *accPhi;
        Value *initVal;
        Value *latchValue;
        PackedOperand lhs;
        PackedOperand rhs;
        // Optional expression-form reduction description.  The legacy
        // two-operand form leaves this empty and uses lhs/rhs directly.
        std::vector<std::pair<LoadInst *, PackedOperand>> expressionLoads;
        std::vector<Value *> expressionTerms;
        bool expressionReduction = false;
        int scalarStep;
        Kind kind = Add;
        bool isAdd = false;
        bool noMul = false;
    };

    void runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    bool findInductionVar(const Loop &loop, InductionVar &iv);
    bool analyzeReductionLoop(const Loop &loop, const InductionVar &iv,
                              ReductionGroup &group);
    bool isLoopInvariant(Value *val,
                         const std::set<BasicBlock *> &loopBlocks);

    bool tryVectorize(Loop &loop, Function *func, Module *module,
                      const BasicAliasAnalysis &BAA);
    void emitReductionVectorizedLoop(const Loop &loop, const InductionVar &iv,
                                     const ReductionGroup &group,
                                     int vecWidth, Function *func,
                                     Module *module);
    bool emitVectorizedLoop(const LoopVectorizationAnalysis::Plan &plan,
                            Function *func, Module *module);
};
