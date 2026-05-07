#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>


class Mem2Reg : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    // -------------------------------------------------------
    // 支配分析
    // -------------------------------------------------------
    void computeDominators(Function *func);
    void computeDominanceFrontiers();
    void computeDomTreeChildren();

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

    // 支配树
    std::map<BasicBlock *, std::set<BasicBlock *>> dom;          // dom[bb] = 支配 bb 的块集合
    std::map<BasicBlock *, BasicBlock *> idom;                  // 立即支配者
    std::map<BasicBlock *, std::set<BasicBlock *>> domFront;    // 支配边界
    std::map<BasicBlock *, std::vector<BasicBlock *>> domChildren; // 支配树子节点

    // 新插入的 phi 与 alloca 的映射
    std::map<PhiInst *, AllocaInst *> phiToAlloca;

    // 待删除指令
    std::set<Instruction *> toDelete;

    bool removeUnusedAlloca(AllocaInfo &info);
    bool rewriteSingleStoreAlloca(AllocaInfo &info);
    bool promoteSingleBlockAlloca(AllocaInfo &info);
    void insertPhiNodes(AllocaInfo &info);
    
};