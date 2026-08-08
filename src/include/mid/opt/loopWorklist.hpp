#pragma once
// A small worklist for loop transforms that invalidate LoopInfo.  Work items
// are loop headers rather than Loop pointers, so a pass can rebuild LoopInfo
// after changing the CFG and resolve the remaining work against the new tree.

#include "../analysis/loopInfo.hpp"

#include <cstddef>
#include <deque>
#include <unordered_set>

class AffectedLoopWorklist {
public:
    void seed(const LoopInfo &loopInfo) {
        for (const auto &ownedLoop : loopInfo.allLoops())
            add(ownedLoop->header);
    }

    void add(BasicBlock *header) {
        if (header && queued_.insert(header).second)
            pending_.push_back(header);
    }

    Loop *take(const LoopInfo &loopInfo) {
        while (!pending_.empty()) {
            BasicBlock *header = pending_.front();
            pending_.pop_front();
            queued_.erase(header);
            if (Loop *loop = findByHeader(loopInfo, header))
                return loop;
        }
        return nullptr;
    }

    // A structural change can alter the candidate itself, its nesting edge,
    // and sibling adjacency.  Those are the only loops reconsidered here;
    // unrelated queued work keeps its original position.
    void addNeighborhood(const LoopInfo &loopInfo, BasicBlock *header) {
        Loop *loop = findByHeader(loopInfo, header);
        if (!loop) return;

        add(loop->header);
        if (loop->parent) add(loop->parent->header);
        for (Loop *child : loop->children)
            add(child->header);

        const std::vector<Loop *> &siblings =
            loop->parent ? loop->parent->children : loopInfo.topLevelLoops();
        for (std::size_t index = 0; index < siblings.size(); ++index) {
            if (siblings[index] != loop) continue;
            if (index != 0) add(siblings[index - 1]->header);
            if (index + 1 < siblings.size()) add(siblings[index + 1]->header);
            break;
        }
    }

private:
    static Loop *findByHeader(const LoopInfo &loopInfo, BasicBlock *header) {
        for (const auto &ownedLoop : loopInfo.allLoops())
            if (ownedLoop->header == header)
                return ownedLoop.get();
        return nullptr;
    }

    std::deque<BasicBlock *> pending_;
    std::unordered_set<BasicBlock *> queued_;
};
