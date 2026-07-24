#pragma once

#include "../opt/pass.hpp"

namespace hira {

class HiraPass final : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &analysisManager)
        override;
    std::string name() const override { return "Hira"; }
    bool convergenceRelevant() const override { return false; }
};

} // namespace hira
