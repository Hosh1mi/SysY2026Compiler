#include "../../include/mid/analysis/rangeAnalysis.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/valueFacts.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/intrinsics.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <queue>

namespace {

bool debugEnabled() {
    static bool enabled = std::getenv("DEBUG_RANGE_ANALYSIS") != nullptr;
    return enabled;
}

void debugLog(const char *what, Value *v, BasicBlock *ctx, const RangeAnalysis::IntRange &r) {
    if (!debugEnabled()) return;
    std::cerr << "[RangeAnalysis] " << what
              << " value=" << (v ? v->name_ : "<null>")
              << " ctx=" << (ctx ? ctx->name_ : "<null>")
              << " valid=" << r.valid
              << " top=" << r.isTop
              << " bottom=" << r.isBottom;
    if (r.valid && !r.isTop && !r.isBottom)
        std::cerr << " [" << r.lower << ", " << r.upper << "]";
    std::cerr << "\n";
}

} // namespace

RangeAnalysis::RangeAnalysis(Function *func, AnalysisManager *AM, const LoopInfo &LI,
                             ScalarEvolution &SE)
    : func_(func), AM_(AM), LI_(&LI), SE_(&SE) {
    buildControlDependence(func);
}

void RangeAnalysis::clear() {
    cache_.clear();
    memoryInFacts_.clear();
    memoryOutFacts_.clear();
    blockFacts_.clear();
    visiting_.clear();
    memoryFactsComputed_ = false;
    memoryFactsComputing_ = false;
    returnSummary_ = ReturnSummary{};
}

void RangeAnalysis::clearCache() {
    cache_.clear();
    memoryInFacts_.clear();
    memoryOutFacts_.clear();
    visiting_.clear();
    memoryFactsComputed_ = false;
    memoryFactsComputing_ = false;
    returnSummary_ = ReturnSummary{};
}

std::pair<long long, long long> RangeAnalysis::typeBounds(Type *ty) {
    if (!ty || ty->tid_ != Type::IntegerTyID) {
        return {std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()};
    }
    auto *ity = static_cast<IntegerType *>(ty);
    if (ity->num_bits_ == 1) return {0, 1};
    if (ity->num_bits_ >= 63) {
        return {std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()};
    }
    long long hi = (1LL << (ity->num_bits_ - 1)) - 1;
    long long lo = -1LL << (ity->num_bits_ - 1);
    return {lo, hi};
}

bool RangeAnalysis::multiplyBounds(long long a, long long b, long long &out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > 0 && b > 0 && a > std::numeric_limits<long long>::max() / b) return false;
    if (a > 0 && b < 0 && b < std::numeric_limits<long long>::min() / a) return false;
    if (a < 0 && b > 0 && a < std::numeric_limits<long long>::min() / b) return false;
    if (a < 0 && b < 0 && a < std::numeric_limits<long long>::max() / b) return false;
    out = a * b;
    return true;
}

bool RangeAnalysis::addBounds(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b))
        return false;
    out = a + b;
    return true;
}

bool RangeAnalysis::subtractBounds(long long a, long long b, long long &out) {
    return addBounds(a, -b, out);
}

bool RangeAnalysis::getConstInt(Value *v, long long &out) {
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        out = ci->value_;
        return true;
    }
    return false;
}

bool RangeAnalysis::isIntegerValue(Value *v) {
    return v && v->type_ && v->type_->tid_ == Type::IntegerTyID;
}

ICmpInst::ICmpOp RangeAnalysis::negatePredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:  return ICmpInst::ICMP_NE;
    case ICmpInst::ICMP_NE:  return ICmpInst::ICMP_EQ;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGT;
    default: return pred;
    }
}

ICmpInst::ICmpOp RangeAnalysis::swapPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:  return ICmpInst::ICMP_EQ;
    case ICmpInst::ICMP_NE:  return ICmpInst::ICMP_NE;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGE;
    default: return pred;
    }
}

RangeAnalysis::IntRange RangeAnalysis::getConstantRange(ConstantInt *ci) const {
    if (!ci) return IntRange::top();
    return IntRange::constant(ci->value_);
}

void RangeAnalysis::buildControlDependence(Function *func) {
    if (!func) return;
    for (auto *bb : func->basic_blocks_) {
        auto *term = bb->get_terminator();
        auto *br = dynamic_cast<BranchInst *>(term);
        if (!br || br->num_ops() != 3) continue;

        std::vector<std::set<BasicBlock *>> reachable(2);
        for (unsigned succIdx = 0; succIdx < 2; ++succIdx) {
            auto *succ = static_cast<BasicBlock *>(br->get_operand(succIdx + 1));
            if (!succ) continue;

            std::queue<BasicBlock *> q;
            q.push(succ);
            reachable[succIdx].insert(succ);
            while (!q.empty()) {
                auto *cur = q.front();
                q.pop();
                for (auto *next : cur->succ_bbs_) {
                    if (reachable[succIdx].insert(next).second) q.push(next);
                }
            }
        }

        for (unsigned succIdx = 0; succIdx < 2; ++succIdx) {
            PredicateFact fact;
            fact.branchTaken = (succIdx == 0);
            if (auto *cond = dynamic_cast<ICmpInst *>(br->get_operand(0))) {
                fact.lhs = cond->get_operand(0);
                fact.rhs = cond->get_operand(1);
                fact.pred = cond->icmp_op_;
            } else {
                fact.lhs = br->get_operand(0);
                fact.rhs = nullptr;
                fact.pred = fact.branchTaken ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ;
            }

            for (auto *cur : reachable[succIdx]) {
                if (cur == bb) continue;
                if (reachable[1 - succIdx].count(cur)) continue;
                blockFacts_[cur].push_back(fact);
            }
        }
    }
}

const std::vector<RangeAnalysis::PredicateFact> &RangeAnalysis::factsForBlock(BasicBlock *bb) {
    static const std::vector<PredicateFact> empty;
    auto it = blockFacts_.find(bb);
    if (it != blockFacts_.end()) return it->second;
    return empty;
}

void RangeAnalysis::collectFacts(BasicBlock *bb, std::vector<PredicateFact> &out,
                                 std::set<BasicBlock *> &visiting) {
    if (!bb || !visiting.insert(bb).second) return;
    auto it = blockFacts_.find(bb);
    if (it != blockFacts_.end()) {
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
}

RangeAnalysis::IntRange RangeAnalysis::refineWithFact(Value *query, const IntRange &base,
                                                      const PredicateFact &fact) const {
    auto refineWithConst = [&](ICmpInst::ICmpOp pred, long long c, bool branchTaken) -> IntRange {
        if (!branchTaken) pred = negatePredicate(pred);
        switch (pred) {
        case ICmpInst::ICMP_EQ:
            return base.intersect(IntRange::constant(c));
        case ICmpInst::ICMP_NE:
            return base;
        case ICmpInst::ICMP_SLT:
            return base.intersect(IntRange::bounded(std::numeric_limits<long long>::min(), c - 1));
        case ICmpInst::ICMP_SLE:
            return base.intersect(IntRange::bounded(std::numeric_limits<long long>::min(), c));
        case ICmpInst::ICMP_SGT:
            return base.intersect(IntRange::bounded(c + 1, std::numeric_limits<long long>::max()));
        case ICmpInst::ICMP_SGE:
            return base.intersect(IntRange::bounded(c, std::numeric_limits<long long>::max()));
        case ICmpInst::ICMP_ULT:
        case ICmpInst::ICMP_ULE:
        case ICmpInst::ICMP_UGT:
        case ICmpInst::ICMP_UGE:
            return base; // unsigned reasoning is handled in compareRanges()
        default:
            return base;
        }
    };

    if (fact.lhs == query) {
        long long c = 0;
        if (getConstInt(fact.rhs, c)) {
            return refineWithConst(fact.pred, c, fact.branchTaken);
        }
    }

    if (fact.rhs == query) {
        long long c = 0;
        if (getConstInt(fact.lhs, c)) {
            return refineWithConst(swapPredicate(fact.pred), c, fact.branchTaken);
        }
    }

    return base;
}

RangeAnalysis::IntRange RangeAnalysis::applyFacts(Value *query, const IntRange &base,
                                                  BasicBlock *ctx) {
    if (!ctx) return base;
    std::vector<PredicateFact> facts;
    std::set<BasicBlock *> visiting;
    collectFacts(ctx, facts, visiting);
    IntRange result = base;
    for (const auto &fact : facts) {
        IntRange next = refineWithFact(query, result, fact);
        if (next.valid && next.isBottom) continue;
        result = next;
    }
    return result;
}

RangeAnalysis::IntRange RangeAnalysis::getRange(Value *v, BasicBlock *ctx) {
    CacheKey key{v, ctx};
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    if (!v) return IntRange::top();
    auto guard = std::make_pair(v, ctx);
    if (!visiting_.insert(guard).second) return IntRange::top();

    if (AM_ && queryDepth_++ == 0) AM_->enterRangeAnalysis(func_);
    IntRange result = getRangeImpl(v, ctx);
    if (v->hasSemFlag(SemFlag::KnownNonNegative) && isIntegerValue(v)) {
        auto [lo, hi] = typeBounds(v->type_);
        result = result.intersect(IntRange::bounded(std::max(0LL, lo), hi));
    }
    result = applyFacts(v, result, ctx);
    visiting_.erase(guard);
    if (AM_ && --queryDepth_ == 0) AM_->leaveRangeAnalysis(func_);
    cache_[key] = result;
    debugLog("query", v, ctx, result);
    return result;
}

RangeAnalysis::IntRange RangeAnalysis::getRangeImpl(Value *v, BasicBlock *ctx) {
    if (!v) return IntRange::top();
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) return getConstantRange(ci);
    if (auto *arg = dynamic_cast<Argument *>(v)) return getArgumentRange(arg, ctx);
    if (auto *call = dynamic_cast<CallInst *>(v)) return getCallRange(call, ctx);
    if (auto *phi = dynamic_cast<PhiInst *>(v)) return getPhiRange(phi, ctx);
    if (auto *bin = dynamic_cast<BinaryInst *>(v)) return getBinaryRange(bin, ctx);
    if (auto *zext = dynamic_cast<ZextInst *>(v)) return getZExtRange(zext, ctx);
    if (auto *icmp = dynamic_cast<ICmpInst *>(v)) return getICmpRange(icmp, ctx);
    if (auto *sel = dynamic_cast<SelectInst *>(v)) return getSelectRange(sel, ctx);
    if (auto *load = dynamic_cast<LoadInst *>(v)) return getLoadRange(load, ctx);
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(v)) return getGEPOffsetRange(gep, ctx);
    if (auto *inst = dynamic_cast<Instruction *>(v)) {
        (void)inst;
        return getSCEVRange(v, ctx);
    }
    return IntRange::top();
}

RangeAnalysis::IntRange RangeAnalysis::getBinaryRange(BinaryInst *bin, BasicBlock *ctx) {
    if (!bin || !isIntegerValue(bin)) return IntRange::top();

    auto lhs = getRange(bin->get_operand(0), ctx ? ctx : bin->parent_);
    auto rhs = getRange(bin->get_operand(1), ctx ? ctx : bin->parent_);
    if (!lhs.valid || !rhs.valid) return IntRange::top();
    if (lhs.isBottom || rhs.isBottom) return IntRange::bottom();
    if (lhs.isTop || rhs.isTop) return IntRange::top();

    switch (bin->op_id_) {
    case Instruction::Add: {
        long long lo = 0;
        long long hi = 0;
        if (!addBounds(lhs.lower, rhs.lower, lo)) return IntRange::top();
        if (!addBounds(lhs.upper, rhs.upper, hi)) return IntRange::top();
        return IntRange::bounded(lo, hi);
    }
    case Instruction::Sub: {
        long long lo = 0;
        long long hi = 0;
        if (!subtractBounds(lhs.lower, rhs.upper, lo)) return IntRange::top();
        if (!subtractBounds(lhs.upper, rhs.lower, hi)) return IntRange::top();
        return IntRange::bounded(lo, hi);
    }
    case Instruction::SDiv: {
        if (!rhs.isSingleton() || rhs.lower == 0) return IntRange::top();
        long long divisor = rhs.lower;
        if (divisor <= 0) return IntRange::top();
        return IntRange::bounded(lhs.lower / divisor, lhs.upper / divisor);
    }
    case Instruction::SRem: {
        if (!rhs.isSingleton() || rhs.lower <= 0) return IntRange::top();
        if (!lhs.knownNonNegative()) return IntRange::top();
        return IntRange::bounded(0, rhs.lower - 1);
    }
    case Instruction::Shl: {
        if (!lhs.knownNonNegative() || !rhs.isSingleton()) return IntRange::top();
        long long shift = rhs.lower;
        if (shift < 0 || shift >= 63) return IntRange::top();
        long long scale = 0;
        if (!multiplyBounds(1LL, 1LL << shift, scale)) return IntRange::top();
        long long lo = 0;
        long long hi = 0;
        if (!multiplyBounds(lhs.lower, scale, lo)) return IntRange::top();
        if (!multiplyBounds(lhs.upper, scale, hi)) return IntRange::top();
        return IntRange::bounded(lo, hi);
    }
    case Instruction::AShr:
    case Instruction::LShr: {
        if (!lhs.knownNonNegative() || !rhs.knownNonNegative()) return IntRange::top();
        long long shift = rhs.lower;
        if (!rhs.isSingleton() || shift < 0 || shift >= 63) return IntRange::top();
        return IntRange::bounded(lhs.lower >> shift, lhs.upper >> shift);
    }
    default:
        return IntRange::top();
    }
}

RangeAnalysis::IntRange RangeAnalysis::getIntrinsicRange(Value *v) {
    if (!v) return IntRange::top();
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) return getConstantRange(ci);
    if (auto *icmp = dynamic_cast<ICmpInst *>(v)) return getICmpRange(icmp, nullptr);
    if (auto *zext = dynamic_cast<ZextInst *>(v)) return getZExtRange(zext, nullptr);
    if (auto *phi = dynamic_cast<PhiInst *>(v)) return getPhiRange(phi, nullptr);
    if (auto *call = dynamic_cast<CallInst *>(v)) return getCallRange(call, nullptr);
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(v)) return getGEPOffsetRange(gep, nullptr);
    return getSCEVRange(v, nullptr);
}

RangeAnalysis::IntRange RangeAnalysis::getZExtRange(ZextInst *zext, BasicBlock *ctx) {
    if (!zext) return IntRange::top();
    auto *srcTy = zext->get_operand(0)->type_;
    if (auto *ity = dynamic_cast<IntegerType *>(srcTy); ity && ity->num_bits_ == 1) {
        return IntRange::bounded(0, 1);
    }

    auto src = getRange(zext->get_operand(0), zext->parent_);
    if (!src.valid || src.isTop) return IntRange::top();
    if (src.isBottom) return IntRange::bottom();

    if (zext->get_operand(0)->type_ && zext->get_operand(0)->type_->tid_ == Type::IntegerTyID) {
        auto [lo, hi] = typeBounds(zext->get_operand(0)->type_);
        if (src.lower >= lo && src.upper <= hi && src.knownNonNegative()) {
            return src;
        }
    }
    (void)ctx;
    return IntRange::top();
}

RangeAnalysis::IntRange RangeAnalysis::getPhiRange(PhiInst *phi, BasicBlock *ctx) {
    if (!phi) return IntRange::top();
    IntRange result = IntRange::bottom();
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        auto *incoming = phi->get_operand(i);
        auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        auto incomingRange = getRange(incoming, predBB);
        result = result.isBottom ? incomingRange : result.join(incomingRange);
    }
    if (!result.valid) result = IntRange::top();
    if (ctx) result = applyFacts(phi, result, ctx);
    return result;
}

RangeAnalysis::IntRange RangeAnalysis::getCallRange(CallInst *call, BasicBlock *ctx) {
    if (!call || !AM_) return IntRange::top();
    auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
    if (!callee || callee->is_declaration()) return IntRange::top();
    if (callee == func_) {
        auto selfSummary = getNormalizedReturnRangeForCall(call, ctx);
        if (selfSummary.valid && !selfSummary.isTop && !selfSummary.isBottom)
            return selfSummary;
        if (returnSummary_.computing && returnSummary_.pendingModulus > 0) {
            if (callSatisfiesReturnRequirements(call, ctx))
                return IntRange::bounded(0, returnSummary_.pendingModulus - 1);
        }
    }
    if (AM_->isRangeAnalysisActive(callee)) return IntRange::top();
    auto &calleeRA = AM_->getRangeAnalysis(callee);
    auto summaryRange = calleeRA.getNormalizedReturnRangeForCall(call, ctx);
    if (summaryRange.valid && !summaryRange.isTop && !summaryRange.isBottom) {
        return summaryRange;
    }

    IntRange result = IntRange::bottom();
    bool found = false;
    for (auto *bb : callee->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_ret()) continue;
            auto *ret = static_cast<ReturnInst *>(inst);
            if (ret->num_ops() == 0) continue;
            auto r = calleeRA.getRange(ret->get_operand(0), ret->parent_);
            if (!found) {
                result = r;
                found = true;
            } else {
                result = result.join(r);
            }
        }
    }
    if (!found) return IntRange::top();
    if (ctx) result = applyFacts(call, result, ctx);
    return result;
}

RangeAnalysis::IntRange RangeAnalysis::getArgumentRange(Argument *arg, BasicBlock *ctx) {
    if (!arg || !arg->parent_ || !AM_) return IntRange::top();

    IntRange result = IntRange::bottom();
    bool found = false;
    auto *func = arg->parent_;
    if (!func->parent_) return IntRange::top();

    // CallInst records its callee as the final operand, so the function use
    // list is the precise, mutation-safe call-site index.  This avoids a
    // complete module walk for every formal argument.
    for (const Use &use : func->use_list_) {
        auto *call = dynamic_cast<CallInst *>(use.user_);
        if (!call || call->num_ops() == 0 ||
            call->get_operand(call->num_ops() - 1) != func ||
            arg->arg_no_ >= call->num_ops() - 1)
            continue;
        auto *caller =
            call->parent_ ? call->parent_->parent_ : nullptr;
        if (!caller || caller->is_declaration())
            continue;
        Value *actual = call->get_operand(arg->arg_no_);
        if (caller == func && actual == arg) {
            // A self-recursive call forwarding the same formal does not add
            // information; keep facts from non-recursive call sites visible.
            continue;
        }
        auto &callerRA = AM_->getRangeAnalysis(caller);
        auto r = callerRA.getRange(actual, call->parent_);
        if (!found) {
            result = r;
            found = true;
        } else {
            result = result.join(r);
        }
    }
    if (!found) result = IntRange::top();

    if (func == func_ && !returnSummary_.computing) {
        computeNormalizedReturnSummary();
        if (returnSummary_.known &&
            std::find(returnSummary_.nonNegativeArgs.begin(),
                      returnSummary_.nonNegativeArgs.end(),
                      arg->arg_no_) != returnSummary_.nonNegativeArgs.end()) {
            auto reqRange = IntRange::bounded(0, std::numeric_limits<long long>::max());
            result = result.intersect(reqRange);
        }
    }

    if (ctx) result = applyFacts(arg, result, ctx);
    return result;
}

RangeAnalysis::IntRange RangeAnalysis::getSCEVRange(Value *v, BasicBlock *ctx) {
    if (!v || !SE_) return IntRange::top();

    const SCEV *s = SE_->getSCEV(v);
    if (!s) return IntRange::top();

    switch (s->kind()) {
    case SCEVKind::Constant: {
        auto *c = static_cast<const SCEVConstant *>(s);
        return IntRange::constant(c->value());
    }
    case SCEVKind::AddRecExpr: {
        auto *ar = static_cast<const SCEVAddRecExpr *>(s);
        auto *loop = ar->loop();
        if (!loop || !loop->hasCanonicalIV() || ar->phi() != loop->canonicalIV)
            return IntRange::top();

        auto *startC = dynamic_cast<const SCEVConstant *>(ar->start());
        auto *stepC = dynamic_cast<const SCEVConstant *>(ar->step());
        if (!startC || !stepC) return IntRange::top();

        auto trip = SE_->getTripCount(loop);
        auto *tripC = dynamic_cast<const SCEVConstant *>(trip);
        if (stepC->value() > 0) {
            if (startC->value() < 0) return IntRange::top();
            if (tripC) {
                long long delta = 0;
                if (!multiplyBounds(stepC->value(), tripC->value() - 1, delta))
                    return IntRange::top();
                long long upper = 0;
                if (!addBounds(startC->value(), delta, upper))
                    return IntRange::top();
                return IntRange::bounded(startC->value(), upper);
            }
            return IntRange::bounded(startC->value(), std::numeric_limits<long long>::max());
        }
        if (stepC->value() < 0) {
            if (tripC) {
                long long delta = 0;
                if (!multiplyBounds(stepC->value(), tripC->value() - 1, delta))
                    return IntRange::top();
                long long lower = 0;
                if (!addBounds(startC->value(), delta, lower))
                    return IntRange::top();
                return IntRange::bounded(lower, startC->value());
            }
            return IntRange::bounded(std::numeric_limits<long long>::min(), startC->value());
        }
        return IntRange::constant(startC->value());
    }
    case SCEVKind::AddExpr:
    case SCEVKind::MulExpr:
    case SCEVKind::Unknown:
    case SCEVKind::CouldNotCompute:
        return IntRange::top();
    }
    return IntRange::top();
}

RangeAnalysis::IntRange RangeAnalysis::getGEPOffsetRange(GetElementPtrInst *gep, BasicBlock *ctx) {
    if (!gep || gep->num_ops() < 2) return IntRange::top();

    Value *base = gep->get_operand(0);
    auto *ptrTy = dynamic_cast<PointerType *>(base->type_);
    if (!ptrTy) return IntRange::top();

    for (unsigned i = 1; i < gep->num_ops(); ++i) {
        auto *idxTy = dynamic_cast<IntegerType *>(gep->get_operand(i)->type_);
        if (!idxTy || idxTy->num_bits_ != 32) return IntRange::top();
    }

    Type *elementTy = ptrTy->contained_;
    std::vector<long long> shape;
    while (auto *arrTy = dynamic_cast<ArrayType *>(elementTy)) {
        shape.push_back(arrTy->num_elements_);
        elementTy = arrTy->contained_;
    }

    IntRange result = IntRange::constant(0);
    bool any = false;

    if (shape.empty()) {
        auto idxRange = getRange(gep->get_operand(1), gep->parent_);
        if (!idxRange.valid || idxRange.isBottom) return IntRange::top();
        if (!idxRange.knownNonNegative()) return IntRange::top();
        return idxRange;
    }

    auto *leading = dynamic_cast<ConstantInt *>(gep->get_operand(1));
    if (!leading || leading->value_ != 0) return IntRange::top();

    for (unsigned idxNo = 0; idxNo < gep->num_ops() - 2; ++idxNo) {
        auto *idxVal = gep->get_operand(idxNo + 2);
        auto idxRange = getRange(idxVal, gep->parent_);
        if (!idxRange.valid || idxRange.isBottom || !idxRange.knownNonNegative())
            return IntRange::top();

        long long stride = 1;
        for (size_t dim = idxNo + 1; dim < shape.size(); ++dim) {
            if (!multiplyBounds(stride, shape[dim], stride))
                return IntRange::top();
        }

        long long loPart = 0;
        if (!multiplyBounds(stride, idxRange.lower, loPart)) return IntRange::top();
        if (!any) {
            result = IntRange::bounded(loPart, std::numeric_limits<long long>::max());
            any = true;
        } else {
            long long newLo = 0;
            if (!addBounds(result.lower, loPart, newLo)) newLo = result.lower;
            result = IntRange::bounded(newLo, std::numeric_limits<long long>::max());
        }
    }

    if (!any) return IntRange::constant(0);
    if (ctx) result = applyFacts(gep, result, ctx);
    return result;
}

RangeAnalysis::TruthValue RangeAnalysis::compareRanges(ICmpInst::ICmpOp pred,
                                                       const IntRange &lhs,
                                                       const IntRange &rhs) const {
    if (!lhs.valid || !rhs.valid || lhs.isBottom || rhs.isBottom) return TruthValue::Unknown;

    auto unsignedView = [](long long v) -> unsigned long long {
        return static_cast<unsigned long long>(static_cast<std::uint32_t>(v));
    };

    switch (pred) {
    case ICmpInst::ICMP_EQ:
        if (lhs.isSingleton() && rhs.isSingleton() && lhs.lower == rhs.lower)
            return TruthValue::AlwaysTrue;
        if ((lhs.isTop || rhs.isTop) == false &&
            (lhs.upper < rhs.lower || rhs.upper < lhs.lower))
            return TruthValue::AlwaysFalse;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_NE:
        if (lhs.isSingleton() && rhs.isSingleton() && lhs.lower == rhs.lower)
            return TruthValue::AlwaysFalse;
        if ((lhs.isTop || rhs.isTop) == false &&
            (lhs.upper < rhs.lower || rhs.upper < lhs.lower))
            return TruthValue::AlwaysTrue;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_SLT:
        if (lhs.upper < rhs.lower) return TruthValue::AlwaysTrue;
        if (lhs.lower >= rhs.upper) return TruthValue::AlwaysFalse;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_SLE:
        if (lhs.upper <= rhs.lower) return TruthValue::AlwaysTrue;
        if (lhs.lower > rhs.upper) return TruthValue::AlwaysFalse;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_SGT:
        if (lhs.lower > rhs.upper) return TruthValue::AlwaysTrue;
        if (lhs.upper <= rhs.lower) return TruthValue::AlwaysFalse;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_SGE:
        if (lhs.lower >= rhs.upper) return TruthValue::AlwaysTrue;
        if (lhs.upper < rhs.lower) return TruthValue::AlwaysFalse;
        return TruthValue::Unknown;
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE:
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE: {
        if (!lhs.knownNonNegative() || !rhs.knownNonNegative()) return TruthValue::Unknown;
        auto llo = unsignedView(lhs.lower);
        auto lhi = unsignedView(lhs.upper);
        auto rlo = unsignedView(rhs.lower);
        auto rhi = unsignedView(rhs.upper);
        switch (pred) {
        case ICmpInst::ICMP_ULT:
            if (lhi < rlo) return TruthValue::AlwaysTrue;
            if (llo >= rhi) return TruthValue::AlwaysFalse;
            return TruthValue::Unknown;
        case ICmpInst::ICMP_ULE:
            if (lhi <= rlo) return TruthValue::AlwaysTrue;
            if (llo > rhi) return TruthValue::AlwaysFalse;
            return TruthValue::Unknown;
        case ICmpInst::ICMP_UGT:
            if (llo > rhi) return TruthValue::AlwaysTrue;
            if (lhi <= rlo) return TruthValue::AlwaysFalse;
            return TruthValue::Unknown;
        case ICmpInst::ICMP_UGE:
            if (llo >= rhi) return TruthValue::AlwaysTrue;
            if (lhi < rlo) return TruthValue::AlwaysFalse;
            return TruthValue::Unknown;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    return TruthValue::Unknown;
}

RangeAnalysis::TruthValue RangeAnalysis::getPredicateResult(ICmpInst::ICmpOp pred, Value *lhs,
                                                            Value *rhs, BasicBlock *ctx) {
    auto l = getRange(lhs, ctx);
    auto r = getRange(rhs, ctx);
    return compareRanges(pred, l, r);
}

bool RangeAnalysis::isKnownNonNegative(Value *v, BasicBlock *ctx) {
    auto r = getRange(v, ctx);
    return r.knownNonNegative();
}

RangeAnalysis::IntRange RangeAnalysis::getICmpRange(ICmpInst *icmp, BasicBlock *ctx) {
    if (!icmp) return IntRange::top();
    auto tv = getPredicateResult(icmp->icmp_op_, icmp->get_operand(0), icmp->get_operand(1), ctx);
    switch (tv) {
    case TruthValue::AlwaysTrue: return IntRange::constant(1);
    case TruthValue::AlwaysFalse: return IntRange::constant(0);
    case TruthValue::Unknown: return IntRange::bounded(0, 1);
    }
    return IntRange::top();
}

RangeAnalysis::IntRange RangeAnalysis::getSelectRange(SelectInst *sel, BasicBlock *ctx) {
    if (!sel) return IntRange::top();
    auto condRange = getRange(sel->get_operand(0), ctx);
    if (condRange.isSingleton()) {
        return condRange.lower ? getRange(sel->get_operand(1), ctx)
                               : getRange(sel->get_operand(2), ctx);
    }
    auto t = getRange(sel->get_operand(1), ctx);
    auto f = getRange(sel->get_operand(2), ctx);

    SignedMinMaxIntrinsic kind;
    Value *lhsValue = nullptr;
    Value *rhsValue = nullptr;
    if (matchSignedMinMaxSelect(sel, kind, lhsValue, rhsValue)) {
        auto lhs = getRange(lhsValue, ctx);
        auto rhs = getRange(rhsValue, ctx);
        if (lhs.valid && rhs.valid && !lhs.isTop && !rhs.isTop &&
            !lhs.isBottom && !rhs.isBottom) {
            long long lower =
                kind == SignedMinMaxIntrinsic::SMax
                    ? std::max(lhs.lower, rhs.lower)
                    : std::min(lhs.lower, rhs.lower);
            long long upper =
                kind == SignedMinMaxIntrinsic::SMax
                    ? std::max(lhs.upper, rhs.upper)
                    : std::min(lhs.upper, rhs.upper);

            // max(x, C-x) has a tighter V-shaped lower bound than the
            // independent operand ranges reveal.  Refine it only when C-x
            // is proven not to wrap over x's complete interval.
            if (kind == SignedMinMaxIntrinsic::SMax) {
                Value *base = nullptr;
                BinaryInst *complement = nullptr;
                if (auto *sub = dynamic_cast<BinaryInst *>(lhsValue);
                    sub && sub->op_id_ == Instruction::Sub &&
                    sub->get_operand(1) == rhsValue &&
                    dynamic_cast<ConstantInt *>(sub->get_operand(0))) {
                    base = rhsValue;
                    complement = sub;
                } else if (auto *sub = dynamic_cast<BinaryInst *>(rhsValue);
                           sub && sub->op_id_ == Instruction::Sub &&
                           sub->get_operand(1) == lhsValue &&
                           dynamic_cast<ConstantInt *>(sub->get_operand(0))) {
                    base = lhsValue;
                    complement = sub;
                }

                if (base && complement) {
                    auto baseRange = getRange(base, ctx);
                    auto *constant = static_cast<ConstantInt *>(
                        complement->get_operand(0));
                    auto [typeLo, typeHi] = typeBounds(sel->type_);
                    const long long c = constant->value_;
                    if (baseRange.valid && !baseRange.isTop &&
                        !baseRange.isBottom &&
                        c - baseRange.upper >= typeLo &&
                        c - baseRange.lower <= typeHi) {
                        auto evaluate = [c](long long x) {
                            return std::max(x, c - x);
                        };
                        long long refined = std::min(
                            evaluate(baseRange.lower),
                            evaluate(baseRange.upper));
                        long long floorHalf =
                            c >= 0 ? c / 2 : (c - 1) / 2;
                        long long ceilHalf = c - floorHalf;
                        if (floorHalf >= baseRange.lower &&
                            floorHalf <= baseRange.upper)
                            refined = std::min(refined,
                                               evaluate(floorHalf));
                        if (ceilHalf >= baseRange.lower &&
                            ceilHalf <= baseRange.upper)
                            refined = std::min(refined,
                                               evaluate(ceilHalf));
                        lower = std::max(lower, refined);
                    }
                }
            }
            return IntRange::bounded(lower, upper);
        }
    }
    return t.join(f);
}

bool RangeAnalysis::addMemoryKeyOffset(MemoryKey &key, long long offset) const {
    return addBounds(key.constantOffset, offset, key.constantOffset);
}

bool RangeAnalysis::addMemoryKeyOffset(MemoryKey &key, Value *idx, long long scale) const {
    long long constIdx = 0;
    if (getConstInt(idx, constIdx)) {
        long long delta = 0;
        if (!multiplyBounds(constIdx, scale, delta)) return false;
        return addMemoryKeyOffset(key, delta);
    }

    if (!idx || scale == 0) return scale == 0;
    if (!key.symbolicIndex) {
        key.symbolicIndex = idx;
        key.symbolicScale = scale;
        return true;
    }
    if (key.symbolicIndex != idx) return false;
    return addBounds(key.symbolicScale, scale, key.symbolicScale);
}

bool RangeAnalysis::typeElementCount(Type *ty, Type *elemType, long long &count) const {
    if (!ty || !elemType) return false;
    if (ty == elemType) {
        count = 1;
        return true;
    }
    auto *arrTy = dynamic_cast<ArrayType *>(ty);
    if (!arrTy) return false;
    long long nested = 0;
    if (!typeElementCount(arrTy->contained_, elemType, nested)) return false;
    return multiplyBounds(static_cast<long long>(arrTy->num_elements_), nested, count);
}

bool RangeAnalysis::decomposeMemoryAddress(Value *ptr, Type *elemType, MemoryKey &key) const {
    if (!ptr || !elemType) return false;

    auto *inst = dynamic_cast<Instruction *>(ptr);
    if (!inst) {
        key = MemoryKey{};
        key.base = ptr;
        key.elemType = elemType;
        return true;
    }

    if (inst->op_id_ == Instruction::BitCast) {
        return decomposeMemoryAddress(inst->get_operand(0), elemType, key);
    }

    if (inst->op_id_ != Instruction::GetElementPtr) {
        key = MemoryKey{};
        key.base = ptr;
        key.elemType = elemType;
        return true;
    }

    auto *gep = static_cast<GetElementPtrInst *>(inst);
    if (gep->num_ops() < 2) return false;

    Value *basePtr = gep->get_operand(0);
    auto *basePtrTy = dynamic_cast<PointerType *>(basePtr->type_);
    if (!basePtrTy) return false;

    if (!decomposeMemoryAddress(basePtr, elemType, key)) return false;

    Type *curTy = basePtrTy->contained_;
    bool pointerToArray = dynamic_cast<ArrayType *>(curTy) != nullptr;

    for (unsigned idxNo = 1; idxNo < gep->num_ops(); ++idxNo) {
        Value *idx = gep->get_operand(idxNo);

        if (pointerToArray && idxNo == 1) {
            long long leading = 0;
            if (!getConstInt(idx, leading) || leading != 0) return false;
            continue;
        }

        long long scale = 0;
        if (auto *arrTy = dynamic_cast<ArrayType *>(curTy)) {
            if (!typeElementCount(arrTy->contained_, elemType, scale)) return false;
            curTy = arrTy->contained_;
        } else {
            if (!typeElementCount(curTy, elemType, scale)) return false;
        }

        if (!addMemoryKeyOffset(key, idx, scale)) return false;
    }

    return true;
}

bool RangeAnalysis::getMemoryKey(Value *ptr, MemoryKey &key) const {
    auto *ptrTy = ptr ? dynamic_cast<PointerType *>(ptr->type_) : nullptr;
    if (!ptrTy) return false;

    Type *elemType = ptrTy->contained_;
    key = MemoryKey{};
    if (!decomposeMemoryAddress(ptr, elemType, key)) return false;
    return key.base && key.elemType == elemType;
}

void RangeAnalysis::killElementFactsFor(Value *ptr, MemoryFactSet &facts,
                                        BasicAliasAnalysis &AA) {
    MemoryKey killKey;
    bool knownKey = getMemoryKey(ptr, killKey);
    Value *killBase = knownKey ? killKey.base : AA.getUnderlyingObject(ptr);

    auto shouldKill = [&](const MemoryKey &factKey) {
        if (!knownKey) {
            return !killBase || factKey.base == killBase;
        }
        if (factKey.base != killKey.base) return false;
        if (killKey.hasSymbolicOffset() || factKey.hasSymbolicOffset())
            return true;
        return factKey.elemType == killKey.elemType &&
               factKey.constantOffset == killKey.constantOffset;
    };

    for (auto it = facts.elementUpper.begin(); it != facts.elementUpper.end();) {
        if (shouldKill(it->first)) {
            it = facts.elementUpper.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = facts.elementAbsUpper.begin(); it != facts.elementAbsUpper.end();) {
        if (shouldKill(it->first)) {
            it = facts.elementAbsUpper.erase(it);
        } else {
            ++it;
        }
    }
}

RangeAnalysis::MemoryFactSet
RangeAnalysis::meetMemoryFacts(const std::vector<MemoryFactSet> &predFacts) {
    MemoryFactSet result;
    if (predFacts.empty()) return result;

    result = predFacts.front();
    for (size_t i = 1; i < predFacts.size(); ++i) {
        for (auto it = result.pointerUpper.begin(); it != result.pointerUpper.end();) {
            auto jt = predFacts[i].pointerUpper.find(it->first);
            if (jt == predFacts[i].pointerUpper.end() || jt->second != it->second) {
                it = result.pointerUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = result.pointerAbsUpper.begin();
             it != result.pointerAbsUpper.end();) {
            auto jt = predFacts[i].pointerAbsUpper.find(it->first);
            if (jt == predFacts[i].pointerAbsUpper.end() || jt->second != it->second) {
                it = result.pointerAbsUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = result.elementUpper.begin(); it != result.elementUpper.end();) {
            auto jt = predFacts[i].elementUpper.find(it->first);
            if (jt == predFacts[i].elementUpper.end() || jt->second != it->second) {
                it = result.elementUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = result.elementAbsUpper.begin(); it != result.elementAbsUpper.end();) {
            auto jt = predFacts[i].elementAbsUpper.find(it->first);
            if (jt == predFacts[i].elementAbsUpper.end() || jt->second != it->second) {
                it = result.elementAbsUpper.erase(it);
            } else {
                ++it;
            }
        }
    }
    return result;
}

long long RangeAnalysis::getNormalizedValueMod(Value *v, BasicBlock *ctx) {
    auto range = getRange(v, ctx);
    if (!range.valid || range.isTop || range.isBottom) return 0;
    if (range.lower < 0 || range.upper < 0) return 0;
    if (range.upper == std::numeric_limits<long long>::max()) return 0;
    return range.upper + 1;
}

void RangeAnalysis::killMemoryFactsFor(Value *ptr, MemoryFactSet &facts,
                                       BasicAliasAnalysis &AA) {
    if (!ptr) {
        facts.pointerUpper.clear();
        facts.pointerAbsUpper.clear();
        facts.elementUpper.clear();
        facts.elementAbsUpper.clear();
        return;
    }

    killElementFactsFor(ptr, facts, AA);

    for (auto it = facts.pointerUpper.begin(); it != facts.pointerUpper.end();) {
        if (AA.alias(it->first, ptr) != AliasResult::NoAlias) {
            it = facts.pointerUpper.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = facts.pointerAbsUpper.begin(); it != facts.pointerAbsUpper.end();) {
        if (AA.alias(it->first, ptr) != AliasResult::NoAlias) {
            it = facts.pointerAbsUpper.erase(it);
        } else {
            ++it;
        }
    }
}

void RangeAnalysis::transferMemoryFact(Instruction *inst, MemoryFactSet &facts) {
    if (!inst || !AM_ || !func_ || !func_->parent_) return;

    auto &AA = AM_->getBasicAA(func_->parent_);

    if (inst->is_store()) {
        auto *ptr = inst->get_operand(1);
        killMemoryFactsFor(ptr, facts, AA);
        MemoryKey key;
        bool hasKey = getMemoryKey(ptr, key);

        long long upperPlusOne = getNormalizedValueMod(inst->get_operand(0), inst->parent_);
        if (upperPlusOne > 0) {
            facts.pointerUpper[ptr] = upperPlusOne - 1;
            if (hasKey)
                facts.elementUpper[key] = upperPlusOne - 1;
        }

        uint32_t absUpper = 0;
        if (ValueFacts::knownAbsBound(inst->get_operand(0), absUpper)) {
            facts.pointerAbsUpper[ptr] = absUpper;
            if (hasKey)
                facts.elementAbsUpper[key] = absUpper;
        }
        return;
    }

    if (inst->is_call()) {
        auto *call = static_cast<CallInst *>(inst);
        for (auto it = facts.pointerUpper.begin(); it != facts.pointerUpper.end();) {
            if (isModSet(AA.getCallModRef(call, it->first))) {
                it = facts.pointerUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = facts.pointerAbsUpper.begin();
             it != facts.pointerAbsUpper.end();) {
            if (isModSet(AA.getCallModRef(call, it->first))) {
                it = facts.pointerAbsUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = facts.elementUpper.begin(); it != facts.elementUpper.end();) {
            if (isModSet(AA.getCallModRef(call, it->first.base))) {
                it = facts.elementUpper.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = facts.elementAbsUpper.begin(); it != facts.elementAbsUpper.end();) {
            if (isModSet(AA.getCallModRef(call, it->first.base))) {
                it = facts.elementAbsUpper.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void RangeAnalysis::computeMemoryFacts() {
    if (memoryFactsComputed_ || memoryFactsComputing_ || !func_ || !AM_) return;
    memoryFactsComputing_ = true;
    memoryInFacts_.clear();
    memoryOutFacts_.clear();

    for (auto *bb : func_->basic_blocks_) {
        memoryInFacts_[bb] = MemoryFactSet{};
        memoryOutFacts_[bb] = MemoryFactSet{};
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : func_->basic_blocks_) {
            std::vector<MemoryFactSet> predFacts;
            for (auto *pred : bb->pre_bbs_) {
                auto it = memoryOutFacts_.find(pred);
                predFacts.push_back(it == memoryOutFacts_.end() ? MemoryFactSet{} : it->second);
            }

            MemoryFactSet in = meetMemoryFacts(predFacts);
            MemoryFactSet out = in;
            for (auto *inst : bb->instr_list_) {
                transferMemoryFact(inst, out);
            }

            if (memoryInFacts_[bb] != in || memoryOutFacts_[bb] != out) {
                memoryInFacts_[bb] = std::move(in);
                memoryOutFacts_[bb] = std::move(out);
                changed = true;
            }
        }
    }

    memoryFactsComputing_ = false;
    memoryFactsComputed_ = true;
    cache_.clear();
}

const RangeAnalysis::MemoryFactSet &RangeAnalysis::entryMemoryFacts(BasicBlock *bb) {
    static const MemoryFactSet empty;
    if (!bb) return empty;
    computeMemoryFacts();
    auto it = memoryInFacts_.find(bb);
    return it == memoryInFacts_.end() ? empty : it->second;
}

RangeAnalysis::MemoryFactSet RangeAnalysis::memoryFactsBefore(Instruction *target) {
    MemoryFactSet facts;
    if (!target || !target->parent_) return facts;
    facts = entryMemoryFacts(target->parent_);
    for (auto *inst : target->parent_->instr_list_) {
        if (inst == target) break;
        transferMemoryFact(inst, facts);
    }
    return facts;
}

RangeAnalysis::IntRange RangeAnalysis::getLoadRange(LoadInst *load, BasicBlock *ctx) {
    (void)ctx;
    if (!load || !isIntegerValue(load)) return IntRange::top();
    if (memoryFactsComputing_) return IntRange::top();

    auto facts = memoryFactsBefore(load);
    auto it = facts.pointerUpper.find(load->get_operand(0));
    if (it != facts.pointerUpper.end())
        return IntRange::bounded(0, it->second);
    auto absIt = facts.pointerAbsUpper.find(load->get_operand(0));
    if (absIt != facts.pointerAbsUpper.end()) {
        long long upper = absIt->second;
        return IntRange::bounded(-upper, upper);
    }
    MemoryKey key;
    if (getMemoryKey(load->get_operand(0), key)) {
        auto elemIt = facts.elementUpper.find(key);
        if (elemIt != facts.elementUpper.end())
            return IntRange::bounded(0, elemIt->second);
        auto elemAbsIt = facts.elementAbsUpper.find(key);
        if (elemAbsIt != facts.elementAbsUpper.end()) {
            long long upper = elemAbsIt->second;
            return IntRange::bounded(-upper, upper);
        }
    }
    return IntRange::top();
}

long long RangeAnalysis::inferDirectReturnModulus(Value *v) const {
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst || inst->op_id_ != Instruction::SRem) return 0;

    auto *mod = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!mod || mod->value_ <= 0) return 0;
    return mod->value_;
}

long long RangeAnalysis::getDirectNormalizedSRemMod(Value *v, BasicBlock *ctx) {
    long long mod = inferDirectReturnModulus(v);
    if (mod <= 0) return 0;

    auto *inst = static_cast<Instruction *>(v);
    auto dividendRange = getRange(inst->get_operand(0), ctx);
    if (!dividendRange.valid || dividendRange.isTop || dividendRange.isBottom ||
        !dividendRange.knownNonNegative()) {
        return 0;
    }
    return mod;
}

bool RangeAnalysis::inferNormalizedModulus(Value *v, long long &mod,
                                           std::set<Value *> &visiting) const {
    if (!v || !visiting.insert(v).second) return true;

    if (auto *inst = dynamic_cast<Instruction *>(v);
        inst && inst->op_id_ == Instruction::SRem) {
        long long directMod = inferDirectReturnModulus(v);
        if (directMod > 0) {
            if (mod == 0 || mod == directMod) {
                mod = directMod;
                return true;
            }
            return false;
        }
    }

    if (long long directMod = inferDirectReturnModulus(v); directMod > 0) {
        return true;
    }

    if (dynamic_cast<ConstantInt *>(v)) return true;

    if (auto *call = dynamic_cast<CallInst *>(v)) {
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
        if (callee == func_ && returnSummary_.pendingModulus > 0) {
            if (mod == 0 || mod == returnSummary_.pendingModulus) {
                mod = returnSummary_.pendingModulus;
                return true;
            }
            return false;
        }
        if (callee && !callee->is_declaration() && AM_ &&
            !AM_->isRangeAnalysisActive(callee)) {
            auto &calleeRA = AM_->getRangeAnalysis(callee);
            calleeRA.computeNormalizedReturnSummary();
            if (calleeRA.returnSummary_.conditionalKnown) {
                long long callMod = calleeRA.returnSummary_.modulus;
                if (mod == 0 || mod == callMod) {
                    mod = callMod;
                    return true;
                }
                return false;
            }
        }
        return true;
    }

    if (auto *phi = dynamic_cast<PhiInst *>(v)) {
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            if (!inferNormalizedModulus(phi->get_operand(i), mod, visiting))
                return false;
        }
        return true;
    }

    if (auto *sel = dynamic_cast<SelectInst *>(v)) {
        return inferNormalizedModulus(sel->get_operand(1), mod, visiting) &&
               inferNormalizedModulus(sel->get_operand(2), mod, visiting);
    }

    return true;
}

bool RangeAnalysis::addPendingNonNegativeArg(unsigned argNo) {
    auto &args = returnSummary_.pendingNonNegativeArgs;
    if (std::find(args.begin(), args.end(), argNo) != args.end()) return false;
    args.push_back(argNo);
    std::sort(args.begin(), args.end());
    return true;
}

bool RangeAnalysis::hasPendingNonNegativeArg(unsigned argNo) const {
    const auto &args = returnSummary_.pendingNonNegativeArgs;
    return std::find(args.begin(), args.end(), argNo) != args.end();
}

bool RangeAnalysis::isKnownNonNegativeForSummary(Value *v, BasicBlock *ctx) {
    std::set<Value *> visiting;
    return isKnownNonNegativeForSummary(v, ctx, visiting);
}

bool RangeAnalysis::isKnownNonNegativeForSummary(Value *v, BasicBlock *ctx,
                                                 std::set<Value *> &visiting) {
    if (!v) return false;
    if (!visiting.insert(v).second) return false;

    if (auto *ci = dynamic_cast<ConstantInt *>(v))
        return ci->value_ >= 0;

    if (auto *arg = dynamic_cast<Argument *>(v)) {
        if (arg->parent_ == func_ && hasPendingNonNegativeArg(arg->arg_no_))
            return true;
    }

    auto range = getRange(v, ctx);
    if (range.valid && !range.isTop && !range.isBottom &&
        range.knownNonNegative()) {
        return true;
    }

    if (auto *phi = dynamic_cast<PhiInst *>(v)) {
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            auto subVisited = visiting;
            if (!isKnownNonNegativeForSummary(phi->get_operand(i), predBB, subVisited))
                return false;
        }
        return true;
    }

    if (auto *sel = dynamic_cast<SelectInst *>(v)) {
        auto trueVisited = visiting;
        auto falseVisited = visiting;
        return isKnownNonNegativeForSummary(sel->get_operand(1), ctx, trueVisited) &&
               isKnownNonNegativeForSummary(sel->get_operand(2), ctx, falseVisited);
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return false;

    switch (inst->op_id_) {
    case Instruction::Add: {
        auto lhsVisited = visiting;
        auto rhsVisited = visiting;
        return isKnownNonNegativeForSummary(inst->get_operand(0), ctx, lhsVisited) &&
               isKnownNonNegativeForSummary(inst->get_operand(1), ctx, rhsVisited);
    }
    case Instruction::Shl: {
        long long shift = 0;
        return getConstInt(inst->get_operand(1), shift) && shift >= 0 &&
               isKnownNonNegativeForSummary(inst->get_operand(0), ctx, visiting);
    }
    case Instruction::SDiv: {
        long long divisor = 0;
        return getConstInt(inst->get_operand(1), divisor) && divisor > 0 &&
               isKnownNonNegativeForSummary(inst->get_operand(0), ctx, visiting);
    }
    case Instruction::SRem: {
        long long divisor = 0;
        return getConstInt(inst->get_operand(1), divisor) && divisor > 0 &&
               isKnownNonNegativeForSummary(inst->get_operand(0), ctx, visiting);
    }
    case Instruction::ZExt:
        return true;
    case Instruction::Call:
        return callSatisfiesReturnRequirements(static_cast<CallInst *>(inst), ctx);
    default:
        return false;
    }
}

bool RangeAnalysis::proveOrRequireNonNegative(Value *v, BasicBlock *ctx) {
    if (isKnownNonNegativeForSummary(v, ctx)) return true;

    auto *arg = dynamic_cast<Argument *>(v);
    if (!arg || arg->parent_ != func_ || !isIntegerValue(arg)) return false;
    addPendingNonNegativeArg(arg->arg_no_);
    return true;
}

bool RangeAnalysis::callSatisfiesReturnRequirements(CallInst *call, BasicBlock *ctx) {
    if (!call) return false;
    auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
    if (!callee || callee->is_declaration()) return false;

    const std::vector<unsigned> *requirements = nullptr;
    long long modulus = 0;
    if (callee == func_ && returnSummary_.computing) {
        requirements = &returnSummary_.pendingNonNegativeArgs;
        modulus = returnSummary_.pendingModulus;
    } else if (callee == func_) {
        computeNormalizedReturnSummary();
        if (!returnSummary_.conditionalKnown) return false;
        requirements = &returnSummary_.nonNegativeArgs;
        modulus = returnSummary_.modulus;
    } else {
        if (!AM_ || AM_->isRangeAnalysisActive(callee)) return false;
        auto &calleeRA = AM_->getRangeAnalysis(callee);
        calleeRA.computeNormalizedReturnSummary();
        if (!calleeRA.returnSummary_.conditionalKnown) return false;
        requirements = &calleeRA.returnSummary_.nonNegativeArgs;
        modulus = calleeRA.returnSummary_.modulus;
    }
    if (modulus <= 0 || !requirements) return false;

    for (unsigned argNo : *requirements) {
        if (argNo >= call->num_ops() - 1) return false;
        Value *actual = call->get_operand(argNo);
        if (callee == func_) {
            auto *actualArg = dynamic_cast<Argument *>(actual);
            if (actualArg && actualArg->parent_ == func_ &&
                actualArg->arg_no_ == argNo) {
                continue;
            }
        }
        auto *caller = call->parent_ ? call->parent_->parent_ : nullptr;
        RangeAnalysis *contextRA = this;
        if (caller && caller != func_) {
            if (!AM_) return false;
            contextRA = &AM_->getRangeAnalysis(caller);
        }
        auto range = contextRA->getRange(actual, ctx);
        if (!range.valid || range.isTop || range.isBottom || range.lower < 0) {
            return false;
        }
    }
    return true;
}

bool RangeAnalysis::allCallSitesSatisfyReturnRequirements() {
    if (!func_ || !func_->parent_) return false;
    if (returnSummary_.pendingNonNegativeArgs.empty()) return true;
    if (!AM_) return false;

    for (auto *caller : func_->parent_->function_list_) {
        if (!caller || caller->is_declaration()) continue;
        for (auto *bb : caller->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call) continue;
                auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
                if (callee != func_) continue;
                if (!callSatisfiesReturnRequirements(call, call->parent_))
                    return false;
            }
        }
    }
    return true;
}

bool RangeAnalysis::valueMatchesNormalizedMod(Value *v, BasicBlock *ctx, long long mod) {
    if (mod <= 0) return false;
    if (inferDirectReturnModulus(v) == mod) {
        auto *inst = static_cast<Instruction *>(v);
        return proveOrRequireNonNegative(inst->get_operand(0), ctx);
    }

    if (auto *phi = dynamic_cast<PhiInst *>(v)) {
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!valueMatchesNormalizedMod(phi->get_operand(i), predBB, mod))
                return false;
        }
        return true;
    }

    if (auto *sel = dynamic_cast<SelectInst *>(v)) {
        return valueMatchesNormalizedMod(sel->get_operand(1), ctx, mod) &&
               valueMatchesNormalizedMod(sel->get_operand(2), ctx, mod);
    }

    if (auto *call = dynamic_cast<CallInst *>(v)) {
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
        if (callee == func_ && returnSummary_.computing && returnSummary_.pendingModulus == mod) {
            return callSatisfiesReturnRequirements(call, ctx);
        }
        if (callee && !callee->is_declaration() && AM_ &&
            !AM_->isRangeAnalysisActive(callee)) {
            auto &calleeRA = AM_->getRangeAnalysis(callee);
            calleeRA.computeNormalizedReturnSummary();
            if (calleeRA.returnSummary_.conditionalKnown &&
                calleeRA.returnSummary_.modulus == mod)
                return calleeRA.callSatisfiesReturnRequirements(call, ctx);
        }
    }

    auto range = getRange(v, ctx);
    return range.valid && !range.isTop && !range.isBottom &&
           range.lower >= 0 && range.upper < mod;
}

void RangeAnalysis::computeNormalizedReturnSummary() {
    if (returnSummary_.computed) {
        return;
    }
    if (returnSummary_.computing) {
        return;
    }
    if (!func_ || func_->is_declaration()) {
        returnSummary_.computed = true;
        return;
    }

    returnSummary_.computing = true;
    returnSummary_.pendingModulus = 0;
    returnSummary_.pendingNonNegativeArgs.clear();

    bool sawReturn = false;
    bool ok = true;
    long long candidateMod = 0;
    std::set<Value *> visitedValues;

    for (auto *bb : func_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *ret = dynamic_cast<ReturnInst *>(inst);
            if (!ret || ret->num_ops() == 0) continue;
            sawReturn = true;
            if (!inferNormalizedModulus(ret->get_operand(0), candidateMod, visitedValues)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    if (ok && candidateMod > 0) {
        returnSummary_.pendingModulus = candidateMod;
        bool changed = true;
        while (ok && changed) {
            auto before = returnSummary_.pendingNonNegativeArgs;
            for (auto *bb : func_->basic_blocks_) {
                for (auto *inst : bb->instr_list_) {
                    auto *ret = dynamic_cast<ReturnInst *>(inst);
                    if (!ret || ret->num_ops() == 0) continue;
                    if (!valueMatchesNormalizedMod(ret->get_operand(0), ret->parent_, candidateMod)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
            }
            changed = before != returnSummary_.pendingNonNegativeArgs;
        }
    } else if (candidateMod == 0) {
        ok = false;
    }

    returnSummary_.computing = false;
    returnSummary_.computed = true;
    returnSummary_.pendingModulus = 0;
    returnSummary_.conditionalKnown = ok && sawReturn && candidateMod > 0;
    returnSummary_.modulus = returnSummary_.conditionalKnown ? candidateMod : 0;
    returnSummary_.nonNegativeArgs = returnSummary_.conditionalKnown
        ? returnSummary_.pendingNonNegativeArgs
        : std::vector<unsigned>{};
    returnSummary_.known = returnSummary_.conditionalKnown &&
                           allCallSitesSatisfyReturnRequirements();
    if (!returnSummary_.conditionalKnown) {
        returnSummary_.pendingNonNegativeArgs.clear();
    }
}

RangeAnalysis::IntRange RangeAnalysis::getNormalizedReturnRange() {
    computeNormalizedReturnSummary();

    if (!returnSummary_.known) return IntRange::top();
    return IntRange::bounded(0, returnSummary_.modulus - 1);
}

RangeAnalysis::IntRange RangeAnalysis::getNormalizedReturnRangeForCall(CallInst *call,
                                                                       BasicBlock *ctx) {
    computeNormalizedReturnSummary();
    if (!returnSummary_.conditionalKnown) return IntRange::top();
    if (!returnSummary_.known && !callSatisfiesReturnRequirements(call, ctx))
        return IntRange::top();
    auto result = IntRange::bounded(0, returnSummary_.modulus - 1);
    if (ctx) result = applyFacts(call, result, ctx);
    return result;
}
