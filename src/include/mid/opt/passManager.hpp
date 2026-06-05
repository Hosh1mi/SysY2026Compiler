#include "pass.hpp"
#include <vector>
#include <memory>

class PassManager {
    std::vector<std::unique_ptr<Pass>> passes;
    bool dump_ir_    = false;
    bool verify_ir_  = false;
public:
    void addPass(std::unique_ptr<Pass> pass);
    void run(Module *module);

    void setDumpIR(bool v)   { dump_ir_   = v; }
    void setVerifyIR(bool v) { verify_ir_ = v; }
};
