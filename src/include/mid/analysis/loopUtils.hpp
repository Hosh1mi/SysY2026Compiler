#pragma once

#include "../ir/instruction.hpp"

inline BasicBlock *getSemanticUseBlock(Instruction *user, unsigned operandIndex) {
    if (!user)
        return nullptr;

    BasicBlock *useBlock = user->parent_;
    if (user->is_phi() && operandIndex + 1 < user->num_ops()) {
        if (auto *incoming =
                dynamic_cast<BasicBlock *>(user->get_operand(operandIndex + 1)))
            useBlock = incoming;
    }
    return useBlock;
}

