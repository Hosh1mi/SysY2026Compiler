// 典型示例：
//   优化前：then 返回 %a，else 返回 %b，函数含两个 return 块。
//   优化后：两块跳到公共出口，出口以 PHI 汇合 %a/%b 后统一 return。
// 单出口形态可简化尾调用识别和后续 CFG 分析。

#include "../../include/mid/opt/unifyExitNodes.hpp"

// 统一函数的多个 return 到单一出口块。void 函数直接跳转到公共出口；有返回值
// 的函数在出口创建 PHI 汇总各路径结果，为尾调用、CFG 简化等后续优化提供
// 规范的单出口形态。

// 对模块内每个有函数体的函数执行出口统一。
void UnifyExitNodes::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

// 创建公共出口并把原 return 改成分支；非 void 返回值通过 PHI 汇合。
bool UnifyExitNodes::runOnFunction(Function *func) {
    // 收集所有以 ReturnInst 结束的基本块；零个或一个出口无需改写。
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
