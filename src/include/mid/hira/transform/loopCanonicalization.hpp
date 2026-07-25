#pragma once

#include "../../opt/pass.hpp"

namespace hira {

class LoopCanonicalizationPass final : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(
        Module *module, AnalysisManager &analysisManager) override;
    std::string name() const override {
        return "HiraLoopCanonicalization";
    }
};

} // namespace hira
