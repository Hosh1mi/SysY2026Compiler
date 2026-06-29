#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>

class Mem2Reg : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "Mem2Reg"; }

private:
    bool run(Module *module);
    bool runOnFunction(Function *func);

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
    DominatorInfo *domInfo_ = nullptr;
    std::vector<AllocaInfo> allocas_;
    std::map<PhiInst *, AllocaInst *> phiOwners_;
    std::set<Instruction *> toDelete_;
};
