#pragma once
#include "../analysis/preservedAnalyses.hpp"
#include "../ir/ir.hpp"

class AnalysisManager;

class Pass {
public:
    virtual void execute(Module *module) = 0;
    // changed 反馈通道（plan 4.2）：约定"未改动 IR ⇒ 返回 all()"。
    // 默认包装保守返回 none()（视为已改动）。
    virtual PreservedAnalyses execute(Module *module, AnalysisManager &AM) {
        (void)AM;
        execute(module);
        return PreservedAnalyses::none();
    }
    // 重复调度组的收敛判定是否采信本 pass 的 changed 报告（plan 4.2）。
    // "生成型"pass（rotate/unswitch/LICM 等暴露新机会的）默认参与；
    // 维持型/清理型（LCSSA 每轮重插 phi、DCE/InstCombine 每轮总有活干）
    // 必须覆写为 false，否则组永不收敛、轮数恒打满。
    virtual bool convergenceRelevant() const { return true; }
    virtual std::string name() const = 0;
    virtual ~Pass() = default;
};
