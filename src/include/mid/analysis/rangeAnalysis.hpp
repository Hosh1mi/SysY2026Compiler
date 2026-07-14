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
class BasicAliasAnalysis;

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
    IntRange getLoadRange(LoadInst *load, BasicBlock *ctx);
    IntRange getICmpRange(ICmpInst *icmp, BasicBlock *ctx);
    IntRange getSelectRange(SelectInst *sel, BasicBlock *ctx);
    IntRange getSCEVRange(Value *v, BasicBlock *ctx);
    IntRange getNormalizedReturnRange();
    IntRange getNormalizedReturnRangeForCall(CallInst *call, BasicBlock *ctx);
    void computeNormalizedReturnSummary();
    bool valueMatchesNormalizedMod(Value *v, BasicBlock *ctx, long long mod);
    bool inferNormalizedModulus(Value *v, long long &mod, std::set<Value *> &visiting) const;
    long long getDirectNormalizedSRemMod(Value *v, BasicBlock *ctx);
    long long inferDirectReturnModulus(Value *v) const;
    long long getNormalizedValueMod(Value *v, BasicBlock *ctx);
    bool proveOrRequireNonNegative(Value *v, BasicBlock *ctx);
    bool isKnownNonNegativeForSummary(Value *v, BasicBlock *ctx);
    bool isKnownNonNegativeForSummary(Value *v, BasicBlock *ctx, std::set<Value *> &visiting);
    bool addPendingNonNegativeArg(unsigned argNo);
    bool hasPendingNonNegativeArg(unsigned argNo) const;
    bool callSatisfiesReturnRequirements(CallInst *call, BasicBlock *ctx);
    bool allCallSitesSatisfyReturnRequirements();

    struct MemoryKey {
        Value *base = nullptr;
        Type *elemType = nullptr;
        long long constantOffset = 0;
        Value *symbolicIndex = nullptr;
        long long symbolicScale = 0;

        bool operator<(const MemoryKey &o) const {
            if (base != o.base) return base < o.base;
            if (elemType != o.elemType) return elemType < o.elemType;
            if (constantOffset != o.constantOffset) return constantOffset < o.constantOffset;
            if (symbolicIndex != o.symbolicIndex) return symbolicIndex < o.symbolicIndex;
            return symbolicScale < o.symbolicScale;
        }

        bool operator==(const MemoryKey &o) const {
            return base == o.base &&
                   elemType == o.elemType &&
                   constantOffset == o.constantOffset &&
                   symbolicIndex == o.symbolicIndex &&
                   symbolicScale == o.symbolicScale;
        }

        bool hasSymbolicOffset() const {
            return symbolicIndex != nullptr;
        }
    };

    struct MemoryFactSet {
        std::map<Value *, long long> pointerUpper;
        std::map<Value *, long long> pointerAbsUpper;
        std::map<MemoryKey, long long> elementUpper;
        std::map<MemoryKey, long long> elementAbsUpper;

        bool operator==(const MemoryFactSet &o) const {
            return pointerUpper == o.pointerUpper &&
                   pointerAbsUpper == o.pointerAbsUpper &&
                   elementUpper == o.elementUpper &&
                   elementAbsUpper == o.elementAbsUpper;
        }

        bool operator!=(const MemoryFactSet &o) const {
            return !(*this == o);
        }
    };

    const MemoryFactSet &entryMemoryFacts(BasicBlock *bb);
    MemoryFactSet memoryFactsBefore(Instruction *target);
    void computeMemoryFacts();
    void transferMemoryFact(Instruction *inst, MemoryFactSet &facts);
    void killMemoryFactsFor(Value *ptr, MemoryFactSet &facts, BasicAliasAnalysis &AA);
    bool getMemoryKey(Value *ptr, MemoryKey &key) const;
    bool decomposeMemoryAddress(Value *ptr, Type *elemType, MemoryKey &key) const;
    bool addMemoryKeyOffset(MemoryKey &key, Value *idx, long long scale) const;
    bool addMemoryKeyOffset(MemoryKey &key, long long offset) const;
    bool typeElementCount(Type *ty, Type *elemType, long long &count) const;
    void killElementFactsFor(Value *ptr, MemoryFactSet &facts, BasicAliasAnalysis &AA);
    static MemoryFactSet meetMemoryFacts(const std::vector<MemoryFactSet> &predFacts);

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
    std::map<BasicBlock *, MemoryFactSet> memoryInFacts_;
    std::map<BasicBlock *, MemoryFactSet> memoryOutFacts_;
    std::map<BasicBlock *, std::set<BasicBlock *>> postDomSets_;
    std::map<BasicBlock *, BasicBlock *> ipdom_;
    std::set<std::pair<Value *, BasicBlock *>> visiting_;
    unsigned queryDepth_ = 0;
    bool memoryFactsComputed_ = false;
    bool memoryFactsComputing_ = false;

    struct ReturnSummary {
        bool computed = false;
        bool computing = false;
        bool known = false;
        bool conditionalKnown = false;
        long long pendingModulus = 0;
        long long modulus = 0;
        std::vector<unsigned> pendingNonNegativeArgs;
        std::vector<unsigned> nonNegativeArgs;
    };
    ReturnSummary returnSummary_;
};
