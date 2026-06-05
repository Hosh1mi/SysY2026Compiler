#include "../../include/mid/opt/passManager.hpp"
#include <iostream>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::run(Module *module) {
    for (auto &pass : passes) {
        if (dump_ir_) {
            std::cerr << "; === IR Before " << pass->name() << " ===\n"
                      << module->print() << "\n";
        }

        pass->execute(module);

        if (verify_ir_) {
            module->verify();
        }

        if (dump_ir_) {
            std::cerr << "; === IR After " << pass->name() << " ===\n"
                      << module->print() << "\n";
        }
    }
}