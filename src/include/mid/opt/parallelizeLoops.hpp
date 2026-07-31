#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <string>
#include <vector>

class ArgumentAliasAnalysis;

// ParallelizeLoops（plan 5.2）：把可证明 DOALL 的循环区域外提为
// __sysy_par_body_<id>(lo, hi)，调用点替换为 __sysy_parallel_for(id, lo, hi)，
// 由 .s 末尾追加的双核 runtime（clone+自旋）平分迭代空间执行。
// 可以外提包含子循环的嵌套循环区域；嵌套叶循环仅保留已有的归约场景，
// 避免在父循环每次迭代中支付一次并行 dispatch。祖先优先的遍历和 call
// 副作用检查保证不会生成递归的 parallel runtime 调用。
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
        bool latchComparesIV = false; // latch 中用 iv < bound 表示闭区间末次迭代
    };

    // 归约操作：store 将累加值写回同一内存位置（如 ans[i] = ans[i] + x）
    struct Reduction {
        Instruction *store;
        Instruction *load;
        Value *accRoot;          // 累加器根（GEP 基址）
    };

    // 标量加/减归约：acc.next = acc +/- term，
    // 可选外层 positive_const srem。
    enum class ScalarModuloSource {
        None,
        InlineModulo,
        LiveOutModulo,
    };

    struct ScalarReduction {
        PhiInst *phi = nullptr;
        BinaryInst *update = nullptr;
        BinaryInst *rem = nullptr;
        std::vector<Value *> liveOutUpdateValues;
        std::vector<Value *> liveOutFinalValues;
        std::vector<BinaryInst *> liveOutRems;
        Value *term = nullptr;
        ConstantInt *mod = nullptr;
        ConstantInt *identity = nullptr;
        bool isSub = false;
        ScalarModuloSource moduloSource = ScalarModuloSource::None;
    };

    bool matchShape(Loop &loop, LoopShape &shape, std::string *reason = nullptr);
    bool isLegalDoall(Loop &loop, const LoopShape &shape, Function *func,
                      AnalysisManager *AM,
                      const ArgumentAliasAnalysis &argAA,
                      std::set<Value *> *privatize,
                      std::vector<Reduction> *reductions = nullptr,
                      std::vector<ScalarReduction> *scalarReductions = nullptr);
    void transform(Loop &loop, const LoopShape &shape, Function *func,
                   Module *module,
                   const std::set<Value *> &privatize,
                   const std::vector<Reduction> &reductions = {},
                   const std::vector<ScalarReduction> &scalarReductions = {});

    // 已外提的 (id, body 函数)；execute 末尾生成 dispatch
    std::vector<Function *> bodies_;
    Function *parallelForDecl_ = nullptr;
};
