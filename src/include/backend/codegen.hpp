#pragma once

#include "../mid/ir/ir.hpp"

#include <ostream>

namespace backend::aarch64 {

struct BackendOptions {
    int optimizationLevel = 0;
    bool verifyMachineIR = true;
    bool dumpSelectionDAG = false;
    // When set, dump MIR before/after every machine pass (like --dump-ir).
    bool dumpMachineIR = false;
};

class AArch64Backend {
public:
    AArch64Backend(Module &module, std::ostream &output,
                   BackendOptions options = {})
        : module_(module), output_(output), options_(options) {}

    void generate();

private:
    void emitGlobal(GlobalVariable *global);
    void emitParallelRuntime();

    Module &module_;
    std::ostream &output_;
    BackendOptions options_;
};

} // namespace backend::aarch64
