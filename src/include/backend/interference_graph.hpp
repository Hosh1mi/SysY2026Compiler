// This file provides the compact interference graph shared by register
// coloring and live-range repair analysis.
#pragma once

#include "live_range.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace backend::aarch64 {

// Dense register-pressure regions make per-edge tree or hash nodes costly.
// The graph therefore stores an undirected bit matrix and exposes an
// allocation-free neighbor iterator for graph algorithms.
class InterferenceGraph {
public:
  class NeighborIterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = VReg;
    using difference_type = std::ptrdiff_t;
    using pointer = const VReg *;
    using reference = VReg;

    VReg operator*() const;
    NeighborIterator &operator++();
    bool operator==(const NeighborIterator &other) const;
    bool operator!=(const NeighborIterator &other) const {
      return !(*this == other);
    }

  private:
    friend class InterferenceGraph;
    NeighborIterator(const InterferenceGraph *graph, std::size_t row,
                     bool end);
    void advance();

    const InterferenceGraph *graph_ = nullptr;
    std::size_t row_ = 0;
    std::size_t nextWord_ = 0;
    std::uint64_t remainingWord_ = 0;
    std::size_t nodeIndex_ = 0;
  };

  class NeighborRange {
  public:
    NeighborIterator begin() const;
    NeighborIterator end() const;

  private:
    friend class InterferenceGraph;
    NeighborRange(const InterferenceGraph *graph, std::size_t row)
        : graph_(graph), row_(row) {}

    const InterferenceGraph *graph_;
    std::size_t row_;
  };

  explicit InterferenceGraph(const std::vector<LiveInterval> &intervals);

  bool contains(VReg reg) const;
  bool addEdge(VReg lhs, VReg rhs);
  bool hasEdge(VReg lhs, VReg rhs) const;
  unsigned degree(VReg reg) const;
  NeighborRange neighbors(VReg reg) const;

private:
  static constexpr std::size_t kNoIndex =
      static_cast<std::size_t>(-1);

  std::size_t indexOf(VReg reg) const;

  std::vector<VReg> nodes_;
  std::vector<RegClass> classes_;
  std::vector<std::size_t> index_;
  std::size_t wordsPerRow_ = 0;
  std::vector<std::uint64_t> edges_;
  std::vector<unsigned> degrees_;
};

} // namespace backend::aarch64
