#include "../../../include/mid/hira/conversion/importer.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace hira {
namespace {

Value *incomingFrom(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

bool isUsedOutsideLoop(Value *value, const Loop &loop) {
    for (const Use &use : value->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && !loop.isInLoop(user))
            return true;
    }
    return false;
}

bool isAddOne(Value *value, PhiInst *phi) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;
    auto *leftConstant =
        dynamic_cast<ConstantInt *>(add->get_operand(0));
    auto *rightConstant =
        dynamic_cast<ConstantInt *>(add->get_operand(1));
    return (add->get_operand(0) == phi && rightConstant &&
            rightConstant->value_ == 1) ||
           (add->get_operand(1) == phi && leftConstant &&
            leftConstant->value_ == 1);
}

std::optional<ComputeKind> computeKindFor(Instruction *instruction) {
    switch (instruction->op_id_) {
    case Instruction::Add:
        return ComputeKind::Add;
    case Instruction::Sub:
        return ComputeKind::Sub;
    case Instruction::Mul:
        return ComputeKind::Mul;
    case Instruction::And:
        return ComputeKind::And;
    case Instruction::Or:
        return ComputeKind::Or;
    case Instruction::Xor:
        return ComputeKind::Xor;
    case Instruction::Shl:
        return ComputeKind::Shl;
    case Instruction::LShr:
        return ComputeKind::LShr;
    case Instruction::AShr:
        return ComputeKind::AShr;
    case Instruction::ICmp:
        return ComputeKind::ICmp;
    case Instruction::Select:
        return ComputeKind::Select;
    case Instruction::GetElementPtr:
        return ComputeKind::GetElementPtr;
    case Instruction::ZExt:
        return ComputeKind::ZExt;
    default:
        return std::nullopt;
    }
}

class RegionImporter {
public:
    RegionImporter(Loop &loop, const LoopInfo &loopInfo)
        : rootLoop_(loop), loopInfo_(loopInfo),
          region_(std::make_unique<HiraRegion>(&loop)) {}

    ImportResult run() {
        (void)loopInfo_;
        if (!importLoop(rootLoop_, region_->rootSequence(), true))
            return std::move(failure_);
        return ImportResult::success(std::move(region_));
    }

private:
    struct LoopControl {
        BasicBlock *entryPredecessor = nullptr;
        BasicBlock *latch = nullptr;
        BasicBlock *exit = nullptr;
        PhiInst *induction = nullptr;
        Value *initial = nullptr;
        Value *bound = nullptr;
    };

    struct CarriedPhi {
        PhiInst *phi = nullptr;
        std::size_t bindingIndex = 0;
        HiraValue *result = nullptr;
    };

    bool fail(ImportRejectReason reason, std::string detail = {}) {
        failure_ = ImportResult::reject(reason, std::move(detail));
        return false;
    }

    bool deriveControl(Loop &loop, LoopControl &control) {
        if (!loop.header || !loop.singleLatch() || !loop.singleExit())
            return false;
        control.latch = loop.singleLatch();
        control.exit = loop.singleExit();

        for (BasicBlock *predecessor : loop.header->pre_bbs_) {
            if (loop.isInLoop(predecessor))
                continue;
            if (control.entryPredecessor)
                return false;
            control.entryPredecessor = predecessor;
        }
        if (!control.entryPredecessor)
            return false;

        Instruction *terminator = loop.header->get_terminator();
        if (!terminator || !terminator->is_br() ||
            terminator->num_ops_ != 3)
            return false;
        auto *guard = dynamic_cast<ICmpInst *>(terminator->get_operand(0));
        if (!guard || guard->icmp_op_ != ICmpInst::ICMP_SLT)
            return false;

        for (Instruction *instruction : loop.header->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            if (phi->num_ops_ != 4)
                continue;
            Value *initial =
                incomingFrom(phi, control.entryPredecessor);
            Value *update = incomingFrom(phi, control.latch);
            if (!initial || !update || !isAddOne(update, phi))
                continue;
            if (guard->get_operand(0) != phi)
                continue;
            control.induction = phi;
            control.initial = initial;
            control.bound = guard->get_operand(1);
            break;
        }
        if (!control.induction)
            return false;

        for (Instruction *instruction : loop.header->instr_list_) {
            if (instruction->is_phi() || instruction == guard ||
                instruction == terminator)
                continue;
            return false;
        }
        return true;
    }

    HiraValue *getValue(Value *value) {
        if (!value)
            return nullptr;
        auto visible = visibleValues_.find(value);
        if (visible != visibleValues_.end())
            return visible->second;

        HiraValue *result = nullptr;
        if (auto *integer = dynamic_cast<ConstantInt *>(value)) {
            result = region_->createIntegerConstant(
                integer->type_, integer->value_);
        } else if (auto *floating = dynamic_cast<ConstantFloat *>(value)) {
            result = region_->createFloatConstant(
                floating->type_, floating->value_);
        } else {
            auto *instruction = dynamic_cast<Instruction *>(value);
            if (instruction && rootLoop_.isInLoop(instruction))
                return nullptr;
            if (dynamic_cast<BasicBlock *>(value))
                return nullptr;
            result = region_->createParameter(value->type_);
        }
        visibleValues_[value] = result;
        region_->sourceMapping().mapValue(result, value);
        return result;
    }

    bool importLoop(Loop &loop, HiraSequence &target, bool isRoot) {
        LoopControl control;
        if (!deriveControl(loop, control))
            return fail(ImportRejectReason::UnsupportedHeader,
                        loop.header ? loop.header->name_ : "<null>");

        HiraValue *lower = getValue(control.initial);
        HiraValue *upper = getValue(control.bound);
        if (!lower || !upper)
            return fail(ImportRejectReason::MissingValue, "loop-bound");

        HiraValue *step =
            region_->createIntegerConstant(control.induction->type_, 1);
        HiraValue *induction =
            region_->createValue(control.induction->type_);
        visibleValues_[control.induction] = induction;
        region_->sourceMapping().mapValue(induction, control.induction);

        auto loopNode =
            std::make_unique<HiraLoop>(induction, lower, upper, step);
        HiraLoop *hiraLoop = loopNode.get();
        region_->sourceMapping().mapLoop(hiraLoop, &loop);

        std::vector<CarriedPhi> carriedPhis;
        for (Instruction *instruction : loop.header->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            if (phi == control.induction)
                continue;
            if (phi->num_ops_ != 4)
                return fail(ImportRejectReason::UnsupportedPhi,
                            loop.header->name_);

            Value *initialSource =
                incomingFrom(phi, control.entryPredecessor);
            HiraValue *initial = getValue(initialSource);
            if (!initial)
                return fail(ImportRejectReason::MissingValue,
                            "carried-initial");

            HiraValue *iteration = region_->createValue(phi->type_);
            HiraValue *result = region_->createValue(phi->type_);
            visibleValues_[phi] = iteration;
            region_->sourceMapping().mapValue(iteration, phi);
            region_->sourceMapping().mapValue(result, phi);
            std::size_t binding =
                hiraLoop->addCarriedValue(initial, iteration, result);
            carriedPhis.push_back({phi, binding, result});
        }

        target.append(std::move(loopNode));
        if (!importLoopBody(loop, control, *hiraLoop))
            return false;

        Value *inductionUpdate =
            incomingFrom(control.induction, control.latch);
        HiraValue *inductionYield = getValue(inductionUpdate);
        if (!inductionYield)
            return fail(ImportRejectReason::MissingYield, "induction");
        if (isUsedOutsideLoop(control.induction, loop))
            return fail(ImportRejectReason::LiveOutInduction);

        auto yield = std::make_unique<HiraYield>();
        yield->addOperand(inductionYield);
        hiraLoop->addYieldValue(inductionYield);

        for (const CarriedPhi &carried : carriedPhis) {
            Value *yieldSource =
                incomingFrom(carried.phi, control.latch);
            HiraValue *yieldValue = getValue(yieldSource);
            if (!yieldValue)
                return fail(ImportRejectReason::MissingYield,
                            carried.phi->name_);
            hiraLoop->setCarriedYield(carried.bindingIndex, yieldValue);
            hiraLoop->addYieldValue(yieldValue);
            yield->addOperand(yieldValue);

            visibleValues_[carried.phi] = carried.result;
            if (isRoot && isUsedOutsideLoop(carried.phi, loop))
                region_->addResult(carried.result);
        }
        hiraLoop->body().append(std::move(yield));
        return true;
    }

    bool importLoopBody(Loop &loop, const LoopControl &control,
                        HiraLoop &hiraLoop) {
        BasicBlock *current = nullptr;
        for (BasicBlock *successor : loop.header->succ_bbs_) {
            if (!loop.isInLoop(successor))
                continue;
            if (current)
                return fail(
                    ImportRejectReason::NonStraightLineControlFlow,
                    loop.header->name_);
            current = successor;
        }
        if (!current)
            return fail(ImportRejectReason::NonStraightLineControlFlow,
                        loop.header->name_);

        std::map<BasicBlock *, Loop *> childHeaders;
        for (Loop *child : loop.children)
            childHeaders[child->header] = child;

        std::set<BasicBlock *> visited;
        while (current != loop.header) {
            auto child = childHeaders.find(current);
            if (child != childHeaders.end()) {
                if (!importLoop(*child->second, hiraLoop.body(), false))
                    return false;
                current = child->second->singleExit();
                if (!current)
                    return fail(
                        ImportRejectReason::NonStraightLineControlFlow,
                        "child-exit");
                continue;
            }

            if (!loop.isInLoop(current) ||
                !visited.insert(current).second)
                return fail(
                    ImportRejectReason::NonStraightLineControlFlow,
                    current ? current->name_ : "<null>");

            Instruction *terminator = current->get_terminator();
            if (!terminator || !terminator->is_br())
                return fail(
                    ImportRejectReason::NonStraightLineControlFlow,
                    current->name_);

            if (!importBlockContents(current, terminator,
                                     hiraLoop.body()))
                return false;

            if (terminator->num_ops_ == 1) {
                current = dynamic_cast<BasicBlock *>(
                    terminator->get_operand(0));
                continue;
            }
            if (terminator->num_ops_ != 3 ||
                !importIfDiamond(loop, current, terminator,
                                 hiraLoop.body(), visited, current))
                return false;
        }
        return visited.count(control.latch) != 0;
    }

    bool importBlockContents(BasicBlock *block,
                             Instruction *terminator,
                             HiraSequence &target) {
        for (Instruction *instruction : block->instr_list_) {
            if (instruction == terminator)
                continue;
            if (instruction->is_phi())
                return fail(ImportRejectReason::UnsupportedPhi,
                            block->name_);
            if (!importInstruction(instruction, target))
                return false;
        }
        return true;
    }

    BasicBlock *unconditionalTarget(BasicBlock *block) const {
        if (!block)
            return nullptr;
        Instruction *terminator = block->get_terminator();
        if (!terminator || !terminator->is_br() ||
            terminator->num_ops_ != 1)
            return nullptr;
        return dynamic_cast<BasicBlock *>(terminator->get_operand(0));
    }

    bool importIfArm(Loop &loop, BasicBlock *arm,
                     BasicBlock *join, HiraSequence &target,
                     std::set<BasicBlock *> &visited) {
        if (!arm)
            return true;
        if (!loop.isInLoop(arm) ||
            !visited.insert(arm).second ||
            arm->pre_bbs_.size() != 1)
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                arm->name_);

        Instruction *terminator = arm->get_terminator();
        if (!terminator || !terminator->is_br() ||
            terminator->num_ops_ != 1 ||
            terminator->get_operand(0) != join)
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                arm->name_);
        return importBlockContents(arm, terminator, target);
    }

    bool importIfDiamond(Loop &loop, BasicBlock *branchBlock,
                         Instruction *terminator,
                         HiraSequence &target,
                         std::set<BasicBlock *> &visited,
                         BasicBlock *&join) {
        HiraValue *condition = getValue(terminator->get_operand(0));
        auto *thenTarget =
            dynamic_cast<BasicBlock *>(terminator->get_operand(1));
        auto *elseTarget =
            dynamic_cast<BasicBlock *>(terminator->get_operand(2));
        if (!condition || !thenTarget || !elseTarget ||
            thenTarget == elseTarget)
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                branchBlock->name_);

        BasicBlock *thenArm = thenTarget;
        BasicBlock *elseArm = elseTarget;
        BasicBlock *thenNext = unconditionalTarget(thenTarget);
        BasicBlock *elseNext = unconditionalTarget(elseTarget);
        if (thenNext == elseTarget) {
            join = elseTarget;
            elseArm = nullptr;
        } else if (elseNext == thenTarget) {
            join = thenTarget;
            thenArm = nullptr;
        } else if (thenNext && thenNext == elseNext) {
            join = thenNext;
        } else {
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                branchBlock->name_);
        }

        if (!join || !loop.isInLoop(join) || join == loop.header)
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                branchBlock->name_);

        std::set<BasicBlock *> expectedPredecessors;
        expectedPredecessors.insert(thenArm ? thenArm : branchBlock);
        expectedPredecessors.insert(elseArm ? elseArm : branchBlock);
        if (join->pre_bbs_.size() != expectedPredecessors.size())
            return fail(
                ImportRejectReason::NonStraightLineControlFlow,
                join->name_);
        for (BasicBlock *predecessor : join->pre_bbs_)
            if (!expectedPredecessors.count(predecessor))
                return fail(
                    ImportRejectReason::NonStraightLineControlFlow,
                    join->name_);

        auto hiraIf = std::make_unique<HiraIf>(condition);
        HiraIf *inserted = hiraIf.get();
        if (!importIfArm(loop, thenArm, join,
                         hiraIf->thenSequence(), visited) ||
            !importIfArm(loop, elseArm, join,
                         hiraIf->elseSequence(), visited))
            return false;
        target.append(std::move(hiraIf));
        region_->sourceMapping().mapNode(inserted, terminator);
        return true;
    }

    bool importInstruction(Instruction *instruction,
                           HiraSequence &target) {
        std::unique_ptr<HiraNode> node;

        if (instruction->is_load()) {
            HiraValue *address = getValue(instruction->get_operand(0));
            if (!address)
                return fail(ImportRejectReason::MissingValue,
                            instruction->name_);
            node = std::make_unique<HiraLoad>(address);
        } else if (instruction->is_store()) {
            HiraValue *stored = getValue(instruction->get_operand(0));
            HiraValue *address = getValue(instruction->get_operand(1));
            if (!stored || !address)
                return fail(ImportRejectReason::MissingValue,
                            "store-operand");
            node = std::make_unique<HiraStore>(stored, address);
        } else {
            std::optional<ComputeKind> kind = computeKindFor(instruction);
            if (!kind) {
                std::ostringstream detail;
                detail << "opcode-"
                       << static_cast<int>(instruction->op_id_);
                return fail(ImportRejectReason::UnsupportedInstruction,
                            detail.str());
            }
            int predicate = 0;
            if (auto *comparison =
                    dynamic_cast<ICmpInst *>(instruction))
                predicate = static_cast<int>(comparison->icmp_op_);
            auto compute =
                std::make_unique<HiraComputeOp>(*kind, predicate);
            for (unsigned i = 0; i < instruction->num_ops_; ++i) {
                HiraValue *operand =
                    getValue(instruction->get_operand(i));
                if (!operand)
                    return fail(ImportRejectReason::MissingValue,
                                instruction->name_);
                compute->addOperand(operand);
            }
            node = std::move(compute);
        }

        if (!instruction->is_store()) {
            HiraValue *result =
                region_->createValue(instruction->type_);
            node->addResult(result);
            visibleValues_[instruction] = result;
            region_->sourceMapping().mapValue(result, instruction);
        }
        HiraNode *inserted = target.append(std::move(node));
        region_->sourceMapping().mapNode(inserted, instruction);
        return true;
    }

    Loop &rootLoop_;
    const LoopInfo &loopInfo_;
    std::unique_ptr<HiraRegion> region_;
    std::map<Value *, HiraValue *> visibleValues_;
    ImportResult failure_;
};

} // namespace

ImportResult ImportResult::success(std::unique_ptr<HiraRegion> region) {
    ImportResult result;
    result.region = std::move(region);
    return result;
}

ImportResult ImportResult::reject(ImportRejectReason reason,
                                  std::string detail) {
    ImportResult result;
    result.reason = reason;
    result.detail = std::move(detail);
    return result;
}

const char *importRejectReasonName(ImportRejectReason reason) {
    switch (reason) {
    case ImportRejectReason::None:
        return "none";
    case ImportRejectReason::NestedLoop:
        return "nested-loop";
    case ImportRejectReason::NonStraightLineControlFlow:
        return "non-straight-line-control-flow";
    case ImportRejectReason::UnsupportedHeader:
        return "unsupported-header";
    case ImportRejectReason::UnsupportedPhi:
        return "unsupported-phi";
    case ImportRejectReason::UnsupportedInstruction:
        return "unsupported-instruction";
    case ImportRejectReason::MissingValue:
        return "missing-value";
    case ImportRejectReason::MissingYield:
        return "missing-yield";
    case ImportRejectReason::LiveOutInduction:
        return "live-out-induction";
    }
    return "unknown";
}

ImportResult importHiraRegion(Loop &loop, const LoopInfo &loopInfo) {
    return RegionImporter(loop, loopInfo).run();
}

} // namespace hira
