#pragma once

#include <cstdint>

class PreservedAnalyses {
public:
    static PreservedAnalyses all() {
        PreservedAnalyses pa;
        pa.all_ = true;
        return pa;
    }

    static PreservedAnalyses none() {
        return PreservedAnalyses();
    }

    static PreservedAnalyses cfgAnalyses() {
        PreservedAnalyses pa;
        pa.preserveCFGAnalyses();
        return pa;
    }

    void preserveBasicAA() { mask_ |= BasicAA; }
    void preserveDominatorTree() { mask_ |= DominatorTree; }
    void preservePostDominatorTree() { mask_ |= PostDominatorTree; }
    void preserveDominanceFrontier() { mask_ |= DominanceFrontier; }
    void preserveLVI() { mask_ |= LazyValueInfo; }
    void preserveLoopInfo() { mask_ |= LoopInfo; }
    void preserveSCEV() { mask_ |= SCEV; }
    void preserveCFGAnalyses() {
        preserveDominatorTree();
        preservePostDominatorTree();
        preserveDominanceFrontier();
    }

    bool preservesAll() const { return all_; }
    bool preservesBasicAA() const { return all_ || (mask_ & BasicAA); }
    bool preservesDominatorTree() const { return all_ || (mask_ & DominatorTree); }
    bool preservesPostDominatorTree() const {
        return all_ || (mask_ & PostDominatorTree);
    }
    bool preservesDominanceFrontier() const {
        return all_ || (mask_ & DominanceFrontier);
    }
    bool preservesLVI() const { return all_ || (mask_ & LazyValueInfo); }
    bool preservesLoopInfo() const { return all_ || (mask_ & LoopInfo); }
    bool preservesSCEV() const { return all_ || (mask_ & SCEV); }

private:
    enum AnalysisBit : std::uint32_t {
        BasicAA = 1u << 0,
        LazyValueInfo = 1u << 1,
        LoopInfo = 1u << 2,
        SCEV = 1u << 3,
        DominatorTree = 1u << 4,
        PostDominatorTree = 1u << 5,
        DominanceFrontier = 1u << 6,
    };

    bool all_ = false;
    std::uint32_t mask_ = 0;
};
