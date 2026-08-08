#include "pass.hpp"
#include "../analysis/analysisManager.hpp"
#include <utility>
#include <vector>
#include <memory>

class PassManager {
    // [begin, end) 内的 pass 作为整体重复执行，直到收敛或达到轮数上限。
    struct RepeatGroup {
        size_t begin = 0;
        size_t end   = 0;
        int    maxRounds = 1;
        bool trackAllChanges = false;
        bool verifyLoopForm = false;
    };

    std::vector<std::unique_ptr<Pass>> passes;
    std::vector<RepeatGroup> groups_;
    AnalysisManager analyses_;
    bool dump_ir_    = false;
    bool verify_ir_  = false;
    bool loop_form_ready_ = false;

    PreservedAnalyses runSinglePass(Pass &pass, Module *module);
    void verifyRepeatGroupExit(Module *module, bool completedNormally);

    void beginRepeatGroup(int maxRounds, bool trackAllChanges,
                          bool verifyLoopForm);
    void endRepeatGroup();

public:
    void addPass(std::unique_ptr<Pass> pass);

    // Build a repeat group via a scoped callback so begin/end stay paired.
    template <class F>
    void addRepeatGroup(int maxRounds, F &&build) {
        beginRepeatGroup(maxRounds, false, true);
        std::forward<F>(build)(*this);
        endRepeatGroup();
    }

    template <class F>
    void addFixedPointGroup(int maxRounds, F &&build) {
        beginRepeatGroup(maxRounds, true, false);
        std::forward<F>(build)(*this);
        endRepeatGroup();
    }

    void run(Module *module);

    void setDumpIR(bool v)   { dump_ir_   = v; }
    void setVerifyIR(bool v) { verify_ir_ = v; }
};
