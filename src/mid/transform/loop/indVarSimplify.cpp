#include "../../../include/mid/opt/indVarSimplify.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/rangeAnalysis.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace {

struct InductionMatch {
    PhiInst *phi = nullptr;
    BinaryInst *next = nullptr;
    ICmpInst *cmp = nullptr;
    BasicBlock *latch = nullptr;
    Value *start = nullptr;
    Value *bound = nullptr;
    long long stride = 0;
    Value *strideValue = nullptr;
    bool strideIsConstant = true;
    ICmpInst::ICmpOp pred = ICmpInst::ICMP_SLT;
    Value *exitExpr = nullptr;
    Value *exitBound = nullptr;
    long long exitStep = 0;
    bool alreadyCanonical = false;
};

struct LinearExpr {
    bool valid = false;
    long long coeff = 0;
};

bool debugEnabled() {
    return std::getenv("DEBUG_INDVAR_SIMPLIFY") != nullptr;
}

bool isI32(Value *v) {
    auto *ty = v ? dynamic_cast<IntegerType *>(v->type_) : nullptr;
    return ty && ty->num_bits_ == 32;
}

bool isZero(Value *v) {
    auto *c = dynamic_cast<ConstantInt *>(v);
    return c && c->value_ == 0;
}

bool getConstInt(Value *v, long long &out) {
    auto *c = dynamic_cast<ConstantInt *>(v);
    if (!c) return false;
    out = c->value_;
    return true;
}

ConstantInt *i32c(Module *m, long long v) {
    return new ConstantInt(m->int32_ty_, static_cast<int>(v));
}

bool isLoopInvariant(Value *v, const Loop &loop) {
    if (dynamic_cast<Constant *>(v)) return true;
    if (dynamic_cast<Argument *>(v)) return true;
    if (dynamic_cast<GlobalVariable *>(v)) return true;
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && !loop.blocks.count(inst->parent_);
}

ICmpInst::ICmpOp swapPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULE;
    default: return pred;
    }
}

bool addNoOverflow(long long a, long long b, long long &out);
bool multiplyNoOverflow(long long a, long long b, long long &out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a == -1 && b == std::numeric_limits<long long>::min()) return false;
    if (b == -1 && a == std::numeric_limits<long long>::min()) return false;
    if (a > 0) {
        if (b > 0 && a > std::numeric_limits<long long>::max() / b) return false;
        if (b < 0 && b < std::numeric_limits<long long>::min() / a) return false;
    } else {
        if (b > 0 && a < std::numeric_limits<long long>::min() / b) return false;
        if (b < 0 && a != 0 && b < std::numeric_limits<long long>::max() / a) return false;
    }
    out = a * b;
    return true;
}

bool analyzeLinear(Value *v, PhiInst *phi, const Loop &loop, LinearExpr &out) {
    if (!v || !isI32(v)) return false;
    if (v == phi) {
        out = {true, 1};
        return true;
    }
    if (isLoopInvariant(v, loop)) {
        out = {true, 0};
        return true;
    }

    auto *bin = dynamic_cast<BinaryInst *>(v);
    if (!bin || !bin->parent_ || !loop.blocks.count(bin->parent_) ||
        bin->type_->tid_ != Type::IntegerTyID)
        return false;

    LinearExpr lhs, rhs;
    switch (bin->op_id_) {
    case Instruction::Add:
        if (!analyzeLinear(bin->get_operand(0), phi, loop, lhs) ||
            !analyzeLinear(bin->get_operand(1), phi, loop, rhs))
            return false;
        out.valid = true;
        return addNoOverflow(lhs.coeff, rhs.coeff, out.coeff);
    case Instruction::Sub:
        if (!analyzeLinear(bin->get_operand(0), phi, loop, lhs) ||
            !analyzeLinear(bin->get_operand(1), phi, loop, rhs))
            return false;
        out.valid = true;
        return addNoOverflow(lhs.coeff, -rhs.coeff, out.coeff);
    case Instruction::Mul: {
        long long c = 0;
        if (getConstInt(bin->get_operand(0), c) &&
            analyzeLinear(bin->get_operand(1), phi, loop, rhs)) {
            out.valid = true;
            return multiplyNoOverflow(c, rhs.coeff, out.coeff);
        }
        if (getConstInt(bin->get_operand(1), c) &&
            analyzeLinear(bin->get_operand(0), phi, loop, lhs)) {
            out.valid = true;
            return multiplyNoOverflow(c, lhs.coeff, out.coeff);
        }
        return false;
    }
    case Instruction::Shl: {
        long long shift = 0;
        if (!getConstInt(bin->get_operand(1), shift) || shift < 0 ||
            shift >= 31 ||
            !analyzeLinear(bin->get_operand(0), phi, loop, lhs))
            return false;
        out.valid = true;
        return multiplyNoOverflow(1LL << shift, lhs.coeff, out.coeff);
    }
    default:
        return false;
    }
}

bool isSupportedDerived(Value *v, PhiInst *phi, const Loop &loop) {
    LinearExpr expr;
    return analyzeLinear(v, phi, loop, expr);
}

bool getIncoming(PhiInst *phi, const Loop &loop, Value *&start,
                 Value *&latchValue) {
    start = nullptr;
    latchValue = nullptr;
    if (!phi || phi->num_ops_ != 4 || !loop.preheader) return false;

    BasicBlock *latch = loop.singleLatch();
    if (!latch) return false;

    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (pred == loop.preheader)
            start = phi->get_operand(i);
        else if (pred == latch)
            latchValue = phi->get_operand(i);
        else
            return false;
    }
    return start && latchValue;
}

bool matchStride(Value *value, PhiInst *phi, const Loop &loop,
                 BinaryInst *&update, Value *&strideValue,
                 long long &stride, bool &strideIsConstant) {
    update = dynamic_cast<BinaryInst *>(value);
    BasicBlock *latch = loop.singleLatch();
    if (!update || update->parent_ != latch) return false;

    Value *lhs = update->get_operand(0);
    Value *rhs = update->get_operand(1);

    auto bindStride = [&](Value *value, bool negate) {
        if (!isI32(value) || !isLoopInvariant(value, loop)) return false;
        auto *constant = dynamic_cast<ConstantInt *>(value);
        if (constant) {
            stride = negate ? -static_cast<long long>(constant->value_)
                            : static_cast<long long>(constant->value_);
            if (stride == 0) return false;
            strideValue = value;
            strideIsConstant = true;
            return true;
        }
        if (negate) return false;
        stride = 0;
        strideValue = value;
        strideIsConstant = false;
        return true;
    };

    if (update->is_add()) {
        if (lhs == phi) return bindStride(rhs, false);
        if (rhs == phi) return bindStride(lhs, false);
    }

    if (update->is_sub() && lhs == phi)
        return bindStride(rhs, true);

    return false;
}

bool matchHeaderGuard(const Loop &loop, PhiInst *phi,
                      InductionMatch &match,
                      ICmpInst *&cmp, Value *&bound,
                      ICmpInst::ICmpOp &pred) {
    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;

    cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->parent_ != loop.header)
        return false;

    auto *trueBB = dynamic_cast<BasicBlock *>(term->get_operand(1));
    auto *falseBB = dynamic_cast<BasicBlock *>(term->get_operand(2));
    if (!trueBB || !falseBB || !loop.blocks.count(trueBB) ||
        loop.blocks.count(falseBB))
        return false;

    Value *lhs = cmp->get_operand(0);
    Value *rhs = cmp->get_operand(1);
    LinearExpr lhsExpr, rhsExpr;
    bool lhsLinear = analyzeLinear(lhs, phi, loop, lhsExpr) && lhsExpr.coeff != 0;
    bool rhsLinear = analyzeLinear(rhs, phi, loop, rhsExpr) && rhsExpr.coeff != 0;

    Value *exitExpr = nullptr;
    if (lhsLinear && !rhsLinear && isLoopInvariant(rhs, loop)) {
        pred = cmp->icmp_op_;
        bound = rhs;
        exitExpr = lhs;
    } else if (rhsLinear && !lhsLinear && isLoopInvariant(lhs, loop)) {
        pred = swapPredicate(cmp->icmp_op_);
        bound = lhs;
        exitExpr = rhs;
        lhsExpr = rhsExpr;
    } else {
        return false;
    }

    if (!match.strideIsConstant) {
        if (exitExpr != phi || lhsExpr.coeff != 1 ||
            (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE))
            return false;
        match.exitExpr = exitExpr;
        match.exitBound = bound;
        match.exitStep = 0;
        return bound && isI32(bound);
    }

    long long exitStep = 0;
    if (!multiplyNoOverflow(lhsExpr.coeff, match.stride, exitStep) ||
        exitStep == 0)
        return false;

    if (exitStep > 0) {
        if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            return false;
    } else {
        if (pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            return false;
    }

    match.exitExpr = exitExpr;
    match.exitBound = bound;
    match.exitStep = exitStep;
    return bound && isI32(bound);
}

bool hasOnlyTerminatorUse(ICmpInst *cmp, BasicBlock *header) {
    auto *term = header ? header->get_terminator() : nullptr;
    if (!cmp || !term) return false;
    for (auto &use : cmp->use_list_) {
        if (use.val_ != term) return false;
    }
    return true;
}

bool usesAreReplaceable(const InductionMatch &match, const Loop &loop) {
    if (!hasOnlyTerminatorUse(match.cmp, loop.header)) return false;

    for (auto &use : match.phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user) return false;
        if (user == match.next || user == match.cmp) continue;
        if (user->parent_ && loop.blocks.count(user->parent_) && user->is_phi())
            return false;
        if (user->parent_ && loop.blocks.count(user->parent_) &&
            !isSupportedDerived(user, match.phi, loop)) {
            for (auto &nestedUse : user->use_list_) {
                auto *nestedUser = dynamic_cast<Instruction *>(nestedUse.val_);
                if (!nestedUser || !nestedUser->parent_ ||
                    !loop.blocks.count(nestedUser->parent_))
                    return false;
            }
        }
    }

    for (auto &use : match.next->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user) return false;
        if (user == match.phi) continue;
        return false;
    }

    return true;
}

RangeAnalysis::IntRange rangeOrI32(RangeAnalysis &RA, Value *v, BasicBlock *ctx) {
    auto r = RA.getRange(v, ctx);
    if (r.valid && !r.isBottom && !r.isTop) return r;
    if (isI32(v))
        return RangeAnalysis::IntRange::bounded(INT_MIN, INT_MAX);
    return RangeAnalysis::IntRange::top();
}

bool addNoOverflow(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b))
        return false;
    out = a + b;
    return true;
}

bool containsOuterCanonicalAddRec(const SCEV *s, const Loop &loop) {
    if (!s) return false;
    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s)) {
        auto *addrecLoop = addrec->loop();
        return addrecLoop && addrecLoop != &loop &&
               addrecLoop->hasCanonicalIV() &&
               addrec->phi() == addrecLoop->canonicalIV;
    }
    if (auto *nary = dynamic_cast<const SCEVNAryExpr *>(s)) {
        for (auto *op : nary->operands())
            if (containsOuterCanonicalAddRec(op, loop)) return true;
    }
    return false;
}

bool proveArithmeticSafe(const InductionMatch &match, RangeAnalysis &RA,
                         ScalarEvolution &SE, const Loop &loop,
                         BasicBlock *ctx) {
    if (!match.strideIsConstant)
        return match.pred == ICmpInst::ICMP_SLT ||
               match.pred == ICmpInst::ICMP_SLE;

    auto sr = rangeOrI32(RA, match.start, ctx);
    auto br = rangeOrI32(RA, match.bound, ctx);
    if (!sr.valid || !br.valid || sr.isTop || br.isTop || sr.isBottom ||
        br.isBottom)
        return false;

    long long absStride = match.stride > 0 ? match.stride : -match.stride;
    if (absStride <= 0 || absStride > INT_MAX) return false;

    bool strict = match.pred == ICmpInst::ICMP_SLT ||
                  match.pred == ICmpInst::ICMP_SGT;
    long long bias = strict ? absStride - 1 : 0;
    bool boundFromOuterCanonical =
        containsOuterCanonicalAddRec(SE.getSCEV(match.bound), loop);

    long long maxDelta = 0;
    long long maxFinalOffset = 0;
    if (match.stride > 0) {
        if (!addNoOverflow(br.upper, -sr.lower, maxDelta)) return false;
        if (maxDelta < 0) maxDelta = 0;
        if (maxDelta > static_cast<long long>(INT_MAX) - bias) return false;
        long long maxTrip = strict ? (maxDelta + absStride - 1) / absStride
                                   : (maxDelta / absStride) + 1;
        maxFinalOffset = maxTrip * absStride;
        if (sr.upper > static_cast<long long>(INT_MAX) - maxFinalOffset &&
            !boundFromOuterCanonical)
            return false;
    } else {
        if (!addNoOverflow(sr.upper, -br.lower, maxDelta)) return false;
        if (maxDelta < 0) maxDelta = 0;
        if (maxDelta > static_cast<long long>(INT_MAX) - bias) return false;
        long long maxTrip = strict ? (maxDelta + absStride - 1) / absStride
                                   : (maxDelta / absStride) + 1;
        maxFinalOffset = maxTrip * absStride;
        if (sr.lower < static_cast<long long>(INT_MIN) + maxFinalOffset &&
            !boundFromOuterCanonical)
            return false;
    }

    return true;
}

void insertBefore(BasicBlock *bb, Instruction *before, Instruction *inst) {
    if (before)
        bb->add_instruction_before_inst(inst, before);
    else
        bb->add_instruction_before_terminator(inst);
}

Value *materializeAffine(Module *m, BasicBlock *bb, Instruction *before,
                         Value *start, Value *iv,
                         Value *strideValue, long long stride,
                         bool strideIsConstant) {
    Value *scaled = iv;
    if (!strideIsConstant) {
        auto *mul = new BinaryInst(m->int32_ty_, Instruction::Mul, iv,
                                   strideValue, bb, true);
        insertBefore(bb, before, mul);
        scaled = mul;
    } else if (stride != 1) {
        if (stride == -1) {
            auto *neg = new BinaryInst(m->int32_ty_, Instruction::Sub, i32c(m, 0),
                                       iv, bb, true);
            insertBefore(bb, before, neg);
            scaled = neg;
        } else {
            auto *mul = new BinaryInst(m->int32_ty_, Instruction::Mul, iv,
                                       i32c(m, stride), bb, true);
            insertBefore(bb, before, mul);
            scaled = mul;
        }
    }

    if (isZero(start)) return scaled;
    if (isZero(scaled)) return start;
    auto *add = new BinaryInst(m->int32_ty_, Instruction::Add, start, scaled,
                               bb, true);
    insertBefore(bb, before, add);
    return add;
}

Value *materializeWithReplacement(Module *m, BasicBlock *bb,
                                  Instruction *before, Value *expr,
                                  PhiInst *phi, Value *replacement,
                                  const Loop &loop) {
    if (expr == phi) return replacement;
    if (isLoopInvariant(expr, loop)) return expr;

    auto *bin = dynamic_cast<BinaryInst *>(expr);
    if (!bin || !bin->parent_ || !loop.blocks.count(bin->parent_))
        return nullptr;

    switch (bin->op_id_) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::Shl:
        break;
    default:
        return nullptr;
    }

    if (bin->op_id_ == Instruction::Mul) {
        long long ignored = 0;
        if (!getConstInt(bin->get_operand(0), ignored) &&
            !getConstInt(bin->get_operand(1), ignored))
            return nullptr;
    }
    if (bin->op_id_ == Instruction::Shl) {
        long long shift = 0;
        if (!getConstInt(bin->get_operand(1), shift) || shift < 0 ||
            shift >= 31)
            return nullptr;
    }

    Value *lhs = materializeWithReplacement(m, bb, before, bin->get_operand(0),
                                            phi, replacement, loop);
    Value *rhs = materializeWithReplacement(m, bb, before, bin->get_operand(1),
                                            phi, replacement, loop);
    if (!lhs || !rhs) return nullptr;

    if (bin->op_id_ == Instruction::Add && isZero(lhs)) return rhs;
    if (bin->op_id_ == Instruction::Add && isZero(rhs)) return lhs;
    if (bin->op_id_ == Instruction::Sub && isZero(rhs)) return lhs;
    if (bin->op_id_ == Instruction::Mul) {
        long long c = 0;
        if (getConstInt(lhs, c)) {
            if (c == 0) return lhs;
            if (c == 1) return rhs;
        }
        if (getConstInt(rhs, c)) {
            if (c == 0) return rhs;
            if (c == 1) return lhs;
        }
    }

    auto *clone = new BinaryInst(m->int32_ty_, bin->op_id_, lhs, rhs, bb, true);
    insertBefore(bb, before, clone);
    return clone;
}

Value *materializeSCEV(Module *m, BasicBlock *bb, Instruction *before,
                       const SCEV *s, const Loop &loop) {
    if (!s) return nullptr;

    if (auto *c = dynamic_cast<const SCEVConstant *>(s))
        return i32c(m, c->value());

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        Value *v = unknown->value();
        if (!v || !isI32(v)) return nullptr;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (inst && loop.blocks.count(inst->parent_))
            return nullptr;
        return v;
    }

    if (auto *add = dynamic_cast<const SCEVAddExpr *>(s)) {
        Value *result = i32c(m, 0);
        for (auto *op : add->operands()) {
            Value *term = materializeSCEV(m, bb, before, op, loop);
            if (!term) return nullptr;
            if (isZero(term)) continue;
            if (isZero(result)) {
                result = term;
                continue;
            }
            auto *inst = new BinaryInst(m->int32_ty_, Instruction::Add,
                                        result, term, bb, true);
            insertBefore(bb, before, inst);
            result = inst;
        }
        return result;
    }

    if (auto *mul = dynamic_cast<const SCEVMulExpr *>(s)) {
        Value *result = i32c(m, 1);
        for (auto *op : mul->operands()) {
            Value *factor = materializeSCEV(m, bb, before, op, loop);
            if (!factor) return nullptr;
            long long c = 0;
            if (getConstInt(factor, c)) {
                if (c == 0) return factor;
                if (c == 1) continue;
            }
            if (getConstInt(result, c) && c == 1) {
                result = factor;
                continue;
            }
            auto *inst = new BinaryInst(m->int32_ty_, Instruction::Mul,
                                        result, factor, bb, true);
            insertBefore(bb, before, inst);
            result = inst;
        }
        return result;
    }

    return nullptr;
}

bool canMaterializeSCEV(const SCEV *s, const Loop &loop) {
    if (!s) return false;
    if (dynamic_cast<const SCEVConstant *>(s)) return true;

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        Value *v = unknown->value();
        if (!v || !isI32(v)) return false;
        auto *inst = dynamic_cast<Instruction *>(v);
        return !inst || !loop.blocks.count(inst->parent_);
    }

    if (auto *nary = dynamic_cast<const SCEVNAryExpr *>(s)) {
        for (auto *op : nary->operands())
            if (!canMaterializeSCEV(op, loop)) return false;
        return true;
    }

    return false;
}

Value *materializeAddRecExitValue(Module *m, BasicBlock *bb,
                                  Instruction *before,
                                  const SCEVAddRecExpr *addrec,
                                  Value *trip, const Loop &loop) {
    if (!addrec || !trip) return nullptr;
    Value *start = materializeSCEV(m, bb, before, addrec->start(), loop);
    Value *step = materializeSCEV(m, bb, before, addrec->step(), loop);
    if (!start || !step) return nullptr;

    Value *scaled = nullptr;
    if (isZero(trip) || isZero(step)) {
        scaled = i32c(m, 0);
    } else {
        long long c = 0;
        if (getConstInt(step, c) && c == 1) {
            scaled = trip;
        } else {
            auto *mul = new BinaryInst(m->int32_ty_, Instruction::Mul,
                                       step, trip, bb, true);
            insertBefore(bb, before, mul);
            scaled = mul;
        }
    }

    if (isZero(start)) return scaled;
    if (isZero(scaled)) return start;
    auto *add = new BinaryInst(m->int32_ty_, Instruction::Add, start, scaled,
                               bb, true);
    insertBefore(bb, before, add);
    return add;
}

Loop *innermostChildLoopContaining(const Loop &loop, BasicBlock *bb) {
    Loop *best = nullptr;
    std::vector<Loop *> work(loop.children.begin(), loop.children.end());
    while (!work.empty()) {
        Loop *child = work.back();
        work.pop_back();
        if (!child || !child->blocks.count(bb)) continue;
        if (!best || child->depth > best->depth) best = child;
        for (auto *grandchild : child->children)
            work.push_back(grandchild);
    }
    return best;
}

Value *materializeTripCount(Module *m, BasicBlock *preheader,
                            const InductionMatch &match,
                            Value *exitStart) {
    auto *term = preheader->get_terminator();
    bool strict = match.pred == ICmpInst::ICMP_SLT ||
                  match.pred == ICmpInst::ICMP_SGT;

    ICmpInst::ICmpOp noIterPred;
    if (match.pred == ICmpInst::ICMP_SLT)
        noIterPred = ICmpInst::ICMP_SGE;
    else if (match.pred == ICmpInst::ICMP_SLE)
        noIterPred = ICmpInst::ICMP_SGT;
    else if (match.pred == ICmpInst::ICMP_SGT)
        noIterPred = ICmpInst::ICMP_SLE;
    else
        noIterPred = ICmpInst::ICMP_SLT;

    Value *bound = match.exitBound ? match.exitBound : match.bound;
    auto *noIter = new ICmpInst(noIterPred, exitStart, bound, preheader,
                                true);
    preheader->add_instruction_before_inst(noIter, term);

    Value *delta = nullptr;
    if (!match.strideIsConstant || match.exitStep > 0)
        delta = new BinaryInst(m->int32_ty_, Instruction::Sub, bound,
                               exitStart, preheader, true);
    else
        delta = new BinaryInst(m->int32_ty_, Instruction::Sub, exitStart,
                               bound, preheader, true);
    preheader->add_instruction_before_inst(static_cast<Instruction *>(delta),
                                           term);

    Value *numerator = delta;
    if (!match.strideIsConstant) {
        auto *div = new BinaryInst(m->int32_ty_, Instruction::SDiv, delta,
                                   match.strideValue, preheader, true);
        preheader->add_instruction_before_inst(div, term);
        Value *rawTrip = div;

        if (strict) {
            auto *rem = new BinaryInst(m->int32_ty_, Instruction::SRem, delta,
                                       match.strideValue, preheader, true);
            preheader->add_instruction_before_inst(rem, term);
            auto *hasRem = new ICmpInst(ICmpInst::ICMP_NE, rem, i32c(m, 0),
                                        preheader, true);
            preheader->add_instruction_before_inst(hasRem, term);
            auto *plusOne = new BinaryInst(m->int32_ty_, Instruction::Add,
                                           rawTrip, i32c(m, 1), preheader, true);
            preheader->add_instruction_before_inst(plusOne, term);
            auto *ceilTrip = new SelectInst(hasRem, plusOne, rawTrip,
                                            m->int32_ty_);
            preheader->add_instruction_before_inst(ceilTrip, term);
            rawTrip = ceilTrip;
        } else {
            auto *plusOne = new BinaryInst(m->int32_ty_, Instruction::Add,
                                           rawTrip, i32c(m, 1), preheader, true);
            preheader->add_instruction_before_inst(plusOne, term);
            rawTrip = plusOne;
        }

        auto *trip = new SelectInst(noIter, i32c(m, 0), rawTrip, m->int32_ty_);
        preheader->add_instruction_before_inst(trip, term);
        return trip;
    }

    long long absStride = match.exitStep > 0 ? match.exitStep : -match.exitStep;
    if (strict && absStride != 1) {
        auto *addBias = new BinaryInst(m->int32_ty_, Instruction::Add, delta,
                                       i32c(m, absStride - 1), preheader, true);
        preheader->add_instruction_before_inst(addBias, term);
        numerator = addBias;
    }

    Value *rawTrip = numerator;
    if (absStride != 1) {
        auto *div = new BinaryInst(m->int32_ty_, Instruction::SDiv, numerator,
                                   i32c(m, absStride), preheader, true);
        preheader->add_instruction_before_inst(div, term);
        rawTrip = div;
    }

    if (!strict) {
        auto *plusOne = new BinaryInst(m->int32_ty_, Instruction::Add, rawTrip,
                                       i32c(m, 1), preheader, true);
        preheader->add_instruction_before_inst(plusOne, term);
        rawTrip = plusOne;
    }

    auto *trip = new SelectInst(noIter, i32c(m, 0), rawTrip, m->int32_ty_);
    preheader->add_instruction_before_inst(trip, term);
    return trip;
}

bool findMatch(Loop &loop, InductionMatch &match) {
    if (!loop.preheader || loop.exiting.size() != 1 ||
        loop.exiting.front() != loop.header || loop.exits.size() != 1)
        return false;

    BasicBlock *latch = loop.singleLatch();
    if (!latch) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (!isI32(inst)) continue;

        auto *phi = static_cast<PhiInst *>(inst);
        Value *start = nullptr;
        Value *latchValue = nullptr;
        if (!getIncoming(phi, loop, start, latchValue)) continue;
        if (!isI32(start) || !isLoopInvariant(start, loop)) continue;

        BinaryInst *update = nullptr;
        Value *strideValue = nullptr;
        long long stride = 0;
        bool strideIsConstant = true;
        if (!matchStride(latchValue, phi, loop, update, strideValue, stride,
                         strideIsConstant))
            continue;
        InductionMatch candidate;
        candidate.stride = stride;
        candidate.strideValue = strideValue;
        candidate.strideIsConstant = strideIsConstant;

        ICmpInst *cmp = nullptr;
        Value *bound = nullptr;
        ICmpInst::ICmpOp pred = ICmpInst::ICMP_SLT;
        if (!matchHeaderGuard(loop, phi, candidate, cmp, bound, pred))
            continue;
        if (!isLoopInvariant(bound, loop)) continue;
        match.phi = phi;
        match.next = update;
        match.cmp = cmp;
        match.latch = latch;
        match.start = start;
        match.bound = bound;
        match.stride = stride;
        match.strideValue = strideValue;
        match.strideIsConstant = strideIsConstant;
        match.pred = pred;
        match.exitExpr = candidate.exitExpr;
        match.exitBound = candidate.exitBound;
        match.exitStep = candidate.exitStep;
        match.alreadyCanonical =
            strideIsConstant && stride == 1 && pred == ICmpInst::ICMP_SLT &&
            isZero(start) && candidate.exitExpr == phi;
        return true;
    }

    return false;
}

void replacePhiUses(PhiInst *oldPhi, Value *iv, Value *outsideValue,
                    const InductionMatch &match, const Loop &loop,
                    Module *module) {
    auto uses = oldPhi->use_list_;
    for (auto &use : uses) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || user == match.next || user == match.cmp) continue;
        Value *replacement = outsideValue;
        if (user->parent_ && loop.blocks.count(user->parent_)) {
            BasicBlock *insertBB = user->parent_;
            Instruction *before = user;
            if (auto *nested = innermostChildLoopContaining(loop, user->parent_);
                nested && nested->preheader) {
                insertBB = nested->preheader;
                before = nested->preheader->get_terminator();
            }
            replacement = materializeAffine(module, insertBB, before,
                                            match.start, iv, match.strideValue,
                                            match.stride,
                                            match.strideIsConstant);
        }
        user->set_operand(use.arg_no_, replacement);
    }
}

void collectDerivedInsts(Value *value, PhiInst *phi, const Loop &loop,
                         std::set<Instruction *> &seen,
                         std::vector<Instruction *> &derived) {
    auto uses = value->use_list_;
    for (auto &use : uses) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !user->parent_ || !loop.blocks.count(user->parent_))
            continue;
        if (!isSupportedDerived(user, phi, loop))
            continue;
        if (!seen.insert(user).second)
            continue;
        derived.push_back(user);
        collectDerivedInsts(user, phi, loop, seen, derived);
    }
}

bool rewriteOutsideDerivedUses(PhiInst *oldPhi, Value *exitValue,
                               const InductionMatch &match,
                               const Loop &loop, Module *module) {
    std::set<Instruction *> seen;
    std::vector<Instruction *> derived;
    collectDerivedInsts(oldPhi, oldPhi, loop, seen, derived);

    for (auto *inst : derived) {
        if (inst == match.next || inst == match.cmp) continue;
        auto uses = inst->use_list_;
        for (auto &use : uses) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || loop.blocks.count(user->parent_))
                continue;
            Value *replacement = materializeWithReplacement(
                module, loop.preheader, loop.preheader->get_terminator(),
                inst, oldPhi, exitValue, loop);
            if (!replacement)
                return false;
            user->set_operand(use.arg_no_, replacement);
        }
    }
    return true;
}

bool rewriteLoopExitAddRecUses(const Loop &loop, const InductionMatch &match,
                               ScalarEvolution &SE, Module *module,
                               Value *trip) {
    if (!loop.preheader || !trip) return false;

    bool changed = false;
    Instruction *insertBefore = loop.preheader->get_terminator();
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == match.phi) continue;
        if (!isI32(phi)) continue;

        std::vector<std::pair<Instruction *, unsigned>> outsideUses;
        for (auto &use : phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_) continue;
            if (!loop.blocks.count(user->parent_))
                outsideUses.push_back({user, use.arg_no_});
        }
        if (outsideUses.empty()) continue;

        auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(SE.getSCEV(phi));
        if (!addrec || addrec->loop() != &loop || addrec->phi() != phi)
            continue;

        Value *exitValue = materializeAddRecExitValue(
            module, loop.preheader, insertBefore, addrec, trip, loop);
        if (!exitValue)
            continue;

        for (auto &[user, argNo] : outsideUses)
            user->set_operand(argNo, exitValue);
        changed = true;
    }

    return changed;
}

bool hasLoopExitAddRecUses(const Loop &loop, const InductionMatch &match,
                           ScalarEvolution &SE) {
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == match.phi || !isI32(phi)) continue;

        bool hasOutsideUse = false;
        for (auto &use : phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ && !loop.blocks.count(user->parent_)) {
                hasOutsideUse = true;
                break;
            }
        }
        if (!hasOutsideUse) continue;

        auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(SE.getSCEV(phi));
        if (addrec && addrec->loop() == &loop && addrec->phi() == phi &&
            canMaterializeSCEV(addrec->start(), loop) &&
            canMaterializeSCEV(addrec->step(), loop))
            return true;
    }
    return false;
}

bool allUsesAre(Value *value, Instruction *onlyUser) {
    for (auto &use : value->use_list_)
        if (use.val_ != onlyUser) return false;
    return true;
}

bool simplifyLoop(Loop &loop, Function *func, Module *module,
                  AnalysisManager &AM) {
    InductionMatch match;
    if (!findMatch(loop, match)) return false;
    if (!usesAreReplaceable(match, loop)) return false;
    if (!match.strideIsConstant) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject header="
                      << loop.header->name_
                      << " reason=variable-stride-not-profitable\n";
        return false;
    }

    auto &RA = AM.getRangeAnalysis(func);
    auto &SE = AM.getScalarEvolution(func);
    if (!proveArithmeticSafe(match, RA, SE, loop, loop.preheader)) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject header="
                      << loop.header->name_ << " reason=unsafe-arithmetic\n";
        return false;
    }

    auto *i32 = module->int32_ty_;
    if (match.alreadyCanonical &&
        !hasLoopExitAddRecUses(loop, match, SE))
        return false;

    Value *exitStart = materializeWithReplacement(
        module, loop.preheader, loop.preheader->get_terminator(),
        match.exitExpr ? match.exitExpr : match.phi, match.phi, match.start,
        loop);
    if (!exitStart) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject header="
                      << loop.header->name_
                      << " reason=unsupported-exit-expression\n";
        return false;
    }

    Value *trip = materializeTripCount(module, loop.preheader, match,
                                       exitStart);
    bool rewroteAddRecExits =
        rewriteLoopExitAddRecUses(loop, match, SE, module, trip);

    if (match.alreadyCanonical) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] rewrote addrec exits func="
                      << func->name_ << " header=" << loop.header->name_
                      << " addrecExits=" << (rewroteAddRecExits ? 1 : 0)
                      << "\n";
        return rewroteAddRecExits;
    }

    std::vector<Value *> vals = {i32c(module, 0), i32c(module, 0)};
    std::vector<BasicBlock *> bbs = {loop.preheader, match.latch};
    auto *iv = new PhiInst(Instruction::PHI, vals, bbs, i32, loop.header);
    loop.header->add_instruction_front(iv);

    auto *latchTerm = match.latch->get_terminator();
    auto *ivNext = new BinaryInst(i32, Instruction::Add, iv, i32c(module, 1),
                                  match.latch, true);
    match.latch->add_instruction_before_inst(ivNext, latchTerm);
    iv->set_operand(2, ivNext);

    Value *exitValue = materializeAffine(module, loop.preheader,
                                         loop.preheader->get_terminator(),
                                         match.start, trip, match.strideValue,
                                         match.stride,
                                         match.strideIsConstant);

    auto *newCmp = new ICmpInst(ICmpInst::ICMP_SLT, iv, trip, loop.header,
                                true);
    loop.header->add_instruction_before_inst(newCmp,
                                             loop.header->get_terminator());
    loop.header->get_terminator()->set_operand(0, newCmp);

    if (!rewriteOutsideDerivedUses(match.phi, exitValue, match, loop, module)) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject header="
                      << loop.header->name_
                      << " reason=unsupported-derived-use\n";
        return false;
    }

    replacePhiUses(match.phi, iv, exitValue, match, loop, module);

    if (match.cmp->use_list_.empty())
        match.cmp->parent_->delete_instr(match.cmp);

    if (allUsesAre(match.phi, match.next) && allUsesAre(match.next, match.phi)) {
        match.phi->parent_->delete_instr(match.phi);
        match.next->parent_->delete_instr(match.next);
    }

    if (debugEnabled())
        std::cerr << "[IndVarSimplify] canonicalized func=" << func->name_
                  << " header=" << loop.header->name_
                  << " stride="
                  << (match.strideIsConstant ? std::to_string(match.stride)
                                             : "<loop-invariant>")
                  << " addrecExits=" << (rewroteAddRecExits ? 1 : 0)
                  << "\n";
    return true;
}

} // namespace

void IndVarSimplify::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses IndVarSimplify::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool IndVarSimplify::runOnFunction(Function *func, AnalysisManager &AM) {
    if (!func || func->basic_blocks_.empty()) return false;

    bool changed = false;
    while (true) {
        std::vector<BasicBlock *> headers;
        LoopInfo &LI = AM.getLoopInfo(func);
        std::vector<Loop *> loops;
        for (auto &loop : LI.allLoops())
            loops.push_back(loop.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });
        for (auto *loop : loops)
            headers.push_back(loop->header);

        bool changedThisIteration = false;
        for (auto *header : headers) {
            LoopInfo &currentLI = AM.getLoopInfo(func);
            Loop *loop = nullptr;
            for (auto &candidate : currentLI.allLoops()) {
                if (candidate->header == header) {
                    loop = candidate.get();
                    break;
                }
            }
            if (!loop) continue;
            if (simplifyLoop(*loop, func, func->parent_, AM)) {
                changed = true;
                changedThisIteration = true;
                AM.invalidateFunction(func, PreservedAnalyses::none());
            }
        }

        if (!changedThisIteration) break;
    }
    return changed;
}
