#include "../../include/mid/opt/unifyExitNodes.hpp"

void UnifyExitNodes::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

bool UnifyExitNodes::runOnFunction(Function *func) {
    // Collect all blocks ending with ReturnInst
    std::vector<BasicBlock *> returningBlocks;
    for (auto bb : func->basic_blocks_) {
        auto term = bb->get_terminator();
        if (term && term->is_ret())
            returningBlocks.push_back(bb);
    }

    if (returningBlocks.size() <= 1) return false;

    // Create unified return block
    auto newRetBB = new BasicBlock(func->parent_, "label_unified_ret", func);
    auto retType = func->get_return_type();
    IRStmtBuilder builder(newRetBB);

    PhiInst *phi = nullptr;
    if (retType->tid_ == Type::VoidTyID) {
        builder.create_void_ret();
    } else {
        phi = PhiInst::create_phi(retType, newRetBB);
        newRetBB->add_instruction(phi);
        builder.create_ret(phi);
    }

    // Redirect all returning blocks to the unified block
    for (auto bb : returningBlocks) {
        auto term = bb->get_terminator();
        if (phi) {
            Value *retVal = term->num_ops() > 0 ? term->get_operand(0) : nullptr;
            if (retVal)
                phi->add_phi_pair_operand(retVal, bb);
        }
        bb->remove_instr(term);
        builder.set_insert_point(bb);
        builder.create_br(newRetBB);
    }

    return true;
}
