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
                      const InductionMatch &match,
                      ICmpInst *&cmp, Value *&bound,
                      ICmpInst::ICmpOp &pred) {
    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;

    cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->parent_ != loop.header || cmp->get_operand(0) != phi)
        return false;

    auto *trueBB = dynamic_cast<BasicBlock *>(term->get_operand(1));
    auto *falseBB = dynamic_cast<BasicBlock *>(term->get_operand(2));
    if (!trueBB || !falseBB || !loop.blocks.count(trueBB) ||
        loop.blocks.count(falseBB))
        return false;

    pred = cmp->icmp_op_;
    if (!match.strideIsConstant || match.stride > 0) {
        if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            return false;
    } else {
        if (pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            return false;
    }

    bound = cmp->get_operand(1);
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
                            const InductionMatch &match) {
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

    auto *noIter = new ICmpInst(noIterPred, match.start, match.bound, preheader,
                                true);
    preheader->add_instruction_before_inst(noIter, term);

    Value *delta = nullptr;
    if (!match.strideIsConstant || match.stride > 0)
        delta = new BinaryInst(m->int32_ty_, Instruction::Sub, match.bound,
                               match.start, preheader, true);
    else
        delta = new BinaryInst(m->int32_ty_, Instruction::Sub, match.start,
                               match.bound, preheader, true);
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

    long long absStride = match.stride > 0 ? match.stride : -match.stride;
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
        if (strideIsConstant && stride == 1 && pred == ICmpInst::ICMP_SLT &&
            isZero(start))
            continue;

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
    Value *trip = materializeTripCount(module, loop.preheader, match);

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
