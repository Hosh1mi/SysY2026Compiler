#include "pass.hpp"
#include "../analysis/analysisManager.hpp"
#include <utility>
#include <vector>
#include <memory>

class PassManager {
    // [begin, end) is a pass sequence whose complete rounds are repeated
    // until every member reports that it preserved the IR.
    struct FixedPointGroup {
        size_t begin = 0;
        size_t end   = 0;
        bool runOnClean = false;
    };

    std::vector<std::unique_ptr<Pass>> passes;
    std::vector<FixedPointGroup> fixed_point_groups_;
    AnalysisManager analyses_;
    bool dump_ir_    = false;
    bool verify_ir_  = false;
    bool loop_form_ready_ = false;
    bool building_group_ = false;

    PreservedAnalyses runSinglePass(Pass &pass, Module *module);

    void beginFixedPointGroup(bool runOnClean);
    void endFixedPointGroup();

public:
    void addPass(std::unique_ptr<Pass> pass);

    // Repeat a complete pass sequence to a fixed point. By default the group
    // is skipped when no pass since the preceding group changed the IR.
    template <class F>
    void addFixedPointGroup(F &&build, bool runOnClean = false) {
        beginFixedPointGroup(runOnClean);
        std::forward<F>(build)(*this);
        endFixedPointGroup();
    }

    void run(Module *module);

    void setDumpIR(bool v)   { dump_ir_   = v; }
    void setVerifyIR(bool v) { verify_ir_ = v; }
};
