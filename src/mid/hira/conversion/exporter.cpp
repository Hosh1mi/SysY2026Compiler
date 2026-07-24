#include "../../../include/mid/hira/conversion/exporter.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <map>
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

        LoopEmission rootEmission;
        if (!emitLoop(*rootLoop_, preheader_, false, rootEmission)) {
            rollbackNewBlocks();
            return failure_;
        }
        rootHeader_ = rootEmission.header;
        new BranchInst(oldExit_, rootEmission.exit);
        commit();
        return ExportResult::success();
    }

private:
    struct LoopEmission {
        BasicBlock *header = nullptr;
        BasicBlock *exit = nullptr;
    };

    struct CarriedPhi {
        const HiraLoop::CarriedBinding *binding = nullptr;
        PhiInst *phi = nullptr;
    };

    bool fail(ExportRejectReason reason, std::string detail = {}) {
        failure_ = ExportResult::reject(reason, std::move(detail));
        return false;
    }

    bool validateNode(const HiraNode &node) {
        if (auto *loop = dynamic_cast<const HiraLoop *>(&node))
            return validateLoop(*loop);
        if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
            if (condition->operands().size() != 1 ||
                !condition->results().empty() ||
                condition->condition()->type() != module_->int1_ty_)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-if-interface");
            for (const auto &branchNode :
                 condition->thenSequence().nodes())
                if (!validateNode(*branchNode))
                    return false;
            for (const auto &branchNode :
                 condition->elseSequence().nodes())
                if (!validateNode(*branchNode))
                    return false;
            return true;
        }
        if (dynamic_cast<const HiraYield *>(&node))
            return fail(ExportRejectReason::InvalidRegion,
                        "misplaced-yield");
        if (dynamic_cast<const HiraLoad *>(&node))
            return node.results().size() == 1 ||
                   fail(ExportRejectReason::InvalidRegion,
                        "invalid-load-result");
        if (dynamic_cast<const HiraStore *>(&node))
            return node.results().empty() ||
                   fail(ExportRejectReason::InvalidRegion,
                        "invalid-store-result");

        auto *compute = dynamic_cast<const HiraComputeOp *>(&node);
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
        return true;
    }

    bool validateLoop(const HiraLoop &loop) {
        const auto &nodes = loop.body().nodes();
        if (loop.yieldValues().size() !=
                loop.carriedValues().size() + 1 ||
            nodes.empty())
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-loop-yields");
        auto *yield =
            dynamic_cast<const HiraYield *>(nodes.back().get());
        if (!yield ||
            yield->operands().size() != loop.yieldValues().size())
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-loop-yield-node");
        for (std::size_t index = 0;
             index < loop.yieldValues().size(); ++index)
            if (yield->operands()[index] != loop.yieldValues()[index])
                return fail(ExportRejectReason::InvalidRegion,
                            "mismatched-loop-yield");
        for (const auto &binding : loop.carriedValues())
            if (!binding.initial || !binding.iteration ||
                !binding.yielded || !binding.result)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-carried-binding");
        for (std::size_t index = 0; index + 1 < nodes.size(); ++index)
            if (!validateNode(*nodes[index]))
                return false;
        return true;
    }

    bool validate() {
        if (!sourceLoop_ || !function_ || !module_ ||
            !sourceLoop_->preheader || !sourceLoop_->singleExit())
            return fail(ExportRejectReason::InvalidRegion);

        preheader_ = sourceLoop_->preheader;
        oldHeader_ = sourceLoop_->header;
        oldExit_ = sourceLoop_->singleExit();
        Instruction *preheaderTerminator = preheader_->get_terminator();
        if (!preheaderTerminator || !preheaderTerminator->is_br() ||
            preheaderTerminator->num_ops_ != 1 ||
            preheaderTerminator->get_operand(0) != oldHeader_)
            return fail(ExportRejectReason::InvalidSourceCFG);

        for (Instruction *instruction : oldExit_->instr_list_) {
            if (instruction->is_phi())
                return fail(ExportRejectReason::UnsupportedExitPhi);
            break;
        }

        if (region_.rootSequence().nodes().size() != 1)
            return fail(ExportRejectReason::InvalidRegion);
        rootLoop_ = dynamic_cast<HiraLoop *>(
            region_.rootSequence().nodes().front().get());
        if (!rootLoop_ || !validateLoop(*rootLoop_))
            return false;

        for (HiraValue *parameter : region_.parameters())
            if (!region_.sourceMapping().sourceValue(parameter))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-parameter");

        for (HiraValue *result : region_.results()) {
            if (!region_.sourceMapping().sourceValue(result))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-result");
            bool found = false;
            for (const auto &binding : rootLoop_->carriedValues())
                found |= binding.result == result;
            if (!found)
                return fail(ExportRejectReason::UnsupportedResult);
        }
        return true;
    }

    BasicBlock *createBlock(const std::string &role, unsigned id) {
        auto *block = new BasicBlock(
            module_, "label_hira_" + role + "_" + std::to_string(id),
            function_);
        newBlocks_.push_back(block);
        return block;
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
            result = new ConstantInt(
                hiraValue->type(),
                static_cast<int>(hiraValue->integerValue()));
            break;
        case ValueKind::FloatConstant:
            result =
                new ConstantFloat(hiraValue->type(),
                                  hiraValue->floatValue());
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

    bool emitLoop(HiraLoop &loop, BasicBlock *entry,
                  bool connectEntry, LoopEmission &emission) {
        const unsigned id = nextBlockId_++;
        BasicBlock *header = createBlock("header", id);
        BasicBlock *body = createBlock("body", id);
        BasicBlock *latch = createBlock("latch", id);
        BasicBlock *exit = createBlock("exit", id);
        emission = {header, exit};

        Value *lower = value(loop.lowerBound());
        Value *upper = value(loop.upperBound());
        if (!lower || !upper)
            return fail(ExportRejectReason::MissingValue,
                        "loop-bound");

        auto *inductionPhi =
            PhiInst::create_phi(loop.induction()->type(), header);
        header->add_instruction(inductionPhi);
        inductionPhi->addIncoming(lower, entry);
        bind(loop.induction(), inductionPhi);

        std::vector<CarriedPhi> carriedPhis;
        for (const auto &binding : loop.carriedValues()) {
            Value *initial = value(binding.initial);
            if (!initial)
                return fail(ExportRejectReason::MissingValue,
                            "carried-initial");
            auto *phi =
                PhiInst::create_phi(binding.iteration->type(), header);
            header->add_instruction(phi);
            phi->addIncoming(initial, entry);
            bind(binding.iteration, phi);
            carriedPhis.push_back({&binding, phi});
        }

        auto *comparison = new ICmpInst(ICmpInst::ICMP_SLT,
                                        inductionPhi, upper, header);
        new BranchInst(comparison, body, exit, header);
        if (connectEntry)
            new BranchInst(header, entry);

        BasicBlock *continuation = nullptr;
        if (!emitSequence(loop.body(), body, continuation))
            return false;

        new BranchInst(latch, continuation);
        new BranchInst(header, latch);

        Value *inductionYield = value(loop.yieldValues().front());
        if (!inductionYield)
            return fail(ExportRejectReason::MissingValue,
                        "induction-yield");
        inductionPhi->addIncoming(inductionYield, latch);

        for (const CarriedPhi &entryPhi : carriedPhis) {
            Value *yielded = value(entryPhi.binding->yielded);
            if (!yielded)
                return fail(ExportRejectReason::MissingValue,
                            "carried-yield");
            entryPhi.phi->addIncoming(yielded, latch);

            auto *exitPhi = PhiInst::create_phi(
                entryPhi.binding->result->type(), exit);
            exit->add_instruction(exitPhi);
            exitPhi->addIncoming(entryPhi.phi, header);
            bind(entryPhi.binding->result, exitPhi);
        }
        return true;
    }

    bool emitSequence(HiraSequence &sequence, BasicBlock *entry,
                      BasicBlock *&continuation) {
        continuation = entry;
        for (const auto &node : sequence.nodes()) {
            if (dynamic_cast<HiraYield *>(node.get()))
                break;
            if (auto *nested = dynamic_cast<HiraLoop *>(node.get())) {
                LoopEmission nestedEmission;
                if (!emitLoop(*nested, continuation, true,
                              nestedEmission))
                    return false;
                continuation = nestedEmission.exit;
                continue;
            }
            if (auto *condition = dynamic_cast<HiraIf *>(node.get())) {
                if (!emitIf(*condition, continuation))
                    return false;
                continue;
            }
            if (!emitNode(*node, continuation))
                return false;
        }
        return true;
    }

    bool emitIf(HiraIf &condition, BasicBlock *&continuation) {
        Value *guard = value(condition.condition());
        if (!guard)
            return fail(ExportRejectReason::MissingValue,
                        "if-condition");

        const unsigned id = nextBlockId_++;
        BasicBlock *thenBlock = createBlock("if_then", id);
        BasicBlock *elseBlock = createBlock("if_else", id);
        BasicBlock *joinBlock = createBlock("if_end", id);
        new BranchInst(guard, thenBlock, elseBlock, continuation);

        BasicBlock *thenContinuation = nullptr;
        if (!emitSequence(condition.thenSequence(), thenBlock,
                          thenContinuation))
            return false;
        new BranchInst(joinBlock, thenContinuation);

        BasicBlock *elseContinuation = nullptr;
        if (!emitSequence(condition.elseSequence(), elseBlock,
                          elseContinuation))
            return false;
        new BranchInst(joinBlock, elseContinuation);

        continuation = joinBlock;
        return true;
    }

    bool emitNode(HiraNode &node, BasicBlock *destination) {
        Instruction *instruction = nullptr;

        if (auto *load = dynamic_cast<HiraLoad *>(&node)) {
            Value *address = value(load->address());
            if (!address)
                return fail(ExportRejectReason::MissingValue, "load");
            instruction = new LoadInst(address, destination);
        } else if (auto *store = dynamic_cast<HiraStore *>(&node)) {
            Value *stored = value(store->value());
            Value *address = value(store->address());
            if (!stored || !address)
                return fail(ExportRejectReason::MissingValue, "store");
            instruction = new StoreInst(stored, address, destination);
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
                    operands[0], operands[1], destination);
            } else if (kind == ComputeKind::ICmp &&
                       operands.size() == 2) {
                instruction = new ICmpInst(
                    static_cast<ICmpInst::ICmpOp>(
                        compute->predicate()),
                    operands[0], operands[1], destination);
            } else if (kind == ComputeKind::Select &&
                       operands.size() == 3) {
                instruction = new SelectInst(
                    operands[0], operands[1], operands[2],
                    destination);
            } else if (kind == ComputeKind::GetElementPtr &&
                       operands.size() >= 2) {
                std::vector<Value *> indices(operands.begin() + 1,
                                             operands.end());
                instruction = new GetElementPtrInst(
                    operands[0], indices, destination);
            } else if (kind == ComputeKind::ZExt &&
                       operands.size() == 1) {
                instruction = new ZextInst(
                    Instruction::ZExt, operands[0],
                    node.results().front()->type(), destination);
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
            Value *oldValue =
                region_.sourceMapping().sourceValue(result);
            Value *newValue = value(result);
            replaceExternalUses(oldValue, newValue);
        }

        Instruction *oldBranch = preheader_->get_terminator();
        preheader_->delete_instr(oldBranch);
        preheader_->remove_succ_basic_block(oldHeader_);
        oldHeader_->remove_pre_basic_block(preheader_);
        new BranchInst(rootHeader_, preheader_);

        std::vector<BasicBlock *> oldBlocks =
            sourceLoop_->blocksOrdered;
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

    HiraRegion &region_;
    Loop *sourceLoop_ = nullptr;
    HiraLoop *rootLoop_ = nullptr;
    Function *function_ = nullptr;
    Module *module_ = nullptr;
    BasicBlock *preheader_ = nullptr;
    BasicBlock *oldHeader_ = nullptr;
    BasicBlock *oldExit_ = nullptr;
    BasicBlock *rootHeader_ = nullptr;
    std::vector<BasicBlock *> newBlocks_;
    std::map<HiraValue *, Value *> values_;
    ExportResult failure_;

    static unsigned nextBlockId_;
};

unsigned RegionExporter::nextBlockId_ = 0;

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
