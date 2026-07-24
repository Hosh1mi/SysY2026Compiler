#include "../../../include/mid/hira/conversion/exporter.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hira {
namespace {

Instruction::OpID binaryOpcode(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
        return Instruction::Add;
    case ComputeKind::Sub:
        return Instruction::Sub;
    case ComputeKind::Mul:
        return Instruction::Mul;
    case ComputeKind::And:
        return Instruction::And;
    case ComputeKind::Or:
        return Instruction::Or;
    case ComputeKind::Xor:
        return Instruction::Xor;
    case ComputeKind::Shl:
        return Instruction::Shl;
    case ComputeKind::LShr:
        return Instruction::LShr;
    case ComputeKind::AShr:
        return Instruction::AShr;
    default:
        return Instruction::Ret;
    }
}

bool isBinaryCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
        return true;
    default:
        return false;
    }
}

class RegionExporter {
public:
    explicit RegionExporter(HiraRegion &region)
        : region_(region), sourceLoop_(region.sourceLoop()) {
        if (sourceLoop_ && sourceLoop_->header) {
            function_ = sourceLoop_->header->parent_;
            module_ = function_ ? function_->parent_ : nullptr;
        }
    }

    ExportResult run() {
        if (!validate())
            return failure_;
        createBlocks();
        if (!emitLoop()) {
            rollbackNewBlocks();
            return failure_;
        }
        commit();
        return ExportResult::success();
    }

private:
    bool fail(ExportRejectReason reason, std::string detail = {}) {
        failure_ = ExportResult::reject(reason, std::move(detail));
        return false;
    }

    bool validate() {
        if (!sourceLoop_ || !function_ || !module_ ||
            !sourceLoop_->preheader || !sourceLoop_->singleExit() ||
            sourceLoop_->children.size() != 0)
            return fail(ExportRejectReason::InvalidRegion);

        preheader_ = sourceLoop_->preheader;
        oldHeader_ = sourceLoop_->header;
        oldExit_ = sourceLoop_->singleExit();
        Instruction *preheaderTerminator = preheader_->get_terminator();
        if (!preheaderTerminator || !preheaderTerminator->is_br() ||
            preheaderTerminator->num_ops_ != 1 ||
            preheaderTerminator->get_operand(0) != oldHeader_)
            return fail(ExportRejectReason::InvalidSourceCFG);

        for (Instruction *instruction : oldExit_->instr_list_)
            if (instruction->is_phi())
                return fail(ExportRejectReason::UnsupportedExitPhi);
            else
                break;

        if (region_.rootSequence().nodes().size() != 1)
            return fail(ExportRejectReason::InvalidRegion);
        loop_ = dynamic_cast<HiraLoop *>(
            region_.rootSequence().nodes().front().get());
        if (!loop_)
            return fail(ExportRejectReason::InvalidRegion);
        if (loop_->yieldValues().empty())
            return fail(ExportRejectReason::InvalidRegion,
                        "missing-induction-yield");

        for (HiraValue *parameter : region_.parameters())
            if (!region_.sourceMapping().sourceValue(parameter))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-parameter");

        for (const auto &node : loop_->body().nodes()) {
            if (dynamic_cast<HiraYield *>(node.get()))
                continue;
            if (dynamic_cast<HiraLoad *>(node.get())) {
                if (node->results().size() != 1)
                    return fail(ExportRejectReason::InvalidRegion,
                                "invalid-load-result");
                continue;
            }
            if (dynamic_cast<HiraStore *>(node.get())) {
                if (!node->results().empty())
                    return fail(ExportRejectReason::InvalidRegion,
                                "invalid-store-result");
                continue;
            }
            auto *compute = dynamic_cast<HiraComputeOp *>(node.get());
            if (!compute)
                return fail(ExportRejectReason::UnsupportedNode);
            if (compute->results().size() != 1)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-compute-result");
            ComputeKind kind = compute->computeKind();
            if (!isBinaryCompute(kind) && kind != ComputeKind::ICmp &&
                kind != ComputeKind::Select &&
                kind != ComputeKind::GetElementPtr &&
                kind != ComputeKind::ZExt)
                return fail(ExportRejectReason::UnsupportedNode);
        }

        for (HiraValue *result : region_.results()) {
            if (!region_.sourceMapping().sourceValue(result))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-result");
            bool found = false;
            for (const auto &binding : loop_->carriedValues())
                found |= binding.result == result;
            if (!found)
                return fail(ExportRejectReason::UnsupportedResult);
        }
        return true;
    }

    void createBlocks() {
        const std::string suffix = std::to_string(nextRegionId_++);
        newHeader_ =
            new BasicBlock(module_, "label_hira_header_" + suffix, function_);
        newBody_ =
            new BasicBlock(module_, "label_hira_body_" + suffix, function_);
        newLatch_ =
            new BasicBlock(module_, "label_hira_latch_" + suffix, function_);
        newExit_ =
            new BasicBlock(module_, "label_hira_exit_" + suffix, function_);
        newBlocks_ = {newHeader_, newBody_, newLatch_, newExit_};
    }

    Value *value(HiraValue *hiraValue) {
        if (!hiraValue)
            return nullptr;
        auto it = values_.find(hiraValue);
        if (it != values_.end())
            return it->second;

        Value *result = nullptr;
        switch (hiraValue->kind()) {
        case ValueKind::Parameter:
            result = region_.sourceMapping().sourceValue(hiraValue);
            break;
        case ValueKind::IntegerConstant:
            result = new ConstantInt(hiraValue->type(),
                                     static_cast<int>(
                                         hiraValue->integerValue()));
            break;
        case ValueKind::FloatConstant:
            result =
                new ConstantFloat(hiraValue->type(), hiraValue->floatValue());
            break;
        case ValueKind::Temporary:
            break;
        }
        if (result)
            values_[hiraValue] = result;
        return result;
    }

    bool bind(HiraValue *hiraValue, Value *llvmValue) {
        if (!hiraValue || !llvmValue)
            return false;
        values_[hiraValue] = llvmValue;
        return true;
    }

    bool emitLoop() {
        Value *lower = value(loop_->lowerBound());
        Value *upper = value(loop_->upperBound());
        if (!lower || !upper)
            return fail(ExportRejectReason::MissingValue, "loop-bound");

        auto *inductionPhi =
            PhiInst::create_phi(loop_->induction()->type(), newHeader_);
        newHeader_->add_instruction(inductionPhi);
        inductionPhi->addIncoming(lower, preheader_);
        bind(loop_->induction(), inductionPhi);

        for (const auto &binding : loop_->carriedValues()) {
            Value *initial = value(binding.initial);
            if (!initial)
                return fail(ExportRejectReason::MissingValue,
                            "carried-initial");
            auto *phi =
                PhiInst::create_phi(binding.iteration->type(), newHeader_);
            newHeader_->add_instruction(phi);
            phi->addIncoming(initial, preheader_);
            bind(binding.iteration, phi);
            carriedPhis_.push_back({&binding, phi});
        }

        auto *comparison = new ICmpInst(ICmpInst::ICMP_SLT, inductionPhi,
                                        upper, newHeader_);
        new BranchInst(comparison, newBody_, newExit_, newHeader_);

        for (const auto &node : loop_->body().nodes()) {
            if (dynamic_cast<HiraYield *>(node.get()))
                continue;
            if (!emitNode(*node))
                return false;
        }

        new BranchInst(newLatch_, newBody_);
        new BranchInst(newHeader_, newLatch_);

        if (loop_->yieldValues().empty())
            return fail(ExportRejectReason::MissingValue,
                        "induction-yield");
        Value *inductionYield = value(loop_->yieldValues().front());
        if (!inductionYield)
            return fail(ExportRejectReason::MissingValue,
                        "induction-yield");
        inductionPhi->addIncoming(inductionYield, newLatch_);

        for (const auto &entry : carriedPhis_) {
            Value *yielded = value(entry.binding->yielded);
            if (!yielded)
                return fail(ExportRejectReason::MissingValue,
                            "carried-yield");
            entry.phi->addIncoming(yielded, newLatch_);

            auto *exitPhi =
                PhiInst::create_phi(entry.binding->result->type(), newExit_);
            newExit_->add_instruction(exitPhi);
            exitPhi->addIncoming(entry.phi, newHeader_);
            bind(entry.binding->result, exitPhi);
        }
        new BranchInst(oldExit_, newExit_);
        return true;
    }

    bool emitNode(HiraNode &node) {
        Instruction *instruction = nullptr;

        if (auto *load = dynamic_cast<HiraLoad *>(&node)) {
            Value *address = value(load->address());
            if (!address)
                return fail(ExportRejectReason::MissingValue, "load");
            instruction = new LoadInst(address, newBody_);
        } else if (auto *store = dynamic_cast<HiraStore *>(&node)) {
            Value *stored = value(store->value());
            Value *address = value(store->address());
            if (!stored || !address)
                return fail(ExportRejectReason::MissingValue, "store");
            instruction = new StoreInst(stored, address, newBody_);
        } else if (auto *compute = dynamic_cast<HiraComputeOp *>(&node)) {
            std::vector<Value *> operands;
            for (HiraValue *operand : compute->operands()) {
                Value *mapped = value(operand);
                if (!mapped)
                    return fail(ExportRejectReason::MissingValue,
                                "compute-operand");
                operands.push_back(mapped);
            }

            ComputeKind kind = compute->computeKind();
            if (isBinaryCompute(kind) && operands.size() == 2) {
                instruction = new BinaryInst(
                    node.results().front()->type(), binaryOpcode(kind),
                    operands[0], operands[1], newBody_);
            } else if (kind == ComputeKind::ICmp &&
                       operands.size() == 2) {
                instruction = new ICmpInst(
                    static_cast<ICmpInst::ICmpOp>(compute->predicate()),
                    operands[0], operands[1], newBody_);
            } else if (kind == ComputeKind::Select &&
                       operands.size() == 3) {
                instruction =
                    new SelectInst(operands[0], operands[1], operands[2],
                                   newBody_);
            } else if (kind == ComputeKind::GetElementPtr &&
                       operands.size() >= 2) {
                std::vector<Value *> indices(operands.begin() + 1,
                                             operands.end());
                instruction =
                    new GetElementPtrInst(operands[0], indices, newBody_);
            } else if (kind == ComputeKind::ZExt &&
                       operands.size() == 1) {
                instruction =
                    new ZextInst(Instruction::ZExt, operands[0],
                                 node.results().front()->type(), newBody_);
            } else {
                return fail(ExportRejectReason::UnsupportedNode);
            }
        } else {
            return fail(ExportRejectReason::UnsupportedNode);
        }

        if (Instruction *source =
                region_.sourceMapping().sourceInstruction(&node))
            instruction->copySemFlagsFrom(source);
        if (!node.results().empty())
            bind(node.results().front(), instruction);
        return true;
    }

    void replaceExternalUses(Value *oldValue, Value *newValue) {
        auto uses = oldValue->use_list_;
        for (const Use &use : uses) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && !sourceLoop_->isInLoop(user))
                user->set_operand(use.arg_no_, newValue);
        }
    }

    void commit() {
        for (HiraValue *result : region_.results()) {
            Value *oldValue = region_.sourceMapping().sourceValue(result);
            Value *newValue = value(result);
            replaceExternalUses(oldValue, newValue);
        }

        Instruction *oldBranch = preheader_->get_terminator();
        preheader_->delete_instr(oldBranch);
        preheader_->remove_succ_basic_block(oldHeader_);
        oldHeader_->remove_pre_basic_block(preheader_);
        new BranchInst(newHeader_, preheader_);

        std::vector<BasicBlock *> oldBlocks = sourceLoop_->blocksOrdered;
        for (BasicBlock *block : oldBlocks) {
            std::vector<Instruction *> instructions(
                block->instr_list_.begin(), block->instr_list_.end());
            for (auto it = instructions.rbegin();
                 it != instructions.rend(); ++it)
                block->delete_instr(*it);
        }
        for (BasicBlock *block : oldBlocks)
            function_->remove_bb(block);
        function_->invalidateDominatorInfo();
    }

    void rollbackNewBlocks() {
        for (BasicBlock *block : newBlocks_) {
            std::vector<Instruction *> instructions(
                block->instr_list_.begin(), block->instr_list_.end());
            for (auto it = instructions.rbegin();
                 it != instructions.rend(); ++it)
                block->delete_instr(*it);
        }
        for (BasicBlock *block : newBlocks_)
            function_->remove_bb(block);
    }

    struct CarriedPhi {
        const HiraLoop::CarriedBinding *binding;
        PhiInst *phi;
    };

    HiraRegion &region_;
    Loop *sourceLoop_ = nullptr;
    HiraLoop *loop_ = nullptr;
    Function *function_ = nullptr;
    Module *module_ = nullptr;
    BasicBlock *preheader_ = nullptr;
    BasicBlock *oldHeader_ = nullptr;
    BasicBlock *oldExit_ = nullptr;
    BasicBlock *newHeader_ = nullptr;
    BasicBlock *newBody_ = nullptr;
    BasicBlock *newLatch_ = nullptr;
    BasicBlock *newExit_ = nullptr;
    std::vector<BasicBlock *> newBlocks_;
    std::map<HiraValue *, Value *> values_;
    std::vector<CarriedPhi> carriedPhis_;
    ExportResult failure_;

    static unsigned nextRegionId_;
};

unsigned RegionExporter::nextRegionId_ = 0;

} // namespace

ExportResult ExportResult::success() {
    ExportResult result;
    result.changed = true;
    return result;
}

ExportResult ExportResult::reject(ExportRejectReason reason,
                                  std::string detail) {
    ExportResult result;
    result.reason = reason;
    result.detail = std::move(detail);
    return result;
}

const char *exportRejectReasonName(ExportRejectReason reason) {
    switch (reason) {
    case ExportRejectReason::None:
        return "none";
    case ExportRejectReason::InvalidRegion:
        return "invalid-region";
    case ExportRejectReason::UnsupportedExitPhi:
        return "unsupported-exit-phi";
    case ExportRejectReason::UnsupportedNode:
        return "unsupported-node";
    case ExportRejectReason::UnsupportedResult:
        return "unsupported-result";
    case ExportRejectReason::MissingValue:
        return "missing-value";
    case ExportRejectReason::InvalidSourceCFG:
        return "invalid-source-cfg";
    }
    return "unknown";
}

ExportResult exportHiraRegion(HiraRegion &region) {
    return RegionExporter(region).run();
}

} // namespace hira
