#pragma once
// LoopInfo: per-function loop analysis.
//   - 自然循环检测（基于支配关系的回边）
//   - 循环嵌套树（parent / children / depth）
//   - 关键控制流件：preheader / latches / exiting / exits
//   - 规范归纳变量识别（init=0, step=+1）与 trip-count
//
// 设计原则：纯查询结构，不修改 IR；passes 持有的 Loop* 在 analyze() 重新调用前稳定。

#include "../ir/ir.hpp"
#include "../ir/instruction.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Loop {
public:
    // CFG 关键块
    BasicBlock *header    = nullptr;   // 循环入口，被所有 latch 跳回
    // Dedicated preheader: header 的唯一循环外前驱，且该前驱只有一条
    // 无条件 br 指向 header。找不到这种规范入口时为 nullptr。
    BasicBlock *preheader = nullptr;
    std::vector<BasicBlock *> latches; // 所有指向 header 的循环内块（back-edge sources）
    std::vector<BasicBlock *> exiting; // 循环内、有后继落在循环外的块（RPO 序）
    std::vector<BasicBlock *> exits;   // 循环外、被 exiting 跳到的块（去重，RPO 序）
    std::set<BasicBlock *>    blocks;  // 本循环及所有子循环的所有块（成员查询用）
    // blocks 的确定序视图（RPO）。std::set 按指针排序，跨进程不稳定——
    // 凡是"遍历顺序会影响产出 IR 顺序"的场景（LICM 外提、IVSR 候选收集、
    // LCSSA 快照等）必须用本列表，不要直接迭代 blocks。
    std::vector<BasicBlock *> blocksOrdered;

    // 嵌套
    Loop *parent = nullptr;
    std::vector<Loop *> children;
    int depth = 0;                     // 顶层=0, 子循环递增

    // 规范归纳变量（从零开始的 +1 归纳变量）。
    PhiInst *canonicalIV = nullptr;
    Value   *tripCount   = nullptr;    // icmp 的上界
    // 更一般的循环归纳变量：允许循环不变的非零初值。
    // canonicalIV 仍仅表示零初值形式，避免改变依赖分析中
    // tripCount 的既有含义；需要处理一般 +1 归纳变量的变换应使用
    // inductionIV / inductionInit。
    PhiInst *inductionIV = nullptr;
    Value   *inductionInit = nullptr;
    ICmpInst::ICmpOp predicate = ICmpInst::ICMP_SLT;  // <, <=（暂只识别 SLT）

    // 查询
    bool isInLoop(BasicBlock *bb) const { return blocks.count(bb) > 0; }
    bool isInLoop(Instruction *inst) const { return inst && isInLoop(inst->parent_); }
    BasicBlock *singleLatch() const { return latches.size() == 1 ? latches[0] : nullptr; }
    BasicBlock *singleExit()  const { return exits.size()   == 1 ? exits[0]   : nullptr; }
    bool hasCanonicalIV() const { return canonicalIV != nullptr; }
    bool hasInductionIV() const { return inductionIV != nullptr; }
    PhiInst *getInductionIV() const {
        return inductionIV ? inductionIV : canonicalIV;
    }

    std::string print() const;
};

class LoopInfo {
public:
    // 重新分析一个函数：清空旧状态后重新计算支配关系、循环、嵌套、IV
    void analyze(Function *func);

    // 清空所有状态
    void reset();

    // 查询
    Loop *getLoopFor(BasicBlock *bb) const;                  // 包含 bb 的最内层循环（无则 nullptr）
    const std::vector<Loop *>      &topLevelLoops() const { return top_; }
    const std::vector<std::unique_ptr<Loop>> &allLoops() const { return loops_; }

    // 支配关系（顺手暴露，避免各 pass 再算一遍）
    bool dominates(BasicBlock *a, BasicBlock *b) const;
    BasicBlock *getIDom(BasicBlock *bb) const;

    // 调试输出
    std::string print() const;

private:
    // 流水线步骤
    std::vector<BasicBlock *> computeRPO(Function *func);
    void                      computeDominators(const std::vector<BasicBlock *> &rpo);
    BasicBlock               *intersect(BasicBlock *a, BasicBlock *b);
    void                      findLoops(Function *func);
    void                      enrichLoop(Loop *loop);   // preheader/latches/exits 等
    void                      buildNestTree();
    void                      analyzeIV(Loop *loop);

    // 存储
    std::vector<std::unique_ptr<Loop>> loops_;             // 拥有所有 Loop
    std::vector<Loop *>                top_;               // 顶层循环
    std::map<BasicBlock *, Loop *>     bb2innermost_;      // 每个 BB 的最内层循环

    // 支配信息（analyze 结束后保留）
    std::map<BasicBlock *, BasicBlock *> idom_;
    std::map<BasicBlock *, int>          rpoIdx_;
};
