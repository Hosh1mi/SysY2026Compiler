#include "pass.hpp"
#include "../analysis/analysisManager.hpp"
#include <vector>
#include <memory>

class PassManager {
    // 重复调度组（plan 4.2）：[begin, end) 区间的 pass 作为整体重复执行,
    // 直到一轮内所有 convergenceRelevant 的 pass 都报告未改动，或打满
    // maxRounds，或指令总数相对组入口膨胀超阈值（防失控护栏）。
    struct RepeatGroup {
        size_t begin = 0;
        size_t end   = 0;
        int    maxRounds = 1;
    };

    std::vector<std::unique_ptr<Pass>> passes;
    std::vector<RepeatGroup> groups_;
    AnalysisManager analyses_;
    bool dump_ir_    = false;
    bool verify_ir_  = false;
    bool loop_form_ready_ = false;

    PreservedAnalyses runSinglePass(Pass &pass, Module *module);
    void verifyRepeatGroupExit(Module *module, bool completedNormally);

public:
    void addPass(std::unique_ptr<Pass> pass);
    void beginRepeatGroup(int maxRounds);
    void endRepeatGroup();
    void run(Module *module);

    void setDumpIR(bool v)   { dump_ir_   = v; }
    void setVerifyIR(bool v) { verify_ir_ = v; }
};
