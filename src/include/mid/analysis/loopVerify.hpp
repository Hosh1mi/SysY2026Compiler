#pragma once
// LoopVerify：循环规范形校验器，建立在 LoopInfo 之上。
//
// 分级断言：
//   L1：每个循环有 dedicated preheader、唯一 latch；
//   L2：每个 exit 块的前驱全在循环内（dedicated exits）；
//   L3：循环内定义的值在循环外的 use 必须是 exit 块顶部的 LCSSA phi。
//
// warnOnly=true（默认）只向 stderr 告警；false 时有违例即 abort。
// PassManager 的 --verify-ir 默认使用 warn-only；设置 LOOP_VERIFY_STRICT=1
// 时对 loop transform 后的违例启用 abort，便于定位第一个破坏规范形的 pass。

#include <string>

class Module;

struct LoopVerifyResult {
    int loops = 0;
    int l1Violations = 0;
    int l2Violations = 0;
    int l3Violations = 0;

    int totalViolations() const {
        return l1Violations + l2Violations + l3Violations;
    }
};

LoopVerifyResult verifyLoopForms(Module *m, int level,
                                 const std::string &context,
                                 bool warnOnly = true,
                                 bool reportClean = false);

int verifyLoops(Module *m, int level, const std::string &context,
                bool warnOnly = true);
