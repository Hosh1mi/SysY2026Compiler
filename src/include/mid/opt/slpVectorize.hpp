#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

// SLP Vectorizer: 超字级并行向量化（Superword Level Parallelism）。
//
// 与 LoopVectorize（循环级，识别 IV-based 的连续访问）互补：
// SLP 在基本块内寻找同构的相邻标量操作，将其打包为 SIMD 向量操作。
//
// 四阶段算法（Sampled Larsen & Amarasinghe, PLDI 2000）：
//   1. findAdjacentMemoryRefs — 识别相邻的 store 对
//   2. extendPackSet        — 沿 use-def 链扩展 PackSet
//   3. combinePacks         — 合并重叠的 Pack
//   4. scheduleAndEmit      — 调度并发射向量指令
//
// 目标：Cortex-A53 NEON 128-bit，支持 <4 x i32> 和 <4 x float>。
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

    void runOnFunction(Function *func, Module *module);

    // ── Phase 1: 识别相邻内存访问 ────────────────────────────────────
    PackSet findAdjacentMemoryRefs(BasicBlock *bb, Module *module);

    // ── Phase 2: 沿 use-def 链扩展 PackSet ────────────────────────────
    PackSet extendPackSet(BasicBlock *bb, PackSet P, Module *module);

    // ── Phase 3: 合并重叠的 Pack ──────────────────────────────────────
    PackSet combinePacks(PackSet P);

    // ── Phase 4: 调度并发射向量指令 ───────────────────────────────────
    void scheduleAndEmit(BasicBlock *bb, PackSet P, Module *module);
    void emitVectorLoad(BasicBlock *bb, Pack &pack, Module *module);
    void emitVectorStore(BasicBlock *bb, Pack &pack, Module *module);
    void emitVectorBinary(BasicBlock *bb, Pack &pack, Module *module,
                          PackSet &packs);

    // ── 辅助函数 ─────────────────────────────────────────────────────
    bool isIsomorphic(Instruction *a, Instruction *b);
    bool isIndependent(Instruction *a, Instruction *b);
    bool isVectorizable(Instruction *inst);
    bool hasInterveningMemoryEffect(
        BasicBlock *bb, const std::vector<Instruction*> &instructions);
    bool isAdjacentStore(Instruction *a, Instruction *b, Module *module);
    bool isAdjacentLoad(Instruction *a, Instruction *b, Module *module);
    Value *getStoredValue(Instruction *store);
    Value *getStorePointer(Instruction *store);
};
