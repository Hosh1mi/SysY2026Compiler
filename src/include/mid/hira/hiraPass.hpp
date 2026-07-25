#pragma once

#include "../opt/pass.hpp"

namespace hira {

class HiraPass final : public Pass {
public:
    explicit HiraPass(bool forceRoundtrip = false,
                      bool dumpHira = false,
                      bool dumpPolyhedral = false)
        : forceRoundtrip_(forceRoundtrip), dumpHira_(dumpHira),
          dumpPolyhedral_(dumpPolyhedral) {}

    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &analysisManager)
        override;
    std::string name() const override { return "Hira"; }
    bool convergenceRelevant() const override { return false; }

private:
    bool forceRoundtrip_;
    bool dumpHira_;
    bool dumpPolyhedral_;
};

} // namespace hira
