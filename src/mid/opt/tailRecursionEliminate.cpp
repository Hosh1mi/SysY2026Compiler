#include "../../include/mid/opt/tailRecursionEliminate.hpp"

#include <vector>

void TailRecursionEliminate::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        if (isTailRecursive(func)) {
            eliminateTailRecursion(func, module);
        }
    }
}

bool TailRecursionEliminate::isTailRecursive(Function *func) {
    bool hasTailRecCall = false;
    for (auto bb : func->basic_blocks_) {
        for (auto instr : bb->instr_list_) {
            auto call = dynamic_cast<CallInst *>(instr);
            if (!call) continue;
            auto callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
            if (callee != func) continue;          // 不是自调用，跳过

            // 自调用必须满足：使用者只有 1 个，且是当前块的 ret 指令
            if (call->use_list_.size() != 1) return false;
            auto use = call->use_list_.front();
            auto term = bb->get_terminator();
            if (!term || !term->is_ret() || term != use.val_) return false;

            hasTailRecCall = true;
        }
    }
    return hasTailRecCall;   // 至少存在一个合法的尾递归调用
}

void TailRecursionEliminate::eliminateTailRecursion(Function *func, Module *module) {
    // 1. 创建 preheader/header，插入到函数块列表首部
    auto entry_bb = func->basic_blocks_.front();
    auto *preheader_bb = new BasicBlock(module, "label_tailrec_preheader", func);
    auto *header_bb = new BasicBlock(module, "label_tailrec_header", func);

    // 将这两个块移到最前面 (推荐使用 std::list 的 remove 成员函数)
    auto &bbs = func->basic_blocks_;
    bbs.erase(std::remove(bbs.begin(), bbs.end(), preheader_bb), bbs.end());
    bbs.erase(std::remove(bbs.begin(), bbs.end(), header_bb), bbs.end());
    bbs.insert(bbs.begin(), header_bb);
    bbs.insert(bbs.begin(), preheader_bb);

    auto *builder = new IRStmtBuilder(entry_bb, module);  // 任意块初始化

    // 2. 为每个参数创建 phi，先替换 use 再添加操作数
    std::vector<PhiInst *> arg_phis;
    for (auto arg : func->arguments_) {
        auto phi = PhiInst::create_phi(arg->type_, header_bb);
        phi->name_ = arg->name_;
        arg->replace_all_use_with(phi);               // 先替换所有使用
        phi->add_phi_pair_operand(arg, preheader_bb); // 再添加初始值
        // 移除 header_bb->add_instruction(phi); 防止双重插入造成链表死循环或非法 IR
        arg_phis.push_back(phi);
    }

    // 3. 构建循环骨架
    builder->set_insert_point(preheader_bb);
    builder->create_br(header_bb);
    builder->set_insert_point(header_bb);
    builder->create_br(entry_bb);

    // 4. 改写所有尾调用点（ret <call func(...)>）
    for (auto bb : func->basic_blocks_) {
        auto term = bb->get_terminator();
        if (!term || !term->is_ret() || term->num_ops_ == 0) continue;

        auto ret_val = term->get_operand(0);
        auto call = dynamic_cast<CallInst *>(ret_val);
        if (!call) continue;
        auto callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee != func) continue;

        for (size_t i = 0; i < func->arguments_.size(); ++i) {
            arg_phis[i]->add_phi_pair_operand(call->get_operand(i), bb);
        }

        bb->remove_instr(term);
        delete static_cast<ReturnInst*>(term);
        
        if (call->parent_) {
            call->parent_->remove_instr(call);
        }
        delete call;
        
        builder->set_insert_point(bb);
        builder->create_br(header_bb);
    }

    func->set_instr_name();
}
