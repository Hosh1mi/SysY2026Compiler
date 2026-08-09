#pragma once
// CFGSimplify —— 化简控制流图。
//
// 合并线性块、折叠常量分支、菱形转 select，并删除不可达块。
//
// 典型支持形式：
//   A → B → C（B 无其他前驱）→ 合并
//   br i1 true, T, F → br T
//   if (c) x = a; else x = b → x = select c, a, b
//
// 循环内 if-body 的专用 select 化由 IfConversion 负责。

#include "pass.hpp"

enum class CFGSimplifyMode {
    Lite,
    Full,
};

class CFGSimplify : public Pass {
public:
    explicit CFGSimplify(CFGSimplifyMode mode = CFGSimplifyMode::Full)
        : mode_(mode) {}
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return mode_ == CFGSimplifyMode::Full ? "CFGSimplify"
                                              : "CFGSimplifyLite";
    }
private:
    CFGSimplifyMode mode_;
    bool runOnModule(Module *module);
    bool convertDiamondsToSelect(Function *func);
    bool hoistLoopInvariantBranch(Function *func);
};
