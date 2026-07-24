#include "../../../include/mid/hira/conversion/importer.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
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
        : loop_(loop), loopInfo_(loopInfo),
          region_(std::make_unique<HiraRegion>(&loop)) {}

    ImportResult run() {
        (void)loopInfo_;
        if (!loop_.children.empty())
            return fail(ImportRejectReason::NestedLoop);
        if (!checkControlFlow())
            return fail(ImportRejectReason::NonStraightLineControlFlow);
        if (!checkHeader())
            return fail(ImportRejectReason::UnsupportedHeader);
        if (!buildLoop())
            return std::move(failure_);
        if (!importBody())
            return std::move(failure_);
        if (!finishYieldsAndResults())
            return std::move(failure_);
        return ImportResult::success(std::move(region_));
    }

private:
    struct CarriedPhi {
        PhiInst *phi = nullptr;
        std::size_t bindingIndex = 0;
        HiraValue *result = nullptr;
    };

    ImportResult fail(ImportRejectReason reason, std::string detail = {}) {
        return ImportResult::reject(reason, std::move(detail));
    }

    bool setFailure(ImportRejectReason reason, std::string detail = {}) {
        failure_ = fail(reason, std::move(detail));
        return false;
    }

    bool checkControlFlow() {
        bodyBlocks_.clear();
        for (BasicBlock *block : loop_.blocksOrdered)
            if (block != loop_.header)
                bodyBlocks_.push_back(block);
        if (bodyBlocks_.empty())
            return false;

        for (std::size_t i = 0; i < bodyBlocks_.size(); ++i) {
            BasicBlock *block = bodyBlocks_[i];
            Instruction *terminator = block->get_terminator();
            if (!terminator || !terminator->is_br() ||
                terminator->num_ops_ != 1)
                return false;
            BasicBlock *expected =
                i + 1 < bodyBlocks_.size() ? bodyBlocks_[i + 1] : loop_.header;
            if (terminator->get_operand(0) != expected)
                return false;
        }
        return bodyBlocks_.back() == loop_.singleLatch();
    }

    bool checkHeader() {
        Instruction *terminator = loop_.header->get_terminator();
        if (!terminator || !terminator->is_br() ||
            terminator->num_ops_ != 3)
            return false;
        auto *guard = dynamic_cast<ICmpInst *>(terminator->get_operand(0));
        if (!guard || guard->icmp_op_ != ICmpInst::ICMP_SLT)
            return false;

        for (Instruction *instruction : loop_.header->instr_list_) {
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
        if (HiraValue *mapped = region_->sourceMapping().hiraValue(value))
            return mapped;

        HiraValue *result = nullptr;
        if (auto *integer = dynamic_cast<ConstantInt *>(value)) {
            result = region_->createIntegerConstant(
                integer->type_, integer->value_);
        } else if (auto *floating = dynamic_cast<ConstantFloat *>(value)) {
            result = region_->createFloatConstant(
                floating->type_, floating->value_);
        } else {
            auto *instruction = dynamic_cast<Instruction *>(value);
            if (instruction && loop_.isInLoop(instruction))
                return nullptr;
            if (dynamic_cast<BasicBlock *>(value))
                return nullptr;
            result = region_->createParameter(value->type_);
        }
        region_->sourceMapping().mapValue(result, value);
        return result;
    }

    bool buildLoop() {
        PhiInst *inductionPhi = loop_.getInductionIV();
        HiraValue *lower = getValue(loop_.inductionInit);
        HiraValue *upper = getValue(loop_.tripCount);
        if (!inductionPhi || !lower || !upper)
            return setFailure(ImportRejectReason::MissingValue,
                              "loop-bound");

        HiraValue *step =
            region_->createIntegerConstant(inductionPhi->type_, 1);
        HiraValue *induction = region_->createValue(inductionPhi->type_);
        region_->sourceMapping().mapValue(induction, inductionPhi);

        auto loopNode =
            std::make_unique<HiraLoop>(induction, lower, upper, step);
        loopNode_ = loopNode.get();
        region_->sourceMapping().mapLoop(loopNode_, &loop_);

        for (Instruction *instruction : loop_.header->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            if (phi == inductionPhi)
                continue;
            if (phi->num_ops_ != 4)
                return setFailure(ImportRejectReason::UnsupportedPhi);

            Value *initialSource = incomingFrom(phi, loop_.preheader);
            if (!initialSource)
                return setFailure(ImportRejectReason::UnsupportedPhi,
                                  "missing-initial");
            HiraValue *initial = getValue(initialSource);
            if (!initial)
                return setFailure(ImportRejectReason::MissingValue,
                                  "carried-initial");

            HiraValue *iteration = region_->createValue(phi->type_);
            HiraValue *result = region_->createValue(phi->type_);
            region_->sourceMapping().mapValue(iteration, phi);
            region_->sourceMapping().mapValue(result, phi);
            std::size_t binding =
                loopNode_->addCarriedValue(initial, iteration, result);
            carriedPhis_.push_back({phi, binding, result});
        }

        region_->rootSequence().append(std::move(loopNode));
        return true;
    }

    bool importBody() {
        for (BasicBlock *block : bodyBlocks_) {
            for (Instruction *instruction : block->instr_list_) {
                if (instruction->is_br())
                    continue;
                if (instruction->is_phi())
                    return setFailure(ImportRejectReason::UnsupportedPhi,
                                      block->name_);
                if (!importInstruction(instruction))
                    return false;
            }
        }
        return true;
    }

    bool importInstruction(Instruction *instruction) {
        std::unique_ptr<HiraNode> node;

        if (instruction->is_load()) {
            HiraValue *address = getValue(instruction->get_operand(0));
            if (!address)
                return setFailure(ImportRejectReason::MissingValue,
                                  instruction->name_);
            node = std::make_unique<HiraLoad>(address);
        } else if (instruction->is_store()) {
            HiraValue *value = getValue(instruction->get_operand(0));
            HiraValue *address = getValue(instruction->get_operand(1));
            if (!value || !address)
                return setFailure(ImportRejectReason::MissingValue,
                                  "store-operand");
            node = std::make_unique<HiraStore>(value, address);
        } else {
            std::optional<ComputeKind> kind = computeKindFor(instruction);
            if (!kind) {
                std::ostringstream detail;
                detail << "opcode-" << static_cast<int>(instruction->op_id_);
                return setFailure(ImportRejectReason::UnsupportedInstruction,
                                  detail.str());
            }
            int predicate = 0;
            if (auto *comparison = dynamic_cast<ICmpInst *>(instruction))
                predicate = static_cast<int>(comparison->icmp_op_);
            auto compute =
                std::make_unique<HiraComputeOp>(*kind, predicate);
            for (unsigned i = 0; i < instruction->num_ops_; ++i) {
                HiraValue *operand = getValue(instruction->get_operand(i));
                if (!operand)
                    return setFailure(ImportRejectReason::MissingValue,
                                      instruction->name_);
                compute->addOperand(operand);
            }
            node = std::move(compute);
        }

        if (!instruction->is_store()) {
            HiraValue *result = region_->createValue(instruction->type_);
            node->addResult(result);
            region_->sourceMapping().mapValue(result, instruction);
        }
        HiraNode *inserted = loopNode_->body().append(std::move(node));
        region_->sourceMapping().mapNode(inserted, instruction);
        return true;
    }

    bool finishYieldsAndResults() {
        BasicBlock *latch = loop_.singleLatch();
        PhiInst *inductionPhi = loop_.getInductionIV();
        Value *inductionUpdate = incomingFrom(inductionPhi, latch);
        HiraValue *inductionYield = getValue(inductionUpdate);
        if (!inductionYield)
            return setFailure(ImportRejectReason::MissingYield,
                              "induction");
        if (isUsedOutsideLoop(inductionPhi, loop_))
            return setFailure(ImportRejectReason::LiveOutInduction);

        auto yield = std::make_unique<HiraYield>();
        yield->addOperand(inductionYield);
        loopNode_->addYieldValue(inductionYield);

        for (const CarriedPhi &carried : carriedPhis_) {
            Value *yieldSource = incomingFrom(carried.phi, latch);
            HiraValue *yieldValue = getValue(yieldSource);
            if (!yieldValue)
                return setFailure(ImportRejectReason::MissingYield,
                                  carried.phi->name_);
            loopNode_->setCarriedYield(carried.bindingIndex, yieldValue);
            loopNode_->addYieldValue(yieldValue);
            yield->addOperand(yieldValue);
            if (isUsedOutsideLoop(carried.phi, loop_))
                region_->addResult(carried.result);
        }

        loopNode_->body().append(std::move(yield));

        for (BasicBlock *block : loop_.blocksOrdered) {
            for (Instruction *instruction : block->instr_list_) {
                if (instruction->is_phi() ||
                    !isUsedOutsideLoop(instruction, loop_))
                    continue;
                HiraValue *value = getValue(instruction);
                if (!value)
                    return setFailure(ImportRejectReason::MissingValue,
                                      "region-result");
                region_->addResult(value);
            }
        }
        return true;
    }

    Loop &loop_;
    const LoopInfo &loopInfo_;
    std::unique_ptr<HiraRegion> region_;
    HiraLoop *loopNode_ = nullptr;
    std::vector<BasicBlock *> bodyBlocks_;
    std::vector<CarriedPhi> carriedPhis_;
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
