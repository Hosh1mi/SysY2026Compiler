#pragma once
// Mem2Reg —— 将可提升的 alloca 提升为 SSA（含 phi）。
//
// 对标量及可 SROA 的数组 alloca 做 SSA 化，消除局部内存流量。
//
// 典型支持形式：
//   int x; x = ...; use(x) → SSA 值传递
//   可拆的小数组元素独立提升
//   多块定义汇合处插入 phi
//
// 地址逃逸则不提升。GlobalScalarPromotion 产生的局部镜像随后由本 Pass
// 提升为 SSA。

#include "../ir/ir.hpp"
#include "pass.hpp"
#include "../analysis/dominanceAnalysis.hpp"
#include <map>
#include <set>
#include <vector>

class Mem2Reg : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "Mem2Reg"; }

private:
    bool run(Module *module, AnalysisManager &AM);
    bool runOnFunction(Function *func, AnalysisManager &AM);

    struct BlockInfo {
        std::vector<LoadInst *> loads;
        std::vector<StoreInst *> stores;
    };

    struct AllocaInfo {
        AllocaInst *alloca = nullptr;
        std::vector<LoadInst *> loads;
        std::vector<StoreInst *> stores;
        std::map<BasicBlock *, BlockInfo> userBlocks;
        std::vector<BasicBlock *> defBlocks;
        std::set<BasicBlock *> liveInBlocks;
        std::set<BasicBlock *> phiBlocks;
    };

    void resetFunctionState(Function *func);
    void collectPromotableAllocas();
    bool tryPromoteTrivialAlloca(AllocaInfo &info);
    bool removeUnusedAlloca(AllocaInfo &info);
    bool rewriteSingleStoreAlloca(AllocaInfo &info);
    bool promoteSingleBlockAlloca(AllocaInfo &info);
    void placePhiNodes(AllocaInfo &info);
    void renamePromotedAllocas();

    bool instructionDominates(Instruction *def, Instruction *use) const;
    bool hasStoreBeforeFirstLoad(const BlockInfo &blockInfo, BasicBlock *bb) const;
    Value *zeroValueFor(Type *ty) const;
    void eraseMarkedInstructions();

    bool runScalarReplacement();
    bool isScalarReplacementCandidate(AllocaInst *alloca);
    void rewriteAlloca(AllocaInst *alloca);
    bool getConstantIndices(GetElementPtrInst *gep, std::vector<int> &indices);
    static bool isScalarType(Type *ty);

    Function *currentFunc_ = nullptr;
    BasicBlock *entryBlock_ = nullptr;
    DominatorTreeAnalysis *domTree_ = nullptr;
    DominanceFrontierAnalysis *domFrontier_ = nullptr;
    std::vector<AllocaInfo> allocas_;
    std::map<PhiInst *, AllocaInst *> phiOwners_;
    std::set<Instruction *> toDelete_;
};
