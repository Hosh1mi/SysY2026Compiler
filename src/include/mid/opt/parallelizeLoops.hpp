#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <string>
#include <vector>

class ArgumentAliasAnalysis;

// ParallelizeLoops（plan 5.2）：把可证明 DOALL 的顶层循环外提为
// __sysy_par_body_<id>(lo, hi)，调用点替换为 __sysy_parallel_for(id, lo, hi)，
// 由 .s 末尾追加的双核 runtime（clone+自旋）平分迭代空间执行。
// live-in 经 @__sysy_par_ctx_* 全局槽传递；分发经生成的
// __sysy_par_dispatch(id, lo, hi)，全程不需要函数指针。
// 设计与判定条件详见 plan 5.2 与 parallel-runtime-design。
class ParallelizeLoops : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "ParallelizeLoops"; }

private:
    struct LoopShape {
        PhiInst *ivPhi = nullptr;
        Value *init = nullptr;        // preheader 入边值
        Value *bound = nullptr;       // 上界（不变）
        Instruction *ivNext = nullptr; // phi+1
        ICmpInst *exitCmp = nullptr;  // slt 出口比较
        BasicBlock *latch = nullptr;
        BasicBlock *exitBlock = nullptr;
        BasicBlock *exitingBlock = nullptr; // 出口比较所在块（header 或 latch）
    };

    bool matchShape(Loop &loop, LoopShape &shape, std::string *reason = nullptr);
    bool isLegalDoall(Loop &loop, const LoopShape &shape, Function *func,
                      AnalysisManager *AM,
                      const ArgumentAliasAnalysis &argAA,
                      std::set<GlobalVariable *> *privatize);
    void transform(Loop &loop, const LoopShape &shape, Function *func,
                   Module *module,
                   const std::set<GlobalVariable *> &privatize);

    // 已外提的 (id, body 函数)；execute 末尾生成 dispatch
    std::vector<Function *> bodies_;
    Function *parallelForDecl_ = nullptr;
};
