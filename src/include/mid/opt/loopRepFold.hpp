#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>

// LoopRepFold: 循环重复折叠
// 识别并消除"纯重复计数循环"：while (r < R) { total += f(data); r++; }
// 其中 f(data) 不依赖 r 且不修改 data，变换为: total += f(data) * R（单次执行+乘法）
// 主要目标：many_mat_cal 中 157 亿次迭代 → T² 次计算 + 1 次乘法
class LoopRepFold : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopRepFold"; }

private:
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;
        BasicBlock *preheader;
        std::set<BasicBlock *> blocks;
    };

    void runOnFunction(Function *func);
    std::vector<Loop> findLoops(Function *func);
    std::vector<BasicBlock *> computeRPO(Function *func);
    void computeDominators(const std::vector<BasicBlock *> &rpo);
    BasicBlock *intersect(BasicBlock *a, BasicBlock *b);
    bool dominates(BasicBlock *a, BasicBlock *b);
    bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks);
    bool isCountingIV(PhiInst *phi, const Loop &loop);
    bool tryFold(Loop &loop, Module *module);

    std::map<BasicBlock *, BasicBlock *> idom_;
    std::map<BasicBlock *, int> rpoIdx_;
};
