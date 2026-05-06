#include "../../include/mid/opt/tailRecursionEliminate.hpp"

#include <algorithm>
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
    for (auto bb : func->basic_blocks_) {
        auto term = bb->get_terminator();
        if (!term || !term->is_ret() || term->num_ops_ == 0) continue;

        auto ret_val = term->get_operand(0);
        auto call = dynamic_cast<CallInst *>(ret_val);
        if (!call) continue;

        auto callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee != func) return false;

        if (call->use_list_.size() != 1 || call->use_list_.front().val_ != term) return false;
    }
    return true;
}

void TailRecursionEliminate::eliminateTailRecursion(Function *func, Module *module) {
    if (func->basic_blocks_.empty() || func->arguments_.empty()) return;

    auto entry_bb = func->basic_blocks_.front();
    auto *builder = new IRStmtBuilder(entry_bb, module);

    auto *preheader_bb = new BasicBlock(module, "label_tailrec_preheader", func);
    auto *header_bb = new BasicBlock(module, "label_tailrec_header", func);

    func->basic_blocks_.erase(std::remove(func->basic_blocks_.begin(), func->basic_blocks_.end(), preheader_bb), func->basic_blocks_.end());
    func->basic_blocks_.erase(std::remove(func->basic_blocks_.begin(), func->basic_blocks_.end(), header_bb), func->basic_blocks_.end());
    func->basic_blocks_.insert(func->basic_blocks_.begin(), header_bb);
    func->basic_blocks_.insert(func->basic_blocks_.begin(), preheader_bb);

    std::vector<PhiInst *> arg_phis;
    arg_phis.reserve(func->arguments_.size());
    for (auto arg : func->arguments_) {
        auto phi = PhiInst::create_phi(arg->type_, header_bb);
        phi->name_ = arg->name_;
        phi->add_phi_pair_operand(arg, preheader_bb);
        arg_phis.push_back(phi);
    }

    if (auto term = entry_bb->get_terminator()) {
        if (term->is_br()) {
            entry_bb->remove_instr(term);
            delete static_cast<BranchInst *>(term);
        }
    }
    builder->set_insert_point(preheader_bb);
    builder->create_br(header_bb);

    builder->set_insert_point(header_bb);
    builder->create_br(entry_bb);

    for (size_t i = 0; i < func->arguments_.size(); ++i) {
        func->arguments_[i]->replace_all_use_with(arg_phis[i]);
    }

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

        bb->remove_instr(call);
        bb->remove_instr(term);
        delete static_cast<CallInst *>(call);
        delete static_cast<ReturnInst *>(term);
        builder->set_insert_point(bb);
        builder->create_br(header_bb);
    }

    func->set_instr_name();
}