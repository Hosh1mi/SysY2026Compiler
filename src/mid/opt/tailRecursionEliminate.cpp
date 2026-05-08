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

// 检查函数是否只包含尾递归调用（允许 ret <call> 或 call + br + phi + ret 两种形式）
bool TailRecursionEliminate::isTailRecursive(Function *func) {
    bool hasTailCall = false;
    for (auto bb : func->basic_blocks_) {
        for (auto instr : bb->instr_list_) {
            auto call = dynamic_cast<CallInst *>(instr);
            if (!call) continue;
            auto callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
            if (callee != func) continue;          // 不是自调用，跳过

            auto term = bb->get_terminator();
            // 模式 1：ret <call>
            if (term && term->is_ret() && term->num_ops_ > 0 &&
                term->get_operand(0) == call) {
                hasTailCall = true;
                continue;
            }

            // 模式 2：call 后无条件 br 到返回块，返回块中有 phi 使用 call，并最终由 ret 返回
            if (term && term->is_br() && term->num_ops_ == 1) {
                BasicBlock *target = static_cast<BasicBlock *>(term->get_operand(0));
                // call 的所有使用者必须都在 target 中且都是 phi
                bool all_in_target = true;
                for (auto &use : call->use_list_) {
                    auto phi = dynamic_cast<PhiInst *>(use.val_);
                    if (!phi || phi->parent_ != target) {
                        all_in_target = false;
                        break;
                    }
                }
                if (!all_in_target) return false;   // 存在非 phi 或不在返回块的使用，不安全

                auto targetTerm = target->get_terminator();
                if (!targetTerm || !targetTerm->is_ret()) return false;
                // 确保至少一个 phi 被 ret 直接使用（返回值类型非 void 时必须）
                if (targetTerm->num_ops_ > 0) {
                    Value *retVal = targetTerm->get_operand(0);
                    bool usedByRet = false;
                    for (auto &use : call->use_list_) {
                        if (use.val_ == retVal) { usedByRet = true; break; }
                    }
                    if (!usedByRet) return false;
                }
                hasTailCall = true;
                continue;
            }

            // 既不是 ret<call> 也不是 br+phi+ret，说明该自调用不是尾调用
            return false;
        }
    }
    return hasTailCall;
}

void TailRecursionEliminate::eliminateTailRecursion(Function *func, Module *module) {
    // 1. 创建循环前置块和头块
    auto entry_bb = func->basic_blocks_.front();
    auto *preheader = new BasicBlock(module, "label_tailrec_preheader", func);
    auto *header = new BasicBlock(module, "label_tailrec_header", func);

    auto &bbs = func->basic_blocks_;
    bbs.erase(std::remove(bbs.begin(), bbs.end(), preheader), bbs.end());
    bbs.erase(std::remove(bbs.begin(), bbs.end(), header), bbs.end());
    bbs.insert(bbs.begin(), header);
    bbs.insert(bbs.begin(), preheader);

    auto *builder = new IRStmtBuilder(entry_bb, module);

    // 2. 为每个参数创建 phi，并替换所有使用
    std::vector<PhiInst *> arg_phis;
    for (auto arg : func->arguments_) {
        auto phi = PhiInst::create_phi(arg->type_, header);
        phi->name_ = arg->name_;                 // 保留原名
        arg->replace_all_use_with(phi);
        phi->add_phi_pair_operand(arg, preheader);
    
        // 将 phi 插入 header 指令列表头部
        header->add_instruction_front(phi);      
        arg_phis.push_back(phi);
    }

    builder->set_insert_point(preheader);
    builder->create_br(header);
    builder->set_insert_point(header);
    builder->create_br(entry_bb);

    // 3. 收集所有尾调用块的信息（bb, call, target_ret_bb），不对 phi 做修改
    struct TailCallSite {
        BasicBlock *bb;
        CallInst *call;
        BasicBlock *ret_bb;   // 模式1为nullptr
        bool is_ret_form;
    };
    std::vector<TailCallSite> sites;

    for (auto bb : func->basic_blocks_) {
        auto term = bb->get_terminator();
        if (!term) continue;

        CallInst *call = nullptr;
        BasicBlock *target = nullptr;
        bool ret_form = false;

        if (term->is_ret() && term->num_ops_ > 0) {
            call = dynamic_cast<CallInst *>(term->get_operand(0));
            if (call) ret_form = true;
        } else if (term->is_br() && term->num_ops_ == 1) {
            target = static_cast<BasicBlock *>(term->get_operand(0));
            for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
                if (*rit == term) continue;
                auto *c = dynamic_cast<CallInst *>(*rit);
                if (c) {
                    auto *callee = dynamic_cast<Function *>(c->get_operand(c->num_ops_ - 1));
                    if (callee == func) {
                        call = c;
                        ret_form = false;
                        break;
                    }
                }
            }
        }

        if (!call) continue;
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee != func) continue;

        sites.push_back({bb, call, target, ret_form});
    }

    // 4. 对每个尾调用块：添加实参到 header 的 phi，并转换为跳转到 header 的循环
    for (auto &site : sites) {
        auto bb = site.bb;
        auto call = site.call;

        // 添加实参到 header 的 phi
        for (size_t i = 0; i < func->arguments_.size(); ++i) {
            arg_phis[i]->add_phi_pair_operand(call->get_operand(i), bb);
        }

        // 转换控制流
        if (site.is_ret_form) {
            auto term = bb->get_terminator();
            bb->delete_instr(term);
            bb->delete_instr(call);
            builder->set_insert_point(bb);
            builder->create_br(header);
        } else {
            auto term = bb->get_terminator();
            bb->delete_instr(call);
            BasicBlock *ret_bb = site.ret_bb;
            bb->delete_instr(term);
            bb->remove_succ_basic_block(ret_bb);
            ret_bb->remove_pre_basic_block(bb);
            builder->set_insert_point(bb);
            builder->create_br(header);
        }
    }

    // 5. 统一处理返回块中的 phi（仅对 br+phi 模式）
    std::set<BasicBlock *> processed_retbb;
    for (auto &site : sites) {
        if (site.is_ret_form || !site.ret_bb) continue;
        BasicBlock *ret_bb = site.ret_bb;
        if (processed_retbb.count(ret_bb)) continue;
        processed_retbb.insert(ret_bb);

        // 收集该返回块中所有 phi
        std::vector<PhiInst *> phis;
        for (auto &inst : ret_bb->instr_list_) {
            if (auto *phi = dynamic_cast<PhiInst *>(inst))
                phis.push_back(phi);
        }

        for (auto *phi : phis) {
            // 找出所有来自已转换尾调用块的前驱边
            std::vector<unsigned> indices_to_remove;  // 存放 value 索引（偶数）
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                BasicBlock *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                // 如果这个前驱已经改为跳转到 header（即前驱是 sites 中的某个 bb）
                bool is_tailcall_pred = false;
                for (auto &s : sites) {
                    if (!s.is_ret_form && s.bb == pred) {
                        is_tailcall_pred = true;
                        break;
                    }
                }
                if (is_tailcall_pred) {
                    indices_to_remove.push_back(i);
                }
            }

            // 按从大到小顺序移除，避免索引偏移
            for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend(); ++it) {
                unsigned i = *it;
                phi->remove_operands(i, i + 1);
            }

            // 如果 phi 只剩一个输入，用该值替换并删除 phi
            if (phi->num_ops_ == 2) {
                Value *replacement = phi->get_operand(0);
                phi->replace_all_use_with(replacement);
                ret_bb->delete_instr(phi);
            }
        }
    }

    func->set_instr_name();
    delete builder;
}