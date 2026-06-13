#pragma once
#include "pass.hpp"
#include "../analysis/loopInfo.hpp"
#include "../analysis/scalarEvolution.hpp"
#include "../ir/ir.hpp"
#include <set>
#include <vector>

// IndVarStrengthReduce：把循环内随 IV 线性变化的 GEP 地址计算削弱为
// 指针 phi + 常量步进。循环结构统一来自 LoopInfo（plan 阶段 3.1）。
class IndVarStrengthReduce : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IndVarStrengthReduce"; }

private:
    void runOnFunction(Function *func, AnalysisManager &AM);

    struct BasicIV {
        PhiInst *phi;       // header phi
        Value *initVal;     // initial value
        Value *stride;      // constant stride
        bool isAdd;         // true for add, false for sub
        BasicBlock *latch;  // block with update instruction
        Instruction *updateInst; // the add/sub instruction
    };

    struct LinearIVExpr {
        bool valid = false;
        int coeff = 0;
        long long constOffset = 0;
        const SCEV *offset = nullptr; // one materializable loop-invariant non-constant term
    };

    // loop-invariant test
    bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &loopBlocks);

    // basic IV detection
    std::vector<BasicIV> findBasicIVs(Loop &loop);

    // ensure preheader exists, return it
    BasicBlock *ensurePreheader(Loop &loop, Function *func, Module *module);

    bool isSCEVLoopInvariant(const SCEV *s, const Loop &loop, ScalarEvolution &SE);
    LinearIVExpr linearizeSCEVForIV(const SCEV *s, const BasicIV &iv,
                                    const Loop &loop, ScalarEvolution &SE);
    bool canMaterializeOffsetInPreheader(const LinearIVExpr &expr, const Loop &loop);
    bool canMaterializeInvariantSCEV(const SCEV *s, const Loop &loop);
    Value *materializeInvariantSCEV(const SCEV *s, BasicBlock *preheader,
                                    const Loop &loop, IRStmtBuilder *builder,
                                    Module *module);
    Value *materializeOffsetInPreheader(const LinearIVExpr &expr, BasicBlock *preheader,
                                        const Loop &loop, IRStmtBuilder *builder,
                                        Module *module);

    // main strength reduction routine
    void processLoop(Loop &loop, Function *func, Module *module, ScalarEvolution &SE);
};
