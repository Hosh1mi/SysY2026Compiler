#include "../../../include/mid/opt/loopSkewing.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace {

bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_SKEWING") != nullptr;
}

void reject(const Loop &loop, const std::string &reason,
            std::string *output) {
    if (output) *output = reason;
    if (debugEnabled())
        std::cerr << "[LoopSkewing] reject header=" << loop.header->name_
                  << ": " << reason << "\n";
}

bool isI32(Value *value) {
    auto *type = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return type && type->num_bits_ == 32;
}

bool affineInOuter(Value *value, PhiInst *outerIV, long long &coefficient,
                   long long &offset) {
    if (value == outerIV) {
        coefficient = 1;
        offset = 0;
        return true;
    }
    if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
        coefficient = 0;
        offset = constant->value_;
        return true;
    }

    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || (!binary->is_add() && !binary->is_sub()))
        return false;

    long long lhsCoefficient = 0;
    long long lhsOffset = 0;
    long long rhsCoefficient = 0;
    long long rhsOffset = 0;
    if (!affineInOuter(binary->get_operand(0), outerIV, lhsCoefficient,
                       lhsOffset) ||
        !affineInOuter(binary->get_operand(1), outerIV, rhsCoefficient,
                       rhsOffset))
        return false;

    if (binary->is_sub()) {
        rhsCoefficient = -rhsCoefficient;
        rhsOffset = -rhsOffset;
    }
    coefficient = lhsCoefficient + rhsCoefficient;
    offset = lhsOffset + rhsOffset;
    return coefficient >= -1 && coefficient <= 1;
}

PhiInst *findParentPhi(Value *value, Loop *outer,
                       std::set<Value *> &visited) {
    if (!value || !outer || !visited.insert(value).second)
        return nullptr;
    if (auto *phi = dynamic_cast<PhiInst *>(value))
        return phi->parent_ == outer->header ? phi : nullptr;
    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || (!binary->is_add() && !binary->is_sub()))
        return nullptr;
    PhiInst *lhs = findParentPhi(binary->get_operand(0), outer, visited);
    PhiInst *rhs = findParentPhi(binary->get_operand(1), outer, visited);
    if (lhs && rhs && lhs != rhs) return nullptr;
    return lhs ? lhs : rhs;
}

bool edgeProvesAtLeast(BasicBlock *source, BasicBlock *target,
                       Value *subject, long long threshold) {
    if (!source || !target || !subject) return false;
    BasicBlock *guard = source;
    BasicBlock *edgeTarget = target;
    auto *branch = dynamic_cast<BranchInst *>(guard->get_terminator());
    if (branch && branch->num_ops_ == 1 && branch->get_operand(0) == target &&
        source->pre_bbs_.size() == 1) {
        edgeTarget = source;
        guard = source->pre_bbs_.front();
        branch = dynamic_cast<BranchInst *>(guard->get_terminator());
    }
    if (!branch || branch->num_ops_ != 3)
        return false;

    auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    auto *constant = compare
                         ? dynamic_cast<ConstantInt *>(compare->get_operand(1))
                         : nullptr;
    if (!compare || compare->get_operand(0) != subject || !constant)
        return false;

    const bool trueEdge = branch->get_operand(1) == edgeTarget;
    const bool falseEdge = branch->get_operand(2) == edgeTarget;
    if (trueEdge == falseEdge) return false;
    long long knownLower = std::numeric_limits<long long>::min();
    if (trueEdge && compare->icmp_op_ == ICmpInst::ICMP_SGE)
        knownLower = constant->value_;
    else if (trueEdge && compare->icmp_op_ == ICmpInst::ICMP_SGT)
        knownLower = constant->value_ + 1;
    else if (falseEdge && compare->icmp_op_ == ICmpInst::ICMP_SLT)
        knownLower = constant->value_;
    else if (falseEdge && compare->icmp_op_ == ICmpInst::ICMP_SLE)
        knownLower = constant->value_ + 1;
    return knownLower >= threshold;
}

bool incomingIsNonNegative(Value *value, BasicBlock *source,
                           BasicBlock *target) {
    if (auto *constant = dynamic_cast<ConstantInt *>(value))
        return constant->value_ >= 0;

    Value *subject = value;
    long long required = 0;
    if (auto *binary = dynamic_cast<BinaryInst *>(value)) {
        auto *constant = dynamic_cast<ConstantInt *>(binary->get_operand(1));
        if (!constant) return false;
        if (binary->is_sub()) {
            subject = binary->get_operand(0);
            required = constant->value_;
        } else if (binary->is_add()) {
            subject = binary->get_operand(0);
            required = -constant->value_;
        } else {
            return false;
        }
    }
    return edgeProvesAtLeast(source, target, subject, required);
}

bool phiIsNonNegative(PhiInst *phi) {
    if (!phi || !phi->parent_ || phi->num_ops_ == 0)
        return false;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        auto *source = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!source ||
            !incomingIsNonNegative(phi->get_operand(i), source, phi->parent_))
            return false;
    }
    return true;
}

bool provePositiveStart(const LoopSkewPlan &plan) {
    if (plan.outerCoefficient != 1 || plan.offset < 1)
        return false;
    return phiIsNonNegative(plan.outerIV);
}

bool preheaderProvesNonEmpty(const LoopSkewPlan &plan) {
    if (!plan.preheader || plan.preheader->pre_bbs_.size() != 1)
        return false;
    BasicBlock *guardBlock = plan.preheader->pre_bbs_.front();
    auto *branch = dynamic_cast<BranchInst *>(guardBlock->get_terminator());
    if (!branch || branch->num_ops_ != 3 ||
        branch->get_operand(1) != plan.preheader)
        return false;
    auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    return compare && compare->icmp_op_ == ICmpInst::ICMP_SLT &&
           compare->get_operand(0) == plan.innerStart &&
           compare->get_operand(1) == plan.innerBound;
}

bool hasUnsupportedUses(const LoopSkewPlan &plan) {
    for (const Use &use : plan.innerIV->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !user->parent_ ||
            (!plan.inner->blocks.count(user->parent_) &&
             user != plan.innerUpdate))
            return true;
    }
    for (const Use &use : plan.innerUpdate->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user != plan.innerIV && user != plan.innerCompare)
            return true;
    }
    return false;
}

Instruction *firstNonPhi(BasicBlock *block) {
    for (auto *instruction : block->instr_list_)
        if (!instruction->is_phi()) return instruction;
    return nullptr;
}

} // namespace

std::optional<LoopSkewPlan> analyzeLoopSkew(Loop &inner,
                                            std::string *reason) {
    if (!inner.parent) {
        reject(inner, "no parent loop", reason);
        return std::nullopt;
    }
    if (!inner.preheader || !inner.singleLatch()) {
        reject(inner, "unsupported nested CFG", reason);
        return std::nullopt;
    }

    const InductionDescriptor *control = inner.getInductionDescriptor();
    if (!control || !control->constantStep || *control->constantStep != 1 ||
        control->predicate != ICmpInst::ICMP_SLT ||
        control->guardPosition != InductionGuardPosition::Latch ||
        !control->comparesUpdate) {
        reject(inner, "requires rotated signed +1 control", reason);
        return std::nullopt;
    }
    if (!isI32(control->phi) || !isI32(control->start) ||
        !isI32(control->bound)) {
        reject(inner, "requires i32 control values", reason);
        return std::nullopt;
    }
    auto *latchBranch = dynamic_cast<BranchInst *>(
        inner.singleLatch()->get_terminator());
    if (!latchBranch || latchBranch->num_ops_ != 3 ||
        latchBranch->get_operand(0) != control->compare ||
        !inner.blocks.count(
            dynamic_cast<BasicBlock *>(latchBranch->get_operand(1))) ||
        inner.blocks.count(
            dynamic_cast<BasicBlock *>(latchBranch->get_operand(2))) ||
        control->compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        control->compare->get_operand(0) != control->update ||
        control->compare->get_operand(1) != control->bound) {
        reject(inner, "requires a true-edge latch guard update < bound",
               reason);
        return std::nullopt;
    }

    std::set<Value *> visited;
    PhiInst *outerIV = findParentPhi(control->start, inner.parent, visited);
    if (!outerIV) {
        reject(inner, "parent has no affine control IV", reason);
        return std::nullopt;
    }

    LoopSkewPlan plan;
    plan.outer = inner.parent;
    plan.inner = &inner;
    plan.outerIV = outerIV;
    plan.innerIV = control->phi;
    plan.innerUpdate = control->update;
    plan.innerCompare = control->compare;
    plan.innerStart = control->start;
    plan.innerBound = control->bound;
    plan.preheader = inner.preheader;
    plan.latch = inner.singleLatch();
    if (!affineInOuter(plan.innerStart, outerIV, plan.outerCoefficient,
                       plan.offset) ||
        plan.outerCoefficient == 0) {
        reject(inner, "inner start is not coupled affinely to parent IV",
               reason);
        return std::nullopt;
    }
    if (!provePositiveStart(plan)) {
        reject(inner, "cannot prove skew coordinate arithmetic in i32",
               reason);
        return std::nullopt;
    }
    if (!preheaderProvesNonEmpty(plan)) {
        reject(inner, "missing dominating non-empty guard", reason);
        return std::nullopt;
    }
    if (hasUnsupportedUses(plan)) {
        reject(inner, "control IV or update has unsupported live-out", reason);
        return std::nullopt;
    }

    if (reason) reason->clear();
    return plan;
}

bool applyLoopSkew(const LoopSkewPlan &plan, Module *module) {
    if (!plan.inner || !plan.innerIV || !plan.innerUpdate ||
        !plan.innerCompare || !plan.preheader || !plan.latch || !module)
        return false;

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    auto *distance = PhiInst::create_phi(module->int32_ty_, plan.inner->header);
    distance->add_phi_pair_operand(zero, plan.preheader);
    plan.inner->header->add_instruction_front(distance);

    auto *bodyIndex = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     plan.innerStart, distance,
                                     plan.inner->header, true);
    Instruction *insertionPoint = firstNonPhi(plan.inner->header);
    if (!insertionPoint ||
        !plan.inner->header->add_instruction_before_inst(bodyIndex,
                                                          insertionPoint))
        return false;

    auto *nextDistance = new BinaryInst(module->int32_ty_, Instruction::Add,
                                        distance, one, plan.latch, true);
    plan.latch->add_instruction_before_inst(nextDistance, plan.innerUpdate);
    distance->add_phi_pair_operand(nextDistance, plan.latch);

    auto *distanceBound = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                         plan.innerBound, plan.innerStart,
                                         plan.preheader, true);
    plan.preheader->add_instruction_before_terminator(distanceBound);

    std::vector<Use> ivUses(plan.innerIV->use_list_.begin(),
                            plan.innerIV->use_list_.end());
    for (const Use &use : ivUses) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || user == plan.innerUpdate) continue;
        user->set_operand(use.arg_no_, bodyIndex);
    }

    plan.innerCompare->set_operand(0, nextDistance);
    plan.innerCompare->set_operand(1, distanceBound);
    plan.innerCompare->icmp_op_ = ICmpInst::ICMP_SLT;

    plan.latch->delete_instr(plan.innerUpdate);
    plan.inner->header->delete_instr(plan.innerIV);
    plan.inner->header->parent_->invalidateDominatorInfo();
    plan.inner->header->parent_->set_instr_name();

    if (debugEnabled())
        std::cerr << "[LoopSkewing] transformed func="
                  << plan.inner->header->parent_->name_ << " header="
                  << plan.inner->header->name_ << " coordinate=(inner-start)"
                  << " outer-coeff=" << plan.outerCoefficient
                  << " offset=" << plan.offset << "\n";
    return true;
}

bool LoopSkewing::runOnFunction(Function *function, AnalysisManager *AM) {
    bool changed = false;
    for (int iteration = 0; iteration < 16; ++iteration) {
        LoopInfo localLoopInfo;
        LoopInfo *loopInfo = nullptr;
        if (AM && !changed)
            loopInfo = &AM->getLoopInfo(function);
        else {
            localLoopInfo.analyze(function);
            loopInfo = &localLoopInfo;
        }

        bool applied = false;
        for (const auto &ownedLoop : loopInfo->allLoops()) {
            Loop *loop = ownedLoop.get();
            std::string reason;
            auto plan = analyzeLoopSkew(*loop, &reason);
            if (!plan) continue;
            if (applyLoopSkew(*plan, function->parent_)) {
                applied = true;
                changed = true;
                break;
            }
        }
        if (!applied) break;
    }
    return changed;
}

void LoopSkewing::execute(Module *module) {
    for (auto *function : module->function_list_)
        if (!function->is_declaration()) runOnFunction(function, nullptr);
}

PreservedAnalyses LoopSkewing::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function, &AM);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
