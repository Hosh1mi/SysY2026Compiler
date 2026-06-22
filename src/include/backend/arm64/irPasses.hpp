#pragma once

#include "../../mid/ir/ir.hpp"

#include <iosfwd>

class Arm64IRPreparationPipeline {
public:
    Arm64IRPreparationPipeline(bool enableOptimizations, bool enableScheduling,
                               bool dumpPreRA, std::ostream &diagnostics);
    void run(Module *module) const;

private:
    bool enableOptimizations_;
    bool enableScheduling_;
    bool dumpPreRA_;
    std::ostream &diagnostics_;
};

