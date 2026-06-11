#pragma once
// LoopVerify：循环规范形校验器（plan 阶段 0.2），建立在 LoopInfo 之上。
//
// 分级断言：
//   L1（LoopSimplify 后应满足）：每个循环有唯一 preheader、唯一 latch；
//   L2（dedicated exits，阶段 1.1 补全后启用）：每个 exit 块的前驱全在循环内；
//   L3（LCSSA，阶段 2 后启用）：循环内定义的值在循环外的 use 必须是
//      exit 块顶部的 LCSSA phi。
//
// warnOnly=true（默认）只向 stderr 告警；false 时有违例即 abort。
// 返回违例总数。

#include <string>

class Module;

int verifyLoops(Module *m, int level, const std::string &context,
                bool warnOnly = true);
