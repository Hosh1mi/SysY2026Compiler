#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <numeric>

// 整数 gcd（取绝对值，gcd(0, x) = |x|）
static long gcdAbs(long a, long b) {
    a = std::abs(a); b = std::abs(b);
    while (b) { long t = a % b; a = b; b = t; }
    return a;
}

// ── helpers ─────────────────────────────────────────────────────────────

GetElementPtrInst *DependenceAnalysis::accessGEP(Instruction *acc) const {
    if (auto *st = dynamic_cast<StoreInst *>(acc))
        return dynamic_cast<GetElementPtrInst *>(st->get_operand(1));
    if (auto *ld = dynamic_cast<LoadInst *>(acc))
        return dynamic_cast<GetElementPtrInst *>(ld->get_operand(0));
    return nullptr;
}

Value *DependenceAnalysis::gepBase(GetElementPtrInst *gep) const {
    if (!gep) return nullptr;
    Value *base = gep->get_operand(0);
    // 追溯到最底层非 GEP 的值（嵌套 GEP 时取根）
    while (auto *inner = dynamic_cast<GetElementPtrInst *>(base))
        base = inner->get_operand(0);
    return base;
}

bool DependenceAnalysis::sameBase(Value *a, Value *b) const {
    if (a == b) return true;
    // 不同全局变量绝对不别名
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return false;
    return true;   // 保守：可能别名
}

// 求两个 BB 共同最内层包围循环（沿嵌套树向上求交）
static std::vector<Loop *> commonNest(Loop *l1, Loop *l2) {
    std::vector<Loop *> chain1, chain2;
    for (Loop *c = l1; c; c = c->parent) chain1.push_back(c);
    for (Loop *c = l2; c; c = c->parent) chain2.push_back(c);
    std::reverse(chain1.begin(), chain1.end());
    std::reverse(chain2.begin(), chain2.end());
    std::vector<Loop *> common;
    for (size_t i = 0; i < chain1.size() && i < chain2.size(); i++) {
        if (chain1[i] != chain2[i]) break;
        common.push_back(chain1[i]);
    }
    return common;   // 顶层在前
}

// ── 主测试 ──────────────────────────────────────────────────────────────

DependenceAnalysis::Result
DependenceAnalysis::test(Instruction *acc1, Instruction *acc2) {
    Result r;

    auto *g1 = accessGEP(acc1);
    auto *g2 = accessGEP(acc2);
    if (!g1 || !g2) {
        r.aliased = true;        // 保守：可能别名
        return r;
    }

    Value *b1 = gepBase(g1);
    Value *b2 = gepBase(g2);
    if (!sameBase(b1, b2)) {
        r.provably_independent = true;
        return r;
    }
    r.aliased = true;

    // 共同嵌套循环
    Loop *l1 = LI_->getLoopFor(acc1->parent_);
    Loop *l2 = LI_->getLoopFor(acc2->parent_);
    r.commonLoops = commonNest(l1, l2);
    if (r.commonLoops.empty()) return r;   // 不在任何共同循环里

    // GEP 索引数得一致
    if (g1->num_ops_ != g2->num_ops_) return r;
    unsigned n_idx = g1->num_ops_ - 1;

    // 对每个索引提取仿射形式，看差异
    // diff_k = idx1_k - idx2_k = c_const + Σ c_iv · iv
    // 若 diff_k 是非零常数：永远不会重合 → 整体无依赖
    // 若 diff_k 含 IV 项：按系数推方向
    std::vector<AffineExpr> diffs;
    diffs.reserve(n_idx);
    for (unsigned k = 0; k < n_idx; k++) {
        AffineExpr a = AA_->analyze(g1->get_operand(k + 1));
        AffineExpr b = AA_->analyze(g2->get_operand(k + 1));
        if (!a.valid || !b.valid) {
            // 无法仿射化：保守，给所有方向 ANY
            r.direction.assign(r.commonLoops.size(), DIR_ANY);
            return r;
        }
        diffs.push_back(a - b);
    }

    // GCD test（包含 "纯常数非零" 这条特例）：
    //   每维 diff = c_const + Σ c_iv · iv 要有整数解 iv ∈ ℤ，必须 gcd(c_iv) | c_const。
    //   有任何一维 gcd 不整除 c_const → 无解 → 整对独立。
    //   gcd = 0 表示所有系数为 0；此时只看 c_const 是否非零。
    bool gcd_proves_independent = false;
    for (auto &d : diffs) {
        long g = 0;
        for (auto &kv : d.coeffs) g = gcdAbs(g, (long)kv.second);
        if (g == 0) {
            if (d.constant != 0) { gcd_proves_independent = true; break; }
        } else {
            if (d.constant % g != 0) { gcd_proves_independent = true; break; }
        }
    }
    if (gcd_proves_independent) {
        r.provably_independent = true;
        return r;
    }

    // 按每层 loop 的 IV 在 diffs 里的总系数推方向
    // 思路（简化）：
    //   对第 i 层 loop 的 IV：
    //     遍历每个 diff 维度，看 IV 的系数；
    //     如果所有维度里系数都是 0 → 这层 IV 不影响 → 方向 EQ
    //     如果存在非 0 系数 → ANY（不精确判断符号）
    for (Loop *loop : r.commonLoops) {
        PhiInst *iv = loop->canonicalIV;
        if (!iv) {
            r.direction.push_back(DIR_ANY);
            continue;
        }
        bool ivAppears = false;
        for (auto &d : diffs) {
            if (d.coeffOf(iv) != 0) {
                ivAppears = true;
                break;
            }
        }
        r.direction.push_back(ivAppears ? DIR_ANY : DIR_EQ);
    }

    return r;
}

// ── 循环交换合法性 ─────────────────────────────────────────────────────────
// 检查给定一组访问之间的依赖方向，看 outer/inner 互换后是否反转。
// 简化规则：
//   - 如果某依赖方向在 (outer_idx, inner_idx) 上是 (<, >)，交换后变 (>, <) 反转 → 非法
//   - (=, *)、(*, =) 都合法
//   - 含 ANY 时保守允许（注：真实工业实现会更严格）

bool DependenceAnalysis::isInterchangeLegal(
    Loop *outer, Loop *inner,
    const std::vector<Instruction *> &accesses)
{
    int n = (int)accesses.size();
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            // 至少一个是 store 才有 RAW/WAR/WAW 风险（read-read 无碍）
            bool has_store = accesses[i]->is_store() || accesses[j]->is_store();
            if (!has_store) continue;

            Result r = test(accesses[i], accesses[j]);
            if (r.provably_independent) continue;

            // 找 outer/inner 在 commonLoops 里的位置
            auto it_o = std::find(r.commonLoops.begin(), r.commonLoops.end(), outer);
            auto it_i = std::find(r.commonLoops.begin(), r.commonLoops.end(), inner);
            if (it_o == r.commonLoops.end() || it_i == r.commonLoops.end()) continue;
            int idx_o = it_o - r.commonLoops.begin();
            int idx_i = it_i - r.commonLoops.begin();
            if (idx_o >= (int)r.direction.size() || idx_i >= (int)r.direction.size())
                continue;

            Dir d_o = r.direction[idx_o];
            Dir d_i = r.direction[idx_i];

            // 反转条件：原本 outer < inner > → 交换后变 outer > inner < 反转
            // 但我们的方向只到 ANY 粒度，没法判 < 还是 >，所以：
            //   - 若两者都是 EQ → 安全
            //   - 否则保守判定：若 outer 不是 EQ（说明有依赖跨外层）且 inner 不是 EQ → 不安全
            if (d_o != DIR_EQ && d_i != DIR_EQ) {
                return false;
            }
        }
    }
    return true;
}
