#include "../../include/mid/opt/passManager.hpp"

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::run(Module *module) {
    for (auto &pass : passes) {
        pass->execute(module);
    }
}