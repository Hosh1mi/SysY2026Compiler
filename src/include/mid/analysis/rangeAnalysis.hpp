#pragma once

#include "scalarEvolution.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

class AnalysisManager;

class RangeAnalysis {
public:
    enum class TruthValue {
        AlwaysFalse,
        Unknown,
        AlwaysTrue,
    };

    struct IntRange {
        bool valid = false;
        bool isTop = true;
        bool isBottom = false;
        long long lower = 0;
        long long upper = 0;

        static IntRange top() {
            IntRange r;
            r.valid = true;
            r.isTop = true;
            return r;
        }

        static IntRange bottom() {
            IntRange r;
            r.valid = true;
            r.isTop = false;
            r.isBottom = true;
            return r;
        }

        static IntRange constant(long long v) {
            IntRange r;
            r.valid = true;
            r.isTop = false;
            r.lower = v;
            r.upper = v;
            return r;
        }

        static IntRange bounded(long long lo, long long hi) {
            if (lo > hi) return bottom();
            IntRange r;
            r.valid = true;
            r.isTop = false;
            r.lower = lo;
            r.upper = hi;
            return r;
        }

        bool isSingleton() const {
            return valid && !isTop && !isBottom && lower == upper;
        }

        bool knownNonNegative() const {
            return valid && !isTop && !isBottom && lower >= 0;
        }

        IntRange join(const IntRange &o) const {
            if (!valid) return o;
            if (!o.valid) return *this;
            if (isTop || o.isTop) return top();
            if (isBottom) return o;
            if (o.isBottom) return *this;
            return bounded(std::min(lower, o.lower), std::max(upper, o.upper));
        }

        IntRange intersect(const IntRange &o) const {
            if (!valid || !o.valid) return bottom();
            if (isBottom || o.isBottom) return bottom();
            if (isTop) return o;
            if (o.isTop) return *this;
            return bounded(std::max(lower, o.lower), std::min(upper, o.upper));
        }
    };

    RangeAnalysis(Function *func, AnalysisManager *AM, const LoopInfo &LI, ScalarEvolution &SE);

    void clear();
    void clearCache();

    IntRange getRange(Value *v, BasicBlock *ctx = nullptr);
    TruthValue getPredicateResult(ICmpInst::ICmpOp pred, Value *lhs, Value *rhs,
                                  BasicBlock *ctx = nullptr);
    bool isKnownNonNegative(Value *v, BasicBlock *ctx = nullptr);
    IntRange getGEPOffsetRange(GetElementPtrInst *gep, BasicBlock *ctx = nullptr);

private:
    struct PredicateFact {
        Value *lhs = nullptr;
        Value *rhs = nullptr;
        ICmpInst::ICmpOp pred = ICmpInst::ICMP_EQ;
        bool branchTaken = true;
    };

    struct CacheKey {
        Value *value = nullptr;
        BasicBlock *ctx = nullptr;

        bool operator<(const CacheKey &o) const {
            if (value != o.value) return value < o.value;
            return ctx < o.ctx;
        }
    };

    IntRange getRangeImpl(Value *v, BasicBlock *ctx);
    IntRange getIntrinsicRange(Value *v);
    IntRange getConstantRange(ConstantInt *ci) const;
    IntRange getBinaryRange(BinaryInst *bin, BasicBlock *ctx);
    IntRange getZExtRange(ZextInst *zext, BasicBlock *ctx);
    IntRange getPhiRange(PhiInst *phi, BasicBlock *ctx);
    IntRange getCallRange(CallInst *call, BasicBlock *ctx);
    IntRange getArgumentRange(Argument *arg, BasicBlock *ctx);
    IntRange getICmpRange(ICmpInst *icmp, BasicBlock *ctx);
    IntRange getSelectRange(SelectInst *sel, BasicBlock *ctx);
    IntRange getSCEVRange(Value *v, BasicBlock *ctx);
    IntRange getNormalizedReturnRange();
    bool valueMatchesNormalizedMod(Value *v, BasicBlock *ctx, long long mod);
    bool inferNormalizedModulus(Value *v, long long &mod, std::set<Value *> &visiting) const;
    long long getDirectNormalizedSRemMod(Value *v, BasicBlock *ctx);
    long long inferDirectReturnModulus(Value *v) const;

    TruthValue compareRanges(ICmpInst::ICmpOp pred, const IntRange &lhs,
                             const IntRange &rhs) const;
    IntRange refineWithFact(Value *query, const IntRange &base,
                            const PredicateFact &fact) const;
    IntRange applyFacts(Value *query, const IntRange &base, BasicBlock *ctx);

    const std::vector<PredicateFact> &factsForBlock(BasicBlock *bb);
    void collectFacts(BasicBlock *bb, std::vector<PredicateFact> &out,
                      std::set<BasicBlock *> &visiting);
    void computePostDom(Function *func);
    void buildControlDependence(Function *func);
    BasicBlock *immediatePostDom(BasicBlock *bb) const;

    static ICmpInst::ICmpOp negatePredicate(ICmpInst::ICmpOp pred);
    static ICmpInst::ICmpOp swapPredicate(ICmpInst::ICmpOp pred);
    static std::pair<long long, long long> typeBounds(Type *ty);
    static bool multiplyBounds(long long a, long long b, long long &out);
    static bool addBounds(long long a, long long b, long long &out);
    static bool subtractBounds(long long a, long long b, long long &out);
    static bool getConstInt(Value *v, long long &out);
    static bool isIntegerValue(Value *v);

    Function *func_ = nullptr;
    AnalysisManager *AM_ = nullptr;
    const LoopInfo *LI_ = nullptr;
    ScalarEvolution *SE_ = nullptr;

    std::map<CacheKey, IntRange> cache_;
    std::map<BasicBlock *, std::vector<PredicateFact>> blockFacts_;
    std::map<BasicBlock *, std::set<BasicBlock *>> postDomSets_;
    std::map<BasicBlock *, BasicBlock *> ipdom_;
    std::set<std::pair<Value *, BasicBlock *>> visiting_;
    unsigned queryDepth_ = 0;

    struct ReturnSummary {
        bool computed = false;
        bool computing = false;
        bool known = false;
        long long pendingModulus = 0;
        long long modulus = 0;
    };
    ReturnSummary returnSummary_;
};
