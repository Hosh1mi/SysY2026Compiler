#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>


class Mem2Reg : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "Mem2Reg"; }

private:
    void runOnFunction(Function *func);

    // -------------------------------------------------------
    // 提升核心
    // -------------------------------------------------------
    void analyseAlloca();
    bool removeUnusedAlloca(struct AllocaInfo &info);
    bool rewriteSingleStoreAlloca(struct AllocaInfo &info);
    bool promoteSingleBlockAlloca(struct AllocaInfo &info);
    void insertPhiNodes(struct AllocaInfo &info);
    void rename();

    // -------------------------------------------------------
    // 工具
    // -------------------------------------------------------
    // 判断指令 ia 是否支配 ib
    bool iADomB(Instruction *ia, Instruction *ib);

    // 临时数据
    Function *currentFunc = nullptr;
    BasicBlock *entryBlock = nullptr;
    Module *curModule = nullptr;

    // 共享支配树（来自 Function）
    DominatorInfo *domInfo_ = nullptr;

    // alloca 信息
    struct BlockInfo {
        std::vector<LoadInst *> loads;
        std::vector<StoreInst *> stores;
    };
    struct AllocaInfo {
        AllocaInst *alloca;
        std::vector<LoadInst *> loads;
        std::vector<StoreInst *> stores;
        std::map<BasicBlock *, BlockInfo> userBlocks;
        std::vector<BasicBlock *> defBlocks;
        std::set<BasicBlock *> liveInBlocks;
        std::set<BasicBlock *> phiBlocks;
    };
    std::vector<AllocaInfo> allocas;

    // 新插入的 phi 与 alloca 的映射
    std::map<PhiInst *, AllocaInst *> phiToAlloca;

    // 待删除指令
    std::set<Instruction *> toDelete;

    // -------------------------------------------------------
    // SROA 预处理：将聚合 alloca 拆分为标量 alloca
    // -------------------------------------------------------
    void runSROA();
    bool isSROACandidate(AllocaInst *alloca);
    void rewriteAlloca(AllocaInst *alloca);
    bool getConstantIndices(GetElementPtrInst *gep, std::vector<int> &indices);
    static bool isScalarType(Type *ty);

    bool removeUnusedAlloca(AllocaInfo &info);
    bool rewriteSingleStoreAlloca(AllocaInfo &info);
    bool promoteSingleBlockAlloca(AllocaInfo &info);
    void insertPhiNodes(AllocaInfo &info);

};