/**
 * @file simpleLoopUnswitch.cpp
 * @brief 简单循环反切换：把循环不变量条件移到循环外并克隆循环版本，消除循环体内重复分支。
 *        把循环内不变的分支条件移到循环外，克隆出"条件为真"和"条件为假"两个循环版本，循环体内的分支随即变成确定路径，被后续 CFGSimplify 删除。
 * @details 限制循环体大小、单函数版本数和代码增长；克隆前确认条件循环不变且两版本 CFG/PHI 均可完整修复。
 */

#include "../../../include/mid/opt/simpleLoopUnswitch.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr int MAX_UNSWITCH_INSTS = 64;
constexpr int MAX_UNSWITCH_PER_FUNC = 8;
constexpr int MAX_UNSWITCH_GROWTH_PER_FUNC = 192;

/**
 * @brief 查询循环版本克隆阶段的值映射。
 * @param v 待重映射的原值。
 * @param m 原值到克隆值的映射表。
 * @return 命中时返回克隆值，否则返回原值。
 */
Value *mapValue(Value *v, const std::unordered_map<Value *, Value *> &m) {
    auto it = m.find(v);
    return it == m.end() ? v : it->second;
}

// 删除 bb 的 terminator 并解除其 CFG 出边（参照 loopRotate 同名逻辑）
/**
 * @brief 原地执行 removeTerminatorAndCfgEdges 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param bb 目标或待修改的基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void removeTerminatorAndCfgEdges(BasicBlock *bb) {
    auto *term = bb->get_terminator();
    std::vector<BasicBlock *> succs = bb->succ_bbs_;
    for (auto *succ : succs)
        succ->remove_pre_basic_block(bb);
    bb->succ_bbs_.clear();
    if (term)
        bb->delete_instr(term);
}

// 从 succ 的所有 phi 中删除来自 pred 的入边对
/**
 * @brief 原地执行 removeIncomingFromPhis 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param succ 后继基本块。
 * @param pred 前驱基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void removeIncomingFromPhis(BasicBlock *succ, BasicBlock *pred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = (int)phi->num_ops() - 1; i >= 1; i -= 2) {
            if (phi->get_operand(i) == pred)
                phi->remove_operands(i - 1, i);
        }
    }
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void SimpleLoopUnswitch::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses SimpleLoopUnswitch::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool SimpleLoopUnswitch::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty())
        return false;

    bool changed = false;
    int budget = MAX_UNSWITCH_PER_FUNC;
    int &growthUsed = growthUsed_[func];
    bool progress = true;
    while (progress && budget > 0 &&
           growthUsed < MAX_UNSWITCH_GROWTH_PER_FUNC) {
        progress = false;

        LoopInfo LI;
        LI.analyze(func);

        // 由内向外：先小后大，避免一次外层克隆翻倍内层全部
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });

        for (auto *loop : loops) {
            int clonedInsts = 0;
            int remainingGrowth = MAX_UNSWITCH_GROWTH_PER_FUNC - growthUsed;
            if (tryUnswitch(*loop, func, remainingGrowth, &clonedInsts)) {
                changed = true;
                progress = true;
                budget--;
                growthUsed += clonedInsts;
                func->set_instr_name();
                break; // CFG 已变，重新分析
            }
        }
    }
    return changed;
}

// 逐类型克隆（参照 InlineExpand::cloneCalleeIntoCaller；phi 由调用方
// 延迟回填）。不支持的类型返回 nullptr，调用方据此放弃整个 unswitch。
/**
 * @brief 克隆 unswitch 循环版本中的一条受支持指令。
 * @param oldInst 待克隆的原指令。
 * @param newBB 克隆指令的目标基本块。
 * @param valMap 原值到当前循环版本值的映射。
 * @return 成功时返回克隆指令，不支持该类型时返回 nullptr。
 */
Instruction *SimpleLoopUnswitch::cloneInst(
    Instruction *oldInst, BasicBlock *newBB,
    std::unordered_map<Value *, Value *> &valMap) {
    auto mv = [&](Value *v) { return mapValue(v, valMap); };

    if (auto *bin = dynamic_cast<BinaryInst *>(oldInst))
        return new BinaryInst(bin->type_, bin->op_id_,
                              mv(bin->get_operand(0)), mv(bin->get_operand(1)),
                              newBB);
    if (auto *un = dynamic_cast<UnaryInst *>(oldInst))
        return new UnaryInst(un->type_, un->op_id_, mv(un->get_operand(0)),
                             newBB);
    if (auto *icmp = dynamic_cast<ICmpInst *>(oldInst))
        return new ICmpInst(icmp->icmp_op_, mv(icmp->get_operand(0)),
                            mv(icmp->get_operand(1)), newBB);
    if (auto *fcmp = dynamic_cast<FCmpInst *>(oldInst))
        return new FCmpInst(fcmp->fcmp_op_, mv(fcmp->get_operand(0)),
                            mv(fcmp->get_operand(1)), newBB);
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(oldInst)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gep->num_ops(); ++i)
            idxs.push_back(mv(gep->get_operand(i)));
        return new GetElementPtrInst(mv(gep->get_operand(0)), idxs, newBB);
    }
    if (auto *ld = dynamic_cast<LoadInst *>(oldInst))
        return new LoadInst(mv(ld->get_operand(0)), newBB);
    if (auto *st = dynamic_cast<StoreInst *>(oldInst))
        return new StoreInst(mv(st->get_operand(0)), mv(st->get_operand(1)),
                             newBB);
    if (auto *call = dynamic_cast<CallInst *>(oldInst)) {
        std::vector<Value *> args;
        for (unsigned i = 0; i < call->num_ops() - 1; ++i)
            args.push_back(mv(call->get_operand(i)));
        auto *callee =
            dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
        if (!callee) return nullptr;
        return new CallInst(callee, args, newBB);
    }
    if (auto *z = dynamic_cast<ZextInst *>(oldInst))
        return new ZextInst(z->op_id_, mv(z->get_operand(0)), z->type_,
                            newBB);
    if (auto *f = dynamic_cast<FpToSiInst *>(oldInst))
        return new FpToSiInst(f->op_id_, mv(f->get_operand(0)), f->type_,
                              newBB);
    if (auto *s = dynamic_cast<SiToFpInst *>(oldInst))
        return new SiToFpInst(s->op_id_, mv(s->get_operand(0)), s->type_,
                              newBB);
    if (auto *bc = dynamic_cast<Bitcast *>(oldInst))
        return new Bitcast(bc->op_id_, mv(bc->get_operand(0)), bc->type_,
                           newBB);
    if (auto *sel = dynamic_cast<SelectInst *>(oldInst))
        return new SelectInst(mv(sel->get_operand(0)), mv(sel->get_operand(1)),
                              mv(sel->get_operand(2)), newBB);
    return nullptr;
}

/**
 * @brief 尝试执行 Unswitch 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @param remainingGrowth 参数 `remainingGrowth`，用于本函数的分析、匹配或 IR 构造。
 * @param clonedInsts 参数 `clonedInsts`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool SimpleLoopUnswitch::tryUnswitch(Loop &loop, Function *func,
                                     int remainingGrowth,
                                     int *clonedInsts) {
    // 先找循环不变量条件并检查代码增长预算，再验证 LCSSA 外部使用和可克隆指令。
    // 最后在预头分流到两个固定条件版本，使循环体内分支能够被 CFGSimplify 删除。
    if (clonedInsts) *clonedInsts = 0;
    if (!loop.children.empty())
        return false;

    // ── 结构前置：真 preheader（单后继无条件 br 到 header）─────────────
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;
    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops() != 1 ||
        preTerm->get_operand(0) != loop.header)
        return false;

    BasicBlock *condBB = nullptr;
    Instruction *condBr = nullptr;
    Value *cond = nullptr;
    ICmpInst *hoistCmp = nullptr; // 形态 (b) 时指向循环内的原比较
    for (auto *bb : loop.blocksOrdered) {
        auto *term = bb->get_terminator();
        if (!term || !term->is_br() || term->num_ops() != 3) continue;
        auto *t = dynamic_cast<BasicBlock *>(term->get_operand(1));
        auto *f = dynamic_cast<BasicBlock *>(term->get_operand(2));
        if (!t || !f || t == f) continue;
        if (!loop.isInLoop(t) || !loop.isInLoop(f)) continue;
        Value *c = term->get_operand(0);
        if (dynamic_cast<Constant *>(c)) continue; // 常量条件交给 CFGSimplify
        ICmpInst *needHoist = nullptr;
        if (auto *ci = dynamic_cast<Instruction *>(c)) {
            if (loop.isInLoop(ci->parent_)) {
                auto *cmp = dynamic_cast<ICmpInst *>(ci);
                if (!cmp) continue;
                bool opsInvariant = true;
                for (unsigned i = 0; i < cmp->num_ops(); i++) {
                    auto *oi = dynamic_cast<Instruction *>(cmp->get_operand(i));
                    if (oi && loop.isInLoop(oi->parent_)) {
                        opsInvariant = false;
                        break;
                    }
                }
                if (!opsInvariant) continue;
                needHoist = cmp;
            }
        }
        condBB = bb; condBr = term; cond = c; hoistCmp = needHoist;
        break;
    }
    if (!condBB) return false;

    // ── 体积限制 ───────────────────────────────────────────────────────
    int instCount = 0;
    for (auto *bb : loop.blocks)
        instCount += (int)bb->instr_list_.size();
    if (instCount > MAX_UNSWITCH_INSTS) return false;
    if (instCount > remainingGrowth) return false;

    // ── 合法性：循环内定义的循环外使用必须全部经 exit phi（LCSSA 形）──
    //    且克隆要求所有指令类型可克隆
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_ret()) return false;
            for (auto &use : inst->use_list_) {
                auto *user = use.user_;
                if (!user || !user->parent_) continue;
                if (loop.isInLoop(user->parent_)) continue;
                bool isExitPhi =
                    user->is_phi() &&
                    std::find(loop.exits.begin(), loop.exits.end(),
                              user->parent_) != loop.exits.end();
                if (!isExitPhi) return false;
            }
        }
    }

    // ── 克隆 ───────────────────────────────────────────────────────────
    std::unordered_map<Value *, Value *> valMap; // 旧值/旧块 → 新值/新块
    std::vector<BasicBlock *> newBlocks;
    for (auto *bb : loop.blocksOrdered) {
        auto *nb = new BasicBlock(func->parent_, bb->name_ + ".unsw", func);
        valMap[bb] = nb;
        newBlocks.push_back(nb);
    }

    /**
     * @brief 记录克隆循环中尚待复制操作数的 PHI 对应关系。
     */
    struct PhiFixup {
        PhiInst *newPhi;  ///< 新循环副本中创建的 PHI。
        PhiInst *oldPhi;  ///< 原循环中提供操作数和前驱关系的 PHI。
    };
    std::vector<PhiFixup> fixups;

    for (auto *bb : loop.blocksOrdered) {
        auto *nb = static_cast<BasicBlock *>(valMap[bb]);
        for (auto *inst : bb->instr_list_) {
            if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
                auto *np = PhiInst::create_phi(phi->type_, nb);
                nb->add_instruction(np);
                valMap[phi] = np;
                fixups.push_back({np, phi});
            } else if (auto *br = dynamic_cast<BranchInst *>(inst)) {
                if (br->num_ops() == 3) {
                    auto *t = static_cast<BasicBlock *>(
                        mapValue(br->get_operand(1), valMap));
                    auto *f = static_cast<BasicBlock *>(
                        mapValue(br->get_operand(2), valMap));
                    new BranchInst(mapValue(br->get_operand(0), valMap), t, f, nb);
                } else {
                    auto *t = static_cast<BasicBlock *>(
                        mapValue(br->get_operand(0), valMap));
                    new BranchInst(t, nb);
                }
            } else {
                auto *clone = cloneInst(inst, nb, valMap);
                if (!clone) {
                    // 放弃：摘掉已建的克隆块（指令先清，再删块）
                    for (auto *cb : newBlocks) {
                        std::vector<Instruction *> insts(cb->instr_list_.begin(),
                                                         cb->instr_list_.end());
                        for (auto *ci : insts) cb->delete_instr(ci);
                    }
                    for (auto *cb : newBlocks) func->remove_bb(cb);
                    return false;
                }
                if (!inst->is_void())
                    valMap[inst] = clone;
            }
        }
    }
    for (auto &fix : fixups) {
        for (unsigned i = 0; i < fix.oldPhi->num_ops(); i += 2) {
            Value *v = mapValue(fix.oldPhi->get_operand(i), valMap);
            auto *bb = static_cast<BasicBlock *>(
                mapValue(fix.oldPhi->get_operand(i + 1), valMap));
            fix.newPhi->addIncoming(v, bb);
        }
    }

    if (std::getenv("DEBUG_LOOP_UNSWITCH"))
        std::cerr << "[LoopUnswitch] func=" << func->name_
                  << " header=" << loop.header->name_
                  << " cond_bb=" << condBB->name_
                  << " insts=" << instCount << "\n";

    // ── exit phi 补克隆侧入边 ──────────────────────────────────────────
    for (auto *exit : loop.exits) {
        for (auto *inst : exit->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);
            std::vector<std::pair<Value *, BasicBlock *>> adds;
            for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                if (!pred || !loop.isInLoop(pred)) continue;
                adds.emplace_back(mapValue(phi->get_operand(i), valMap),
                                  static_cast<BasicBlock *>(valMap[pred]));
            }
            for (auto &[v, bb] : adds)
                phi->addIncoming(v, bb);
        }
    }

    // ── 两份拷贝中折叠被 unswitch 的分支 ───────────────────────────────
    auto *trueSucc = static_cast<BasicBlock *>(condBr->get_operand(1));
    auto *falseSucc = static_cast<BasicBlock *>(condBr->get_operand(2));
    // 原循环 = 条件为真版本
    removeIncomingFromPhis(falseSucc, condBB);
    removeTerminatorAndCfgEdges(condBB);
    new BranchInst(trueSucc, condBB);
    // 克隆 = 条件为假版本
    auto *cCondBB = static_cast<BasicBlock *>(valMap[condBB]);
    auto *cTrueSucc = static_cast<BasicBlock *>(valMap[trueSucc]);
    auto *cFalseSucc = static_cast<BasicBlock *>(valMap[falseSucc]);
    removeIncomingFromPhis(cTrueSucc, cCondBB);
    removeTerminatorAndCfgEdges(cCondBB);
    new BranchInst(cFalseSucc, cCondBB);

    // ── preheader 改为按条件分派 ───────────────────────────────────────
    // 经由两个落地块分派而非直接 cond br：保住两份循环各自的真 preheader
    //（单后继无条件 br），否则下游 LoopRotate/规范形校验全部失配。
    auto *cloneHeader = static_cast<BasicBlock *>(valMap[loop.header]);
    auto *phTrue = new BasicBlock(func->parent_,
                                  preheader->name_ + ".unsw.t", func);
    auto *phFalse = new BasicBlock(func->parent_,
                                   preheader->name_ + ".unsw.f", func);
    new BranchInst(loop.header, phTrue);
    new BranchInst(cloneHeader, phFalse);
    removeTerminatorAndCfgEdges(preheader);
    if (hoistCmp) {
        // 形态 (b)：操作数全不变的循环内比较，克隆到 preheader 作分派条件
        //（此刻 preheader 无 terminator，直接尾部追加）
        cond = new ICmpInst(hoistCmp->icmp_op_, hoistCmp->get_operand(0),
                            hoistCmp->get_operand(1), preheader);
    }
    new BranchInst(cond, phTrue, phFalse, preheader);

    // 两侧 header phi 的 preheader 入边改挂到各自落地块
    auto retargetIncoming = [](BasicBlock *header, BasicBlock *oldPred,
                               BasicBlock *newPred) {
        for (auto *inst : header->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);
            for (unsigned i = 1; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i) == oldPred)
                    phi->set_operand(i, newPred);
            }
        }
    };
    retargetIncoming(loop.header, preheader, phTrue);
    retargetIncoming(cloneHeader, preheader, phFalse);

    removeUnreachableBlocks(func);

    if (clonedInsts) *clonedInsts = instCount;

    return true;
}
