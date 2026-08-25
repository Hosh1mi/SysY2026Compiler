#include "../../../include/mid/hira/polyhedral/dimensionSemanticAnalysis.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace hira::polyhedral {
namespace {

std::string variableName(AffineVariable variable) {
    return std::string(
               variable.kind == AffineVariableKind::Dimension
                   ? "d"
                   : "s") +
           std::to_string(variable.position);
}

std::optional<std::int64_t>
coefficientOf(const AffineExpr &expression,
              AffineVariable dimension) {
    if (!expression.valid())
        return std::nullopt;
    auto iterator = expression.coefficients().find(dimension);
    if (iterator == expression.coefficients().end())
        return 0;
    return iterator->second;
}

enum class SubscriptUse {
    None,
    Leading,
    Trailing,
    Sole,
};

SubscriptUse subscriptUse(const AffineExpr &expression,
                          AffineVariable dimension,
                          bool trailingIndex) {
    auto coefficient = coefficientOf(expression, dimension);
    if (!coefficient || *coefficient == 0)
        return SubscriptUse::None;
    if (trailingIndex)
        return SubscriptUse::Trailing;
    return expression.coefficients().size() == 1
               ? SubscriptUse::Sole
               : SubscriptUse::Leading;
}

SubscriptUse subscriptUse(const AffineExpr &expression,
                          AffineVariable dimension) {
    return subscriptUse(expression, dimension, false);
}

bool isConstantSubscript(const AffineExpr &expression) {
    return expression.valid() && expression.isConstant();
}

std::vector<const AffineExpr *>
varyingSubscripts(const AccessRelation &access) {
    std::vector<const AffineExpr *> varying;
    for (const AffineExpr &subscript : access.subscripts)
        if (!isConstantSubscript(subscript))
            varying.push_back(&subscript);
    return varying;
}

SubscriptUse accessUse(const AccessRelation &access,
                       AffineVariable dimension) {
    const std::vector<const AffineExpr *> varying =
        varyingSubscripts(access);
    if (varying.empty())
        return SubscriptUse::None;
    if (varying.size() == 1)
        return subscriptUse(*varying.front(), dimension);
    SubscriptUse leading =
        subscriptUse(*varying.front(), dimension, false);
    SubscriptUse trailing =
        subscriptUse(*varying.back(), dimension, true);
    if (leading != SubscriptUse::None &&
        trailing != SubscriptUse::None)
        return SubscriptUse::None;
    if (leading != SubscriptUse::None)
        return SubscriptUse::Leading;
    return trailing;
}

const char *roleName(bool outputColumn, bool reductionRow,
                     bool initOutput) {
    if (reductionRow)
        return "reduction";
    if (outputColumn)
        return initOutput ? "output-init" : "output";
    return "unknown";
}

struct DimensionSummary {
    std::set<std::string> accessPatterns;
    bool outputColumn = false;
    bool reductionRow = false;
    bool initOutput = false;
};

void recordAccess(DimensionSummary &summary,
                  MemoryObjectId object,
                  SubscriptUse use) {
    std::ostringstream pattern;
    pattern << "M" << object;
    switch (use) {
    case SubscriptUse::Sole:
        pattern << "[d]";
        summary.accessPatterns.insert(pattern.str());
        summary.outputColumn = true;
        break;
    case SubscriptUse::Leading:
        pattern << "[d,*]";
        summary.accessPatterns.insert(pattern.str());
        summary.reductionRow = true;
        break;
    case SubscriptUse::Trailing:
        pattern << "[*,d]";
        summary.accessPatterns.insert(pattern.str());
        summary.outputColumn = true;
        break;
    default:
        break;
    }
}

DimensionSummary summarizeDimension(
    const PolyhedralModel &model, AffineVariable dimension) {
    DimensionSummary summary;
    bool leadingNonScratch = false;
    bool trailingNonScratch = false;
    bool scratchSole = false;
    for (const AccessRelation &access : model.accesses()) {
        SubscriptUse use = accessUse(access, dimension);
        if (use == SubscriptUse::None)
            continue;
        recordAccess(summary, access.object, use);
        const bool taskPrivate =
            model.memoryObjects()[access.object].taskPrivate;
        if (taskPrivate && use == SubscriptUse::Sole) {
            scratchSole = true;
            if (access.kind == MemoryAccessKind::Write)
                summary.initOutput = true;
            continue;
        }
        if (use == SubscriptUse::Leading)
            leadingNonScratch = true;
        if (use == SubscriptUse::Trailing)
            trailingNonScratch = true;
    }

    bool inDeepCompute = false;
    std::size_t maxDepth = 0;
    for (const PolyhedralStatement &statement :
         model.statements())
        maxDepth = std::max(maxDepth, statement.dimensions.size());
    for (const PolyhedralStatement &statement :
         model.statements()) {
        if (statement.dimensions.size() < maxDepth)
            continue;
        if (std::find(statement.dimensions.begin(),
                      statement.dimensions.end(),
                      dimension) != statement.dimensions.end())
            inDeepCompute = true;
    }
    if (summary.outputColumn && inDeepCompute)
        summary.initOutput = false;
    summary.reductionRow =
        leadingNonScratch && trailingNonScratch && !scratchSole;
    summary.outputColumn =
        scratchSole ||
        (trailingNonScratch && !leadingNonScratch);
    return summary;
}

} // namespace

std::string printDimensionSemantics(const PolyhedralModel &model) {
    std::ostringstream out;
    out << "polyhedral.dimension_semantics {\n";
    std::set<AffineVariable> dimensions;
    for (const IterationDomain &domain : model.domains())
        for (AffineVariable dimension : domain.dimensions)
            dimensions.insert(dimension);
    for (AffineVariable dimension : dimensions) {
        DimensionSummary summary =
            summarizeDimension(model, dimension);
        out << "  " << variableName(dimension);
        if (const HiraValue *induction =
                model.space().source(dimension))
            out << " iv=%h" << induction->id();
        out << " role="
            << roleName(summary.outputColumn,
                       summary.reductionRow,
                       summary.initOutput);
        if (!summary.accessPatterns.empty()) {
            out << " accesses=[";
            bool first = true;
            for (const std::string &pattern :
                 summary.accessPatterns) {
                if (!first)
                    out << ", ";
                first = false;
                out << pattern;
            }
            out << "]";
        }
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
