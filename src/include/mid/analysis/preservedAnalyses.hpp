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

    void preserveBasicAA() { mask_ |= BasicAA; }
    void preserveLoopInfo() { mask_ |= LoopInfo; }
    void preserveSCEV() { mask_ |= SCEV; }

    bool preservesAll() const { return all_; }
    bool preservesBasicAA() const { return all_ || (mask_ & BasicAA); }
    bool preservesLoopInfo() const { return all_ || (mask_ & LoopInfo); }
    bool preservesSCEV() const { return all_ || (mask_ & SCEV); }

private:
    enum AnalysisBit : std::uint32_t {
        BasicAA = 1u << 0,
        LoopInfo = 1u << 1,
        SCEV = 1u << 2,
    };

    bool all_ = false;
    std::uint32_t mask_ = 0;
};
