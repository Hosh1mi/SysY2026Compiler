#include "../../include/mid/analysis/lazyValueInfo.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"

#include <tuple>
#include <unordered_set>

namespace {

struct PredicateQueryKey {
    ICmpInst::ICmpOp pred;
    Value *lhs;
    Value *rhs;

    bool operator==(const PredicateQueryKey &other) const {
        return pred == other.pred && lhs == other.lhs && rhs == other.rhs;
    }
};

struct PredicateQueryKeyHash {
    size_t operator()(const PredicateQueryKey &key) const {
        ICmpKeyHash hash;
        return hash(ICmpKey{key.pred, key.lhs, key.rhs});
    }
};

struct QueryState {
    Function *func = nullptr;
    Module *module = nullptr;
    BasicBlock *block = nullptr;
    BasicBlock *fromEdge = nullptr;
    BasicBlock *toEdge = nullptr;
    bool useBlockValue = true;
    const LoopInfo *loopInfo = nullptr;
    const DominatorTreeAnalysis *domTree = nullptr;
    int depth = 0;
    std::unordered_set<Value *> activeValues;
    std::unordered_set<PredicateQueryKey, PredicateQueryKeyHash> activePredicates;
};

static ICmpInst::ICmpOp swapICmpPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_UGT:
        return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE:
        return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_ULT:
        return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE:
        return ICmpInst::ICMP_UGE;
    default:
        return pred;
    }
}

static bool evaluateICmpConstants(ICmpInst::ICmpOp pred, int lhs, int rhs) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:
        return lhs == rhs;
    case ICmpInst::ICMP_NE:
        return lhs != rhs;
    case ICmpInst::ICMP_UGT:
        return static_cast<unsigned>(lhs) > static_cast<unsigned>(rhs);
    case ICmpInst::ICMP_UGE:
        return static_cast<unsigned>(lhs) >= static_cast<unsigned>(rhs);
    case ICmpInst::ICMP_ULT:
        return static_cast<unsigned>(lhs) < static_cast<unsigned>(rhs);
    case ICmpInst::ICMP_ULE:
        return static_cast<unsigned>(lhs) <= static_cast<unsigned>(rhs);
    case ICmpInst::ICMP_SGT:
        return lhs > rhs;
    case ICmpInst::ICMP_SGE:
        return lhs >= rhs;
    case ICmpInst::ICMP_SLT:
        return lhs < rhs;
    case ICmpInst::ICMP_SLE:
        return lhs <= rhs;
    }
    return false;
}

static Value *getIncomingValueForPred(PhiInst *phi, BasicBlock *pred) {
    if (!phi || !pred) return nullptr;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

static void collectEdgeFacts(BasicBlock *fromBB, BasicBlock *toBB,
                             BoolFactMap &boolFacts, ICmpFactMap &cmpFacts) {
    if (!fromBB || !toBB) return;
    auto *br = dynamic_cast<BranchInst *>(fromBB->get_terminator());
    if (!br || br->num_ops() != 3) return;

    auto *trueDest = dynamic_cast<BasicBlock *>(br->get_operand(1));
    auto *falseDest = dynamic_cast<BasicBlock *>(br->get_operand(2));
    if (!trueDest || !falseDest) return;
    if (trueDest != toBB && falseDest != toBB) return;

    recordAssumedBool(br->get_operand(0), trueDest == toBB, boolFacts, cmpFacts);
}

static bool isSameOrNestedLoop(const Loop *inner, const Loop *outer) {
    if (!outer) return true;
    for (const Loop *loop = inner; loop; loop = loop->parent)
        if (loop == outer) return true;
    return false;
}

static void collectDominatingEdgeFacts(const QueryState &state,
                                       BoolFactMap &boolFacts,
                                       ICmpFactMap &cmpFacts) {
    if (!state.func || !state.block || !state.loopInfo || !state.domTree)
        return;

    Loop *contextLoop = state.loopInfo->getLoopFor(state.block);
    for (auto *bb : state.func->basic_blocks_) {
        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br || br->num_ops() != 3 || !bb->parent_)
            continue;

        Loop *branchLoop = state.loopInfo->getLoopFor(bb);
        // A fact established in a loop iteration is not valid after leaving
        // that loop: the same SSA instruction may execute again with the next
        // iteration's phi values.
        if (!isSameOrNestedLoop(contextLoop, branchLoop))
            continue;

        auto recordIfDominating = [&](unsigned successorIndex, bool assumed) {
            auto *succ = dynamic_cast<BasicBlock *>(br->get_operand(successorIndex));
            if (!succ)
                return;
            bool hasDedicatedEdge =
                succ->pre_bbs_.size() == 1 && succ->pre_bbs_.front() == bb;
            if (!hasDedicatedEdge) {
                Loop *successorLoop = state.loopInfo->getLoopFor(succ);
                bool isUniqueLoopEntry =
                    successorLoop && successorLoop->header == succ &&
                    !successorLoop->isInLoop(bb);
                if (isUniqueLoopEntry) {
                    for (auto *pred : succ->pre_bbs_) {
                        if (!successorLoop->isInLoop(pred) && pred != bb) {
                            isUniqueLoopEntry = false;
                            break;
                        }
                    }
                }
                if (!isUniqueLoopEntry)
                    return;
            }
            if (!state.domTree->dominates(succ, state.block))
                return;
            recordAssumedBool(br->get_operand(0), assumed, boolFacts, cmpFacts);
        };

        recordIfDominating(1, true);
        recordIfDominating(2, false);
    }
}

static void gatherFacts(const QueryState &state, BoolFactMap &boolFacts,
                        ICmpFactMap &cmpFacts) {
    collectDominatingEdgeFacts(state, boolFacts, cmpFacts);
    if (state.fromEdge && state.toEdge) {
        collectEdgeFacts(state.fromEdge, state.toEdge, boolFacts, cmpFacts);
        return;
    }

    if (!state.useBlockValue || !state.block || state.block->pre_bbs_.size() != 1)
        return;

    collectEdgeFacts(state.block->pre_bbs_.front(), state.block, boolFacts, cmpFacts);
}

static Constant *evaluateConstant(Value *value, QueryState &state);
static std::optional<bool> evaluateBool(Value *value, QueryState &state);

static std::optional<bool> lookupPredicateFacts(ICmpInst::ICmpOp pred, Value *lhs,
                                                Value *rhs,
                                                const BoolFactMap &boolFacts,
                                                const ICmpFactMap &cmpFacts) {
    if (auto known = getKnownBool(lhs, boolFacts, cmpFacts);
        known.has_value() && lhs == rhs &&
        (pred == ICmpInst::ICMP_EQ || pred == ICmpInst::ICMP_NE)) {
        return pred == ICmpInst::ICMP_EQ;
    }

    ICmpKey exact{pred, lhs, rhs};
    auto exactIt = cmpFacts.find(exact);
    if (exactIt != cmpFacts.end())
        return exactIt->second;

    ICmpKey inverse{invertICmpPredicate(pred), lhs, rhs};
    auto inverseIt = cmpFacts.find(inverse);
    if (inverseIt != cmpFacts.end())
        return !inverseIt->second;

    ICmpKey swapped{swapICmpPredicate(pred), rhs, lhs};
    auto swappedIt = cmpFacts.find(swapped);
    if (swappedIt != cmpFacts.end())
        return swappedIt->second;

    ICmpKey swappedInverse{invertICmpPredicate(swapICmpPredicate(pred)), rhs, lhs};
    auto swappedInverseIt = cmpFacts.find(swappedInverse);
    if (swappedInverseIt != cmpFacts.end())
        return !swappedInverseIt->second;

    return std::nullopt;
}

static std::optional<bool> evaluatePredicate(ICmpInst::ICmpOp pred, Value *lhs,
                                             Value *rhs, QueryState &state) {
    if (state.depth > 24) return std::nullopt;

    BoolFactMap boolFacts;
    ICmpFactMap cmpFacts;
    gatherFacts(state, boolFacts, cmpFacts);
    if (auto known = lookupPredicateFacts(pred, lhs, rhs, boolFacts, cmpFacts);
        known.has_value()) {
        return known;
    }

    if (lhs == rhs) {
        switch (pred) {
        case ICmpInst::ICMP_EQ:
        case ICmpInst::ICMP_UGE:
        case ICmpInst::ICMP_ULE:
        case ICmpInst::ICMP_SGE:
        case ICmpInst::ICMP_SLE:
            return true;
        case ICmpInst::ICMP_NE:
        case ICmpInst::ICMP_UGT:
        case ICmpInst::ICMP_ULT:
        case ICmpInst::ICMP_SGT:
        case ICmpInst::ICMP_SLT:
            return false;
        }
    }

    int lhsConst = 0;
    int rhsConst = 0;
    Constant *lhsValue = evaluateConstant(lhs, state);
    Constant *rhsValue = evaluateConstant(rhs, state);
    if (lhsValue && rhsValue &&
        getIntegerConstantValue(lhsValue, lhsConst) &&
        getIntegerConstantValue(rhsValue, rhsConst)) {
        return evaluateICmpConstants(pred, lhsConst, rhsConst);
    }

    auto decodeWrappedBool = [&](Value *boolValue, Value *constant,
                                 bool boolIsLHS) -> std::optional<bool> {
        if (!boolValue || boolValue->type_->tid_ != Type::IntegerTyID ||
            static_cast<IntegerType *>(boolValue->type_)->num_bits_ != 1) {
            return std::nullopt;
        }

        int wrappedConst = 0;
        if (!getIntegerConstantValue(constant, wrappedConst))
            return std::nullopt;

        auto knownBool = evaluateBool(boolValue, state);
        if (!knownBool.has_value()) return std::nullopt;

        bool eq = (*knownBool == (wrappedConst != 0));
        if (pred == ICmpInst::ICMP_EQ) return eq;
        if (pred == ICmpInst::ICMP_NE) return !eq;

        if (!boolIsLHS) return std::nullopt;
        if (pred == ICmpInst::ICMP_SLT || pred == ICmpInst::ICMP_ULT)
            return wrappedConst == 1 ? !*knownBool : false;
        if (pred == ICmpInst::ICMP_SLE || pred == ICmpInst::ICMP_ULE)
            return wrappedConst == 0 ? !*knownBool : true;
        if (pred == ICmpInst::ICMP_SGT || pred == ICmpInst::ICMP_UGT)
            return wrappedConst == 0 ? *knownBool : false;
        if (pred == ICmpInst::ICMP_SGE || pred == ICmpInst::ICMP_UGE)
            return wrappedConst == 1 ? *knownBool : true;
        return std::nullopt;
    };

    if (auto wrapped = decodeWrappedBool(lhs, rhs, true); wrapped.has_value())
        return wrapped;
    if (auto wrapped = decodeWrappedBool(rhs, lhs, false); wrapped.has_value())
        return wrapped;

    return std::nullopt;
}

static std::optional<bool> evaluateBool(Value *value, QueryState &state) {
    if (!value || state.depth > 24) return std::nullopt;

    BoolFactMap boolFacts;
    ICmpFactMap cmpFacts;
    gatherFacts(state, boolFacts, cmpFacts);
    if (auto known = getKnownBool(value, boolFacts, cmpFacts); known.has_value())
        return known;

    if (!state.activeValues.insert(value).second)
        return std::nullopt;

    state.depth++;
    std::optional<bool> result;

    if (auto *icmp = dynamic_cast<ICmpInst *>(value)) {
        PredicateQueryKey key{icmp->icmp_op_, icmp->get_operand(0),
                              icmp->get_operand(1)};
        if (state.activePredicates.insert(key).second) {
            result = evaluatePredicate(icmp->icmp_op_, icmp->get_operand(0),
                                       icmp->get_operand(1), state);
            state.activePredicates.erase(key);
        }
    } else if (auto *phi = dynamic_cast<PhiInst *>(value)) {
        if (state.fromEdge && state.toEdge == phi->parent_) {
            if (Value *incoming = getIncomingValueForPred(phi, state.fromEdge))
                result = evaluateBool(incoming, state);
        }
    } else if (auto *select = dynamic_cast<SelectInst *>(value)) {
        if (auto cond = evaluateBool(select->get_operand(0), state);
            cond.has_value()) {
            result = evaluateBool(select->get_operand(*cond ? 1 : 2), state);
        }
    }

    state.depth--;
    state.activeValues.erase(value);
    return result;
}

static Constant *evaluateConstant(Value *value, QueryState &state) {
    if (!value || state.depth > 24) return nullptr;

    if (auto *constant = dynamic_cast<Constant *>(value))
        return constant;

    if (value->type_->tid_ == Type::IntegerTyID &&
        static_cast<IntegerType *>(value->type_)->num_bits_ == 1) {
        if (auto known = evaluateBool(value, state); known.has_value())
            return new ConstantInt(state.module->int1_ty_, *known ? 1 : 0);
    }

    if (!state.activeValues.insert(value).second)
        return nullptr;

    state.depth++;
    Constant *result = nullptr;

    if (auto *phi = dynamic_cast<PhiInst *>(value)) {
        if (state.fromEdge && state.toEdge == phi->parent_) {
            if (Value *incoming = getIncomingValueForPred(phi, state.fromEdge))
                result = evaluateConstant(incoming, state);
        }
    } else if (auto *select = dynamic_cast<SelectInst *>(value)) {
        if (auto cond = evaluateBool(select->get_operand(0), state);
            cond.has_value()) {
            result = evaluateConstant(select->get_operand(*cond ? 1 : 2), state);
        }
    } else if (auto *zext = dynamic_cast<ZextInst *>(value)) {
        if (auto known = evaluateBool(zext->get_operand(0), state);
            known.has_value()) {
            result = new ConstantInt(zext->type_, *known ? 1 : 0);
        }
    } else if (auto *icmp = dynamic_cast<ICmpInst *>(value)) {
        if (auto known = evaluatePredicate(icmp->icmp_op_, icmp->get_operand(0),
                                           icmp->get_operand(1), state);
            known.has_value()) {
            result = new ConstantInt(state.module->int1_ty_, *known ? 1 : 0);
        }
    }

    state.depth--;
    state.activeValues.erase(value);
    return result;
}

} // namespace

void LazyValueInfo::analyze(Function *func, const LoopInfo *loopInfo,
                            const DominatorTreeAnalysis *domTree) {
    function_ = func;
    module_ = func ? func->parent_ : nullptr;
    loopInfo_ = loopInfo;
    domTree_ = domTree;
}

Constant *LazyValueInfo::getConstant(Value *value, Instruction *cxtI) {
    QueryState state;
    state.func = function_;
    state.module = module_;
    state.loopInfo = loopInfo_;
    state.domTree = domTree_;
    state.block = cxtI ? cxtI->parent_ : nullptr;
    return evaluateConstant(value, state);
}

Constant *LazyValueInfo::getConstantOnEdge(Value *value, BasicBlock *fromBB,
                                           BasicBlock *toBB, Instruction *) {
    QueryState state;
    state.func = function_;
    state.module = module_;
    state.loopInfo = loopInfo_;
    state.domTree = domTree_;
    state.block = toBB;
    state.fromEdge = fromBB;
    state.toEdge = toBB;
    return evaluateConstant(value, state);
}

std::optional<bool> LazyValueInfo::getPredicateAt(ICmpInst::ICmpOp pred,
                                                  Value *lhs, Value *rhs,
                                                  Instruction *cxtI,
                                                  bool useBlockValue) {
    QueryState state;
    state.func = function_;
    state.module = module_;
    state.loopInfo = loopInfo_;
    state.domTree = domTree_;
    state.block = cxtI ? cxtI->parent_ : nullptr;
    state.useBlockValue = useBlockValue;
    return evaluatePredicate(pred, lhs, rhs, state);
}

std::optional<bool> LazyValueInfo::getPredicateOnEdge(ICmpInst::ICmpOp pred,
                                                      Value *lhs, Value *rhs,
                                                      BasicBlock *fromBB,
                                                      BasicBlock *toBB,
                                                      Instruction *) {
    QueryState state;
    state.func = function_;
    state.module = module_;
    state.loopInfo = loopInfo_;
    state.domTree = domTree_;
    state.block = toBB;
    state.fromEdge = fromBB;
    state.toEdge = toBB;
    return evaluatePredicate(pred, lhs, rhs, state);
}

void LazyValueInfo::forgetValue(Value *) {}

void LazyValueInfo::eraseBlock(BasicBlock *) {}

void LazyValueInfo::clear() {
    function_ = nullptr;
    module_ = nullptr;
    loopInfo_ = nullptr;
    domTree_ = nullptr;
}
