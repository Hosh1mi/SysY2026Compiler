#include "../../../include/mid/hira/ir/hiraPrinter.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/type.hpp"
#include "../../../include/mid/ir/value.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace hira {
namespace {

std::string valueRef(const HiraValue *value) {
    if (!value)
        return "<null>";
    switch (value->kind()) {
    case ValueKind::IntegerConstant:
        return std::to_string(value->integerValue());
    case ValueKind::FloatConstant: {
        std::ostringstream out;
        out << std::setprecision(9) << value->floatValue();
        return out.str();
    }
    case ValueKind::Temporary:
    case ValueKind::Parameter:
    case ValueKind::Scratch:
        return "%h" + std::to_string(value->id());
    }
    return "<invalid>";
}

std::string typeRef(const HiraValue *value) {
    return value && value->type() ? value->type()->print() : "<null-type>";
}

std::string sourceRef(::Value *value) {
    if (!value)
        return "<none>";
    if (value->name_.empty()) {
        if (dynamic_cast<Constant *>(value))
            return value->print();
        if (auto *argument = dynamic_cast<Argument *>(value))
            return "%arg" + std::to_string(argument->arg_no_);
        if (auto *instruction = dynamic_cast<Instruction *>(value)) {
            std::size_t index = 0;
            if (instruction->parent_) {
                for (Instruction *candidate :
                     instruction->parent_->instr_list_) {
                    if (candidate == instruction)
                        break;
                    ++index;
                }
                return "%" + instruction->parent_->name_ + ".i" +
                       std::to_string(index);
            }
        }
        return "<anon>";
    }
    return print_as_op(value, false);
}

std::string nodeSourceSuffix(const HiraRegion &region,
                             const HiraNode &node) {
    Instruction *source =
        region.sourceMapping().sourceInstruction(&node);
    return source ? "  // llvm=" + sourceRef(source) : "";
}

const char *computeName(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
        return "add";
    case ComputeKind::Sub:
        return "sub";
    case ComputeKind::Mul:
        return "mul";
    case ComputeKind::SDiv:
        return "sdiv";
    case ComputeKind::SRem:
        return "srem";
    case ComputeKind::UDiv:
        return "udiv";
    case ComputeKind::URem:
        return "urem";
    case ComputeKind::FAdd:
        return "fadd";
    case ComputeKind::FSub:
        return "fsub";
    case ComputeKind::FMul:
        return "fmul";
    case ComputeKind::FDiv:
        return "fdiv";
    case ComputeKind::And:
        return "and";
    case ComputeKind::Or:
        return "or";
    case ComputeKind::Xor:
        return "xor";
    case ComputeKind::Shl:
        return "shl";
    case ComputeKind::LShr:
        return "lshr";
    case ComputeKind::AShr:
        return "ashr";
    case ComputeKind::ICmp:
        return "icmp";
    case ComputeKind::Select:
        return "select";
    case ComputeKind::GetElementPtr:
        return "gep";
    case ComputeKind::ZExt:
        return "zext";
    case ComputeKind::BitCast:
        return "bitcast";
    case ComputeKind::Splat:
        return "splat";
    case ComputeKind::ExtractElement:
        return "extract";
    }
    return "unknown";
}

const char *predicateName(int predicate) {
    switch (static_cast<ICmpInst::ICmpOp>(predicate)) {
    case ICmpInst::ICMP_EQ:
        return "eq";
    case ICmpInst::ICMP_NE:
        return "ne";
    case ICmpInst::ICMP_UGT:
        return "ugt";
    case ICmpInst::ICMP_UGE:
        return "uge";
    case ICmpInst::ICMP_ULT:
        return "ult";
    case ICmpInst::ICMP_ULE:
        return "ule";
    case ICmpInst::ICMP_SGT:
        return "sgt";
    case ICmpInst::ICMP_SGE:
        return "sge";
    case ICmpInst::ICMP_SLT:
        return "slt";
    case ICmpInst::ICMP_SLE:
        return "sle";
    }
    return "unknown";
}

void printSequence(std::ostringstream &out, const HiraRegion &region,
                   const HiraSequence &sequence, unsigned indent);

void printOperands(std::ostringstream &out,
                   const std::vector<HiraValue *> &operands) {
    for (std::size_t i = 0; i < operands.size(); ++i) {
        if (i)
            out << ", ";
        out << valueRef(operands[i]);
    }
}

void printNode(std::ostringstream &out, const HiraRegion &region,
               const HiraNode &node, unsigned indent) {
    const std::string spaces(indent, ' ');

    if (auto *loop = dynamic_cast<const HiraLoop *>(&node)) {
        out << spaces << "hira.loop " << valueRef(loop->induction())
            << " = " << valueRef(loop->lowerBound())
            << " to " << valueRef(loop->upperBound())
            << " step " << valueRef(loop->step());
        if (loop->role() == HiraLoop::Role::VectorMain)
            out << " vector_main";
        else if (loop->role() ==
                 HiraLoop::Role::ScalarRemainder)
            out << " scalar_remainder";
        else if (loop->role() == HiraLoop::Role::Parallel)
            out << " parallel";
        if (Loop *source = region.sourceMapping().sourceLoop(loop))
            out << "  // llvm-loop="
                << (source->header ? source->header->name_ : "<null>");
        out << "\n";
        if (!loop->carriedValues().empty()) {
            out << spaces << "    iter_args(";
            for (std::size_t i = 0; i < loop->carriedValues().size(); ++i) {
                if (i)
                    out << ", ";
                const auto &binding = loop->carriedValues()[i];
                out << valueRef(binding.iteration) << " = "
                    << valueRef(binding.initial) << " -> "
                    << valueRef(binding.result);
            }
            out << ")\n";
        }
        out << spaces << "{\n";
        printSequence(out, region, loop->body(), indent + 2);
        out << spaces << "}\n";
        return;
    }

    if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
        if (!condition->results().empty()) {
            out << spaces;
            for (std::size_t index = 0;
                 index < condition->results().size(); ++index) {
                if (index)
                    out << ", ";
                out << valueRef(condition->results()[index]);
            }
            out << " = ";
        } else {
            out << spaces;
        }
        out << "hira.if " << valueRef(condition->condition())
            << " {\n";
        printSequence(out, region, condition->thenSequence(), indent + 2);
        out << spaces << "} else {\n";
        printSequence(out, region, condition->elseSequence(), indent + 2);
        out << spaces << "}";
        if (!condition->resultBindings().empty()) {
            out << " yields(";
            for (std::size_t index = 0;
                 index < condition->resultBindings().size(); ++index) {
                if (index)
                    out << ", ";
                const HiraIf::ResultBinding &binding =
                    condition->resultBindings()[index];
                out << valueRef(binding.thenValue) << " : "
                    << valueRef(binding.elseValue);
            }
            out << ")";
        }
        out << "\n";
        return;
    }

    if (auto *compute = dynamic_cast<const HiraComputeOp *>(&node)) {
        if (!compute->results().empty())
            out << spaces << valueRef(compute->results().front()) << " = ";
        else
            out << spaces;
        out << "hira." << computeName(compute->computeKind());
        if (compute->computeKind() == ComputeKind::ICmp)
            out << "." << predicateName(compute->predicate());
        out << " ";
        printOperands(out, compute->operands());
        if (!compute->results().empty())
            out << " : " << typeRef(compute->results().front());
        out << nodeSourceSuffix(region, node) << "\n";
        return;
    }

    if (auto *load = dynamic_cast<const HiraLoad *>(&node)) {
        out << spaces << valueRef(load->results().front())
            << " = hira.load " << valueRef(load->address())
            << " : " << typeRef(load->results().front())
            << nodeSourceSuffix(region, node) << "\n";
        return;
    }

    if (auto *store = dynamic_cast<const HiraStore *>(&node)) {
        out << spaces << "hira.store " << valueRef(store->value())
            << ", " << valueRef(store->address())
            << nodeSourceSuffix(region, node) << "\n";
        return;
    }

    if (dynamic_cast<const HiraYield *>(&node)) {
        out << spaces << "hira.yield ";
        printOperands(out, node.operands());
        out << "\n";
    }
}

void printSequence(std::ostringstream &out, const HiraRegion &region,
                   const HiraSequence &sequence, unsigned indent) {
    for (const auto &node : sequence.nodes())
        printNode(out, region, *node, indent);
}

void printBoundary(std::ostringstream &out, const char *label,
                   const std::vector<HiraValue *> &values,
                   const SourceMapping &mapping) {
    out << "  " << label << "(";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ", ";
        HiraValue *value = values[i];
        out << valueRef(value) << ": " << typeRef(value)
            << " [llvm=" << sourceRef(mapping.sourceValue(value)) << "]";
    }
    out << ")\n";
}

void printScratches(std::ostringstream &out,
                    const std::vector<HiraValue *> &scratches) {
    out << "  scratches(";
    for (std::size_t index = 0; index < scratches.size(); ++index) {
        if (index)
            out << ", ";
        const HiraValue *scratch = scratches[index];
        out << valueRef(scratch) << ": " << typeRef(scratch)
            << " allocates "
            << (scratch && scratch->allocatedType()
                    ? scratch->allocatedType()->print()
                    : "<null-type>");
    }
    out << ")\n";
}

} // namespace

std::string printHiraRegion(const HiraRegion &region,
                            const std::string &functionName) {
    std::ostringstream out;
    out << "hira.region @" << functionName;
    if (Loop *source = region.sourceLoop())
        out << "." << (source->header ? source->header->name_ : "<null>");
    out << " {\n";
    printBoundary(out, "parameters", region.parameters(),
                  region.sourceMapping());
    printScratches(out, region.scratches());
    printBoundary(out, "results", region.results(),
                  region.sourceMapping());
    printSequence(out, region, region.rootSequence(), 2);
    out << "}\n";
    return out.str();
}

} // namespace hira
