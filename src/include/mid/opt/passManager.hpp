#include "pass.hpp"
#include <vector>
#include <memory>

class PassManager {
    std::vector<std::unique_ptr<Pass>> passes;
public:
    void addPass(std::unique_ptr<Pass> pass);
    void run(Module *module);
};
