#include "../../include/mid/analysis/rangeAnalysis.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

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
    computePostDom(func);
    buildControlDependence(func);
}

void RangeAnalysis::clear() {
    cache_.clear();
    blockFacts_.clear();
    postDomSets_.clear();
    ipdom_.clear();
    visiting_.clear();
    returnSummary_ = ReturnSummary{};
}

void RangeAnalysis::clearCache() {
    cache_.clear();
    visiting_.clear();
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

void RangeAnalysis::computePostDom(Function *func) {
    if (!func || func->basic_blocks_.empty()) return;

    std::set<BasicBlock *> all(func->basic_blocks_.begin(), func->basic_blocks_.end());
    for (auto *bb : func->basic_blocks_) {
        if (bb->succ_bbs_.empty()) postDomSets_[bb] = {bb};
        else postDomSets_[bb] = all;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : func->basic_blocks_) {
            if (bb->succ_bbs_.empty()) continue;

            std::set<BasicBlock *> next;
            bool first = true;
            for (auto *succ : bb->succ_bbs_) {
                auto it = postDomSets_.find(succ);
                if (it == postDomSets_.end()) continue;
                if (first) {
                    next = it->second;
                    first = false;
                } else {
                    std::set<BasicBlock *> inter;
                    for (auto *x : next) {
                        if (it->second.count(x)) inter.insert(x);
                    }
                    next = std::move(inter);
                }
            }
            next.insert(bb);
            if (next != postDomSets_[bb]) {
                postDomSets_[bb] = std::move(next);
                changed = true;
            }
        }
    }

    for (auto *bb : func->basic_blocks_) {
        if (bb->succ_bbs_.empty()) {
            ipdom_[bb] = nullptr;
            continue;
        }

        auto it = postDomSets_.find(bb);
        if (it == postDomSets_.end()) continue;
        const auto &set = it->second;
        BasicBlock *cand = nullptr;
        size_t targetSize = set.size() > 0 ? set.size() - 1 : 0;
        for (auto *p : set) {
            if (p == bb) continue;
            auto pit = postDomSets_.find(p);
            if (pit != postDomSets_.end() && pit->second.size() == targetSize) {
                cand = p;
                break;
            }
        }
        ipdom_[bb] = cand;
    }
}

BasicBlock *RangeAnalysis::immediatePostDom(BasicBlock *bb) const {
    auto it = ipdom_.find(bb);
    return it == ipdom_.end() ? nullptr : it->second;
}

void RangeAnalysis::buildControlDependence(Function *func) {
    if (!func) return;
    for (auto *bb : func->basic_blocks_) {
        auto *term = bb->get_terminator();
        auto *br = dynamic_cast<BranchInst *>(term);
        if (!br || br->num_ops_ != 3) continue;

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
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
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
    auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
    if (!callee || callee->is_declaration()) return IntRange::top();
    if (callee == func_) {
        auto selfSummary = getNormalizedReturnRange();
        if (selfSummary.valid && !selfSummary.isTop && !selfSummary.isBottom) {
            if (ctx) selfSummary = applyFacts(call, selfSummary, ctx);
            return selfSummary;
        }
        if (returnSummary_.computing && returnSummary_.pendingModulus > 0) {
            return IntRange::bounded(0, returnSummary_.pendingModulus - 1);
        }
    }
    if (AM_->isRangeAnalysisActive(callee)) return IntRange::top();
    auto &calleeRA = AM_->getRangeAnalysis(callee);
    auto summaryRange = calleeRA.getNormalizedReturnRange();
    if (summaryRange.valid && !summaryRange.isTop && !summaryRange.isBottom) {
        if (ctx) summaryRange = applyFacts(call, summaryRange, ctx);
        return summaryRange;
    }

    IntRange result = IntRange::bottom();
    bool found = false;
    for (auto *bb : callee->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_ret()) continue;
            auto *ret = static_cast<ReturnInst *>(inst);
            if (ret->num_ops_ == 0) continue;
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
    auto *module = func->parent_;
    if (!module) return IntRange::top();

    for (auto *caller : module->function_list_) {
        if (caller->is_declaration()) continue;
        auto &callerRA = AM_->getRangeAnalysis(caller);
        for (auto *bb : caller->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call) continue;
                auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
                if (callee != func) continue;
                if (arg->arg_no_ >= call->num_ops_ - 1) continue;
                auto r = callerRA.getRange(call->get_operand(arg->arg_no_), call->parent_);
                if (!found) {
                    result = r;
                    found = true;
                } else {
                    result = result.join(r);
                }
            }
        }
    }
    if (!found) return IntRange::top();
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
    if (!gep || gep->num_ops_ < 2) return IntRange::top();

    Value *base = gep->get_operand(0);
    auto *ptrTy = dynamic_cast<PointerType *>(base->type_);
    if (!ptrTy) return IntRange::top();

    for (unsigned i = 1; i < gep->num_ops_; ++i) {
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

    for (unsigned idxNo = 0; idxNo < gep->num_ops_ - 2; ++idxNo) {
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
    return t.join(f);
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
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee == func_ && returnSummary_.pendingModulus > 0) {
            if (mod == 0 || mod == returnSummary_.pendingModulus) {
                mod = returnSummary_.pendingModulus;
                return true;
            }
            return false;
        }
        if (callee && !callee->is_declaration() && AM_ &&
            !AM_->isRangeAnalysisActive(callee)) {
            auto summary = AM_->getRangeAnalysis(callee).getNormalizedReturnRange();
            if (summary.valid && !summary.isTop && !summary.isBottom) {
                long long callMod = summary.upper + 1;
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
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
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

bool RangeAnalysis::valueMatchesNormalizedMod(Value *v, BasicBlock *ctx, long long mod) {
    if (mod <= 0) return false;
    if (inferDirectReturnModulus(v) == mod) return true;

    if (auto *phi = dynamic_cast<PhiInst *>(v)) {
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
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
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee == func_ && returnSummary_.computing && returnSummary_.pendingModulus == mod) {
            return true;
        }
        if (callee && !callee->is_declaration() && AM_ &&
            !AM_->isRangeAnalysisActive(callee)) {
            auto summary = AM_->getRangeAnalysis(callee).getNormalizedReturnRange();
            if (summary.valid && !summary.isTop && !summary.isBottom &&
                summary.lower == 0 && summary.upper + 1 == mod) {
                return true;
            }
        }
    }

    auto range = getRange(v, ctx);
    return range.valid && !range.isTop && !range.isBottom &&
           range.lower >= 0 && range.upper < mod;
}

RangeAnalysis::IntRange RangeAnalysis::getNormalizedReturnRange() {
    if (returnSummary_.computed) {
        if (!returnSummary_.known) return IntRange::top();
        return IntRange::bounded(0, returnSummary_.modulus - 1);
    }
    if (returnSummary_.computing) {
        if (returnSummary_.pendingModulus > 0)
            return IntRange::bounded(0, returnSummary_.pendingModulus - 1);
        return IntRange::top();
    }
    if (!func_ || func_->is_declaration()) return IntRange::top();

    returnSummary_.computing = true;
    returnSummary_.pendingModulus = 0;

    bool sawReturn = false;
    bool ok = true;
    long long candidateMod = 0;
    std::set<Value *> visitedValues;

    for (auto *bb : func_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *ret = dynamic_cast<ReturnInst *>(inst);
            if (!ret || ret->num_ops_ == 0) continue;
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
        for (auto *bb : func_->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                auto *ret = dynamic_cast<ReturnInst *>(inst);
                if (!ret || ret->num_ops_ == 0) continue;
                if (!valueMatchesNormalizedMod(ret->get_operand(0), ret->parent_, candidateMod)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
    } else if (candidateMod == 0) {
        ok = false;
    }

    returnSummary_.computing = false;
    returnSummary_.computed = true;
    returnSummary_.pendingModulus = 0;
    returnSummary_.known = ok && sawReturn && candidateMod > 0;
    returnSummary_.modulus = returnSummary_.known ? candidateMod : 0;

    if (!returnSummary_.known) return IntRange::top();
    return IntRange::bounded(0, returnSummary_.modulus - 1);
}
