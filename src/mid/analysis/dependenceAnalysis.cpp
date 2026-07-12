#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>

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
    // 过程间参数别名 oracle：可证明不别名则不别名（含参数 vs global/参数）
    if (argAA_ && argAA_->noAlias(a, b))
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

// ── Banerjee 不等式测试 ──────────────────────────────────────────────────
// Banerjee 测试通过计算仿射差表达式在 IV 界约束下的 min/max，
// 实现更精确的依赖方向和独立性判断。
//   - 独立性：全范围 lo>0 或 hi<0 → 无解 → 独立（GCD 失败时补充）
//   - 方向细化：约束 Δ_k≤-1 / Δ_k=0 / Δ_k≥1，判断各方向是否可行

static long long getConstantTripCount(Loop *loop) {
    auto *ci = dynamic_cast<ConstantInt *>(loop->tripCount);
    return ci ? (long long)ci->value_ : -1;
}

static bool allLoopsHaveConstantTrip(const std::vector<Loop *> &loops) {
    for (auto *loop : loops)
        if (getConstantTripCount(loop) < 0) return false;
    return true;
}

struct BanerjeeRange { long long lo; long long hi; bool valid; };

// 核心：计算 diff = c_const + Σ c_i·Δ_i 在给定 Δ 范围下的 min/max
// loops: 共同嵌套循环（最外层在前），与 diff 中的 IV 对应
// constrainedIdx: -1=全范围，k=约束 loops[k] 的 Δ：
//   dirSign<0 → Δ∈[-(N-1),-1]  (方向 '<')
//   dirSign=0 → Δ=0            (方向 '=')
//   dirSign>0 → Δ∈[1,N-1]      (方向 '>')
static BanerjeeRange computeBanerjee(const AffineExpr         &diff,
                                     const std::vector<Loop *> &loops,
                                     int constrainedIdx, int dirSign) {
    long long loVal = diff.constant;
    long long hiVal = diff.constant;

    for (size_t i = 0; i < loops.size(); i++) {
        long long N = getConstantTripCount(loops[i]);
        if (N < 0) return {0, 0, false};
        if (N <= 1 && (int)i == constrainedIdx && dirSign != 0)
            return {0, 0, false};

        PhiInst   *iv = loops[i]->canonicalIV;
        long long  c  = iv ? (long long)diff.coeffOf(iv) : 0;

        long long dLo, dHi;
        if ((int)i == constrainedIdx) {
            if      (dirSign < 0) { dLo = -(N - 1); dHi = -1;    }
            else if (dirSign > 0) { dLo = 1;        dHi = N - 1; }
            else                  { dLo = 0;        dHi = 0;     }
        } else {
            dLo = -(N - 1);
            dHi = N - 1;
        }
        if (dLo > dHi) return {0, 0, false};

        if (c >= 0) { loVal += c * dLo; hiVal += c * dHi; }
        else        { loVal += c * dHi; hiVal += c * dLo; }
    }
    return {loVal, hiVal, true};
}

// 全范围 Banerjee 独立性测试（每维独立，任一无解即独立）
static bool banerjeeIndependent(const AffineExpr         &diff,
                                const std::vector<Loop *> &loops) {
    auto r = computeBanerjee(diff, loops, -1, 0);
    return r.valid && (r.lo > 0 || r.hi < 0);
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

    // 逐维提取仿射差异；某维无法仿射化时标记为"不透明"而【不】污染其它维。
    // 关键：一个维度不透明，只说明该维地址不可解析；它不会凭空给别的循环层
    // 引入依赖。某层 IV 的方向只取决于它是否真的进入下标（仿射维看系数、
    // 不透明维看 use-def 可达性）。
    struct Dim {
        bool       affine = false;
        AffineExpr diff;            // affine=true 时有效
        Value     *v1 = nullptr;    // affine=false 时保留两侧原始下标
        Value     *v2 = nullptr;
    };
    std::vector<Dim> dims;
    dims.reserve(n_idx);
    for (unsigned k = 0; k < n_idx; k++) {
        Value *o1 = g1->get_operand(k + 1);
        Value *o2 = g2->get_operand(k + 1);
        AffineExpr a = AA_->analyze(o1);
        AffineExpr b = AA_->analyze(o2);
        Dim d;
        if (a.valid && b.valid) { d.affine = true;  d.diff = a - b; }
        else                    { d.affine = false; d.v1 = o1; d.v2 = o2; }
        dims.push_back(d);
    }

    // GCD test：只能用【可仿射维】证明无解（不透明维无法贡献独立性）。
    //   某可仿射维 gcd(c_iv) 不整除 c_const → 该维永不重合 → 整对独立。
    bool gcd_proves_independent = false;
    for (auto &d : dims) {
        if (!d.affine) continue;
        long g = 0;
        for (auto &kv : d.diff.coeffs) g = gcdAbs(g, (long)kv.second);
        if (g == 0) {
            if (d.diff.constant != 0) { gcd_proves_independent = true; break; }
        } else {
            if (d.diff.constant % g != 0) { gcd_proves_independent = true; break; }
        }
    }
    if (gcd_proves_independent) {
        r.provably_independent = true;
        return r;
    }

    // ── Banerjee 增强 ──────────────────────────────────────────────────────
    // GCD 未能证明独立时，尝试 Banerjee 测试
    // 前提：所有维度仿射化且所有循环有常量 trip count
    bool allCanBanerjee = allLoopsHaveConstantTrip(r.commonLoops);
    for (auto &d : dims) { if (!d.affine) allCanBanerjee = false; }
    if (allCanBanerjee) {
        // Banerjee 独立性测试（逐维，任一无解即独立）
        bool banerjee_independent = false;
        for (auto &d : dims) {
            if (banerjeeIndependent(d.diff, r.commonLoops)) {
                banerjee_independent = true;
                break;
            }
        }
        if (banerjee_independent) {
            r.provably_independent = true;
            return r;
        }
        // Banerjee 方向细化：逐层测试 = / < / >，逐维取交集
        for (size_t k = 0; k < r.commonLoops.size(); k++) {
            bool eqOk = true, ltOk = true, gtOk = true;
            for (auto &d : dims) {
                auto rEq = computeBanerjee(d.diff, r.commonLoops, (int)k, 0);
                auto rLt = computeBanerjee(d.diff, r.commonLoops, (int)k, -1);
                auto rGt = computeBanerjee(d.diff, r.commonLoops, (int)k, 1);
                if (!rEq.valid || rEq.lo > 0 || rEq.hi < 0) eqOk = false;
                if (!rLt.valid || rLt.lo > 0 || rLt.hi < 0) ltOk = false;
                if (!rGt.valid || rGt.lo > 0 || rGt.hi < 0) gtOk = false;
            }
            Dir dir;
            if (ltOk && gtOk)                              dir = DIR_ANY;
            else if (ltOk && !gtOk)                        dir = DIR_LT;
            else if (gtOk && !ltOk)                        dir = DIR_GT;
            else if (eqOk && !ltOk && !gtOk)              dir = DIR_EQ;
            else                                           dir = DIR_ANY;
            r.direction.push_back(dir);
        }
        return r;
    }

    // ── 回退：粗粒度方向（IV 出现在下标 → ANY，否则 EQ） ─────────────────
    for (Loop *loop : r.commonLoops) {
        PhiInst *iv = loop->canonicalIV;
        if (!iv) {
            r.direction.push_back(DIR_ANY);
            continue;
        }
        bool ivAppears = false;
        for (auto &d : dims) {
            if (d.affine) {
                if (d.diff.coeffOf(iv) != 0) { ivAppears = true; break; }
            } else {
                if (!AffineAnalysis::provablyIndependentOfIV(d.v1, iv) ||
                    !AffineAnalysis::provablyIndependentOfIV(d.v2, iv)) { ivAppears = true; break; }
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

            // Banerjee 提供精确 < / = / > 时，仅 (<,>) (>,<) 反转非法。
            // 含 DIR_ANY 时回退到旧保守规则：
            //   仅当另一方为 EQ 时才安全（=,* 或 *,=），否则未知方向可能反转为非法。
            if (d_o == DIR_ANY || d_i == DIR_ANY) {
                if (d_o != DIR_EQ && d_i != DIR_EQ)
                    return false;
                continue;
            }
            bool lt_gt = (d_o == DIR_LT && d_i == DIR_GT) ||
                         (d_o == DIR_GT && d_i == DIR_LT);
            if (lt_gt) return false;
        }
    }
    return true;
}

// ── 单层并行性 ─────────────────────────────────────────────────────────────
// L 是否携带依赖（不同 L 迭代之间存在内存依赖）。
//
// 对每对可能别名、含 store 的访问 (a1,a2)：它们【只在相同 L 迭代】碰撞才不携带
// 依赖。判据（kIV = L 的规范 IV）：
//   - 逐维取仿射差 diff = idx1 - idx2；某维 GCD 不整除 → 整对独立，跳过。
//   - 若 kIV 出现在某维 diff（系数≠0）：两访问对 k 的依赖【不同】，不同 k 迭代
//     可能碰撞 → 携带。
//   - 否则 kIV 在 diff 中抵消：
//       · 若 kIV 确实出现在 a1 的下标里（两访问 k-依赖相同）→ 仅相同 k 碰撞 →
//         不携带（loop-independent）。
//       · 若 kIV 根本不在 a1 下标里（地址对 k 不变）→ 每个 k 迭代写同一地址 →
//         携带（loop-invariant 碰撞，如 reduction 写回 / 计时循环里的定址写）。
//   - 无法仿射化的维：用 use-def 保守判断 kIV 是否可能进入下标。
bool DependenceAnalysis::loopCarriesDependence(
    Loop *L, const std::vector<Instruction *> &accesses)
{
    PhiInst *kIV = L->canonicalIV;
    if (!kIV) return true;

    int n = (int)accesses.size();
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            bool has_store = accesses[i]->is_store() || accesses[j]->is_store();
            if (!has_store) continue;   // read-read 无依赖

            auto *g1 = accessGEP(accesses[i]);
            auto *g2 = accessGEP(accesses[j]);
            if (!g1 || !g2) return true;                 // 访问无法解析 → 保守
            if (!sameBase(gepBase(g1), gepBase(g2))) continue;   // 不别名
            if (g1->num_ops_ != g2->num_ops_) return true;

            unsigned n_idx = g1->num_ops_ - 1;
            bool kInDiff = false, kInA1 = false, indep = false;
            for (unsigned d = 0; d < n_idx && !kInDiff; d++) {
                Value *o1 = g1->get_operand(d + 1);
                Value *o2 = g2->get_operand(d + 1);
                AffineExpr e1 = AA_->analyze(o1);
                AffineExpr e2 = AA_->analyze(o2);
                if (e1.valid && e2.valid) {
                    AffineExpr diff = e1 - e2;
                    long g = 0;
                    for (auto &kv : diff.coeffs) g = gcdAbs(g, (long)kv.second);
                    if ((g == 0 && diff.constant != 0) ||
                        (g != 0 && diff.constant % g != 0))
                        indep = true;
                    if (diff.coeffOf(kIV) != 0) kInDiff = true;
                    if (e1.coeffOf(kIV) != 0)   kInA1   = true;
                } else {
                    // 不透明维：kIV 若可能进入任一侧 → 视作 diff 含 kIV（保守）
                    if (!AffineAnalysis::provablyIndependentOfIV(o1, kIV) ||
                        !AffineAnalysis::provablyIndependentOfIV(o2, kIV))
                        kInDiff = true;
                    if (!AffineAnalysis::provablyIndependentOfIV(o1, kIV))
                        kInA1 = true;
                }
            }

            if (indep)    continue;        // 某维永不重合 → 独立
            if (kInDiff)  return true;      // k-依赖不同 → 跨迭代碰撞
            if (kInA1)    continue;         // k-依赖相同 → 仅同迭代碰撞
            return true;                    // 地址对 k 不变 → 每迭代碰撞，携带
        }
    }
    return false;
}
