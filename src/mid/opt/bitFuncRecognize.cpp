// BitFuncRecognize — bit-level abstract interpretation for recognizing
// bitwise-op emulations. See header and docs/BITOP_LOWERING.md.
//
// ── Sections ──────────────────────────────────────────────────────────
//   §A  Abstract domain (BitExpr, hash-consing)
//   §B  Smart constructors with simplification
//   §C  BitVec (32-bit abstract value)
//   §D  Transfer functions per IR op
//   §E  Function-body analyzer
//   §F  Closed-form recognizer
//   §G  Module-level driver

#include "../../include/mid/opt/bitFuncRecognize.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef BITFUNC_DEBUG
#define BITFUNC_DEBUG 0
#endif

namespace bitfunc {

// ══════════════════════════════════════════════════════════════════════
// §A  Abstract domain
// ══════════════════════════════════════════════════════════════════════

enum class BitOp : uint8_t {
    ZERO, ONE, BIT_OF, AND, OR, XOR, NOT, TOP
};

class BitExpr;
using BE = const BitExpr *;

class BitExpr {
public:
    BitOp op;
    Value *source; unsigned idx;
    BE a, b;
    BitExpr(BitOp o, Value *s = nullptr, unsigned i = 0, BE x = nullptr, BE y = nullptr)
        : op(o), source(s), idx(i), a(x), b(y) {}
};

struct BEKey {
    BitOp op; Value *src; unsigned idx; BE a, b;
    bool operator==(const BEKey &o) const {
        return op == o.op && src == o.src && idx == o.idx && a == o.a && b == o.b;
    }
};
struct BEHash {
    size_t operator()(const BEKey &k) const {
        size_t h = (size_t)k.op;
        h = h * 1315423911u + (size_t)k.src;
        h = h * 1315423911u + (size_t)k.idx;
        h = h * 1315423911u + (size_t)k.a;
        h = h * 1315423911u + (size_t)k.b;
        return h;
    }
};

static std::unordered_map<BEKey, std::unique_ptr<BitExpr>, BEHash> g_cache;

static BE intern(BitOp op, Value *src = nullptr, unsigned idx = 0,
                 BE a = nullptr, BE b = nullptr) {
    BEKey key{op, src, idx, a, b};
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.get();
    auto owned = std::make_unique<BitExpr>(op, src, idx, a, b);
    BE result = owned.get();
    g_cache.emplace(key, std::move(owned));
    return result;
}

static void clearCache() { g_cache.clear(); }
static BE Zero()              { return intern(BitOp::ZERO); }
static BE One()               { return intern(BitOp::ONE); }
static BE Top()               { return intern(BitOp::TOP); }
static BE BitOf(Value *v, unsigned i) { return intern(BitOp::BIT_OF, v, i); }

// ══════════════════════════════════════════════════════════════════════
// §B  Smart constructors with simplification
// ══════════════════════════════════════════════════════════════════════

static BE Not_(BE a);

static BE And_(BE a, BE b) {
    if (a == Zero() || b == Zero()) return Zero();
    if (a == One())  return b;
    if (b == One())  return a;
    if (a == Top() || b == Top()) return Top();
    if (a == b) return a;
    if (a->op == BitOp::NOT && a->a == b) return Zero();
    if (b->op == BitOp::NOT && b->a == a) return Zero();
    // Absorption: a & (a | x) = a;  a & (x | a) = a.
    if (b->op == BitOp::OR && (b->a == a || b->b == a)) return a;
    if (a->op == BitOp::OR && (a->a == b || a->b == b)) return b;
    if (a > b) std::swap(a, b);
    return intern(BitOp::AND, nullptr, 0, a, b);
}

// Test whether `e` is structurally equal to (a & x) or (x & a) for some x,
// and if so, return x.  Returns nullptr when no match.
static BE matchAndWith(BE e, BE a) {
    if (!e || e->op != BitOp::AND) return nullptr;
    if (e->a == a) return e->b;
    if (e->b == a) return e->a;
    return nullptr;
}

static BE Or_(BE a, BE b) {
    if (a == Zero()) return b;
    if (b == Zero()) return a;
    if (a == One() || b == One()) return One();
    if (a == Top() || b == Top()) return Top();
    if (a == b) return a;
    if (a->op == BitOp::NOT && a->a == b) return One();
    if (b->op == BitOp::NOT && b->a == a) return One();
    // Absorption: a | (a & x) = a;  a | (x & a) = a.
    if (matchAndWith(b, a)) return a;
    if (matchAndWith(a, b)) return b;
    // Absorption with negation: a | (~a & x) = a | x;  a | (x & ~a) = a | x.
    if (a->op != BitOp::NOT) {
        BE notA = intern(BitOp::NOT, nullptr, 0, a);  // probe (no allocation if cached)
        if (BE x = matchAndWith(b, notA)) return Or_(a, x);
        if (BE x = matchAndWith(a, notA)) return Or_(b, x);
    }
    // (~a | x) absorbs (a & y) when x == y: (~a | x) | (a & x) = ~a | x | (a&x) = ~a | x
    // Skipped — these patterns are rare in our domain.
    if (a > b) std::swap(a, b);
    return intern(BitOp::OR, nullptr, 0, a, b);
}

static BE Xor_(BE a, BE b) {
    if (a == Zero()) return b;
    if (b == Zero()) return a;
    if (a == Top() || b == Top()) return Top();
    if (a == b) return Zero();
    if (a == One()) return Not_(b);
    if (b == One()) return Not_(a);
    if (a > b) std::swap(a, b);
    return intern(BitOp::XOR, nullptr, 0, a, b);
}

static BE Not_(BE a) {
    if (a == Zero()) return One();
    if (a == One())  return Zero();
    if (a == Top())  return Top();
    if (a->op == BitOp::NOT) return a->a;
    return intern(BitOp::NOT, nullptr, 0, a);
}

static BE Select_(BE c, BE t, BE f) {
    if (c == Zero()) return f;
    if (c == One())  return t;
    if (t == f)      return t;
    if (t == One()  && f == Zero()) return c;
    if (t == Zero() && f == One())  return Not_(c);
    return Or_(And_(c, t), And_(Not_(c), f));
}

// ══════════════════════════════════════════════════════════════════════
// §C  BitVec
// ══════════════════════════════════════════════════════════════════════

using BitVec = std::array<BE, 32>;

static BitVec makeConst(int v) {
    BitVec bv;
    for (unsigned i = 0; i < 32; i++)
        bv[i] = ((uint32_t)v & (1u << i)) ? One() : Zero();
    return bv;
}

static BitVec makeSymbolic(Value *src) {
    BitVec bv;
    for (unsigned i = 0; i < 32; i++) bv[i] = BitOf(src, i);
    return bv;
}

static bool isConcrete(const BitVec &bv, int *out = nullptr) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 32; i++) {
        if      (bv[i] == One())  v |= (1u << i);
        else if (bv[i] != Zero()) return false;
    }
    if (out) *out = (int)v;
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// §D  Transfer functions
// ══════════════════════════════════════════════════════════════════════

static BitVec bvAnd(const BitVec &x, const BitVec &y) {
    BitVec r; for (unsigned i = 0; i < 32; i++) r[i] = And_(x[i], y[i]); return r;
}
static BitVec bvOr(const BitVec &x, const BitVec &y) {
    BitVec r; for (unsigned i = 0; i < 32; i++) r[i] = Or_(x[i], y[i]); return r;
}
static BitVec bvXor(const BitVec &x, const BitVec &y) {
    BitVec r; for (unsigned i = 0; i < 32; i++) r[i] = Xor_(x[i], y[i]); return r;
}

static BitVec bvShl(const BitVec &x, int k) {
    BitVec r;
    for (unsigned i = 0; i < 32; i++) {
        int from = (int)i - k;
        r[i] = (from >= 0 && from < 32) ? x[from] : Zero();
    }
    return r;
}

static BitVec bvLshr(const BitVec &x, int k) {
    BitVec r;
    for (unsigned i = 0; i < 32; i++) {
        int from = (int)i + k;
        r[i] = (from >= 0 && from < 32) ? x[from] : Zero();
    }
    return r;
}

static bool bvMulByConst(const BitVec &x, int c, BitVec &out) {
    if (c <= 0 || (c & (c - 1)) != 0) return false;
    int k = 0; while ((1 << k) < c) ++k;
    out = bvShl(x, k); return true;
}

static bool bvSdivByConstAsLshr(const BitVec &x, int c, BitVec &out) {
    if (c <= 0 || (c & (c - 1)) != 0) return false;
    int k = 0; while ((1 << k) < c) ++k;
    out = bvLshr(x, k); return true;
}

static BitVec bvSremByTwo(const BitVec &x) {
    BitVec r; r[0] = x[0];
    for (unsigned i = 1; i < 32; i++) r[i] = Zero();
    return r;
}

// Fast path: x and y have no overlapping non-Zero bits → result = OR (no carry).
static bool bvAddNoCarry(const BitVec &x, const BitVec &y, BitVec &out) {
    for (unsigned i = 0; i < 32; i++)
        if (x[i] != Zero() && y[i] != Zero()) return false;
    out = bvOr(x, y);
    return true;
}

// General full-adder add: sum[i] = x[i] ^ y[i] ^ carry[i]
//                         carry[i+1] = (x[i] & y[i]) | (carry[i] & (x[i] ^ y[i]))
// Returns false if any bit becomes TOP (analysis diverged).
static bool bvAddFull(const BitVec &x, const BitVec &y, BitVec &out) {
    BE carry = Zero();
    for (unsigned i = 0; i < 32; i++) {
        BE xy_xor = Xor_(x[i], y[i]);
        out[i] = Xor_(xy_xor, carry);
        if (out[i] == Top()) return false;
        if (i == 31) break;  // discard final carry (32-bit wrap)
        BE xy_and = And_(x[i], y[i]);
        BE c_x    = And_(carry, x[i]);
        BE c_y    = And_(carry, y[i]);
        // Equivalent simplified form: maj(x,y,c) = (x&y) | (x&c) | (y&c)
        carry = Or_(xy_and, Or_(c_x, c_y));
        if (carry == Top()) return false;
    }
    return true;
}

static bool bvAdd(const BitVec &x, const BitVec &y, BitVec &out) {
    if (bvAddNoCarry(x, y, out)) return true;
    return bvAddFull(x, y, out);
}

// Two's complement negation: -x = ~x + 1
static bool bvNeg(const BitVec &x, BitVec &out) {
    BitVec nx;
    for (unsigned i = 0; i < 32; i++) nx[i] = Not_(x[i]);
    BitVec one = makeConst(1);
    return bvAdd(nx, one, out);
}

static bool bvSub(const BitVec &x, const BitVec &y, BitVec &out) {
    BitVec ny;
    if (!bvNeg(y, ny)) return false;
    return bvAdd(x, ny, out);
}

// AND with a constant mask. Used by Phase-3 recognizer.
static BitVec bvAndMask(const BitVec &x, uint32_t mask) {
    BitVec r;
    for (unsigned i = 0; i < 32; i++)
        r[i] = (mask & (1u << i)) ? x[i] : Zero();
    return r;
}

// ══════════════════════════════════════════════════════════════════════
// §E  Function-body analyzer
// ══════════════════════════════════════════════════════════════════════
//
// Control-flow model (independent of CFGSimplify):
//
//   analyzeRegion(start, stop, prev, state) walks straight-line from start
//   until either (a) reaching `stop` (without executing it), or (b) executing
//   a return.  At a symbolic conditional branch it forks state two ways,
//   recursively analyzes both arms up to the immediate post-dominator (IPDOM),
//   then merges using Select_ at the IPDOM's phis.  All four corners of the
//   fork–join lattice (both-return / both-merge / mixed) are handled, so
//   constructs like rotlN's early-return chain are tractable without any
//   prior diamond→select rewrite.

struct ValueState {
    BitVec bv        = {};
    bool   knownConcrete = false;
    int    concreteVal   = 0;
    bool   isI1          = false;
    bool   i1Val         = false;
};

static ValueState vsConst(int v) {
    ValueState s; s.bv = makeConst(v); s.knownConcrete = true; s.concreteVal = v; return s;
}
static ValueState vsSymbolic(Value *v) {
    ValueState s; s.bv = makeSymbolic(v); return s;
}
static ValueState vsI1(bool b) {
    ValueState s; s.isI1 = true; s.knownConcrete = true; s.i1Val = b;
    s.concreteVal = b ? 1 : 0; s.bv = makeConst(b ? 1 : 0); return s;
}

using State = std::unordered_map<Value *, ValueState>;

// BFS-reachable set in CFG starting from `start`, never traversing through any
// of `avoid`.  `start` itself is included in `out` even if it appears in
// `avoid` (the start block is always reachable from itself).
static void bfsReachable(BasicBlock *start,
                         std::initializer_list<BasicBlock *> avoid,
                         std::unordered_set<BasicBlock *> &out) {
    if (!start) return;
    std::unordered_set<BasicBlock *> avoidSet(avoid.begin(), avoid.end());
    std::vector<BasicBlock *> stk{start};
    out.insert(start);
    while (!stk.empty()) {
        auto *b = stk.back(); stk.pop_back();
        for (auto *s : b->succ_bbs_) {
            if (avoidSet.count(s)) continue;
            if (out.insert(s).second) stk.push_back(s);
        }
    }
}

// Iterative post-dominator dataflow.  For each BB, computes the set of blocks
// that post-dominate it (including itself).  Standard formulation:
//   PD[B] = {B} ∪ (∩ PD[S] for S ∈ succ(B)),  PD[exit] = {exit}.
// Blocks with no successors (return blocks) are seeds.  Blocks that cannot
// reach any return (infinite loops) get PD[B] = {B} as a convention so the
// algorithm terminates; this never affects IPDOM queries on such blocks.
static std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>>
computePostDom(Function *f) {
    std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>> pd;
    std::unordered_set<BasicBlock *> all(f->basic_blocks_.begin(), f->basic_blocks_.end());
    for (auto *b : f->basic_blocks_) {
        if (b->succ_bbs_.empty()) pd[b] = {b};
        else                      pd[b] = all;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *b : f->basic_blocks_) {
            if (b->succ_bbs_.empty()) continue;
            std::unordered_set<BasicBlock *> next;
            bool first = true;
            for (auto *s : b->succ_bbs_) {
                if (first) { next = pd[s]; first = false; }
                else {
                    std::unordered_set<BasicBlock *> isect;
                    for (auto *x : next) if (pd[s].count(x)) isect.insert(x);
                    next = std::move(isect);
                }
            }
            next.insert(b);
            if (next != pd[b]) { pd[b] = std::move(next); changed = true; }
        }
    }
    return pd;
}

// Pick the immediate post-dominator of `b` from its post-dom set:
// the unique block whose own PD set is `pd[b] \ {b}`.
static BasicBlock *immediatePostDom(BasicBlock *b,
        const std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>> &pd) {
    auto it = pd.find(b);
    if (it == pd.end()) return nullptr;
    size_t target = it->second.size() - 1;
    for (auto *cand : it->second) {
        if (cand == b) continue;
        auto cit = pd.find(cand);
        if (cit != pd.end() && cit->second.size() == target) return cand;
    }
    return nullptr;
}

struct RegionResult {
    bool         returned = false;   // true → returnBv valid; false → exitBB/exitPrev valid
    BitVec       returnBv;
    BasicBlock  *exitBB   = nullptr; // block at which analysis stopped (== stop arg)
    BasicBlock  *exitPrev = nullptr; // last block on the path leading into exitBB
};

class FunctionAnalyzer {
public:
    Function   *func;
    int         budget      = 60000;
    int         blockVisits = 0;
    const int   MAX_VISITS  = 4000;
    BitVec      resultBv;
    bool        ok          = false;
    const char *failReason  = nullptr;

    std::unordered_map<BasicBlock *, BasicBlock *> ipdomCache;

    // Optional pin: force a specific Value to a constant int at the start of
    // analysis (used by the parametric-trip-count fallback to coerce a
    // symbolic loop bound to 32 so the standard implicit unrolling can run).
    std::unordered_map<Value *, int> pins;

    explicit FunctionAnalyzer(Function *f) : func(f) {}
    bool run();

    BasicBlock *ipdomOf(BasicBlock *bb) {
        auto it = ipdomCache.find(bb);
        return it == ipdomCache.end() ? nullptr : it->second;
    }

private:
    bool resolve(Value *v, ValueState &out, const State &state) {
        if (auto *ci = dynamic_cast<ConstantInt *>(v)) { out = vsConst(ci->value_); return true; }
        auto it = state.find(v);
        if (it != state.end()) { out = it->second; return true; }
        return false;
    }

    bool processPhi(PhiInst *phi, BasicBlock *prev, State &state) {
        // prev == nullptr happens only when entering a BB from a synthetic
        // merge produced by mergeAtIpdom — that path pre-resolves all phis at
        // the merge target's head and leaves them in `state`.  Don't overwrite.
        if (!prev) {
            if (state.count(phi)) return true;
            failReason = "phi: no prev"; return false;
        }
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == prev) {
                ValueState s;
                if (!resolve(phi->get_operand(i), s, state)) { failReason = "phi: unres val"; return false; }
                state[phi] = s;
                return true;
            }
        }
        failReason = "phi: pred not found";
        return false;
    }

    bool processBinop(BinaryInst *bi, State &state) {
        ValueState a, b;
        if (!resolve(bi->get_operand(0), a, state)) { failReason = "binop: unres a"; return false; }
        if (!resolve(bi->get_operand(1), b, state)) { failReason = "binop: unres b"; return false; }
        Instruction::OpID op = bi->op_id_;

        if (a.knownConcrete && b.knownConcrete) {
            int av = a.concreteVal, bv = b.concreteVal, rv = 0;
            switch (op) {
                case Instruction::Add:  rv = av + bv; break;
                case Instruction::Sub:  rv = av - bv; break;
                case Instruction::Mul:  rv = av * bv; break;
                case Instruction::SDiv: if (!bv) { failReason = "div0"; return false; } rv = av / bv; break;
                case Instruction::SRem: if (!bv) { failReason = "rem0"; return false; } rv = av % bv; break;
                case Instruction::And:  rv = av & bv; break;
                case Instruction::Or:   rv = av | bv; break;
                case Instruction::Xor:  rv = av ^ bv; break;
                case Instruction::Shl:  rv = (int)((uint32_t)av << (bv & 31)); break;
                case Instruction::LShr: rv = (int)((uint32_t)av >> (bv & 31)); break;
                default: failReason = "binop: unsupported concrete op"; return false;
            }
            state[bi] = vsConst(rv); return true;
        }

        BitVec r;
        switch (op) {
            case Instruction::And: r = bvAnd(a.bv, b.bv); break;
            case Instruction::Or:  r = bvOr(a.bv, b.bv);  break;
            case Instruction::Xor: r = bvXor(a.bv, b.bv); break;
            case Instruction::Mul:
                if      (b.knownConcrete) { if (!bvMulByConst(a.bv, b.concreteVal, r)) { failReason = "mul: not pow2"; return false; } }
                else if (a.knownConcrete) { if (!bvMulByConst(b.bv, a.concreteVal, r)) { failReason = "mul: not pow2"; return false; } }
                else { failReason = "mul: both sym"; return false; }
                break;
            case Instruction::SDiv:
                if (!b.knownConcrete || !bvSdivByConstAsLshr(a.bv, b.concreteVal, r))
                    { failReason = "sdiv: not pow2 const"; return false; }
                break;
            case Instruction::SRem:
                if (!b.knownConcrete || b.concreteVal != 2) { failReason = "srem: not by 2"; return false; }
                r = bvSremByTwo(a.bv); break;
            case Instruction::Shl:
                if (!b.knownConcrete) { failReason = "shl: sym amt"; return false; }
                r = bvShl(a.bv, b.concreteVal & 31); break;
            case Instruction::LShr:
                if (!b.knownConcrete) { failReason = "lshr: sym amt"; return false; }
                r = bvLshr(a.bv, b.concreteVal & 31); break;
            case Instruction::Add:
                if (!bvAdd(a.bv, b.bv, r)) { failReason = "add: top"; return false; }
                break;
            case Instruction::Sub:
                if (!bvSub(a.bv, b.bv, r)) { failReason = "sub: top"; return false; }
                break;
            default: failReason = "binop: unsupported sym op"; return false;
        }
        ValueState out; out.bv = r;
        out.knownConcrete = isConcrete(r, &out.concreteVal);
        state[bi] = out;
        return true;
    }

    bool processICmp(ICmpInst *ci, State &state) {
        ValueState a, b;
        if (!resolve(ci->get_operand(0), a, state)) { failReason = "icmp: unres a"; return false; }
        if (!resolve(ci->get_operand(1), b, state)) { failReason = "icmp: unres b"; return false; }

        if (a.knownConcrete && b.knownConcrete) {
            int av = a.concreteVal, bv = b.concreteVal; bool r = false;
            switch (ci->icmp_op_) {
                case ICmpInst::ICMP_EQ:  r = (av == bv); break;
                case ICmpInst::ICMP_NE:  r = (av != bv); break;
                case ICmpInst::ICMP_SGT: r = (av >  bv); break;
                case ICmpInst::ICMP_SGE: r = (av >= bv); break;
                case ICmpInst::ICMP_SLT: r = (av <  bv); break;
                case ICmpInst::ICMP_SLE: r = (av <= bv); break;
                default: failReason = "icmp: unsupported"; return false;
            }
            state[ci] = vsI1(r); return true;
        }

        // Symbolic: only EQ and NE are tractable
        if (ci->icmp_op_ != ICmpInst::ICMP_EQ && ci->icmp_op_ != ICmpInst::ICMP_NE)
            { failReason = "icmp: sym non-eq"; return false; }
        bool wantEq = (ci->icmp_op_ == ICmpInst::ICMP_EQ);
        BE acc = wantEq ? One() : Zero();
        for (unsigned i = 0; i < 32; i++) {
            BE diff = Xor_(a.bv[i], b.bv[i]);
            if (wantEq) acc = And_(acc, Not_(diff));
            else        acc = Or_(acc, diff);
            if (acc == Top()) { failReason = "icmp: top"; return false; }
        }
        ValueState out; out.isI1 = true;
        out.bv = makeConst(0); out.bv[0] = acc;
        if      (acc == Zero()) { out.knownConcrete = true; out.i1Val = false; out.concreteVal = 0; }
        else if (acc == One())  { out.knownConcrete = true; out.i1Val = true;  out.concreteVal = 1; }
        state[ci] = out;
        return true;
    }

    bool processSelect(SelectInst *si, State &state) {
        ValueState c, t, f;
        if (!resolve(si->get_operand(0), c, state)) { failReason = "select: unres cond"; return false; }
        if (!resolve(si->get_operand(1), t, state)) { failReason = "select: unres true"; return false; }
        if (!resolve(si->get_operand(2), f, state)) { failReason = "select: unres false"; return false; }
        if (c.knownConcrete) { state[si] = c.i1Val ? t : f; return true; }
        BE cb = c.bv[0];
        BitVec r;
        for (unsigned i = 0; i < 32; i++) r[i] = Select_(cb, t.bv[i], f.bv[i]);
        ValueState out; out.bv = r;
        out.knownConcrete = isConcrete(r, &out.concreteVal);
        state[si] = out;
        return true;
    }

    // Execute non-terminator instructions of bb (entered from prev).  Returns:
    //   'B' → reached terminator (branch); caller inspects it
    //   'R' → reached return; returnBvOut filled with the return value's BV
    //   'X' → analysis failed
    char execBlock(BasicBlock *bb, BasicBlock *prev, State &state, BitVec &returnBvOut) {
        if (BITFUNC_DEBUG > 1) fprintf(stderr, "[bitfunc] >> exec %s (prev=%s) visits=%d budget=%d\n",
                                        bb->name_.c_str(), prev ? prev->name_.c_str() : "<merge>",
                                        blockVisits, budget);
        for (auto *inst : bb->instr_list_) {
            if (--budget <= 0) {
                if (BITFUNC_DEBUG) fprintf(stderr, "[bitfunc] budget exhausted in BB %s, visits=%d, state.size=%zu\n",
                                            bb->name_.c_str(), blockVisits, state.size());
                failReason = "budget"; return 'X';
            }

            if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
                if (!processPhi(phi, prev, state)) return 'X';
                continue;
            }
            if (auto *bi = dynamic_cast<BinaryInst *>(inst)) {
                if (!processBinop(bi, state)) return 'X';
                continue;
            }
            if (auto *icmp = dynamic_cast<ICmpInst *>(inst)) {
                if (!processICmp(icmp, state)) return 'X';
                continue;
            }
            if (auto *si = dynamic_cast<SelectInst *>(inst)) {
                if (!processSelect(si, state)) return 'X';
                continue;
            }
            if (dynamic_cast<BranchInst *>(inst)) return 'B';
            if (auto *ret = dynamic_cast<ReturnInst *>(inst)) {
                if (ret->num_ops_ == 0) { failReason = "void ret"; return 'X'; }
                ValueState r;
                if (!resolve(ret->get_operand(0), r, state)) { failReason = "ret: unres val"; return 'X'; }
                returnBvOut = r.bv;
                return 'R';
            }
            failReason = "unsupported inst";
            return 'X';
        }
        failReason = "no terminator";
        return 'X';
    }

    // Merge phis at the head of `ipdom` coming from two converging arms.
    // Classification of each phi-incoming (val, pred_bb):
    //   * pred_bb on T side iff (tDest == ipdom && pred_bb == branchBB) ∨
    //     (pred_bb ∈ T-interior); similarly for F.
    //   T-interior = blocks reachable from tDest without crossing branchBB,
    //   minus ipdom itself.  This anchors classification to the CFG geometry
    //   rather than to per-path `prev` markers, which become unreliable across
    //   nested forks (an inner merge erases the single-pred chain).
    //
    // The merged value for each phi is Select_(cond, valT, valF).
    bool mergeAtIpdom(BasicBlock *branchBB,
                      BasicBlock *tDest, BasicBlock *fDest,
                      BasicBlock *ipdom, BE cond,
                      const State &sT, const State &sF, State &outState) {
        // Strict interior of each arm: blocks reachable from tDest/fDest
        // without crossing the branch (loop back-edge) or the merge (ipdom).
        // When tDest/fDest *is* the ipdom (empty arm), the interior is empty
        // — skip BFS, otherwise it would walk past ipdom into the loop tail.
        std::unordered_set<BasicBlock *> tInt, fInt;
        bool tDirect = (tDest == ipdom);
        bool fDirect = (fDest == ipdom);
        if (!tDirect) bfsReachable(tDest, {branchBB, ipdom}, tInt);
        if (!fDirect) bfsReachable(fDest, {branchBB, ipdom}, fInt);
        tInt.erase(ipdom);
        fInt.erase(ipdom);

        outState = sT;  // baseline; phis get overwritten with merged values below

        for (auto *inst : ipdom->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) break;  // phis cluster at BB head

            ValueState valT{}, valF{};
            bool foundT = false, foundF = false;

            // Fast path: an inner fork-join whose ipdom == this ipdom may have
            // pre-resolved the phi inside one (or both) of the arm states.
            // Use those values directly — they already account for the inner
            // arm's sub-paths.
            auto tIt = sT.find(phi);
            auto fIt = sF.find(phi);
            if (tIt != sT.end()) { valT = tIt->second; foundT = true; }
            if (fIt != sF.end()) { valF = fIt->second; foundF = true; }

            // Slow path: classify each incoming by which arm produced it.
            for (unsigned i = 0; (i + 1 < phi->num_ops_) && (!foundT || !foundF); i += 2) {
                auto *pb  = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                auto *inc = phi->get_operand(i);
                bool onT = (tDirect && pb == branchBB) || tInt.count(pb);
                bool onF = (fDirect && pb == branchBB) || fInt.count(pb);
                if (onT && !foundT) {
                    if (!resolve(inc, valT, sT)) {
                        if (BITFUNC_DEBUG) fprintf(stderr, "[bitfunc]   merge fail: phi at %s, inc T pb=%s\n",
                                                    ipdom->name_.c_str(), pb->name_.c_str());
                        failReason = "ipdom phi: unres T"; return false;
                    }
                    foundT = true;
                }
                if (onF && !foundF) {
                    if (!resolve(inc, valF, sF)) {
                        if (BITFUNC_DEBUG) fprintf(stderr, "[bitfunc]   merge fail: phi at %s, inc F pb=%s\n",
                                                    ipdom->name_.c_str(), pb->name_.c_str());
                        failReason = "ipdom phi: unres F"; return false;
                    }
                    foundF = true;
                }
            }
            if (!foundT || !foundF) { failReason = "ipdom phi: pred not classified"; return false; }

            BitVec r;
            for (unsigned i = 0; i < 32; i++)
                r[i] = Select_(cond, valT.bv[i], valF.bv[i]);
            ValueState merged; merged.bv = r;
            merged.knownConcrete = isConcrete(r, &merged.concreteVal);
            outState[phi] = merged;
        }

        // Bring in F-only definitions (SSA values defined only in the F arm).
        for (auto &kv : sF)
            if (!outState.count(kv.first)) outState[kv.first] = kv.second;
        return true;
    }

    // Analyze from `start` until either reaching `stop` (without executing it)
    // or hitting a return.  `stop == nullptr` means "go until return".
    // `state` is mutated in place; caller must copy if it needs the original.
    bool analyzeRegion(BasicBlock *start, BasicBlock *stop,
                       BasicBlock *startPrev, State &state,
                       RegionResult &out) {
        BasicBlock *cur  = start;
        BasicBlock *prev = startPrev;

        while (true) {
            if (cur == stop) {
                out.returned = false;
                out.exitBB   = cur;
                out.exitPrev = prev;
                return true;
            }
            if (++blockVisits > MAX_VISITS) { failReason = "too many block visits"; return false; }

            BitVec retBv;
            char rc = execBlock(cur, prev, state, retBv);
            if (rc == 'R') {
                out.returned = true;
                out.returnBv = retBv;
                return true;
            }
            if (rc == 'X') return false;

            auto *br = dynamic_cast<BranchInst *>(cur->get_terminator());
            if (!br) { failReason = "no branch terminator"; return false; }

            if (br->num_ops_ == 1) {
                prev = cur;
                cur  = static_cast<BasicBlock *>(br->get_operand(0));
                continue;
            }

            ValueState cv;
            if (!resolve(br->get_operand(0), cv, state)) { failReason = "cond: unresolvable"; return false; }

            auto *tDest = static_cast<BasicBlock *>(br->get_operand(1));
            auto *fDest = static_cast<BasicBlock *>(br->get_operand(2));

            if (cv.knownConcrete) {
                prev = cur;
                cur  = cv.i1Val ? tDest : fDest;
                continue;
            }

            // Symbolic branch: fork-join with IPDOM merge.
            BasicBlock *ipdom = ipdomOf(cur);
            BE condBit = cv.bv[0];

            State sT = state, sF = state;
            // Clear any pre-existing entries for ipdom's phis from both arm
            // copies.  Otherwise, in a loop, the previous iteration's value
            // for an ipdom phi would survive into this iteration's sT/sF and
            // mergeAtIpdom's fast path would mistake it for a fresh inner-fork
            // resolution — leading to stale per-iteration values overwriting
            // the correct merged result.
            if (ipdom) {
                for (auto *inst : ipdom->instr_list_) {
                    auto *phi = dynamic_cast<PhiInst *>(inst);
                    if (!phi) break;
                    sT.erase(phi);
                    sF.erase(phi);
                }
            }
            RegionResult rT{}, rF{};
            if (!analyzeRegion(tDest, ipdom, cur, sT, rT)) return false;
            if (!analyzeRegion(fDest, ipdom, cur, sF, rF)) return false;

            if (rT.returned && rF.returned) {
                BitVec merged;
                for (unsigned i = 0; i < 32; i++)
                    merged[i] = Select_(condBit, rT.returnBv[i], rF.returnBv[i]);
                out.returned = true;
                out.returnBv = merged;
                return true;
            }
            if (!rT.returned && !rF.returned) {
                if (!ipdom) { failReason = "fork: no merge but neither returned"; return false; }
                State merged;
                if (!mergeAtIpdom(cur, tDest, fDest, ipdom, condBit, sT, sF, merged))
                    return false;
                state = std::move(merged);
                prev  = nullptr;  // entering ipdom from synthetic merge; phis pre-resolved
                cur   = ipdom;
                continue;
            }
            // Asymmetric: one arm returns, the other reached `ipdom` (or fell
            // through to end of function if ipdom == nullptr).  Walk the
            // continuation to its eventual return, then Select between the
            // two return values keyed on `condBit`.
            bool tReturned = rT.returned;
            State &contState = tReturned ? sF : sT;
            RegionResult contExit{};
            // contState is already at the ipdom entry (or at function exit).
            // Continue from there (or, if ipdom != nullptr, the continuation
            // already stopped at ipdom — resume from ipdom to return).
            BasicBlock *contStart    = tReturned ? rF.exitBB    : rT.exitBB;
            BasicBlock *contStartPrv = tReturned ? rF.exitPrev  : rT.exitPrev;
            if (!analyzeRegion(contStart, /*stop*/nullptr, contStartPrv, contState, contExit))
                return false;
            if (!contExit.returned) { failReason = "asym fork: cont did not return"; return false; }
            const BitVec &tBv = tReturned ? rT.returnBv : contExit.returnBv;
            const BitVec &fBv = tReturned ? contExit.returnBv : rF.returnBv;
            BitVec merged;
            for (unsigned i = 0; i < 32; i++)
                merged[i] = Select_(condBit, tBv[i], fBv[i]);
            out.returned = true;
            out.returnBv = merged;
            return true;
        }
    }
};

bool FunctionAnalyzer::run() {
    if (func->basic_blocks_.empty()) { failReason = "no blocks"; return false; }
    // Pre-compute IPDOM for every block.  Cheap O(N^2) iterative dataflow is
    // fine here: candidate functions are small (<200 blocks each, and we
    // bail on the analysis otherwise via budget).
    auto pd = computePostDom(func);
    for (auto *bb : func->basic_blocks_)
        ipdomCache[bb] = immediatePostDom(bb, pd);

    State state;
    for (auto *arg : func->arguments_) {
        if (arg->type_->tid_ != Type::IntegerTyID) { failReason = "non-int arg"; return false; }
        auto pinIt = pins.find(arg);
        state[arg] = (pinIt != pins.end()) ? vsConst(pinIt->second) : vsSymbolic(arg);
    }
    RegionResult res{};
    if (!analyzeRegion(func->basic_blocks_.front(), /*stop*/nullptr, /*prev*/nullptr,
                       state, res))
        return false;
    if (!res.returned) { failReason = "fell out of cfg without return"; return false; }
    resultBv = res.returnBv;
    ok = true;
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// §F  Closed-form recognizer
// ══════════════════════════════════════════════════════════════════════

struct ClosedForm {
    enum Kind { NONE, AND_OP, OR_OP, XOR_OP, LSHR_OP, SHL_OP, COPY_OP };
    Kind    kind        = NONE;
    Value  *x           = nullptr;
    Value  *y           = nullptr;
    int     shiftAmount = 0;
};

static bool matchBitwiseOp(const BitVec &bv, BitOp op, Value *&X, Value *&Y) {
    X = Y = nullptr;
    for (unsigned i = 0; i < 32; i++) {
        BE e = bv[i];
        if (e->op != op) return false;
        BE l = e->a, r = e->b;
        if (l->op != BitOp::BIT_OF || r->op != BitOp::BIT_OF) return false;
        if (l->idx != i || r->idx != i) return false;
        Value *Xc = l->source, *Yc = r->source;
        if (i == 0) { X = Xc; Y = Yc; }
        else if (!((Xc == X && Yc == Y) || (Xc == Y && Yc == X))) return false;
    }
    return true;
}

static bool matchShift(const BitVec &bv, Value *&X, int &k) {
    X = nullptr; k = 0; bool found = false;
    for (unsigned i = 0; i < 32; i++) {
        BE e = bv[i];
        if (e == Zero()) continue;
        if (e->op != BitOp::BIT_OF) return false;
        int delta = (int)e->idx - (int)i;
        if (!found) { X = e->source; k = delta; found = true; }
        else if (e->source != X || delta != k) return false;
    }
    return found;
}

static bool matchCopy(const BitVec &bv, Value *&X) {
    X = nullptr;
    for (unsigned i = 0; i < 32; i++) {
        BE e = bv[i];
        if (e->op != BitOp::BIT_OF || e->idx != i) return false;
        if (i == 0) X = e->source;
        else if (e->source != X) return false;
    }
    return X != nullptr;
}

static ClosedForm recognize(const BitVec &bv) {
    ClosedForm cf; Value *X = nullptr, *Y = nullptr; int k = 0;
    if (matchCopy(bv, X))                      { cf.kind = ClosedForm::COPY_OP; cf.x = X; return cf; }
    if (matchBitwiseOp(bv, BitOp::AND, X, Y))  { cf.kind = ClosedForm::AND_OP; cf.x = X; cf.y = Y; return cf; }
    if (matchBitwiseOp(bv, BitOp::OR,  X, Y))  { cf.kind = ClosedForm::OR_OP;  cf.x = X; cf.y = Y; return cf; }
    if (matchBitwiseOp(bv, BitOp::XOR, X, Y))  { cf.kind = ClosedForm::XOR_OP; cf.x = X; cf.y = Y; return cf; }
    if (matchShift(bv, X, k)) {
        if (k > 0) { cf.kind = ClosedForm::LSHR_OP; cf.x = X; cf.shiftAmount =  k; return cf; }
        if (k < 0) { cf.kind = ClosedForm::SHL_OP;  cf.x = X; cf.shiftAmount = -k; return cf; }
    }
    return cf;
}

// ══════════════════════════════════════════════════════════════════════
// §G  Module-level driver
// ══════════════════════════════════════════════════════════════════════

#include <cstdio>

struct FuncEquiv {
    ClosedForm::Kind kind        = ClosedForm::NONE;
    int              inputIdxA   = -1;
    int              inputIdxB   = -1;
    int              shiftAmount = 0;
    // For parametric-trip-count loops recognised via the SCEV-lite fallback:
    // `maskArgIdx >= 0` means "the closed-form result must be masked to the
    // low `args[maskArgIdx]` bits".  Equivalent IR: `result & ((1 << n) - 1)`.
    // Valid only when the trip count `n` is in [0, 32]; n > 32 in source
    // would still be handled correctly by the original loop (power overflows
    // to zero after 32 iterations, so result saturates at 32-bit closed form)
    // but our mask formula `(1 << n) - 1` evaluated in 32-bit wraps for n >= 32.
    int              maskArgIdx  = -1;
};

static int argIndex(Value *v, Function *f) {
    auto *arg = dynamic_cast<Argument *>(v);
    if (!arg) return -1;
    for (size_t i = 0; i < f->arguments_.size(); i++)
        if (f->arguments_[i] == arg) return (int)i;
    return -1;
}

static void dumpBE(BE e, int depth, FILE *out) {
    if (depth > 4) { fprintf(out, "..."); return; }
    if (!e) { fprintf(out, "<nil>"); return; }
    switch (e->op) {
        case BitOp::ZERO: fprintf(out, "0"); break;
        case BitOp::ONE:  fprintf(out, "1"); break;
        case BitOp::TOP:  fprintf(out, "T"); break;
        case BitOp::BIT_OF: fprintf(out, "bit(%p,%u)", (void*)e->source, e->idx); break;
        case BitOp::NOT:    fprintf(out, "~"); dumpBE(e->a, depth+1, out); break;
        case BitOp::AND: fprintf(out, "("); dumpBE(e->a, depth+1, out); fprintf(out, "&");  dumpBE(e->b, depth+1, out); fprintf(out, ")"); break;
        case BitOp::OR:  fprintf(out, "("); dumpBE(e->a, depth+1, out); fprintf(out, "|");  dumpBE(e->b, depth+1, out); fprintf(out, ")"); break;
        case BitOp::XOR: fprintf(out, "("); dumpBE(e->a, depth+1, out); fprintf(out, "^");  dumpBE(e->b, depth+1, out); fprintf(out, ")"); break;
    }
}

// Identify a parametric "countdown-to-zero" induction variable in `f` and
// return the function Argument used as its initial value.  Pattern matched:
//
//   header:  c_phi = phi i32 [arg, preheader], [c_dec, latch]
//            cond = icmp ne c_phi, 0  (or icmp sgt c_phi, 0)
//            br cond, body, exit
//   latch:   c_dec = sub c_phi, 1
//            br header
//
// Returns nullptr if no such IV/arg can be confidently identified.
static Argument *findCountdownParam(Function *f) {
    LoopInfo LI;
    LI.analyze(f);
    if (LI.allLoops().size() != 1) return nullptr;
    Loop *L = LI.allLoops()[0].get();
    if (!L->preheader || L->latches.size() != 1) return nullptr;
    BasicBlock *latch = L->latches[0];

    Argument *paramArg = nullptr;
    for (auto *inst : L->header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) break;
        Value *preIn = nullptr, *latchIn = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            auto *pb  = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            auto *val = phi->get_operand(i);
            if (pb == latch)         latchIn = val;
            else if (pb == L->preheader) preIn = val;
        }
        if (!preIn || !latchIn) continue;
        auto *arg  = dynamic_cast<Argument *>(preIn);
        auto *step = dynamic_cast<BinaryInst *>(latchIn);
        if (!arg || !step || step->op_id_ != Instruction::Sub) continue;
        if (step->get_operand(0) != phi) continue;
        auto *ci = dynamic_cast<ConstantInt *>(step->get_operand(1));
        if (!ci || ci->value_ != 1) continue;
        // Multiple candidate IVs → ambiguous; bail.
        if (paramArg) return nullptr;
        paramArg = arg;
    }
    if (!paramArg) return nullptr;

    // Soundness: paramArg must not flow into bit data — only IV uses.
    // Approximation: all uses of paramArg must be inside L->header's phi list
    // (== the IV phi).  Any other use disqualifies the substitution.
    for (auto &use : paramArg->use_list_) {
        auto *inst = dynamic_cast<Instruction *>(use.val_);
        if (!inst || inst->parent_ != L->header) return nullptr;
        if (!dynamic_cast<PhiInst *>(inst)) return nullptr;
    }
    return paramArg;
}

static FuncEquiv tryRecognize(Function *f) {
    FunctionAnalyzer fa(f);
    if (!fa.run()) {
        if (BITFUNC_DEBUG)
            fprintf(stderr, "[bitfunc] %s: analysis failed (%s)\n",
                    f->name_.c_str(), fa.failReason ? fa.failReason : "?");
        return {};
    }
    ClosedForm cf = recognize(fa.resultBv);
    if (cf.kind == ClosedForm::NONE) {
        if (BITFUNC_DEBUG) {
            fprintf(stderr, "[bitfunc] %s: no closed form\n", f->name_.c_str());
            for (int b = 0; b < 4; b++) {
                fprintf(stderr, "[bitfunc]   bit[%d] = ", b);
                dumpBE(fa.resultBv[b], 0, stderr);
                fprintf(stderr, "\n");
            }
        }
        return {};
    }
    FuncEquiv eq;
    eq.kind        = cf.kind;
    eq.inputIdxA   = argIndex(cf.x, f);
    eq.inputIdxB   = argIndex(cf.y, f);
    eq.shiftAmount = cf.shiftAmount;
    bool needB = (cf.kind == ClosedForm::AND_OP ||
                  cf.kind == ClosedForm::OR_OP  ||
                  cf.kind == ClosedForm::XOR_OP);
    if (eq.inputIdxA < 0) return {};
    if (needB && eq.inputIdxB < 0) return {};
    if (BITFUNC_DEBUG) {
        const char *k = "?";
        switch (cf.kind) {
            case ClosedForm::AND_OP:  k = "AND";  break;
            case ClosedForm::OR_OP:   k = "OR";   break;
            case ClosedForm::XOR_OP:  k = "XOR";  break;
            case ClosedForm::LSHR_OP: k = "LSHR"; break;
            case ClosedForm::SHL_OP:  k = "SHL";  break;
            case ClosedForm::COPY_OP: k = "COPY"; break;
            default: break;
        }
        fprintf(stderr, "[bitfunc] %s → %s arg[%d] arg[%d] shift=%d\n",
                f->name_.c_str(), k, eq.inputIdxA, eq.inputIdxB, eq.shiftAmount);
    }
    return eq;
}

// SCEV-lite fallback: when the standard analysis failed because the loop's
// trip count is a function parameter (not a constant), try pinning that
// parameter to 32 (the bit width) and re-analyzing.  Rationale: a bit-by-bit
// emulation that runs N times processes the low N bits of inputs.  Running
// it 32 times processes all 32 bits, giving the same closed form as the
// "full-width" version would, e.g. (A & B) for AND.  The actual semantics
// for N < 32 are recovered by masking the result with `(1 << N) - 1`.
static FuncEquiv tryRecognizeParametric(Function *f) {
    Argument *param = findCountdownParam(f);
    if (!param) {
        if (BITFUNC_DEBUG) fprintf(stderr, "[bitfunc] %s: no parametric IV\n", f->name_.c_str());
        return {};
    }
    FunctionAnalyzer fa(f);
    fa.pins[param] = 32;
    if (!fa.run()) {
        if (BITFUNC_DEBUG)
            fprintf(stderr, "[bitfunc] %s: parametric retry failed (%s)\n",
                    f->name_.c_str(), fa.failReason ? fa.failReason : "?");
        return {};
    }
    ClosedForm cf = recognize(fa.resultBv);
    if (cf.kind == ClosedForm::NONE) {
        if (BITFUNC_DEBUG)
            fprintf(stderr, "[bitfunc] %s: parametric retry: no closed form\n", f->name_.c_str());
        return {};
    }
    // Shifts under parametric retry are unsafe: we'd need to scale the mask
    // by the shift amount, which we don't bother with for now.
    if (cf.kind == ClosedForm::LSHR_OP ||
        cf.kind == ClosedForm::SHL_OP  ||
        cf.kind == ClosedForm::COPY_OP) return {};

    FuncEquiv eq;
    eq.kind        = cf.kind;
    eq.inputIdxA   = argIndex(cf.x, f);
    eq.inputIdxB   = argIndex(cf.y, f);
    eq.maskArgIdx  = argIndex(param, f);
    bool needB = (cf.kind == ClosedForm::AND_OP ||
                  cf.kind == ClosedForm::OR_OP  ||
                  cf.kind == ClosedForm::XOR_OP);
    if (eq.inputIdxA < 0 || (needB && eq.inputIdxB < 0) || eq.maskArgIdx < 0) return {};
    if (BITFUNC_DEBUG) {
        const char *k = "?";
        switch (cf.kind) {
            case ClosedForm::AND_OP: k = "AND_MASKED"; break;
            case ClosedForm::OR_OP:  k = "OR_MASKED";  break;
            case ClosedForm::XOR_OP: k = "XOR_MASKED"; break;
            default: break;
        }
        fprintf(stderr, "[bitfunc] %s → %s arg[%d] arg[%d] mask=arg[%d]\n",
                f->name_.c_str(), k, eq.inputIdxA, eq.inputIdxB, eq.maskArgIdx);
    }
    return eq;
}

static Instruction::OpID kindToOpID(ClosedForm::Kind k) {
    switch (k) {
        case ClosedForm::AND_OP:  return Instruction::And;
        case ClosedForm::OR_OP:   return Instruction::Or;
        case ClosedForm::XOR_OP:  return Instruction::Xor;
        case ClosedForm::LSHR_OP: return Instruction::LShr;
        case ClosedForm::SHL_OP:  return Instruction::Shl;
        default:                  return Instruction::Add;
    }
}

static void rewriteCallSites(Module *module,
                             const std::unordered_map<Function *, FuncEquiv> &equiv) {
    for (auto *caller : module->function_list_) {
        if (caller->is_declaration()) continue;
        for (auto *bb : caller->basic_blocks_) {
            std::vector<CallInst *> toRewrite;
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call) continue;
                auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
                if (!callee || !equiv.count(callee)) continue;
                toRewrite.push_back(call);
            }
            for (auto *call : toRewrite) {
                auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
                const FuncEquiv &eq = equiv.at(callee);

                if (eq.kind == ClosedForm::COPY_OP) {
                    call->replace_all_use_with(call->get_operand(eq.inputIdxA));
                    bb->delete_instr(call);
                    continue;
                }

                Instruction *newInst = nullptr;
                if (eq.kind == ClosedForm::AND_OP ||
                    eq.kind == ClosedForm::OR_OP  ||
                    eq.kind == ClosedForm::XOR_OP) {
                    Value *a = call->get_operand(eq.inputIdxA);
                    Value *b = call->get_operand(eq.inputIdxB);
                    newInst = new BinaryInst(call->type_, kindToOpID(eq.kind), a, b, bb, true);
                } else if (eq.kind == ClosedForm::LSHR_OP || eq.kind == ClosedForm::SHL_OP) {
                    Value *a   = call->get_operand(eq.inputIdxA);
                    Value *amt = new ConstantInt(call->type_, eq.shiftAmount);
                    newInst = new BinaryInst(call->type_, kindToOpID(eq.kind), a, amt, bb, true);
                }
                if (!newInst) continue;
                bb->add_instruction_before_inst(newInst, call);

                // Apply mask for parametric closed forms.
                // Naive `(1 << n) - 1` is wrong at n == 32 because AArch64 LSL
                // takes the shift amount mod 32: `1 << 32` becomes 1, mask
                // becomes 0, and the entire result gets zeroed.  We emit a
                // saturating form:
                //     shifted = 1 << n           ; wraps to 1 when n == 32
                //     mask_lo = shifted - 1      ; correct for n in [0, 31]
                //     full    = n >= 32          ; saturation predicate
                //     mask    = select full, -1, mask_lo
                //     result  = op_result & mask
                if (eq.maskArgIdx >= 0) {
                    Value *n      = call->get_operand(eq.maskArgIdx);
                    auto *one     = new ConstantInt(call->type_, 1);
                    auto *shifted = new BinaryInst(call->type_, Instruction::Shl, one, n, bb, true);
                    bb->add_instruction_before_inst(shifted, call);
                    auto *one2    = new ConstantInt(call->type_, 1);
                    auto *maskLo  = new BinaryInst(call->type_, Instruction::Sub, shifted, one2, bb, true);
                    bb->add_instruction_before_inst(maskLo, call);
                    auto *thirtyTwo = new ConstantInt(call->type_, 32);
                    auto *isFull  = new ICmpInst(ICmpInst::ICMP_SGE, n, thirtyTwo, bb, true);
                    bb->add_instruction_before_inst(isFull, call);
                    auto *negOne  = new ConstantInt(call->type_, -1);
                    auto *mask    = new SelectInst(isFull, negOne, maskLo, call->type_);
                    bb->add_instruction_before_inst(mask, call);
                    auto *masked  = new BinaryInst(call->type_, Instruction::And, newInst, mask, bb, true);
                    bb->add_instruction_before_inst(masked, call);
                    newInst = masked;
                }

                call->replace_all_use_with(newInst);
                bb->delete_instr(call);
            }
        }
    }
}

} // namespace bitfunc

void BitFuncRecognize::execute(Module *module) {
    bitfunc::clearCache();
    std::unordered_map<Function *, bitfunc::FuncEquiv> equiv;
    for (auto *f : module->function_list_) {
        if (f->is_declaration()) continue;
        if (f->name_ == "main") continue;
        auto *ft = dynamic_cast<FunctionType *>(f->type_);
        if (!ft || ft->result_->tid_ != Type::IntegerTyID) continue;
        if (f->arguments_.empty() || f->arguments_.size() > 3) continue;
        bool allInt = true;
        for (auto *a : f->arguments_)
            if (a->type_->tid_ != Type::IntegerTyID) { allInt = false; break; }
        if (!allInt) continue;

        // Standard recognition (fully unrolled constant-trip loops).
        auto eq = bitfunc::tryRecognize(f);
        if (eq.kind != bitfunc::ClosedForm::NONE) { equiv[f] = eq; continue; }

        // SCEV-lite fallback: parametric trip count.  Only meaningful when
        // the function has a parametric arg that serves as a countdown IV;
        // tryRecognizeParametric checks that internally.
        auto eq2 = bitfunc::tryRecognizeParametric(f);
        if (eq2.kind != bitfunc::ClosedForm::NONE) equiv[f] = eq2;
    }
    if (!equiv.empty())
        bitfunc::rewriteCallSites(module, equiv);
    bitfunc::clearCache();
}
