// This file implements the compact interference graph used by allocation
// and live-range repair without allocating objects for individual edges.
#include "backend/interference_graph.hpp"

#include <algorithm>

namespace backend::aarch64 {
namespace {

bool sameRegisterBank(RegClass lhs, RegClass rhs) {
  const bool lhsGPR = lhs == RegClass::GPR32 || lhs == RegClass::GPR64;
  const bool rhsGPR = rhs == RegClass::GPR32 || rhs == RegClass::GPR64;
  const bool lhsVector = lhs == RegClass::FPR32 || lhs == RegClass::NEON128;
  const bool rhsVector = rhs == RegClass::FPR32 || rhs == RegClass::NEON128;
  return (lhsGPR && rhsGPR) || (lhsVector && rhsVector);
}

} // namespace

InterferenceGraph::InterferenceGraph(
    const std::vector<LiveInterval> &intervals) {
  nodes_.reserve(intervals.size());
  classes_.reserve(intervals.size());

  VReg maximumRegister = 0;
  for (const LiveInterval &interval : intervals)
    maximumRegister = std::max(maximumRegister, interval.reg);
  if (!intervals.empty())
    index_.assign(static_cast<std::size_t>(maximumRegister) + 1, kNoIndex);

  for (const LiveInterval &interval : intervals) {
    index_[interval.reg] = nodes_.size();
    nodes_.push_back(interval.reg);
    classes_.push_back(interval.regClass);
  }

  wordsPerRow_ = (nodes_.size() + 63) / 64;
  edges_.assign(nodes_.size() * wordsPerRow_, 0);
  degrees_.assign(nodes_.size(), 0);
}

std::size_t InterferenceGraph::indexOf(VReg reg) const {
  if (reg >= index_.size())
    return kNoIndex;
  return index_[reg];
}

bool InterferenceGraph::contains(VReg reg) const {
  return indexOf(reg) != kNoIndex;
}

bool InterferenceGraph::addEdge(VReg lhs, VReg rhs) {
  if (lhs == rhs)
    return false;
  const std::size_t lhsIndex = indexOf(lhs);
  const std::size_t rhsIndex = indexOf(rhs);
  if (lhsIndex == kNoIndex || rhsIndex == kNoIndex ||
      !sameRegisterBank(classes_[lhsIndex], classes_[rhsIndex]))
    return false;

  const std::size_t lhsWord =
      lhsIndex * wordsPerRow_ + rhsIndex / 64;
  const std::uint64_t rhsBit = std::uint64_t{1} << (rhsIndex % 64);
  if (edges_[lhsWord] & rhsBit)
    return false;

  edges_[lhsWord] |= rhsBit;
  edges_[rhsIndex * wordsPerRow_ + lhsIndex / 64] |=
      std::uint64_t{1} << (lhsIndex % 64);
  ++degrees_[lhsIndex];
  ++degrees_[rhsIndex];
  return true;
}

bool InterferenceGraph::hasEdge(VReg lhs, VReg rhs) const {
  const std::size_t lhsIndex = indexOf(lhs);
  const std::size_t rhsIndex = indexOf(rhs);
  if (lhsIndex == kNoIndex || rhsIndex == kNoIndex)
    return false;
  return (edges_[lhsIndex * wordsPerRow_ + rhsIndex / 64] &
          (std::uint64_t{1} << (rhsIndex % 64))) != 0;
}

unsigned InterferenceGraph::degree(VReg reg) const {
  const std::size_t index = indexOf(reg);
  return index == kNoIndex ? 0 : degrees_[index];
}

InterferenceGraph::NeighborRange
InterferenceGraph::neighbors(VReg reg) const {
  return NeighborRange(this, indexOf(reg));
}

InterferenceGraph::NeighborIterator::NeighborIterator(
    const InterferenceGraph *graph, std::size_t row, bool end)
    : graph_(graph), row_(row) {
  nodeIndex_ = graph_->nodes_.size();
  if (!end && row_ != kNoIndex)
    advance();
}

void InterferenceGraph::NeighborIterator::advance() {
  while (true) {
    if (remainingWord_ != 0) {
      const unsigned bit =
          static_cast<unsigned>(__builtin_ctzll(remainingWord_));
      remainingWord_ &= remainingWord_ - 1;
      nodeIndex_ = (nextWord_ - 1) * 64 + bit;
      if (nodeIndex_ < graph_->nodes_.size())
        return;
      continue;
    }
    if (nextWord_ == graph_->wordsPerRow_) {
      nodeIndex_ = graph_->nodes_.size();
      return;
    }
    remainingWord_ =
        graph_->edges_[row_ * graph_->wordsPerRow_ + nextWord_];
    ++nextWord_;
  }
}

VReg InterferenceGraph::NeighborIterator::operator*() const {
  return graph_->nodes_[nodeIndex_];
}

InterferenceGraph::NeighborIterator &
InterferenceGraph::NeighborIterator::operator++() {
  advance();
  return *this;
}

bool InterferenceGraph::NeighborIterator::operator==(
    const NeighborIterator &other) const {
  return graph_ == other.graph_ && nodeIndex_ == other.nodeIndex_;
}

InterferenceGraph::NeighborIterator
InterferenceGraph::NeighborRange::begin() const {
  return NeighborIterator(graph_, row_, false);
}

InterferenceGraph::NeighborIterator
InterferenceGraph::NeighborRange::end() const {
  return NeighborIterator(graph_, row_, true);
}

} // namespace backend::aarch64
