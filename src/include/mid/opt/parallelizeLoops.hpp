#pragma once
// ParallelizeLoops —— 将可证 DOALL 的循环并行化。
//
// 把可证明无跨迭代依赖的循环外提为并行 body，调用点改为
// parallel_for 运行时划分迭代空间。
//
// 典型支持形式：
//   for (i = lo; i < hi; ++i) A[i] = ...;     → 无依赖 DOALL
//   ans[i] += ...;                             → 可证的内存归约
//   s += term; / s = (s + term) % m;           → 可证的标量加减归约
//
// 有 loop-carried 依赖、副作用 call，或无法安全传递 live-in 时不并行化。
// 成功后生成 __sysy_par_body(lo, hi)，由运行时双核分派执行。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <string>
#include <vector>

class ArgumentAliasAnalysis;

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
