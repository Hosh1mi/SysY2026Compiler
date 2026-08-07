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
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/constantEvaluator.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
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
    ZERO, ONE, BIT_OF, AND, OR, XOR, NOT, TOP,
    // Reified `icmp eq Value, const` predicate.  Stored as source=Value, idx=(uint)const.
    // Used so the recognizer can later identify "n == k" arms in Select chains;
    // without this atom the predicate would canonicalize into a deep AND-of-XOR-NOT
    // tree on the value's bits that can't be matched back.
    ICMP_EQ_CONST
};

class BitExpr;
using BE = const BitExpr *;

class BitExpr {
public:
    BitOp op;
    Value *source; unsigned idx;
    BE a, b;
    uint64_t stableId;
    BitExpr(BitOp o, Value *s, unsigned i, BE x, BE y, uint64_t id)
        : op(o), source(s), idx(i), a(x), b(y), stableId(id) {}
};

struct BEKey {
    BitOp op; Value *src; unsigned idx; BE a, b;
    bool operator==(const BEKey &o) const {
        return op == o.op && src == o.src && idx == o.idx && a == o.a && b == o.b;
    }
};
struct BEHash {
    // Multiplier is Justin Sobel's "FNV-ish" mixing constant (also known from
    // the Hsieh / SDBM hash family).  Chosen for cheap mixing across 64 bits
    // when combining heterogeneous tuples; not collision-critical because the
    // cache is small (≤ MAX_VISITS * 32 entries per analyzed function).
    size_t operator()(const BEKey &k) const {
        constexpr size_t kMix = 1315423911u;
        size_t h = (size_t)k.op;
        h = h * kMix + (size_t)k.src;
        h = h * kMix + (size_t)k.idx;
        h = h * kMix + (size_t)k.a;
        h = h * kMix + (size_t)k.b;
        return h;
    }
};

// Hash-consing arena for BitExpr atoms.
//
// All BE pointers handed out by intern() alias entries in `cache_`.  The arena
// owns those BitExpr nodes; its destruction invalidates every BE that came
// out of it.  Lifetime is bounded by BitExprArenaScope (RAII) so that no BE
// can outlive a single pass invocation.
//
// Access is funneled through the thread_local `t_arena` pointer set by
// BitExprArenaScope.  This keeps the existing free-function call sites
// (Zero(), And_(), bvAnd(), …) ignorant of the arena instance while still
// making the storage instance-local — the moral equivalent of an implicit
// `this` parameter, but without rewriting ~40 helper signatures.  Two
// consequences:
//   - thread-safe (each thread carries its own current arena).
//   - re-entrancy is allowed (a nested Scope stacks a new arena and
//     restores the previous on destruction), although the recognizer
//     currently never re-enters itself.
class BitExprArena {
public:
    BE intern(BitOp op, Value *src = nullptr, unsigned idx = 0,
              BE a = nullptr, BE b = nullptr) {
        BEKey key{op, src, idx, a, b};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second.get();
        auto owned = std::make_unique<BitExpr>(op, src, idx, a, b, nextId_++);
        BE result = owned.get();
        cache_.emplace(key, std::move(owned));
        return result;
    }
private:
    std::unordered_map<BEKey, std::unique_ptr<BitExpr>, BEHash> cache_;
    uint64_t nextId_ = 0;
};

static thread_local BitExprArena *t_arena = nullptr;

class BitExprArenaScope {
public:
    BitExprArenaScope() : prev_(t_arena) { t_arena = &arena_; }
    ~BitExprArenaScope() { t_arena = prev_; }
    BitExprArenaScope(const BitExprArenaScope &) = delete;
    BitExprArenaScope &operator=(const BitExprArenaScope &) = delete;
private:
    BitExprArena  arena_;
    BitExprArena *prev_;
};

static BE intern(BitOp op, Value *src = nullptr, unsigned idx = 0,
                 BE a = nullptr, BE b = nullptr) {
    return t_arena->intern(op, src, idx, a, b);
}

static BE Zero()              { return intern(BitOp::ZERO); }
static BE One()               { return intern(BitOp::ONE); }
static BE Top()               { return intern(BitOp::TOP); }
static BE BitOf(Value *v, unsigned i) { return intern(BitOp::BIT_OF, v, i); }
static BE IcmpEqConst(Value *v, int k) { return intern(BitOp::ICMP_EQ_CONST, v, (unsigned)k); }

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
    if (a->stableId > b->stableId) std::swap(a, b);
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
    if (a->stableId > b->stableId) std::swap(a, b);
    return intern(BitOp::OR, nullptr, 0, a, b);
}

static BE Xor_(BE a, BE b) {
    if (a == Zero()) return b;
    if (b == Zero()) return a;
    if (a == Top() || b == Top()) return Top();
    if (a == b) return Zero();
    if (a == One()) return Not_(b);
    if (b == One()) return Not_(a);
    if (a->stableId > b->stableId) std::swap(a, b);
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

// If `bv` is exactly the symbolic form of a single Value `v`
// (bit i = BitOf(v, i) for all 32 bits), return v.  Otherwise nullptr.
// Used by processICmp's fast path and by matchVarShl's bit-source check.
static Value *pureSymbolicSource(const BitVec &bv) {
    Value *src = nullptr;
    for (unsigned i = 0; i < 32; i++) {
        BE e = bv[i];
        if (e->op != BitOp::BIT_OF) return nullptr;
        if (e->idx != i) return nullptr;
        if (i == 0) src = e->source;
        else if (e->source != src) return nullptr;
    }
    return src;
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

// NOTE on soundness: this abstraction is intentionally imprecise.  SysY/C99
// `srem x, 2` rounds the quotient toward zero, so for negative odd x the
// result is `-1` (all 32 bits set), not `1`.  A precise model would set
// high bits to `And(sign_bit_of_x, BitOf(x, 0))`.  We keep them as ZERO
// because:
//   1. With ZERO high bits, the `bit_a == 1` check in `_and`-style sources
//      simplifies cleanly to `BitOf(a, i)`, letting the AND/OR closed
//      forms be recognized at all.  A precise model would inject sign
//      terms into every result bit and the closed-form match would fail.
//   2. The unsoundness is contained: the AND/OR rewrites in §G compensate
//      by emitting a runtime guard that emulates the source's actual
//      negative-input semantics (`_and(neg, *) = 0`,
//      `_or(neg, b) = (b>=0 ? b : 0)`).  See rewriteCallSites().
//   3. XOR is dropped entirely (recognize() refuses), because the
//      negative-input behavior of `_xor` has no clean closed form.
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

struct RegionResult {
    bool         returned = false;   // true → returnBv valid; false → exitBB/exitPrev valid
    BitVec       returnBv;
    BasicBlock  *exitBB   = nullptr; // block at which analysis stopped (== stop arg)
    BasicBlock  *exitPrev = nullptr; // last block on the path leading into exitBB
};

class FunctionAnalyzer {
public:
    Function   *func;
    const PostDominatorTreeAnalysis *postDomTree;
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

    FunctionAnalyzer(Function *f, const PostDominatorTreeAnalysis &PDT)
        : func(f), postDomTree(&PDT) {}
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
            if (ConstantEvaluator::foldIntegerBinary(op, av, bv, rv)) {
                state[bi] = vsConst(rv); return true;
            }
            switch (op) {
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

        // Fast path: `eq/ne Value, const_int` where the Value is in pure-symbolic
        // form (each bit is BitOf(src, i)).  Emit an ICMP_EQ_CONST atom so
        // downstream recognizers (e.g. matchVarShl) can recover the predicate
        // identity; otherwise the bit-by-bit expansion below produces an AND-of-XOR
        // tree that hides the semantics.
        if (Value *symSrc = (a.knownConcrete ? pureSymbolicSource(b.bv)
                                              : (b.knownConcrete ? pureSymbolicSource(a.bv) : nullptr))) {
            int kConst = a.knownConcrete ? a.concreteVal : b.concreteVal;
            BE pred = IcmpEqConst(symSrc, kConst);
            if (!wantEq) pred = Not_(pred);
            ValueState out; out.isI1 = true;
            out.bv = makeConst(0); out.bv[0] = pred;
            state[ci] = out;
            return true;
        }

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
    for (auto *bb : func->basic_blocks_)
        ipdomCache[bb] = postDomTree->getIPostDominator(bb);

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
    enum Kind {
        NONE,
        AND_OP, OR_OP, XOR_OP,
        LSHR_OP, SHL_OP, COPY_OP,
        // Variable shift: `shl x, n` (n is a Value, K is the highest covered
        // shift amount in the source if-chain).  Rewriter must guard against
        // n outside [1, K] because the source returns x unchanged there.
        VAR_SHL_OP,
        // Variable signed-divide-by-power-of-two: source `x / 2^n` if-chain
        // (rotrN style).  Bit-vec abstraction collapses sdiv-by-2^k to
        // lshr(x, k); rewriter emits sdiv to preserve language semantics
        // for negative x.
        VAR_LSHR_OP,
    };
    Kind    kind        = NONE;
    Value  *x           = nullptr;   // base value
    Value  *y           = nullptr;   // 2nd operand for AND/OR/XOR; shift amount for VAR_SHL/VAR_LSHR
    int     shiftAmount = 0;         // const shift for LSHR/SHL; max K for VAR_SHL/VAR_LSHR
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

// Verify bit `i` of a presumed `shl X, N` body, where the source code spells
// out one return per `n == k` for k ∈ [1, K] and falls through to `return X`.
// The canonicalised form at each level k is either:
//   Or(And(eq_k, BitOf(X, i-k)), And(Not(eq_k), inner))   — full Select
//   And(Not(eq_k), inner)                                 — Select(c, ZERO, f)
// The innermost arm equals BitOf(X, i).
static bool matchVarShlBit(BE e, Value *X, Value *N, unsigned i, int K) {
    for (int k = 1; k <= K; k++) {
        BE c    = IcmpEqConst(N, k);
        BE notC = intern(BitOp::NOT, nullptr, 0, c);
        int target = (int)i - k;
        BE t = (target >= 0 && target < 32) ? BitOf(X, (unsigned)target) : Zero();

        if (t == Zero()) {
            if (!e || e->op != BitOp::AND) return false;
            if      (e->a == notC) e = e->b;
            else if (e->b == notC) e = e->a;
            else return false;
            continue;
        }
        if (!e || e->op != BitOp::OR) return false;
        BE L = e->a, R = e->b;
        auto isAndCT = [&](BE n_) {
            return n_ && n_->op == BitOp::AND &&
                   ((n_->a == c && n_->b == t) || (n_->a == t && n_->b == c));
        };
        auto extractInner = [&](BE n_) -> BE {
            if (!n_ || n_->op != BitOp::AND) return nullptr;
            if (n_->a == notC) return n_->b;
            if (n_->b == notC) return n_->a;
            return nullptr;
        };
        BE next = nullptr;
        if      (isAndCT(L)) next = extractInner(R);
        else if (isAndCT(R)) next = extractInner(L);
        if (!next) return false;
        e = next;
    }
    return e == BitOf(X, i);
}

// Detect `shl x, n` style if-chain (rotlN / variants).  Identifies (N, K) by
// collecting ICMP_EQ_CONST atoms from bv[0], then verifies the structure of
// every bit against the canonicalised SHL form.
static bool matchVarShl(const BitVec &bv, Value *&X, Value *&N, int &K) {
    // Step 1: bv[0] is an all-collapsed chain `And(Not(eq_1), And(Not(eq_2), ..., BitOf(X, 0)))`.
    // Walk it to extract X, N, K simultaneously.
    BE e = bv[0];
    std::vector<int> ks;
    Value *Ncand = nullptr;
    while (e && e->op == BitOp::AND) {
        BE notNode = nullptr, inner = nullptr;
        for (BE side : {e->a, e->b}) {
            if (side && side->op == BitOp::NOT && side->a &&
                side->a->op == BitOp::ICMP_EQ_CONST) {
                notNode = side;
                inner   = (side == e->a) ? e->b : e->a;
                break;
            }
        }
        if (!notNode) break;
        Value *n_here = notNode->a->source;
        int    k_here = (int)notNode->a->idx;
        if (!Ncand) Ncand = n_here;
        else if (n_here != Ncand) return false;
        ks.push_back(k_here);
        e = inner;
    }
    if (ks.empty()) return false;
    if (!e || e->op != BitOp::BIT_OF || e->idx != 0) return false;
    X = e->source;
    N = Ncand;

    // The k values appear from outermost (k=1) to innermost (k=K).  Verify
    // they form {1, 2, ..., K}.
    K = (int)ks.size();
    for (int j = 0; j < K; j++) if (ks[j] != j + 1) return false;

    // Step 2: verify each bit.
    for (unsigned i = 0; i < 32; i++)
        if (!matchVarShlBit(bv[i], X, N, i, K)) return false;
    return true;
}

// Same canonical shape as matchVarShlBit, but for `lshr X, N` (rotrN style):
//   At each k in [1, K], the arm contributes BitOf(X, i+k) (Zero if i+k >= 32).
//   Fallthrough is BitOf(X, i).
static bool matchVarLshrBit(BE e, Value *X, Value *N, unsigned i, int K) {
    for (int k = 1; k <= K; k++) {
        BE c    = IcmpEqConst(N, k);
        BE notC = intern(BitOp::NOT, nullptr, 0, c);
        int target = (int)i + k;
        BE t = (target < 32) ? BitOf(X, (unsigned)target) : Zero();

        if (t == Zero()) {
            if (!e || e->op != BitOp::AND) return false;
            if      (e->a == notC) e = e->b;
            else if (e->b == notC) e = e->a;
            else return false;
            continue;
        }
        if (!e || e->op != BitOp::OR) return false;
        BE L = e->a, R = e->b;
        auto isAndCT = [&](BE n_) {
            return n_ && n_->op == BitOp::AND &&
                   ((n_->a == c && n_->b == t) || (n_->a == t && n_->b == c));
        };
        auto extractInner = [&](BE n_) -> BE {
            if (!n_ || n_->op != BitOp::AND) return nullptr;
            if (n_->a == notC) return n_->b;
            if (n_->b == notC) return n_->a;
            return nullptr;
        };
        BE next = nullptr;
        if      (isAndCT(L)) next = extractInner(R);
        else if (isAndCT(R)) next = extractInner(L);
        if (!next) return false;
        e = next;
    }
    return e == BitOf(X, i);
}

// Detect `lshr x, n` style if-chain (rotrN / variants).  Walks bv[31] for
// initial (X, N, K) extraction: bit 31 of `x >> k` is Zero for any k >= 1,
// so bv[31] collapses to a pure And(Not(eq_k), inner) chain (analogous to
// matchVarShl's use of bv[0]).
static bool matchVarLshr(const BitVec &bv, Value *&X, Value *&N, int &K) {
    BE e = bv[31];
    std::vector<int> ks;
    Value *Ncand = nullptr;
    while (e && e->op == BitOp::AND) {
        BE notNode = nullptr, inner = nullptr;
        for (BE side : {e->a, e->b}) {
            if (side && side->op == BitOp::NOT && side->a &&
                side->a->op == BitOp::ICMP_EQ_CONST) {
                notNode = side;
                inner   = (side == e->a) ? e->b : e->a;
                break;
            }
        }
        if (!notNode) break;
        Value *n_here = notNode->a->source;
        int    k_here = (int)notNode->a->idx;
        if (!Ncand) Ncand = n_here;
        else if (n_here != Ncand) return false;
        ks.push_back(k_here);
        e = inner;
    }
    if (ks.empty()) return false;
    if (!e || e->op != BitOp::BIT_OF || e->idx != 31) return false;
    X = e->source;
    N = Ncand;

    K = (int)ks.size();
    for (int j = 0; j < K; j++) if (ks[j] != j + 1) return false;

    for (unsigned i = 0; i < 32; i++)
        if (!matchVarLshrBit(bv[i], X, N, i, K)) return false;
    return true;
}

static ClosedForm recognize(const BitVec &bv) {
    ClosedForm cf; Value *X = nullptr, *Y = nullptr; int k = 0;
    if (matchCopy(bv, X))                      { cf.kind = ClosedForm::COPY_OP; cf.x = X; return cf; }
    if (matchBitwiseOp(bv, BitOp::AND, X, Y))  { cf.kind = ClosedForm::AND_OP; cf.x = X; cf.y = Y; return cf; }
    if (matchBitwiseOp(bv, BitOp::OR,  X, Y))  { cf.kind = ClosedForm::OR_OP;  cf.x = X; cf.y = Y; return cf; }
    // XOR_OP is recognized, but is UNSOUND for negative operands and so must
    // NOT be lowered to a bare native `xor`.  Source `_xor(a, b)` detects a
    // bit mismatch with `a%2 != b%2`; for negative x, `x % 2 ∈ {0, -1}`
    // (SysY/C99 srem truncates toward zero), and both `-1 != 0` and `-1 != 1`
    // hold, so the source's per-bit condition misfires once a negative operand
    // enters the a/=2 chain.  The result differs from native `xor a, b` and
    // has no clean closed form.  The abstract domain over-approximates
    // `srem x, 2` with ZERO high bits (see bvSremByTwo), which is exactly why
    // the pure-XOR closed form matches here at all.  rewriteCallSites()
    // compensates by emitting a non-negative fast-path guard: the native
    // `eor` runs only when both operands are non-negative, otherwise the
    // original `_xor` is called.  See the XOR_OP branch there.
    if (matchBitwiseOp(bv, BitOp::XOR, X, Y))  { cf.kind = ClosedForm::XOR_OP; cf.x = X; cf.y = Y; return cf; }
    if (matchShift(bv, X, k)) {
        // Limitation: only emit constant SHL (k < 0 in our delta sign).  We do
        // NOT recognize LSHR (k > 0) here, because the bit-vector abstraction
        // is ambiguous about its source IR op:
        //   - `sdiv x, 1<<k` (SysY's only way to spell a constant right shift)
        //     abstracts to lshr via bvSdivByConstAsLshr.  Rewriting back to
        //     `lshr` would miscompile negative x (sdiv rounds toward zero;
        //     lshr fills with 0).
        //   - A hand-written bit-assembly emulating `(unsigned)x >> k` would
        //     produce the same bv shape.  Rewriting that to `sdiv x, 1<<k`
        //     would also miscompile negative x (opposite direction).
        // The two sources are indistinguishable at the bv level, so neither
        // rewrite target is sound.  SHL is safe because SysY `x * (1<<k)` and
        // a hand-written shl-emulation agree on all x (mul/shl have identical
        // two's-complement semantics).
        if (k < 0) { cf.kind = ClosedForm::SHL_OP;  cf.x = X; cf.shiftAmount = -k; return cf; }
    }
    if (matchVarShl(bv, X, Y, k)) {
        cf.kind = ClosedForm::VAR_SHL_OP;
        cf.x = X; cf.y = Y;          // y is the shift amount Value
        cf.shiftAmount = k;          // max supported shift
        return cf;
    }
    if (matchVarLshr(bv, X, Y, k)) {
        cf.kind = ClosedForm::VAR_LSHR_OP;
        cf.x = X; cf.y = Y;
        cf.shiftAmount = k;
        return cf;
    }
    return cf;
}

// ══════════════════════════════════════════════════════════════════════
// §G  Module-level driver
// ══════════════════════════════════════════════════════════════════════

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
        case BitOp::ICMP_EQ_CONST: fprintf(out, "(%p==%d)", (void*)e->source, (int)e->idx); break;
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

static FuncEquiv tryRecognize(Function *f,
                              const PostDominatorTreeAnalysis &PDT) {
    FunctionAnalyzer fa(f, PDT);
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
    bool needB = (cf.kind == ClosedForm::AND_OP    ||
                  cf.kind == ClosedForm::OR_OP     ||
                  cf.kind == ClosedForm::XOR_OP    ||
                  cf.kind == ClosedForm::VAR_SHL_OP ||
                  cf.kind == ClosedForm::VAR_LSHR_OP);
    if (eq.inputIdxA < 0) return {};
    if (needB && eq.inputIdxB < 0) return {};
    if (BITFUNC_DEBUG) {
        const char *k = "?";
        switch (cf.kind) {
            case ClosedForm::AND_OP:     k = "AND";     break;
            case ClosedForm::OR_OP:      k = "OR";      break;
            case ClosedForm::XOR_OP:     k = "XOR";     break;
            case ClosedForm::LSHR_OP:    k = "LSHR";    break;
            case ClosedForm::SHL_OP:     k = "SHL";     break;
            case ClosedForm::COPY_OP:    k = "COPY";    break;
            case ClosedForm::VAR_SHL_OP: k = "VAR_SHL"; break;
            case ClosedForm::VAR_LSHR_OP: k = "VAR_LSHR"; break;
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
static FuncEquiv tryRecognizeParametric(
    Function *f, const PostDominatorTreeAnalysis &PDT) {
    Argument *param = findCountdownParam(f);
    if (!param) {
        if (BITFUNC_DEBUG) fprintf(stderr, "[bitfunc] %s: no parametric IV\n", f->name_.c_str());
        return {};
    }
    FunctionAnalyzer fa(f, PDT);
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
    //
    // XOR is also excluded here: its non-negative fast-path guard (see
    // rewriteCallSites) would have to compose with the trip-count mask, and
    // that interaction is not worth the complexity.  Parametric `_xor` keeps
    // its original call.
    if (cf.kind == ClosedForm::LSHR_OP    ||
        cf.kind == ClosedForm::SHL_OP     ||
        cf.kind == ClosedForm::COPY_OP    ||
        cf.kind == ClosedForm::XOR_OP     ||
        cf.kind == ClosedForm::VAR_SHL_OP ||
        cf.kind == ClosedForm::VAR_LSHR_OP) return {};

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

// Lower a recognized `_xor` call with a non-negative fast-path guard.
//
//   _xor(a, b) is a bit-by-bit XOR that is correct ONLY when both operands
//   are non-negative; for a negative operand the source's `a%2 != b%2` test
//   (srem truncates toward zero, parity ∈ {0,-1}) misfires and the result
//   has no clean closed form.  We therefore branch on the sign of the
//   operands instead of unconditionally emitting native `eor`:
//
//       cond = (a | b) < 0          ; true iff a<0 or b<0 (sign bit set)
//       br cond, slow, fast
//     fast:  f = eor a, b           ; both non-negative → native XOR
//       br merge
//     slow:  s = call _xor(a, b)    ; some operand negative → exact source
//       br merge
//     merge: r = phi [s, slow], [f, fast]
//
//   This is correct for ALL inputs (slow path re-runs the original function)
//   and is a real speed-up on the hot non-negative path, independent of any
//   downstream pass.  Using a branch (not a select) also avoids materializing
//   the call on the fast path and sidesteps the select-around-call register
//   pressure the AND/OR rewrite warns about.
static void lowerXorCall(CallInst *call, const FuncEquiv &eq) {
    BasicBlock *bb   = call->parent_;
    Function   *func = bb->parent_;
    Module     *mod  = func->parent_;
    Type       *ity  = call->type_;

    Value    *a      = call->get_operand(eq.inputIdxA);
    Value    *b      = call->get_operand(eq.inputIdxB);
    auto     *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));

    // Snapshot the successors `bb`'s terminator branches to; after the split
    // they become predecessors of `merge` instead of `bb`.
    std::vector<BasicBlock *> oldSuccs = bb->succ_bbs_;

    // merge: everything strictly after `call` (including the terminator).
    BasicBlock *merge = new BasicBlock(mod, bb->name_ + ".xor.merge", func);
    std::vector<Instruction *> tail;
    bool seen = false;
    for (auto *inst : bb->instr_list_) {
        if (seen) tail.push_back(inst);
        if (inst == call) seen = true;
    }
    for (auto *inst : tail) { bb->remove_instr(inst); merge->add_instruction(inst); }

    // Re-point old successor edges and their phi incomings: bb → merge.
    for (auto *S : oldSuccs) {
        S->remove_pre_basic_block(bb);
        S->add_pre_basic_block(merge);
        merge->add_succ_basic_block(S);
        bb->remove_succ_basic_block(S);
        for (auto *inst : S->instr_list_) {
            if (!inst->is_phi()) continue;
            for (unsigned i = 1; i < inst->num_ops_; i += 2)
                if (inst->get_operand(i) == bb) inst->set_operand(i, merge);
        }
    }

    // fast: native eor.
    BasicBlock *fast = new BasicBlock(mod, bb->name_ + ".xor.fast", func);
    auto *eorI = new BinaryInst(ity, Instruction::Xor, a, b, fast, true);
    fast->add_instruction(eorI);
    new BranchInst(merge, fast);

    // slow: re-call the original function (exact source semantics).
    BasicBlock *slow = new BasicBlock(mod, bb->name_ + ".xor.slow", func);
    std::vector<Value *> args;
    for (int i = 0; i < (int)call->num_ops_ - 1; i++) args.push_back(call->get_operand(i));
    auto *slowCall = new CallInst(callee, args, slow);   // auto-appended to slow
    new BranchInst(merge, slow);

    // merge: phi selecting between the two paths.
    auto *phi = new PhiInst(Instruction::PHI,
                            {static_cast<Value *>(slowCall), static_cast<Value *>(eorI)},
                            {slow, fast}, ity, merge);
    merge->add_instruction_front(phi);
    call->replace_all_use_with(phi);

    // bb: compute the sign guard and branch.  (a|b) < 0  ⇔  a<0 || b<0.
    auto *zero = new ConstantInt(ity, 0);
    auto *orAB = new BinaryInst(ity, Instruction::Or, a, b, bb, true);
    bb->add_instruction_before_inst(orAB, call);
    auto *cond = new ICmpInst(ICmpInst::ICMP_SLT, orAB, zero, bb, true);
    bb->add_instruction_before_inst(cond, call);
    new BranchInst(cond, slow, fast, bb);   // appended as bb's terminator
    bb->delete_instr(call);
}

static void rewriteCallSites(Module *module,
                             const std::unordered_map<Function *, FuncEquiv> &equiv) {
    // XOR rewrites split basic blocks, so they cannot run while we iterate a
    // block's instruction list.  Collect them here and lower them afterwards;
    // each call is located dynamically via call->parent_ at lowering time, so
    // splitting a block that holds a later xor call stays correct.
    std::vector<CallInst *> xorCalls;
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

                if (eq.kind == ClosedForm::XOR_OP) {
                    // Defer: control-flow split cannot run during this iteration.
                    xorCalls.push_back(call);
                    continue;
                }

                if (eq.kind == ClosedForm::COPY_OP) {
                    call->replace_all_use_with(call->get_operand(eq.inputIdxA));
                    bb->delete_instr(call);
                    continue;
                }

                Instruction *newInst = nullptr;
                // ── Negative-input correctness for AND/OR ─────────────────────
                // Source `_and(a, b)` and `_or(a, b)` extract bits with `a % 2`.
                // SysY/C99 srem rounds the quotient toward zero, so for negative
                // odd a, the parity is `-1` (not 1).  The source's `bit == 1`
                // test then misfires:
                //   _and: `(a<0 ? -1 : a&1) == 1 && (b<0 ? -1 : b&1) == 1` only
                //         ever holds when BOTH operands are non-negative; once
                //         either operand reaches -1 in the a/=2 chain, no more
                //         contributions are added.  Effective semantics:
                //             _and(a, b) = (a<0 || b<0) ? 0 : (a & b)
                //   _or:  symmetric.  A negative operand contributes nothing
                //         because its parity is never == 1.  Effective:
                //             _or(a, b) = (a<0 ? 0 : a) | (b<0 ? 0 : b)
                //
                // The abstract domain over-approximates (see bvSremByTwo's NOTE)
                // and happily produces the pure AND / OR closed form.  Naively
                // lowering to `and a, b` / `or a, b` is WRONG for negative
                // operands — that is the bug fixed here.
                //
                // We emit the source's negative-input semantics inline using
                // select.  For non-negative inputs the guard is statically
                // false and the result simplifies back to the fast native op;
                // subsequent SCCP / range analysis can then strip the guard at
                // call sites where the args are provably non-negative
                // (e.g. huffman: bytes 0..255).  ── end note ───────────────────
                // Implementation note: we mask each operand individually
                // *before* ANDing them, mirroring the OR rewrite below:
                //     `(a<0 ? 0 : a) & (b<0 ? 0 : b)`
                // This is semantically equivalent to the source's "0 if either
                // operand is negative, else a&b": masking a negative operand
                // to 0 makes the resulting AND collapse to 0 regardless of
                // the other side.
                //
                // We do NOT use the more compact `select((a|b)<0, 0, a&b)`
                // form, nor a chained `select(b<0, 0, select(a<0, 0, a&b))`.
                // Both triggered a register-allocator interaction in the
                // backend that clobbered the call's `n` argument register
                // before the trailing `bits -= n` (observed in asm as
                // `csel w0, wzr, w1, lt` immediately followed by
                // `sub w2, w7, w0` reading the just-overwritten w0).  The
                // parallel mask-then-AND shape keeps each select's live
                // range short and matches the proven-safe OR_OP rewrite.
                if (eq.kind == ClosedForm::AND_OP) {
                    Value *a       = call->get_operand(eq.inputIdxA);
                    Value *b       = call->get_operand(eq.inputIdxB);
                    auto *zeroA1   = new ConstantInt(call->type_, 0);
                    auto *aNeg     = new ICmpInst(ICmpInst::ICMP_SLT, a, zeroA1, bb, true);
                    bb->add_instruction_before_inst(aNeg, call);
                    auto *zeroA2   = new ConstantInt(call->type_, 0);
                    auto *aMasked  = new SelectInst(aNeg, zeroA2, a, call->type_);
                    bb->add_instruction_before_inst(aMasked, call);
                    auto *zeroB1   = new ConstantInt(call->type_, 0);
                    auto *bNeg     = new ICmpInst(ICmpInst::ICMP_SLT, b, zeroB1, bb, true);
                    bb->add_instruction_before_inst(bNeg, call);
                    auto *zeroB2   = new ConstantInt(call->type_, 0);
                    auto *bMasked  = new SelectInst(bNeg, zeroB2, b, call->type_);
                    bb->add_instruction_before_inst(bMasked, call);
                    auto *result   = new BinaryInst(call->type_, Instruction::And, aMasked, bMasked, bb, true);
                    bb->add_instruction_before_inst(result, call);
                    newInst = result;
                } else if (eq.kind == ClosedForm::OR_OP) {
                    Value *a       = call->get_operand(eq.inputIdxA);
                    Value *b       = call->get_operand(eq.inputIdxB);
                    auto *zeroA1   = new ConstantInt(call->type_, 0);
                    auto *aNeg     = new ICmpInst(ICmpInst::ICMP_SLT, a, zeroA1, bb, true);
                    bb->add_instruction_before_inst(aNeg, call);
                    auto *zeroA2   = new ConstantInt(call->type_, 0);
                    auto *aMasked  = new SelectInst(aNeg, zeroA2, a, call->type_);
                    bb->add_instruction_before_inst(aMasked, call);
                    auto *zeroB1   = new ConstantInt(call->type_, 0);
                    auto *bNeg     = new ICmpInst(ICmpInst::ICMP_SLT, b, zeroB1, bb, true);
                    bb->add_instruction_before_inst(bNeg, call);
                    auto *zeroB2   = new ConstantInt(call->type_, 0);
                    auto *bMasked  = new SelectInst(bNeg, zeroB2, b, call->type_);
                    bb->add_instruction_before_inst(bMasked, call);
                    auto *result   = new BinaryInst(call->type_, Instruction::Or, aMasked, bMasked, bb, true);
                    bb->add_instruction_before_inst(result, call);
                    newInst = result;
                }
                // XOR_OP intentionally unreachable here — recognize() refuses
                // to return XOR_OP because the negative-input semantics of
                // `_xor` have no clean closed form.  See recognize().
                else if (eq.kind == ClosedForm::SHL_OP) {
                    // Note: LSHR_OP is intentionally not handled here.  See
                    // recognize() — constant lshr cannot be safely materialized
                    // because the bv abstraction loses the original signed-ness
                    // of the source (sdiv vs hand-written lshr-emulation).
                    Value *a   = call->get_operand(eq.inputIdxA);
                    Value *amt = new ConstantInt(call->type_, eq.shiftAmount);
                    newInst = new BinaryInst(call->type_, kindToOpID(eq.kind), a, amt, bb, true);
                    bb->add_instruction_before_inst(newInst, call);
                } else if (eq.kind == ClosedForm::VAR_SHL_OP ||
                           eq.kind == ClosedForm::VAR_LSHR_OP) {
                    // Source `rotlN(x, n)` / `rotrN(x, n)` returns the shifted /
                    // divided form for n ∈ [1, K] and x unchanged otherwise.
                    // Compress the range check `(n >= 1) && (n <= K)` to a
                    // single unsigned compare `(unsigned)(K - n) < K`, which
                    // is correct for any signed n (negative n yields a large
                    // unsigned K-n, n==0 yields K itself which fails ult K,
                    // n>K yields a wrap to a large unsigned).  We deliberately
                    // emit `sub K, n` (constant on LHS, variable on RHS) so
                    // foldICmpAddSub's Category B (which requires constant on
                    // sub's RHS) does not fire — that fold drops 2's-complement
                    // wrap guarantees and would silently break the predicate.
                    // The (K-n, ult) expressions CSE across sibling calls
                    // sharing the same `n` and `K`.
                    //     body    = shl x, n   (VAR_SHL)
                    //     body    = sdiv x, (shl 1, n)   (VAR_LSHR)
                    //     diff    = sub K, n
                    //     inRange = icmp ult diff, K
                    //     result  = select inRange, body, x
                    Value *a       = call->get_operand(eq.inputIdxA);
                    Value *n       = call->get_operand(eq.inputIdxB);
                    int    K       = eq.shiftAmount;
                    Instruction *body = nullptr;
                    if (eq.kind == ClosedForm::VAR_SHL_OP) {
                        body = new BinaryInst(call->type_, Instruction::Shl, a, n, bb, true);
                        bb->add_instruction_before_inst(body, call);
                    } else {
                        auto *one1 = new ConstantInt(call->type_, 1);
                        auto *pow  = new BinaryInst(call->type_, Instruction::Shl, one1, n, bb, true);
                        bb->add_instruction_before_inst(pow, call);
                        body = new BinaryInst(call->type_, Instruction::SDiv, a, pow, bb, true);
                        bb->add_instruction_before_inst(body, call);
                    }
                    auto *kLhs     = new ConstantInt(call->type_, K);
                    auto *diff     = new BinaryInst(call->type_, Instruction::Sub, kLhs, n, bb, true);
                    bb->add_instruction_before_inst(diff, call);
                    auto *kRhs     = new ConstantInt(call->type_, K);
                    auto *inRange  = new ICmpInst(ICmpInst::ICMP_ULT, diff, kRhs, bb, true);
                    bb->add_instruction_before_inst(inRange, call);
                    auto *result   = new SelectInst(inRange, body, a, call->type_);
                    bb->add_instruction_before_inst(result, call);
                    newInst        = result;
                }
                if (!newInst) continue;

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

    // Lower deferred XOR calls (each splits its containing block).
    for (auto *call : xorCalls) {
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        lowerXorCall(call, equiv.at(callee));
    }
}

} // namespace bitfunc

void BitFuncRecognize::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses BitFuncRecognize::execute(Module *module,
                                             AnalysisManager &AM) {
    // Install a fresh BitExpr arena for this pass invocation.  All BE pointers
    // produced inside this scope are owned by the arena and die when it does;
    // nothing outside must hold one past the closing brace.
    bitfunc::BitExprArenaScope arena_scope;

    std::unordered_map<Function *, bitfunc::FuncEquiv> equiv;
    for (auto *f : module->function_list_) {
        if (f->is_declaration()) continue;
        auto *ft = dynamic_cast<FunctionType *>(f->type_);
        if (!ft || ft->result_->tid_ != Type::IntegerTyID) continue;
        if (f->arguments_.empty() || f->arguments_.size() > 3) continue;
        // The symbolic recognizer is intended for compact bit-operation
        // helpers. Its path-state exploration copies symbolic maps, so reject
        // functions outside the documented structural scope before
        // constructing the analyzer. Post-dominance is supplied by the shared
        // AnalysisManager cache. This is a general complexity bound, independent of
        // function identity or call-site values.
        constexpr size_t kMaxCandidateBlocks = 200;
        if (f->basic_blocks_.size() > kMaxCandidateBlocks) continue;
        bool allInt = true;
        for (auto *a : f->arguments_)
            if (a->type_->tid_ != Type::IntegerTyID) { allInt = false; break; }
        if (!allInt) continue;

        // Standard recognition (fully unrolled constant-trip loops).
        PostDominatorTreeAnalysis &PDT = AM.getPostDominatorTree(f);
        auto eq = bitfunc::tryRecognize(f, PDT);
        if (eq.kind != bitfunc::ClosedForm::NONE) { equiv[f] = eq; continue; }

        // SCEV-lite fallback: parametric trip count.  Only meaningful when
        // the function has a parametric arg that serves as a countdown IV;
        // tryRecognizeParametric checks that internally.
        auto eq2 = bitfunc::tryRecognizeParametric(f, PDT);
        if (eq2.kind != bitfunc::ClosedForm::NONE) equiv[f] = eq2;
    }
    if (!equiv.empty()) {
        bitfunc::rewriteCallSites(module, equiv);
        return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
}
