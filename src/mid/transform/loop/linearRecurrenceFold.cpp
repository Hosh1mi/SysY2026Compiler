#include "../../../include/mid/opt/linearRecurrenceFold.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kMaxDim = 4;

bool debugEnabled() {
    static bool on = std::getenv("DEBUG_LINEAR_RECURRENCE") != nullptr;
    return on;
}

bool reject(const char *reason) {
    if (debugEnabled())
        std::cerr << "[LinearRecurrenceFold] reject: " << reason << "\n";
    return false;
}

uint32_t mulu(uint32_t a, uint32_t b) { return a * b; }
uint32_t addu(uint32_t a, uint32_t b) { return a + b; }

// Modular inverse for odd uint32 (R = Z/2^32Z units include odds).
bool modInverseOdd(uint32_t a, uint32_t &inv) {
    if ((a & 1u) == 0u)
        return false;
    // Newton iteration for 2-adic inverse: inv_{k+1} = inv_k (2 - a inv_k)
    uint32_t x = a;
    x = mulu(x, 2u - mulu(a, x));
    x = mulu(x, 2u - mulu(a, x));
    x = mulu(x, 2u - mulu(a, x));
    x = mulu(x, 2u - mulu(a, x));
    x = mulu(x, 2u - mulu(a, x));
    inv = x;
    return mulu(a, inv) == 1u;
}

using CoeffVec = std::array<int32_t, kMaxDim>;

struct LinearForm {
    CoeffVec c{};
    int dim = 0;
    bool valid = false;
};

LinearForm zeroForm(int dim) {
    LinearForm f;
    f.dim = dim;
    f.valid = true;
    return f;
}

LinearForm addForms(const LinearForm &a, const LinearForm &b) {
    if (!a.valid || !b.valid || a.dim != b.dim)
        return {};
    LinearForm r = zeroForm(a.dim);
    for (int i = 0; i < a.dim; ++i)
        r.c[i] = static_cast<int32_t>(addu(static_cast<uint32_t>(a.c[i]),
                                           static_cast<uint32_t>(b.c[i])));
    return r;
}

LinearForm scaleForm(const LinearForm &a, int32_t s) {
    if (!a.valid)
        return {};
    LinearForm r = zeroForm(a.dim);
    for (int i = 0; i < a.dim; ++i)
        r.c[i] = static_cast<int32_t>(mulu(static_cast<uint32_t>(a.c[i]),
                                           static_cast<uint32_t>(s)));
    return r;
}

bool formsEqual(const LinearForm &a, const LinearForm &b) {
    if (!a.valid || !b.valid || a.dim != b.dim)
        return false;
    for (int i = 0; i < a.dim; ++i)
        if (a.c[i] != b.c[i])
            return false;
    return true;
}

struct System {
    int dim = 0;
    PhiInst *iv = nullptr;
    Value *bound = nullptr;
    std::vector<PhiInst *> state;          // size dim
    std::vector<Value *> x0;              // preheader inits
    int32_t A[kMaxDim][kMaxDim]{};        // row i = coeffs of state[i]'
    LinearForm live;                      // single escaping linear form
    Instruction *liveRoot = nullptr;      // IR value equal to live^T x_n
};

bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks) {
    if (dynamic_cast<Constant *>(val))
        return true;
    if (dynamic_cast<GlobalVariable *>(val))
        return true;
    if (dynamic_cast<Argument *>(val))
        return true;
    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst)
        return true;
    return !blocks.count(inst->parent_);
}

// Express v as linear combination of state PHIs (homogeneous). Intermediate
// values inside the loop are followed; constants must be zero.
std::optional<LinearForm>
expressInState(Value *v, const System &sys, const std::set<BasicBlock *> &blocks,
               std::map<Value *, LinearForm> &memo) {
    auto it = memo.find(v);
    if (it != memo.end())
        return it->second.valid ? std::optional(it->second) : std::nullopt;

    memo[v] = {}; // mark visiting / fail by default

    for (int i = 0; i < sys.dim; ++i) {
        if (v == sys.state[i]) {
            LinearForm f = zeroForm(sys.dim);
            f.c[i] = 1;
            memo[v] = f;
            return f;
        }
    }

    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        if (ci->value_ == 0) {
            LinearForm f = zeroForm(sys.dim);
            memo[v] = f;
            return f;
        }
        return std::nullopt;
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return std::nullopt;

    // LCSSA / single-incoming phi of a state value.
    if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
        if (phi->num_ops_ != 2)
            return std::nullopt;
        auto inner =
            expressInState(phi->get_operand(0), sys, blocks, memo);
        if (inner)
            memo[v] = *inner;
        return inner;
    }

    auto *bin = dynamic_cast<BinaryInst *>(inst);
    if (!bin)
        return std::nullopt;

    if (bin->is_add()) {
        auto l = expressInState(bin->get_operand(0), sys, blocks, memo);
        auto r = expressInState(bin->get_operand(1), sys, blocks, memo);
        if (!l || !r)
            return std::nullopt;
        LinearForm f = addForms(*l, *r);
        memo[v] = f;
        return f;
    }
    if (bin->is_sub()) {
        auto l = expressInState(bin->get_operand(0), sys, blocks, memo);
        auto r = expressInState(bin->get_operand(1), sys, blocks, memo);
        if (!l || !r)
            return std::nullopt;
        LinearForm f = addForms(*l, scaleForm(*r, -1));
        memo[v] = f;
        return f;
    }
    if (bin->is_mul()) {
        auto *c0 = dynamic_cast<ConstantInt *>(bin->get_operand(0));
        auto *c1 = dynamic_cast<ConstantInt *>(bin->get_operand(1));
        if (c0 && !c1) {
            auto r = expressInState(bin->get_operand(1), sys, blocks, memo);
            if (!r)
                return std::nullopt;
            LinearForm f = scaleForm(*r, static_cast<int32_t>(c0->value_));
            memo[v] = f;
            return f;
        }
        if (c1 && !c0) {
            auto l = expressInState(bin->get_operand(0), sys, blocks, memo);
            if (!l)
                return std::nullopt;
            LinearForm f = scaleForm(*l, static_cast<int32_t>(c1->value_));
            memo[v] = f;
            return f;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool buildMatrix(System &sys, Loop &loop, BasicBlock *latch) {
    std::map<Value *, LinearForm> memo;
    for (int i = 0; i < sys.dim; ++i) {
        PhiInst *phi = sys.state[i];
        Value *latchVal = nullptr;
        Value *preVal = nullptr;
        for (unsigned oi = 0; oi < phi->num_ops_; oi += 2) {
            auto *bb = static_cast<BasicBlock *>(phi->get_operand(oi + 1));
            if (bb == loop.preheader)
                preVal = phi->get_operand(oi);
            else if (bb == latch)
                latchVal = phi->get_operand(oi);
        }
        if (!preVal || !latchVal)
            return reject("missing phi incomings");
        if (!isLoopInvariant(preVal, loop.blocks))
            return reject("non-invariant state init");
        sys.x0[i] = preVal;

        auto form = expressInState(latchVal, sys, loop.blocks, memo);
        if (!form)
            return reject("latch update not linear in state");
        for (int j = 0; j < sys.dim; ++j)
            sys.A[i][j] = form->c[j];
    }
    return true;
}

// c^T A == λ c^T ?
bool findLeftEigen(const System &sys, int32_t &lambdaOut) {
    const int d = sys.dim;
    uint32_t v[kMaxDim]{};
    for (int j = 0; j < d; ++j) {
        uint32_t sum = 0;
        for (int i = 0; i < d; ++i)
            sum = addu(sum, mulu(static_cast<uint32_t>(sys.live.c[i]),
                                 static_cast<uint32_t>(sys.A[i][j])));
        v[j] = sum;
    }

    bool found = false;
    uint32_t lambda = 0;
    for (int j = 0; j < d; ++j) {
        uint32_t cj = static_cast<uint32_t>(sys.live.c[j]);
        if (cj == 0) {
            if (v[j] != 0)
                return false;
            continue;
        }
        uint32_t inv = 0;
        if (!modInverseOdd(cj, inv)) {
            // c_j even: try exact match against an already chosen lambda later.
            continue;
        }
        uint32_t cand = mulu(v[j], inv);
        if (!found) {
            lambda = cand;
            found = true;
        } else if (cand != lambda) {
            return false;
        }
    }
    if (!found) {
        // All c_j even or zero — try lambda from first non-zero c via int try:
        // require v[j] == 0 for c[j]==0, and for others v = λc with same λ
        // by checking pairwise: v[j]*c[k] == v[k]*c[j].
        int base = -1;
        for (int j = 0; j < d; ++j) {
            if (sys.live.c[j] != 0) {
                base = j;
                break;
            }
        }
        if (base < 0)
            return false; // c == 0
        for (int j = 0; j < d; ++j) {
            uint32_t cj = static_cast<uint32_t>(sys.live.c[j]);
            uint32_t cb = static_cast<uint32_t>(sys.live.c[base]);
            if (mulu(v[j], cb) != mulu(v[base], cj))
                return false;
        }
        // λ * c_base = v_base; if c_base has no inverse we cannot uniquely
        // recover λ as a ring element for emitting λ^n — bail to matpow.
        return false;
    }

    for (int j = 0; j < d; ++j) {
        uint32_t cj = static_cast<uint32_t>(sys.live.c[j]);
        if (mulu(lambda, cj) != v[j])
            return false;
    }
    lambdaOut = static_cast<int32_t>(lambda);
    return true;
}

Value *emitNonNegTripManual(Module *module, BasicBlock *bb, Value *bound) {
    auto *i32 = module->int32_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, bound, zero, bb, true);
    auto *sel = new SelectInst(cmp, zero, bound, i32);
    bb->add_instruction_before_terminator(cmp);
    bb->add_instruction_before_terminator(sel);
    return sel;
}

Value *emitDotManual(Module *module, BasicBlock *bb, const System &sys) {
    auto *i32 = module->int32_ty_;
    Value *acc = nullptr;
    for (int i = 0; i < sys.dim; ++i) {
        Value *term = sys.x0[i];
        int32_t coef = sys.live.c[i];
        if (coef == 0)
            continue;
        if (coef != 1) {
            auto *k = new ConstantInt(i32, coef);
            auto *mul =
                new BinaryInst(i32, Instruction::Mul, term, k, bb, true);
            bb->add_instruction_before_terminator(mul);
            term = mul;
        }
        if (!acc) {
            acc = term;
        } else {
            auto *add =
                new BinaryInst(i32, Instruction::Add, acc, term, bb, true);
            bb->add_instruction_before_terminator(add);
            acc = add;
        }
    }
    if (!acc)
        return new ConstantInt(i32, 0);
    return acc;
}

// λ^n * s0. Special-case λ==2 with guarded shl.
Value *emitScalePow(Module *module, BasicBlock *bb, Value *s0, int32_t lambda,
                    Value *n) {
    auto *i32 = module->int32_ty_;
    if (lambda == 0) {
        // 0^n: 0 for n>0, 1 for n==0 — then * s0. Rare; use select.
        auto *zero = new ConstantInt(i32, 0);
        auto *one = new ConstantInt(i32, 1);
        auto *isZero = new ICmpInst(ICmpInst::ICMP_EQ, n, zero, bb, true);
        auto *pow = new SelectInst(isZero, one, zero, i32);
        bb->add_instruction_before_terminator(isZero);
        bb->add_instruction_before_terminator(pow);
        auto *mul = new BinaryInst(i32, Instruction::Mul, pow, s0, bb, true);
        bb->add_instruction_before_terminator(mul);
        return mul;
    }
    if (lambda == 1)
        return s0;
    if (lambda == 2) {
        // (n >= 32) ? 0 : (s0 << n)   — logical shift amount for i32
        auto *c32 = new ConstantInt(i32, 32);
        auto *zero = new ConstantInt(i32, 0);
        auto *ge = new ICmpInst(ICmpInst::ICMP_UGE, n, c32, bb, true);
        auto *sh = new BinaryInst(i32, Instruction::Shl, s0, n, bb, true);
        auto *sel = new SelectInst(ge, zero, sh, i32);
        bb->add_instruction_before_terminator(ge);
        bb->add_instruction_before_terminator(sh);
        bb->add_instruction_before_terminator(sel);
        return sel;
    }
    if (lambda == -1) {
        // (-1)^n * s0 = (n&1)? -s0 : s0
        auto *one = new ConstantInt(i32, 1);
        auto *bit = new BinaryInst(i32, Instruction::And, n, one, bb, true);
        auto *zero = new ConstantInt(i32, 0);
        auto *odd = new ICmpInst(ICmpInst::ICMP_NE, bit, zero, bb, true);
        auto *neg = new BinaryInst(i32, Instruction::Sub, zero, s0, bb, true);
        auto *sel = new SelectInst(odd, neg, s0, i32);
        bb->add_instruction_before_terminator(bit);
        bb->add_instruction_before_terminator(odd);
        bb->add_instruction_before_terminator(neg);
        bb->add_instruction_before_terminator(sel);
        return sel;
    }

    // General: binary exponentiation for λ^n, then * s0. Straight-line 32 steps.
    Value *base = new ConstantInt(i32, lambda);
    Value *res = new ConstantInt(i32, 1);
    Value *exp = n;
    for (int bit = 0; bit < 32; ++bit) {
        auto *one = new ConstantInt(i32, 1);
        auto *zero = new ConstantInt(i32, 0);
        auto *masked = new BinaryInst(i32, Instruction::And, exp, one, bb, true);
        auto *isOdd = new ICmpInst(ICmpInst::ICMP_NE, masked, zero, bb, true);
        auto *resMul =
            new BinaryInst(i32, Instruction::Mul, res, base, bb, true);
        auto *resNext = new SelectInst(isOdd, resMul, res, i32);
        auto *baseSq =
            new BinaryInst(i32, Instruction::Mul, base, base, bb, true);
        auto *expShr =
            new BinaryInst(i32, Instruction::LShr, exp, one, bb, true);
        bb->add_instruction_before_terminator(masked);
        bb->add_instruction_before_terminator(isOdd);
        bb->add_instruction_before_terminator(resMul);
        bb->add_instruction_before_terminator(resNext);
        bb->add_instruction_before_terminator(baseSq);
        bb->add_instruction_before_terminator(expShr);
        res = resNext;
        base = baseSq;
        exp = expShr;
    }
    auto *out = new BinaryInst(i32, Instruction::Mul, res, s0, bb, true);
    bb->add_instruction_before_terminator(out);
    return out;
}

struct MatPowLoopEmit {
    Value *result = nullptr;
    BasicBlock *header = nullptr; // preheader should branch here
    BasicBlock *after = nullptr;  // defines result; already branches to exit
};

// Emit y = A^n x0 via a compact binary-exponentiation loop, then result = c·y.
// CFG: PH -> header <-> body; header -> after -> exit.
MatPowLoopEmit emitMatPowLoop(Module *module, Function *func, BasicBlock *PH,
                              const System &sys, Value *n, BasicBlock *exit) {
    MatPowLoopEmit out;
    auto *i32 = module->int32_ty_;
    const int d = sys.dim;
    std::string baseName =
        (sys.iv && sys.iv->parent_) ? sys.iv->parent_->name_ : "linrec";

    auto *header =
        new BasicBlock(module, baseName + ".matpow.header", func);
    auto *body = new BasicBlock(module, baseName + ".matpow.body", func);
    auto *after =
        new BasicBlock(module, baseName + ".matpow.after", func);
    out.header = header;
    out.after = after;

    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);

    // header phis: n, y[d], M[d][d]
    auto *nPhi = PhiInst::create_phi(i32, header);
    header->add_instruction(nPhi);
    nPhi->add_phi_pair_operand(n, PH);

    PhiInst *yPhi[kMaxDim]{};
    for (int i = 0; i < d; ++i) {
        yPhi[i] = PhiInst::create_phi(i32, header);
        header->add_instruction(yPhi[i]);
        yPhi[i]->add_phi_pair_operand(sys.x0[i], PH);
    }

    PhiInst *mPhi[kMaxDim][kMaxDim]{};
    for (int i = 0; i < d; ++i) {
        for (int j = 0; j < d; ++j) {
            mPhi[i][j] = PhiInst::create_phi(i32, header);
            header->add_instruction(mPhi[i][j]);
            mPhi[i][j]->add_phi_pair_operand(new ConstantInt(i32, sys.A[i][j]),
                                             PH);
        }
    }

    auto *cont = new ICmpInst(ICmpInst::ICMP_NE, nPhi, zero, header);
    new BranchInst(cont, body, after, header);

    // body: y = (n&1) ? M*y : y; M = M*M; n >>= 1
    auto *masked = new BinaryInst(i32, Instruction::And, nPhi, one, body);
    auto *isOdd = new ICmpInst(ICmpInst::ICMP_NE, masked, zero, body);

    Value *My[kMaxDim];
    for (int i = 0; i < d; ++i) {
        Value *acc = nullptr;
        for (int j = 0; j < d; ++j) {
            auto *mul =
                new BinaryInst(i32, Instruction::Mul, mPhi[i][j], yPhi[j], body);
            if (!acc)
                acc = mul;
            else
                acc = new BinaryInst(i32, Instruction::Add, acc, mul, body);
        }
        My[i] = acc ? acc : static_cast<Value *>(zero);
    }
    Value *yNext[kMaxDim];
    for (int i = 0; i < d; ++i)
        yNext[i] = new SelectInst(isOdd, My[i], yPhi[i], body);

    Value *MM[kMaxDim][kMaxDim];
    for (int i = 0; i < d; ++i) {
        for (int j = 0; j < d; ++j) {
            Value *acc = nullptr;
            for (int k = 0; k < d; ++k) {
                auto *mul = new BinaryInst(i32, Instruction::Mul, mPhi[i][k],
                                           mPhi[k][j], body);
                if (!acc)
                    acc = mul;
                else
                    acc = new BinaryInst(i32, Instruction::Add, acc, mul, body);
            }
            MM[i][j] = acc ? acc : static_cast<Value *>(zero);
        }
    }
    auto *nNext = new BinaryInst(i32, Instruction::LShr, nPhi, one, body);
    new BranchInst(header, body);

    nPhi->add_phi_pair_operand(nNext, body);
    for (int i = 0; i < d; ++i)
        yPhi[i]->add_phi_pair_operand(yNext[i], body);
    for (int i = 0; i < d; ++i)
        for (int j = 0; j < d; ++j)
            mPhi[i][j]->add_phi_pair_operand(MM[i][j], body);

    // after: result = c·y
    Value *acc = nullptr;
    for (int i = 0; i < d; ++i) {
        if (sys.live.c[i] == 0)
            continue;
        Value *term = yPhi[i];
        if (sys.live.c[i] != 1) {
            auto *k = new ConstantInt(i32, sys.live.c[i]);
            term = new BinaryInst(i32, Instruction::Mul, term, k, after);
        }
        if (!acc)
            acc = term;
        else
            acc = new BinaryInst(i32, Instruction::Add, acc, term, after);
    }
    out.result = acc ? acc : static_cast<Value *>(new ConstantInt(i32, 0));
    new BranchInst(exit, after);

    return out;
}

bool collectLiveOut(System &sys, Loop &loop) {
    // All outside uses of state (or of values only defined from state) must
    // collapse to a single linear form root.
    std::map<Value *, LinearForm> memo;
    std::set<Instruction *> linearInsts;
    std::vector<Instruction *> candidates;

    auto tryMark = [&](Value *v) -> bool {
        auto f = expressInState(v, sys, loop.blocks, memo);
        if (!f)
            return false;
        if (auto *inst = dynamic_cast<Instruction *>(v))
            linearInsts.insert(inst);
        return true;
    };

    // Seed: outside users of state PHIs.
    for (int i = 0; i < sys.dim; ++i) {
        for (auto &use : sys.state[i]->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_)
                continue;
            if (loop.blocks.count(user->parent_))
                continue;
            if (!tryMark(user))
                return reject("outside use not linear");
            candidates.push_back(user);
        }
    }

    // Also allow linear ops in exit that combine LCSSA — already covered if
    // LCSSA is the user of state.

    // Grow: if a linear inst's user is still a linear extension, mark it.
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Instruction *> snap(linearInsts.begin(), linearInsts.end());
        for (auto *inst : snap) {
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || !user->parent_ || loop.blocks.count(user->parent_))
                    continue;
                if (linearInsts.count(user))
                    continue;
                if (!tryMark(user))
                    continue;
                candidates.push_back(user);
                changed = true;
            }
        }
    }

    // Escaping roots: linear values used by a non-linear outside user
    // (call/ret/store/icmp/...) or used outside exit in a non-linear way.
    std::vector<Instruction *> roots;
    for (auto *inst : linearInsts) {
        bool escapes = false;
        for (auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user)
                continue;
            if (linearInsts.count(user))
                continue;
            if (loop.blocks.count(user->parent_))
                return reject("linear value used back in loop");
            escapes = true;
            break;
        }
        if (escapes)
            roots.push_back(inst);
    }

    if (roots.size() != 1)
        return reject("expected exactly one escaping linear live-out");

    sys.liveRoot = roots[0];
    auto live = expressInState(sys.liveRoot, sys, loop.blocks, memo);
    if (!live)
        return reject("live root not linear");
    sys.live = *live;

    // No other loop values may escape (IV, intermediates).
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst == sys.iv)
                continue;
            bool isState = false;
            for (int i = 0; i < sys.dim; ++i)
                if (inst == sys.state[i])
                    isState = true;
            if (isState)
                continue;
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_)) {
                    // Allowed if this inst is part of the linear tree feeding
                    // liveRoot (expressible and in linearInsts — but inst is
                    // inside loop). Latch updates used only by state PHIs OK.
                    if (!user->is_phi())
                        return reject("non-state loop value escapes");
                }
            }
        }
    }
    return true;
}

bool rewriteAndDeleteLoop(Loop &loop, Module *module, Value *folded,
                          Instruction *liveRoot, BasicBlock *newTarget,
                          BasicBlock *exitPred) {
    BasicBlock *PH = loop.preheader;
    BasicBlock *exit = loop.singleExit();
    if (!PH || !exit)
        return false;

    auto *preheaderBr = PH->get_terminator();
    if (!preheaderBr || !preheaderBr->is_br())
        return reject("bad preheader terminator");
    if (preheaderBr->num_ops_ != 1)
        return reject("preheader not unconditional");
    if (preheaderBr->get_operand(0) != loop.header)
        return reject("preheader does not target header");

    // Replace all uses of liveRoot with folded, then drop the dead sum tree.
    std::vector<std::pair<Instruction *, unsigned>> uses;
    for (auto &use : liveRoot->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user)
            uses.push_back({user, use.arg_no_});
    }
    for (auto &[user, arg] : uses)
        user->set_operand(arg, folded);

    if (liveRoot->parent_ && liveRoot->use_list_.empty())
        liveRoot->parent_->delete_instr(liveRoot);

    // Drop dead arithmetic left in the exit (former sum tree / LCSSA feeds).
    bool cleaned = true;
    while (cleaned) {
        cleaned = false;
        std::vector<Instruction *> snap(exit->instr_list_.begin(),
                                        exit->instr_list_.end());
        for (auto *inst : snap) {
            if (inst->is_phi() || inst->is_ret() || inst->is_br() ||
                inst->is_store() || inst->is_call())
                continue;
            if (!inst->use_list_.empty())
                continue;
            exit->delete_instr(inst);
            cleaned = true;
        }
    }

    BasicBlock *phTarget = newTarget ? newTarget : exit;
    BasicBlock *arrive = exitPred ? exitPred : PH;

    preheaderBr->set_operand(0, phTarget);
    PH->remove_succ_basic_block(loop.header);
    PH->add_succ_basic_block(phTarget);
    loop.header->remove_pre_basic_block(PH);
    if (phTarget != exit)
        phTarget->add_pre_basic_block(PH);
    else
        exit->add_pre_basic_block(PH);

    // Fix / remove exit phis that referenced the deleted loop.
    std::vector<Instruction *> exitPhis;
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi())
            break;
        exitPhis.push_back(inst);
    }
    for (auto *inst : exitPhis) {
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops_;) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == loop.header || loop.blocks.count(pred)) {
                phi->remove_operands(i, i + 1);
                continue;
            }
            i += 2;
        }
        bool hasArrive = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == arrive)
                hasArrive = true;
        }
        if (!hasArrive && !phi->use_list_.empty()) {
            auto *zero = new ConstantInt(module->int32_ty_, 0);
            phi->add_phi_pair_operand(zero, arrive);
        }
        if (phi->use_list_.empty())
            exit->delete_instr(phi);
    }

    Function *func = loop.header->parent_;
    std::vector<BasicBlock *> dead(loop.blocksOrdered.begin(),
                                   loop.blocksOrdered.end());
    for (auto *bb : dead)
        func->remove_bb(bb);
    return true;
}

} // namespace

bool LinearRecurrenceFold::tryFold(Loop &loop, Module *module) {
    if (!loop.preheader)
        return reject("no preheader");
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!latch || !exit)
        return reject("need single latch/exit");
    if (loop.depth != 0 && !loop.children.empty()) {
        // Still allow innermost; skip if has children.
    }
    if (!loop.children.empty())
        return reject("not innermost");

    // Side-effect free.
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store() || inst->is_call())
                return reject("side effects");
        }
    }

    // Header: IV + state phis, then icmp slt + br.
    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3)
        return reject("bad header terminator");
    auto *cond = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cond || cond->icmp_op_ != ICmpInst::ICMP_SLT)
        return reject("need slt guard");

    std::vector<PhiInst *> phis;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi())
            break;
        phis.push_back(static_cast<PhiInst *>(inst));
    }
    if (phis.size() < 2 || phis.size() > static_cast<size_t>(kMaxDim + 1))
        return reject("phi count");

    // Identify counting IV: init 0, step +1.
    PhiInst *iv = nullptr;
    Value *bound = nullptr;
    for (auto *phi : phis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        if (phi->num_ops_ != 4)
            continue;
        Value *pre = nullptr, *lat = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *bb = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (bb == loop.preheader)
                pre = phi->get_operand(i);
            else if (bb == latch)
                lat = phi->get_operand(i);
        }
        auto *ci = dynamic_cast<ConstantInt *>(pre);
        if (!ci || ci->value_ != 0)
            continue;
        auto *add = dynamic_cast<BinaryInst *>(lat);
        if (!add || !add->is_add())
            continue;
        Value *a = add->get_operand(0), *b = add->get_operand(1);
        ConstantInt *step = nullptr;
        if (a == phi)
            step = dynamic_cast<ConstantInt *>(b);
        else if (b == phi)
            step = dynamic_cast<ConstantInt *>(a);
        if (!step || step->value_ != 1)
            continue;
        if (cond->get_operand(0) != phi)
            continue;
        if (!isLoopInvariant(cond->get_operand(1), loop.blocks))
            continue;
        iv = phi;
        bound = cond->get_operand(1);
        break;
    }
    if (!iv)
        return reject("no unit IV");

    System sys;
    sys.iv = iv;
    sys.bound = bound;
    for (auto *phi : phis) {
        if (phi == iv)
            continue;
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops_ != 4)
            return reject("bad state phi");
        sys.state.push_back(phi);
    }
    sys.dim = static_cast<int>(sys.state.size());
    if (sys.dim < 2 || sys.dim > kMaxDim)
        return reject("state dim");
    sys.x0.resize(sys.dim);

    // True successor must be in loop, false = exit.
    auto *body = static_cast<BasicBlock *>(term->get_operand(1));
    auto *falseBB = static_cast<BasicBlock *>(term->get_operand(2));
    if (!loop.blocks.count(body) || falseBB != exit)
        return reject("header successors");

    if (!buildMatrix(sys, loop, latch))
        return false;
    if (!collectLiveOut(sys, loop))
        return false;

    if (debugEnabled()) {
        std::cerr << "[LinearRecurrenceFold] hit func="
                  << loop.header->parent_->name_
                  << " header=" << loop.header->name_ << " dim=" << sys.dim
                  << "\n  A=\n";
        for (int i = 0; i < sys.dim; ++i) {
            std::cerr << "   ";
            for (int j = 0; j < sys.dim; ++j)
                std::cerr << sys.A[i][j] << ' ';
            std::cerr << "\n";
        }
        std::cerr << "  c=";
        for (int i = 0; i < sys.dim; ++i)
            std::cerr << sys.live.c[i] << ' ';
        std::cerr << "\n";
    }

    BasicBlock *PH = loop.preheader;
    Value *trip = emitNonNegTripManual(module, PH, bound);
    Value *folded = nullptr;
    BasicBlock *newTarget = nullptr;
    BasicBlock *exitPred = nullptr;

    int32_t lambda = 0;
    if (findLeftEigen(sys, lambda)) {
        if (debugEnabled())
            std::cerr << "[LinearRecurrenceFold] left-eigen lambda=" << lambda
                      << "\n";
        Value *s0 = emitDotManual(module, PH, sys);
        folded = emitScalePow(module, PH, s0, lambda, trip);
        // PH computes result then jumps straight to exit.
    } else {
        if (debugEnabled())
            std::cerr << "[LinearRecurrenceFold] matpow loop path\n";
        MatPowLoopEmit mp = emitMatPowLoop(module, PH->parent_, PH, sys, trip,
                                           exit);
        folded = mp.result;
        newTarget = mp.header;
        exitPred = mp.after;
    }

    if (!rewriteAndDeleteLoop(loop, module, folded, sys.liveRoot, newTarget,
                              exitPred))
        return false;

    if (debugEnabled())
        std::cerr << "[LinearRecurrenceFold] folded "
                  << loop.header->parent_->name_ << "\n";
    return true;
}

void LinearRecurrenceFold::runOnFunction(Function *func, AnalysisManager *AM) {
    if (!func || func->is_declaration() || !AM)
        return;
    bool changed = true;
    while (changed) {
        changed = false;
        LoopInfo &LI = AM->getLoopInfo(func);
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        // Prefer smaller / inner loops first.
        std::sort(loops.begin(), loops.end(), [](Loop *a, Loop *b) {
            return a->blocks.size() < b->blocks.size();
        });
        for (auto *loop : loops) {
            if (tryFold(*loop, func->parent_)) {
                changed = true;
                AM->clear(func);
                break;
            }
        }
    }
}

void LinearRecurrenceFold::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LinearRecurrenceFold::execute(Module *module,
                                                AnalysisManager &AM) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, &AM);
    }
    return PreservedAnalyses::none();
}
