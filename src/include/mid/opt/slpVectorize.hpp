#pragma once
// SLPVectorize —— 基本块内将相邻同构标量运算打包为 SIMD。
//
// 从相邻 store 出发，沿 use-def 扩展同构运算串，发射 <4 x i32/float>
// 向量 load / store / 算术。
//
// 典型支持形式：
//   a[i]=x0; a[i+1]=x1; a[i+2]=x2; a[i+3]=x3;   → 向量 store
//   对应相邻 load 与同构 add/sub/mul（及移位、逻辑、浮点加减乘）
//
// 要求访问相邻、运算同构、内存依赖可证安全且代价模型盈利。
// 本 Pass 做 basic-block 级超字并行；带 induction variable 的循环级
// 向量化由 LoopVectorize 负责。

#include "../ir/ir.hpp"
#include "../analysis/basicAliasAnalysis.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

class SLPVectorize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SLPVectorize"; }

private:
    static const int VF = 4;  // NEON 128-bit → 4 × i32/float

    struct Pack {
        std::vector<Instruction*> instrs;  // VF 条同构指令
        Value *vecValue = nullptr;         // 产生的向量值（发射后）
        std::vector<Value*> scalarValues;  // 未打包用户所需的逐 lane 值
        bool emitted = false;
    };

    struct PackSet {
        std::vector<Pack> packs;
        bool contains(Instruction *s) const;
        bool containsAny(const std::vector<Instruction*> &cands) const;
        void add(Pack p);
    };

    bool runOnFunction(Function *func, Module *module,
                       const BasicAliasAnalysis &BAA);

    // ── Phase 1: 识别相邻内存访问 ────────────────────────────────────
    PackSet findAdjacentMemoryRefs(BasicBlock *bb, Module *module);

    // ── Phase 2: 沿 use-def 链扩展 PackSet ────────────────────────────
    PackSet extendPackSet(BasicBlock *bb, PackSet P, Module *module);

    // ── Phase 3: 合并重叠的 Pack ──────────────────────────────────────
    PackSet combinePacks(PackSet P);

    // ── Phase 4: 调度并发射向量指令 ───────────────────────────────────
    bool scheduleAndEmit(BasicBlock *bb, PackSet P, Module *module,
                         const BasicAliasAnalysis &BAA);
    void emitVectorLoad(BasicBlock *bb, Pack &pack, Module *module);
    void emitVectorStore(BasicBlock *bb, Pack &pack, Module *module);
    void emitVectorBinary(BasicBlock *bb, Pack &pack, Module *module,
                          PackSet &packs);

    // ── 辅助函数 ─────────────────────────────────────────────────────
    bool isIsomorphic(Instruction *a, Instruction *b);
    bool isIndependent(Instruction *a, Instruction *b);
    bool isVectorizable(Instruction *inst);
    bool hasInterveningMemoryEffect(
        BasicBlock *bb, const std::vector<Instruction*> &instructions,
        const BasicAliasAnalysis &BAA);
    bool isProfitable(const PackSet &P) const;
    bool isAdjacentStore(Instruction *a, Instruction *b, Module *module);
    bool isAdjacentLoad(Instruction *a, Instruction *b, Module *module);
    Value *getStoredValue(Instruction *store);
    Value *getStorePointer(Instruction *store);
};
