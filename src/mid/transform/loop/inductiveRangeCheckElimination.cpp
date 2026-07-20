#include "../../../include/mid/opt/inductiveRangeCheckElimination.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <unordered_set>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

struct CanonicalIV {
    PhiInst *phi = nullptr;
    Value *init = nullptr;
    Instruction *next = nullptr;
    int step = 0;
    ICmpInst *latchCmp = nullptr;
    ICmpInst::ICmpOp exitPred = ICmpInst::ICMP_SLT;
    Value *bound = nullptr;
};

struct BranchShape {
    BasicBlock *work = nullptr;
    BasicBlock *skip = nullptr; // nullptr means header jumps directly to latch.
    bool workOnTrue = false;
};

struct AffineExpr {
    Value *base = nullptr;
    int64_t offset = 0;
};

struct LinearExpr {
    int ivCoeff = 0;
    std::vector<std::pair<Value *, int>> terms;
    int64_t constant = 0;
};

struct RotatedIV {
    PhiInst *phi = nullptr;
    Value *init = nullptr;
    Instruction *next = nullptr;
    BasicBlock *preheader = nullptr;
    BasicBlock *header = nullptr;
    BasicBlock *latch = nullptr;
    BasicBlock *bodyEntry = nullptr;
    BasicBlock *exit = nullptr;
    ICmpInst *headerCmp = nullptr;
    Value *bound = nullptr;
};

struct GuardBound {
    enum Kind { LowerInclusive, UpperExclusive } kind;
    LinearExpr expr;
};

struct GuardBranch {
    BranchInst *branch = nullptr;
    ICmpInst *cmp = nullptr;
    bool hotOnTrue = false;
};

bool isLoopInvariant(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) || dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ && !loop.blocks.count(inst->parent_);
}

bool getConstInt(Value *value, int &out) {
    auto *ci = dynamic_cast<ConstantInt *>(value);
    if (!ci)
        return false;
    out = ci->value_;
    return true;
}

int phiIncomingIndex(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return static_cast<int>(i);
    }
    return -1;
}

bool setPhiIncomingValue(PhiInst *phi, BasicBlock *pred, Value *val) {
    int idx = phiIncomingIndex(phi, pred);
    if (idx < 0)
        return false;
    phi->set_operand(static_cast<unsigned>(idx), val);
    return true;
}

Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

bool isOnlyIVUpdateAndLatchCmp(BasicBlock *latch, Instruction *ivNext,
                               ICmpInst *latchCmp) {
    std::vector<Instruction *> body;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator())
            break;
        body.push_back(inst);
    }
    return body.size() == 2 && body[0] == ivNext && body[1] == latchCmp;
}

bool isPureSkipBlock(BasicBlock *block, BasicBlock *latch) {
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    if (!term || term->num_ops_ != 1 || term->get_operand(0) != latch)
        return false;

    for (auto *inst : block->instr_list_) {
        if (inst == term)
            break;
        if (inst->is_store() || inst->is_call() || inst->is_load() ||
            inst->is_alloca())
            return false;
        for (auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ != block)
                return false;
        }
    }
    return true;
}

bool isWorkBlock(BasicBlock *block, BasicBlock *latch, const Loop &loop) {
    if (!block || block == latch || !loop.isInLoop(block))
        return false;
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    return term && term->num_ops_ == 1 && term->get_operand(0) == latch;
}

bool isUnconditionalTo(BasicBlock *block, BasicBlock *target) {
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    return term && term->num_ops_ == 1 && term->get_operand(0) == target;
}

bool isSkipSuccessor(BasicBlock *block, BasicBlock *latch, const Loop &loop) {
    if (block == latch)
        return true;
    return block && loop.isInLoop(block) && isPureSkipBlock(block, latch);
}

ICmpInst::ICmpOp negateCmp(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:
        return ICmpInst::ICMP_NE;
    case ICmpInst::ICMP_NE:
        return ICmpInst::ICMP_EQ;
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGT;
    default:
        return pred;
    }
}

ICmpInst::ICmpOp swapCmp(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_NE:
        return pred;
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGE;
    default:
        return pred;
    }
}

void addLinearTerm(LinearExpr &expr, Value *value, int coeff) {
    if (coeff == 0)
        return;
    for (auto it = expr.terms.begin(); it != expr.terms.end(); ++it) {
        if (it->first == value) {
            it->second += coeff;
            if (it->second == 0)
                expr.terms.erase(it);
            return;
        }
    }
    expr.terms.push_back({value, coeff});
}

bool parseLinearExpr(Value *value, const Loop &loop, Value *iv, LinearExpr &out,
                     int depth = 0) {
    if (!value || depth > 8 || value->type_->tid_ != Type::IntegerTyID)
        return false;

    int c = 0;
    if (getConstInt(value, c)) {
        out.constant += c;
        return true;
    }
    if (value == iv) {
        out.ivCoeff += 1;
        return out.ivCoeff <= 1;
    }
    if (isLoopInvariant(value, loop)) {
        addLinearTerm(out, value, 1);
        return true;
    }

    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (!bin || (!bin->is_add() && !bin->is_sub()))
        return false;

    LinearExpr lhs;
    if (!parseLinearExpr(bin->get_operand(0), loop, iv, lhs, depth + 1))
        return false;
    LinearExpr rhs;
    if (!parseLinearExpr(bin->get_operand(1), loop, iv, rhs, depth + 1))
        return false;

    out.ivCoeff += lhs.ivCoeff;
    out.constant += lhs.constant;
    for (auto &term : lhs.terms)
        addLinearTerm(out, term.first, term.second);

    const int sign = bin->is_add() ? 1 : -1;
    out.ivCoeff += sign * rhs.ivCoeff;
    out.constant += sign * rhs.constant;
    for (auto &term : rhs.terms)
        addLinearTerm(out, term.first, sign * term.second);

    return out.ivCoeff >= -1 && out.ivCoeff <= 1;
}

LinearExpr subLinearExpr(const LinearExpr &lhs, const LinearExpr &rhs) {
    LinearExpr out = lhs;
    out.ivCoeff -= rhs.ivCoeff;
    out.constant -= rhs.constant;
    for (auto &term : rhs.terms)
        addLinearTerm(out, term.first, -term.second);
    return out;
}

LinearExpr addLinearConstant(LinearExpr expr, int64_t delta) {
    expr.constant += delta;
    return expr;
}

Value *materializeLinearExpr(const LinearExpr &expr, Type *ty, BasicBlock *bb,
                             Instruction *insertBefore) {
    if (expr.ivCoeff != 0)
        return nullptr;

    Value *current = nullptr;
    if (expr.terms.empty()) {
        current = new ConstantInt(ty, static_cast<int>(expr.constant));
        return current;
    }

    for (auto &term : expr.terms) {
        Value *termVal = term.first;
        if (term.second == -1) {
            auto *zero = new ConstantInt(ty, 0);
            auto *neg = new BinaryInst(ty, Instruction::Sub, zero, termVal, bb, true);
            if (!bb->add_instruction_before_inst(neg, insertBefore))
                return nullptr;
            termVal = neg;
        } else if (term.second != 1) {
            return nullptr;
        }

        if (!current) {
            current = termVal;
        } else {
            auto *sum = new BinaryInst(ty, Instruction::Add, current, termVal, bb, true);
            if (!bb->add_instruction_before_inst(sum, insertBefore))
                return nullptr;
            current = sum;
        }
    }

    if (expr.constant != 0) {
        auto *c = new ConstantInt(ty, static_cast<int>(expr.constant > 0 ? expr.constant
                                                                         : -expr.constant));
        auto op = expr.constant > 0 ? Instruction::Add : Instruction::Sub;
        auto *adj = new BinaryInst(ty, op, current, c, bb, true);
        if (!bb->add_instruction_before_inst(adj, insertBefore))
            return nullptr;
        current = adj;
    }
    return current;
}

bool decomposeInvariantAffine(Value *value, const Loop &loop, AffineExpr &out) {
    if (!value || value->type_->tid_ != Type::IntegerTyID ||
        !isLoopInvariant(value, loop))
        return false;

    int c = 0;
    if (getConstInt(value, c)) {
        out.base = nullptr;
        out.offset = c;
        return true;
    }

    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (bin && (bin->is_add() || bin->is_sub())) {
        int rhsConst = 0;
        if (getConstInt(bin->get_operand(1), rhsConst)) {
            AffineExpr inner;
            if (!decomposeInvariantAffine(bin->get_operand(0), loop, inner))
                return false;
            out = inner;
            out.offset += bin->is_add() ? rhsConst : -rhsConst;
            return true;
        }
        int lhsConst = 0;
        if (bin->is_add() && getConstInt(bin->get_operand(0), lhsConst)) {
            AffineExpr inner;
            if (!decomposeInvariantAffine(bin->get_operand(1), loop, inner))
                return false;
            out = inner;
            out.offset += lhsConst;
            return true;
        }
    }

    out.base = value;
    out.offset = 0;
    return true;
}

Value *materializeAffine(const AffineExpr &expr, int64_t extraDelta, Type *ty,
                         BasicBlock *bb, Instruction *insertBefore) {
    const int64_t total = expr.offset + extraDelta;
    if (!expr.base)
        return new ConstantInt(ty, static_cast<int>(total));
    if (total == 0)
        return expr.base;

    auto *delta = new ConstantInt(ty, static_cast<int>(total > 0 ? total : -total));
    auto op = total > 0 ? Instruction::Add : Instruction::Sub;
    auto *adj = new BinaryInst(ty, op, expr.base, delta, bb, true);
    if (!bb->add_instruction_before_inst(adj, insertBefore))
        return nullptr;
    return adj;
}

bool matchCanonicalIV(BasicBlock *header, BasicBlock *preheader, BasicBlock *latch,
                      const Loop &loop, CanonicalIV &out) {
    auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchTerm || latchTerm->num_ops_ != 3)
        return false;

    auto *latchTrue = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
    if (latchTrue != header || !latchFalse || loop.isInLoop(latchFalse))
        return false;

    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops_ != 4)
            continue;

        Value *fromPre = incomingFrom(phi, preheader);
        Value *fromLatch = incomingFrom(phi, latch);
        auto *update = dynamic_cast<BinaryInst *>(fromLatch);
        if (!fromPre || !update || update->type_ != phi->type_)
            continue;
        if (!isLoopInvariant(fromPre, loop))
            continue;

        int step = 0;
        int c = 0;
        if (update->is_add()) {
            if (update->get_operand(0) == phi && getConstInt(update->get_operand(1), c))
                step = c;
            else if (update->get_operand(1) == phi &&
                     getConstInt(update->get_operand(0), c))
                step = c;
        } else if (update->is_sub() && update->get_operand(0) == phi &&
                   getConstInt(update->get_operand(1), c)) {
            step = -c;
        }
        if (step != 1 && step != -1)
            continue;

        auto *latchCmp = dynamic_cast<ICmpInst *>(latchTerm->get_operand(0));
        if (!latchCmp)
            continue;

        ICmpInst::ICmpOp pred = latchCmp->icmp_op_;
        Value *bound = nullptr;
        if (latchCmp->get_operand(0) == update) {
            bound = latchCmp->get_operand(1);
        } else if (latchCmp->get_operand(1) == update) {
            pred = swapCmp(pred);
            bound = latchCmp->get_operand(0);
        } else {
            continue;
        }

        if (!bound || bound->type_ != phi->type_ || !isLoopInvariant(bound, loop))
            continue;
        if (step == 1 && pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            continue;
        if (step == -1 && pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            continue;

        out.phi = phi;
        out.init = fromPre;
        out.next = update;
        out.step = step;
        out.latchCmp = latchCmp;
        out.exitPred = pred;
        out.bound = bound;
        return true;
    }
    return false;
}

bool matchBranchShape(BasicBlock *header, BasicBlock *latch, const Loop &loop,
                      BranchShape &out) {
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerTerm || headerTerm->num_ops_ != 3)
        return false;

    auto *trueSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *falseSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!trueSucc || !falseSucc || trueSucc == falseSucc)
        return false;

    const bool trueIsSkip = isSkipSuccessor(trueSucc, latch, loop);
    const bool falseIsSkip = isSkipSuccessor(falseSucc, latch, loop);
    const bool trueIsWork = isWorkBlock(trueSucc, latch, loop);
    const bool falseIsWork = isWorkBlock(falseSucc, latch, loop);

    if (trueIsSkip && falseIsWork && !falseIsSkip) {
        out.work = falseSucc;
        out.skip = trueSucc == latch ? nullptr : trueSucc;
        out.workOnTrue = false;
        return true;
    }
    if (falseIsSkip && trueIsWork && !trueIsSkip) {
        out.work = trueSucc;
        out.skip = falseSucc == latch ? nullptr : falseSucc;
        out.workOnTrue = true;
        return true;
    }
    return false;
}

Value *buildTightenedBound(const CanonicalIV &iv, ICmpInst *guardCmp,
                           bool workOnTrue, const Loop &loop, Module *module,
                           BasicBlock *insertionBlock, Instruction *insertBefore,
                           const LoopInfo &LI) {
    ICmpInst::ICmpOp pred =
        workOnTrue ? guardCmp->icmp_op_ : negateCmp(guardCmp->icmp_op_);

    Value *limitExpr = nullptr;
    if (guardCmp->get_operand(0) == iv.phi) {
        limitExpr = guardCmp->get_operand(1);
    } else if (guardCmp->get_operand(1) == iv.phi) {
        pred = swapCmp(pred);
        limitExpr = guardCmp->get_operand(0);
    } else {
        return nullptr;
    }

    auto dominatesInsertion = [&](Value *value) {
        auto *inst = dynamic_cast<Instruction *>(value);
        if (!inst) return true;
        if (!inst->parent_) return false;
        return inst->parent_ == insertionBlock ||
               LI.dominates(inst->parent_, insertionBlock);
    };
    // A dedicated loop preheader is after the zero-trip guard.  Loop-invariant
    // only means "defined outside the loop"; it does not imply that a value
    // dominates this earlier guard block.
    if (!dominatesInsertion(limitExpr) || !dominatesInsertion(iv.bound) ||
        !dominatesInsertion(iv.init))
        return nullptr;

    AffineExpr limit;
    if (!decomposeInvariantAffine(limitExpr, loop, limit))
        return nullptr;

    int64_t delta = 0;
    if (iv.step == 1) {
        if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            return nullptr;
        if (iv.exitPred == ICmpInst::ICMP_SLT)
            delta = pred == ICmpInst::ICMP_SLE ? 1 : 0;
        else
            delta = pred == ICmpInst::ICMP_SLT ? -1 : 0;
    } else {
        if (pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            return nullptr;
        if (iv.exitPred == ICmpInst::ICMP_SGT)
            delta = pred == ICmpInst::ICMP_SGE ? -1 : 0;
        else
            delta = pred == ICmpInst::ICMP_SGT ? 1 : 0;
    }

    Value *candidate =
        materializeAffine(limit, delta, iv.phi->type_, insertionBlock, insertBefore);
    if (!candidate || candidate == iv.bound)
        return nullptr;

    ICmpInst::ICmpOp choosePred =
        iv.step == 1 ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_SGT;
    auto *chooseCmp =
        new ICmpInst(choosePred, candidate, iv.bound, insertionBlock, true);
    if (!insertionBlock->add_instruction_before_inst(chooseCmp, insertBefore))
        return nullptr;

    Value *tightened = nullptr;
    if (iv.step == 1)
        tightened = new SelectInst(chooseCmp, candidate, iv.bound, iv.phi->type_);
    else
        tightened = new SelectInst(chooseCmp, candidate, iv.bound, iv.phi->type_);
    if (!insertionBlock->add_instruction_before_inst(
            static_cast<Instruction *>(tightened), insertBefore))
        return nullptr;
    return tightened;
}

bool matchRotatedIV(Loop &loop, RotatedIV &out) {
    BasicBlock *header = loop.header;
    BasicBlock *preheader = loop.preheader;
    if (!header || !preheader)
        return false;

    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerTerm || headerTerm->num_ops_ != 3)
        return false;

    auto *headerCmp = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!headerCmp)
        return false;

    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *loopExit = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!bodyEntry || !loopExit || !loop.isInLoop(bodyEntry) || loop.isInLoop(loopExit))
        return false;

    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops_ != 4)
            continue;

        Value *fromPre = incomingFrom(phi, preheader);
        if (!fromPre || !isLoopInvariant(fromPre, loop))
            continue;

        BasicBlock *latch = nullptr;
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred && pred != preheader && loop.isInLoop(pred)) {
                latch = pred;
                fromLatch = phi->get_operand(i);
                break;
            }
        }
        if (!latch || !fromLatch)
            continue;

        auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
        if (!latchTerm || latchTerm->num_ops_ != 1 || latchTerm->get_operand(0) != header)
            continue;

        auto *update = dynamic_cast<BinaryInst *>(fromLatch);
        if (!update || !update->is_add() || update->type_ != phi->type_)
            continue;
        int step = 0;
        if (update->get_operand(0) == phi && getConstInt(update->get_operand(1), step)) {
        } else if (update->get_operand(1) == phi &&
                   getConstInt(update->get_operand(0), step)) {
        } else {
            continue;
        }
        if (step != 1)
            continue;

        ICmpInst::ICmpOp pred = headerCmp->icmp_op_;
        Value *bound = nullptr;
        if (headerCmp->get_operand(0) == phi) {
            bound = headerCmp->get_operand(1);
        } else if (headerCmp->get_operand(1) == phi) {
            pred = swapCmp(pred);
            bound = headerCmp->get_operand(0);
        } else {
            continue;
        }
        if (pred != ICmpInst::ICMP_SLT || !bound || !isLoopInvariant(bound, loop))
            continue;

        out.phi = phi;
        out.init = fromPre;
        out.next = update;
        out.preheader = preheader;
        out.header = header;
        out.latch = latch;
        out.bodyEntry = bodyEntry;
        out.exit = loopExit;
        out.headerCmp = headerCmp;
        out.bound = bound;
        return true;
    }
    return false;
}

bool matchGuardChain(BasicBlock *entry, BasicBlock *latch, const Loop &loop,
                     std::vector<GuardBranch> &guards) {
    guards.clear();
    BasicBlock *cursor = entry;
    for (int depth = 0; depth < 8; ++depth) {
        auto *term = dynamic_cast<BranchInst *>(cursor->get_terminator());
        if (!term)
            return false;
        if (term->num_ops_ == 1)
            return term->get_operand(0) == latch;
        if (term->num_ops_ != 3)
            return false;

        auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
        auto *trueSucc = dynamic_cast<BasicBlock *>(term->get_operand(1));
        auto *falseSucc = dynamic_cast<BasicBlock *>(term->get_operand(2));
        if (!cmp || !trueSucc || !falseSucc || trueSucc == falseSucc)
            return false;

        if (trueSucc == latch && falseSucc != latch) {
            guards.push_back({term, cmp, false});
            cursor = falseSucc;
            continue;
        }
        if (falseSucc == latch && trueSucc != latch) {
            guards.push_back({term, cmp, true});
            cursor = trueSucc;
            continue;
        }
        return false;
    }
    return false;
}

bool isHeaderPhi(const RotatedIV &shape, Value *value) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    return phi && phi->parent_ == shape.header;
}

bool isTrivialRotatedLatch(const RotatedIV &shape) {
    std::unordered_set<Instruction *> incomingUpdates;
    for (auto *inst : shape.header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        auto *incoming = dynamic_cast<Instruction *>(incomingFrom(phi, shape.latch));
        if (incoming && incoming->parent_ == shape.latch)
            incomingUpdates.insert(incoming);
    }

    for (auto *inst : shape.latch->instr_list_) {
        if (inst->is_phi() || inst == shape.latch->get_terminator())
            continue;
        if (!incomingUpdates.count(inst))
            return false;

        if (auto *bin = dynamic_cast<BinaryInst *>(inst)) {
            int step = 0;
            bool ok = false;
            if (bin->is_add()) {
                ok = (isHeaderPhi(shape, bin->get_operand(0)) &&
                      getConstInt(bin->get_operand(1), step)) ||
                     (isHeaderPhi(shape, bin->get_operand(1)) &&
                      getConstInt(bin->get_operand(0), step));
            } else if (bin->is_sub()) {
                ok = isHeaderPhi(shape, bin->get_operand(0)) &&
                     getConstInt(bin->get_operand(1), step);
            }
            if (!ok || step == 0)
                return false;
            continue;
        }

        if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            int step = 0;
            if (gep->num_ops_ != 2 || !isHeaderPhi(shape, gep->get_operand(0)) ||
                !getConstInt(gep->get_operand(1), step) || step == 0)
                return false;
            continue;
        }

        return false;
    }
    return true;
}

bool deriveGuardBound(const GuardBranch &guard, const Loop &loop, PhiInst *iv,
                      GuardBound &out) {
    ICmpInst::ICmpOp pred =
        guard.hotOnTrue ? guard.cmp->icmp_op_ : negateCmp(guard.cmp->icmp_op_);

    LinearExpr lhs;
    if (!parseLinearExpr(guard.cmp->get_operand(0), loop, iv, lhs))
        return false;
    LinearExpr rhs;
    if (!parseLinearExpr(guard.cmp->get_operand(1), loop, iv, rhs))
        return false;

    if (rhs.ivCoeff != 0 && lhs.ivCoeff == 0) {
        std::swap(lhs, rhs);
        pred = swapCmp(pred);
    }
    if (lhs.ivCoeff != 1 || rhs.ivCoeff != 0)
        return false;

    LinearExpr rest = lhs;
    rest.ivCoeff = 0;
    LinearExpr bound = subLinearExpr(rhs, rest);

    switch (pred) {
    case ICmpInst::ICMP_SGE:
        out.kind = GuardBound::LowerInclusive;
        out.expr = bound;
        return true;
    case ICmpInst::ICMP_SGT:
        out.kind = GuardBound::LowerInclusive;
        out.expr = addLinearConstant(bound, 1);
        return true;
    case ICmpInst::ICMP_SLT:
        out.kind = GuardBound::UpperExclusive;
        out.expr = bound;
        return true;
    case ICmpInst::ICMP_SLE:
        out.kind = GuardBound::UpperExclusive;
        out.expr = addLinearConstant(bound, 1);
        return true;
    default:
        return false;
    }
}

Value *combineBound(Value *current, Value *candidate, GuardBound::Kind kind,
                    Type *ty, BasicBlock *bb, Instruction *insertBefore) {
    if (!current)
        return candidate;
    ICmpInst::ICmpOp cmpPred =
        kind == GuardBound::LowerInclusive ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_SLT;
    auto *cmp = new ICmpInst(cmpPred, candidate, current, bb, true);
    if (!bb->add_instruction_before_inst(cmp, insertBefore))
        return nullptr;
    auto *sel = new SelectInst(cmp, candidate, current, ty);
    if (!bb->add_instruction_before_inst(sel, insertBefore))
        return nullptr;
    return sel;
}

Value *buildDelta(Value *newInit, Value *origInit, Type *ty, BasicBlock *bb,
                  Instruction *insertBefore) {
    if (newInit == origInit)
        return nullptr;
    int c = 0;
    if (getConstInt(origInit, c) && c == 0)
        return newInit;
    auto *delta = new BinaryInst(ty, Instruction::Sub, newInit, origInit, bb, true);
    if (!bb->add_instruction_before_inst(delta, insertBefore))
        return nullptr;
    return delta;
}

Value *materializeScaledDelta(Value *delta, int64_t step, Type *ty, BasicBlock *bb,
                              Instruction *insertBefore) {
    if (!delta || step == 0)
        return new ConstantInt(ty, 0);
    if (step == 1)
        return delta;
    auto *scale = new ConstantInt(ty, static_cast<int>(step));
    auto *mul = new BinaryInst(ty, Instruction::Mul, delta, scale, bb, true);
    if (!bb->add_instruction_before_inst(mul, insertBefore))
        return nullptr;
    return mul;
}

bool rebaseCompanionPhis(const RotatedIV &loopShape, Value *delta,
                         Instruction *insertBefore) {
    if (!delta)
        return true;

    for (auto *inst : loopShape.header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == loopShape.phi)
            continue;

        Value *fromPre = incomingFrom(phi, loopShape.preheader);
        Value *fromLatch = incomingFrom(phi, loopShape.latch);
        if (!fromPre || !fromLatch)
            continue;

        Value *rebased = nullptr;
        if (auto *bin = dynamic_cast<BinaryInst *>(fromLatch)) {
            int step = 0;
            if (bin->is_add()) {
                if (bin->get_operand(0) == phi && getConstInt(bin->get_operand(1), step)) {
                } else if (bin->get_operand(1) == phi &&
                           getConstInt(bin->get_operand(0), step)) {
                } else {
                    step = 0;
                }
            } else if (bin->is_sub() && bin->get_operand(0) == phi &&
                       getConstInt(bin->get_operand(1), step)) {
                step = -step;
            }
            if (step != 0) {
                Value *scaled = materializeScaledDelta(
                    delta, step > 0 ? step : -step, phi->type_, loopShape.preheader,
                    insertBefore);
                if (!scaled)
                    return false;
                if (step > 0)
                    rebased = new BinaryInst(phi->type_, Instruction::Add, fromPre, scaled,
                                             loopShape.preheader, true);
                else
                    rebased = new BinaryInst(phi->type_, Instruction::Sub, fromPre, scaled,
                                             loopShape.preheader, true);
                if (!loopShape.preheader->add_instruction_before_inst(
                        static_cast<Instruction *>(rebased), insertBefore))
                    return false;
            }
        } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(fromLatch)) {
            if (gep->get_operand(0) != phi || gep->num_ops_ != 2)
                continue;
            int step = 0;
            if (!getConstInt(gep->get_operand(1), step) || step <= 0)
                continue;
            Value *scaled = materializeScaledDelta(
                delta, step, loopShape.phi->type_, loopShape.preheader, insertBefore);
            if (!scaled)
                return false;
            std::vector<Value *> idxs{scaled};
            auto *newGep = new GetElementPtrInst(fromPre, idxs, loopShape.preheader, true);
            if (!loopShape.preheader->add_instruction_before_inst(newGep, insertBefore))
                return false;
            rebased = newGep;
        }

        if (rebased && !setPhiIncomingValue(phi, loopShape.preheader, rebased))
            return false;
    }
    return true;
}

bool tryTightenMonotoneGuardLoop(Loop &loop, Module *module) {
    RotatedIV shape;
    if (!matchRotatedIV(loop, shape))
        return false;
    if (!isTrivialRotatedLatch(shape))
        return false;

    std::vector<GuardBranch> guards;
    if (!matchGuardChain(shape.bodyEntry, shape.latch, loop, guards) || guards.empty())
        return false;

    std::vector<GuardBound> bounds;
    for (auto &guard : guards) {
        GuardBound bound;
        if (!deriveGuardBound(guard, loop, shape.phi, bound))
            return false;
        bounds.push_back(bound);
    }

    Instruction *insertBefore = shape.preheader->get_terminator();
    Value *newInit = shape.init;
    Value *newBound = shape.bound;
    bool changed = false;

    for (auto &bound : bounds) {
        Value *candidate =
            materializeLinearExpr(bound.expr, shape.phi->type_, shape.preheader, insertBefore);
        if (!candidate)
            return false;
        Value *combined =
            combineBound(bound.kind == GuardBound::LowerInclusive ? newInit : newBound,
                         candidate, bound.kind, shape.phi->type_, shape.preheader,
                         insertBefore);
        if (!combined)
            return false;
        if (bound.kind == GuardBound::LowerInclusive) {
            if (combined != newInit) {
                newInit = combined;
                changed = true;
            }
        } else {
            if (combined != newBound) {
                newBound = combined;
                changed = true;
            }
        }
    }

    if (!changed || (newInit == shape.init && newBound == shape.bound))
        return false;

    Value *delta =
        buildDelta(newInit, shape.init, shape.phi->type_, shape.preheader, insertBefore);
    if (newInit != shape.init) {
        if (!setPhiIncomingValue(shape.phi, shape.preheader, newInit))
            return false;
        if (!rebaseCompanionPhis(shape, delta, insertBefore))
            return false;
    }

    if (shape.headerCmp->get_operand(0) == shape.phi)
        shape.headerCmp->set_operand(1, newBound);
    else
        shape.headerCmp->set_operand(0, newBound);

    for (auto &guard : guards) {
        guard.branch->set_operand(
            0, new ConstantInt(module->int1_ty_, guard.hotOnTrue ? 1 : 0));
    }
    return true;
}

} // namespace

void inductiveRangeCheckElimination::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses inductiveRangeCheckElimination::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool inductiveRangeCheckElimination::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty())
        return false;

    LoopInfo LI;
    LI.analyze(func);

    std::vector<Loop *> loops;
    for (auto &loop : LI.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops)
        changed |= tryTightenLoop(*loop, func->parent_, LI);

    if (changed)
        func->set_instr_name();
    return changed;
}

bool inductiveRangeCheckElimination::tryTightenLoop(
    Loop &loop, Module *module, const LoopInfo &LI) {
    const bool debug = std::getenv("DEBUG_INDUCTIVE_RANGE") != nullptr;
    auto reject = [&](const char *reason) {
        if (debug)
            std::cerr << "[InductiveRange] reject header="
                      << (loop.header ? loop.header->name_ : "<null>")
                      << " reason=" << reason << "\n";
        return false;
    };
    if (debug)
        std::cerr << "[InductiveRange] inspect header="
                  << (loop.header ? loop.header->name_ : "<null>")
                  << " blocks=" << loop.blocks.size() << "\n";
    BasicBlock *preheader = loop.preheader;
    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    if (!preheader || !header || !latch)
        return reject("missing-loop-structure");

    if (tryTightenMonotoneGuardLoop(loop, module)) {
        if (debug)
            std::cerr << "[InductiveRange] tightened-monotone header="
                      << header->name_ << "\n";
        return true;
    }

    auto *preTerm = dynamic_cast<BranchInst *>(preheader->get_terminator());
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!preTerm || !headerTerm || headerTerm->num_ops_ != 3)
        return reject("non-conditional-entry-or-header");

    // LoopRotate preserves a dedicated preheader and leaves the zero-trip
    // condition in its unique outside predecessor.  Accept the old direct
    // guard shape as well so this analysis is independent of scheduling.
    BasicBlock *guardBlock = preheader;
    BranchInst *guardTerm = preTerm;
    BasicBlock *guardContinue = header;
    if (preTerm->num_ops_ == 1 && preTerm->get_operand(0) == header) {
        if (preheader->pre_bbs_.size() != 1)
            return reject("entry-guard-predecessor");
        guardBlock = preheader->pre_bbs_[0];
        if (loop.isInLoop(guardBlock))
            return reject("entry-guard-inside-loop");
        guardTerm = dynamic_cast<BranchInst *>(guardBlock->get_terminator());
        guardContinue = preheader;
    }
    if (!guardTerm || guardTerm->num_ops_ != 3)
        return reject("entry-guard-terminator");

    auto *guardTrue = dynamic_cast<BasicBlock *>(guardTerm->get_operand(1));
    auto *guardFalse = dynamic_cast<BasicBlock *>(guardTerm->get_operand(2));
    if (guardTrue != guardContinue || !guardFalse || loop.isInLoop(guardFalse))
        return reject("entry-guard-shape");

    CanonicalIV iv;
    if (!matchCanonicalIV(header, preheader, latch, loop, iv))
        return reject("canonical-iv");
    if (!isOnlyIVUpdateAndLatchCmp(latch, iv.next, iv.latchCmp))
        return reject("nontrivial-latch");

    BranchShape shape;
    if (!matchBranchShape(header, latch, loop, shape))
        return reject("branch-shape");

    auto *guardCmp = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!guardCmp || guardCmp->parent_ != header)
        return reject("header-guard");

    Instruction *insertBefore = guardBlock->get_terminator();
    Value *tightened = buildTightenedBound(iv, guardCmp, shape.workOnTrue, loop,
                                           module, guardBlock, insertBefore, LI);
    if (!tightened)
        return reject("bound-construction");

    auto *entryCmp =
        new ICmpInst(iv.exitPred, iv.init, tightened, guardBlock, true);
    if (!guardBlock->add_instruction_before_inst(entryCmp, insertBefore))
        return reject("entry-compare-insertion");

    guardTerm->set_operand(0, entryCmp);
    if (iv.latchCmp->get_operand(0) == iv.next)
        iv.latchCmp->set_operand(1, tightened);
    else
        iv.latchCmp->set_operand(0, tightened);
    // The tightened iteration domain is exactly the subset on which the work
    // successor is taken.  Make that proof explicit so CFG cleanup removes
    // the skip path and repeat scheduling cannot wrap the same bound in an
    // unbounded chain of equivalent min/max selects.
    headerTerm->set_operand(
        0, new ConstantInt(module->int1_ty_, shape.workOnTrue ? 1 : 0));
    if (debug)
        std::cerr << "[InductiveRange] tightened-branch header="
                  << header->name_ << "\n";
    return true;
}
