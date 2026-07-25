#pragma once

#include "polyhedralModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hira::polyhedral {

using ScheduleCandidateId = std::uint32_t;

enum class ScheduleCandidateKind {
    Identity,
    Interchange,
    Permutation,
};

using ScheduleTreeNodeId = std::uint32_t;

enum class ScheduleTreeNodeKind {
    Sequence,
    Band,
    Filter,
    Guard,
};

struct ScheduleBand {
    std::vector<AffineVariable> dimensions;
    std::vector<bool> coincident;
    std::vector<std::uint32_t> tileSizes;
    bool permutable = false;
    std::optional<AffineVariable> parallelDimension;
    std::optional<AffineVariable> vectorDimension;
};

struct ScheduleTreeNode {
    ScheduleTreeNodeId id = 0;
    ScheduleTreeNodeKind kind = ScheduleTreeNodeKind::Sequence;
    std::optional<ScheduleTreeNodeId> parent;
    std::vector<ScheduleTreeNodeId> children;
    std::vector<StatementId> statements;
    ScheduleBand band;
};

struct StatementSchedule;

class ScheduleTree {
public:
    ScheduleTreeNodeId root() const { return root_; }
    const std::vector<ScheduleTreeNode> &nodes() const {
        return nodes_;
    }
    const ScheduleTreeNode *node(ScheduleTreeNodeId id) const {
        return id < nodes_.size() ? &nodes_[id] : nullptr;
    }

private:
    friend ScheduleTree buildScheduleTree(
        const std::vector<StatementSchedule> &statements);

    ScheduleTreeNodeId root_ = 0;
    std::vector<ScheduleTreeNode> nodes_;
};

struct StatementSchedule {
    StatementId statement = 0;
    std::vector<ScheduleComponent> components;
};

struct ScheduleCandidate {
    ScheduleCandidateId id = 0;
    ScheduleCandidateKind kind = ScheduleCandidateKind::Identity;
    AffineVariable outerDimension;
    AffineVariable innerDimension;
    std::vector<AffineVariable> originalDimensions;
    std::vector<AffineVariable> scheduledDimensions;
    std::vector<StatementSchedule> statements;
    ScheduleTree tree;
};

class ScheduleCandidateSet {
public:
    const std::vector<ScheduleCandidate> &candidates() const {
        return candidates_;
    }

private:
    friend ScheduleCandidateSet
    buildScheduleCandidates(const PolyhedralModel &model);

    std::vector<ScheduleCandidate> candidates_;
};

ScheduleCandidateSet
buildScheduleCandidates(const PolyhedralModel &model);
ScheduleTree buildScheduleTree(
    const std::vector<StatementSchedule> &statements);
std::string printScheduleCandidates(
    const ScheduleCandidateSet &schedules);

} // namespace hira::polyhedral
