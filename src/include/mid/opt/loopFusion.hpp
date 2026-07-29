#pragma once
// LoopFusion:
//   保守的同级相邻循环融合。只处理边界【完全一致】的规范 while 循环：
//   两侧都有 preheader / header 出口测试 / 单 latch / 单 dedicated exit，
//   canonical IV（init=0, step=+1, slt bound），且 bound 是同一个 SSA 值
//   （或等值常量），从而两侧 trip count 恒等。
//
// 相邻性：
//   L1 的唯一 exit 经一串中间块到达 L2 的 preheader。中间块只允许
//   无内存副作用、非 phi 的纯指令，且其操作数在两个循环之外定义；
//   这些指令执行次数与 L1.preheader 一致，融合前整体提到 L1.preheader
//   末尾，语义不变。中间块出现 phi（如 LCSSA 转发）或有内存副作用
//   指令时拒绝——融合会改变其求值时机。
//
// 合法性：
//   - 标量 SSA：L2 内任何指令不得使用 L1 内定义的值（融合后 L2 看到的
//     将是中间迭代值而非 L1 完成后的终值）。
//   - 内存依赖：对 L1×L2 的访存对（至少一端为 store）：
//       * 基址可证明不别名 → 无关；
//       * 基址 MustAlias：逐维比较仿射下标 e1(i1)/e2(i2)，只要某一维
//         在等式成立时强制 i1 <= i2（SIV：i1 - i2 = 非正常数，含 0），
//         或某一维可证明恒不等（两边都是常数且不同），该对即安全；
//         否则（任一维都无法给出保证）拒绝。
//       * 基址 MayAlias、非 GEP 的同址访问、维数不一致 → 拒绝。
//   - 任一侧循环体内（含后代循环）出现 call → 拒绝。
//
// 收益保护：
//   LoopInterchange 紧随本 pass；若任一侧已有依赖分析与 stride 代价模型
//   认可的 parallel-sink / parallel-float 方案，融合加入的额外 payload
//   会破坏其 single-child/perfect-nest 形态，因此拒绝融合并保留该方案。
//
// 变换（保留 L1 骨架）：
//   L1.latch 改跳 L2.bodyEntry，L2.latch 改跳 L1.header，
//   L1.header 的 exit 边改指 L2.exit；L2 的非 IV phi 迁入 L1.header
//   （preheader 入边 P2→P1），iv2 全部 RAUW 为 iv1（两边迭代区间相同）。
//   中间块与 L2.header 变不可达，由 removeUnreachableBlocks 回收。
//   结果仍是 simplified + LCSSA 形态。嵌套融合靠不动点重扫自然完成：
//   外层融合后两侧子循环在新父循环体内相邻，下一轮按同一规则融合。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/argumentAliasAnalysis.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class LoopFusion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopFusion"; }

private:
    // 规范 while 循环形态：header 做 iv<bound 出口测试，latch 无条件跳回。
    struct Shape {
        BasicBlock *preheader = nullptr;
        BasicBlock *header    = nullptr;
        BasicBlock *latch     = nullptr;
        BasicBlock *exit      = nullptr;
        BasicBlock *bodyEntry = nullptr;  // header 的循环内后继
        PhiInst    *iv        = nullptr;
        Value      *bound     = nullptr;
        Value      *backedge  = nullptr;  // IV phi 在 latch 的 incoming 值
        ICmpInst   *guard     = nullptr;
        bool ok = false;
    };

    bool runOnFunction(Function *func);

    Shape analyzeShape(Loop *L) const;
    // 从 L1.exit 沿单后继链走到某个同级循环的 preheader；chain 收集途经块
    // （含 L1.exit 与 L2.preheader）。找不到同级后继循环时返回 nullptr。
    Loop *walkToSibling(const Shape &s1, Loop *L1, LoopInfo &LI,
                        std::vector<BasicBlock *> &chain) const;
    bool boundsEqual(const Shape &s1, const Shape &s2) const;
    bool chainHoistable(Loop *L1, Loop *L2,
                        const std::vector<BasicBlock *> &chain) const;
    bool callsArePure(Loop *L1, Loop *L2) const;
    bool noScalarCrossUse(Loop *L1, Loop *L2) const;
    bool headerContentSimple(const Shape &s2) const;
    // L2.header 中 phi 的初值在融合后必须在 L1.preheader 可用。
    bool phiInitsAvailable(Loop *L1, const Shape &s1, Loop *L2,
                           const Shape &s2,
                           const std::vector<BasicBlock *> &chain) const;
    // E2 / L2.bodyEntry 中 phi 的入边值在改接前驱后仍支配新前驱。
    bool exitUsesAvailable(Loop *L1, const Shape &s1, Loop *L2,
                           const Shape &s2,
                           const std::vector<BasicBlock *> &chain) const;
    bool memoryLegal(Loop *L1, Loop *L2, AffineAnalysis &AA) const;
    const char *profitabilityRejection(
        Loop *L1, Loop *L2, LoopInterchangeAnalysis &IA) const;

    void applyFusion(Function *func, const Shape &s1, const Shape &s2,
                     const std::vector<BasicBlock *> &chain);

    const ArgumentAliasAnalysis *argAA_ = nullptr;
};
