/**
 * @file loopRotate.cpp
 * @brief 循环旋转：旋转循环 CFG，把循环条件调整到更适合后续优化的规范位置。
 * @details 克隆头部可安全执行的指令到入口检查块，使首次迭代与回边共享条件，同时修复退出 PHI。
 */

#include "../../../include/mid/opt/loopRotate.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopRotate::execute(Module *module) {
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
PreservedAnalyses LoopRotate::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 查询循环旋转克隆阶段的值映射。
 * @param value 待重映射的原值。
 * @param valueMap 原值到克隆值的映射表。
 * @return 命中时返回克隆值，否则返回原值。
 */
static Value *remapValue(Value *value, const std::map<Value *, Value *> &valueMap) {
    auto it = valueMap.find(value);
    return it == valueMap.end() ? value : it->second;
}

/**
 * @brief 查询 PHI 来自指定前驱的入值。
 * @param phi 待查询的 PHI 指令。
 * @param pred 指定前驱基本块。
 * @return 找到时返回对应值，否则返回 nullptr。
 */
static Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

/**
 * @brief 判断 isSupportedHeaderInst 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isSupportedHeaderInst(Instruction *inst) {
    if (inst->is_phi() || inst->isTerminator())
        return false;
    if (dynamic_cast<BinaryInst *>(inst) ||
        dynamic_cast<UnaryInst *>(inst) ||
        dynamic_cast<ICmpInst *>(inst))
        return true;
    return false;
}

/**
 * @brief 原地执行 removeTerminatorAndCfgEdges 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param bb 目标或待修改的基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void removeTerminatorAndCfgEdges(BasicBlock *bb) {
    auto *term = bb->get_terminator();
    std::vector<BasicBlock *> succs = bb->succ_bbs_;
    for (auto *succ : succs)
        succ->remove_pre_basic_block(bb);
    bb->succ_bbs_.clear();
    if (term)
        bb->delete_instr(term);
}

/**
 * @brief 原地执行 replaceWithConditionalBranch 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param bb 目标或待修改的基本块。
 * @param cond 参数 `cond`，用于本函数的分析、匹配或 IR 构造。
 * @param trueSucc 参数 `trueSucc`，用于本函数的分析、匹配或 IR 构造。
 * @param falseSucc 参数 `falseSucc`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void replaceWithConditionalBranch(BasicBlock *bb, Value *cond,
                                         BasicBlock *trueSucc,
                                         BasicBlock *falseSucc) {
    removeTerminatorAndCfgEdges(bb);
    new BranchInst(cond, trueSucc, falseSucc, bb);
}

/**
 * @brief 实现 placeBlockBefore 对应的局部分析或变换辅助逻辑。
 * @param func 待分析或改写的函数。
 * @param block 目标或待检查的基本块。
 * @param before 参数 `before`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void placeBlockBefore(Function *func, BasicBlock *block, BasicBlock *before) {
    auto &bbs = func->basic_blocks_;
    auto blockIt = std::find(bbs.begin(), bbs.end(), block);
    if (blockIt != bbs.end())
        bbs.erase(blockIt);

    auto beforeIt = std::find(bbs.begin(), bbs.end(), before);
    if (beforeIt != bbs.end())
        bbs.insert(beforeIt, block);
    else
        bbs.push_back(block);
}

/**
 * @brief 判断 isHeaderLocalUse 所描述的结构、合法性或安全条件是否成立。
 * @param def 参数 `def`，用于本函数的分析、匹配或 IR 构造。
 * @param user 参数 `user`，用于本函数的分析、匹配或 IR 构造。
 * @param header 循环头基本块。
 * @param exit 参数 `exit`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isHeaderLocalUse(Instruction *def, Instruction *user,
                             BasicBlock *header, BasicBlock *exit) {
    if (user->parent_ == header)
        return true;
    if (user->parent_ == exit && user->is_phi())
        return true;
    return false;
}

/**
 * @brief 判断 isInvariantForLoop 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isInvariantForLoop(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) || dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ && !loop.isInLoop(inst->parent_);
}

// Recognize an innermost monotone-domain guard before rotating a canonical
// while loop.  One branch must be a side-effect-free skip and the other a
// memory/call work block; both rejoin the unique latch directly.  This is the
// source form consumed by inductiveRangeCheckElimination after rotation.
/**
 * @brief 判断 hasTightenableDomainGuard 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool hasTightenableDomainGuard(const Loop &loop) {
    if (!loop.canonicalIV || !loop.children.empty())
        return false;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *header = loop.header;
    if (!latch || !header)
        return false;
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerTerm || headerTerm->num_ops() != 3)
        return false;
    auto *body = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *exit = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!body || !exit || !loop.isInLoop(body) || loop.isInLoop(exit))
        return false;

    auto *guardTerm = dynamic_cast<BranchInst *>(body->get_terminator());
    if (!guardTerm || guardTerm->num_ops() != 3)
        return false;
    auto *guardCmp = dynamic_cast<ICmpInst *>(guardTerm->get_operand(0));
    if (!guardCmp)
        return false;
    switch (guardCmp->icmp_op_) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
        break;
    default:
        return false;
    }
    Value *other = nullptr;
    if (guardCmp->get_operand(0) == loop.canonicalIV)
        other = guardCmp->get_operand(1);
    else if (guardCmp->get_operand(1) == loop.canonicalIV)
        other = guardCmp->get_operand(0);
    else
        return false;
    if (!isInvariantForLoop(other, loop))
        return false;

    auto classifyPath = [&](BasicBlock *path) -> int {
        if (path == latch)
            return 0;
        if (!path || !loop.isInLoop(path))
            return -1;
        auto *term = dynamic_cast<BranchInst *>(path->get_terminator());
        if (!term || term->num_ops() != 1 || term->get_operand(0) != latch)
            return -1;
        bool hasWork = false;
        for (auto *inst : path->instr_list_) {
            if (inst == term) break;
            if (inst->is_load() || inst->is_store() || inst->is_call())
                hasWork = true;
        }
        return hasWork ? 1 : 0;
    };

    int trueKind = classifyPath(
        dynamic_cast<BasicBlock *>(guardTerm->get_operand(1)));
    int falseKind = classifyPath(
        dynamic_cast<BasicBlock *>(guardTerm->get_operand(2)));
    return (trueKind == 0 && falseKind == 1) ||
           (trueKind == 1 && falseKind == 0);
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopRotate::runOnFunction(Function *func) {
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *call = dynamic_cast<CallInst *>(inst);
            if (!call || call->num_ops() == 0)
                continue;
            if (call->get_operand(call->num_ops() - 1) == func)
                return false;
        }
    }
    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;

        LoopInfo LI;
        LI.analyze(func);

        std::vector<Loop *> loops;
        for (auto &loop : LI.allLoops())
            loops.push_back(loop.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });

        for (auto *loop : loops) {
            if (rotateLoop(loop, func)) {
                changed = true;
                progress = true;
                func->set_instr_name();
                break;
            }
        }
    }
    return changed;
}

/**
 * @brief 克隆循环头中可安全复制的标量计算指令。
 * @param inst 待克隆的原指令。
 * @param dest 克隆指令的目标基本块。
 * @param valueMap 原值到当前旋转版本值的映射。
 * @return 成功时返回克隆指令，不支持该指令类型时返回 nullptr。
 */
Instruction *LoopRotate::cloneInstruction(
    Instruction *inst, BasicBlock *dest,
    const std::map<Value *, Value *> &valueMap) {
    auto remap = [&](Value *value) { return remapValue(value, valueMap); };

    if (auto *bin = dynamic_cast<BinaryInst *>(inst))
        return new BinaryInst(bin->type_, bin->op_id_,
                              remap(bin->get_operand(0)),
                              remap(bin->get_operand(1)), dest, true);
    if (auto *un = dynamic_cast<UnaryInst *>(inst))
        return new UnaryInst(un->type_, un->op_id_,
                             remap(un->get_operand(0)), dest, true);
    if (auto *icmp = dynamic_cast<ICmpInst *>(inst))
        return new ICmpInst(icmp->icmp_op_,
                            remap(icmp->get_operand(0)),
                            remap(icmp->get_operand(1)), dest, true);
    return nullptr;
}

/**
 * @brief 为指定循环退出边创建专用中间块。
 * @param func 所属函数。
 * @param pred 原退出边的前驱基本块。
 * @param exit 原退出目标块。
 * @return 新建并跳向 exit 的边分裂块。
 */
BasicBlock *LoopRotate::splitExitEdge(Function *func, BasicBlock *pred,
                                      BasicBlock *exit) {
    auto *split = new BasicBlock(func->parent_,
                                 exit->name_ + ".from." + pred->name_,
                                 func);
    placeBlockBefore(func, split, exit);
    new BranchInst(exit, split);
    return split;
}

/**
 * @brief 原地执行 rotateLoop 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopRotate::rotateLoop(Loop *loop, Function *func) {
    // 旋转分为预检、克隆头部计算、重连入口/回边和修复退出 PHI 四步。
    // 所有可失败条件在改 CFG 前完成，避免留下只旋转了一半的循环。
    if (!loop || !loop->header || !loop->preheader)
        return false;
    // 调用既是控制/内存屏障，其返回值还可能经出口 PHI 继续存活；当前克隆器只会
    // 重映射 header 中的标量表达式，因此含调用的循环不能安全旋转。
    for (auto *bb : loop->blocks)
        for (auto *inst : bb->instr_list_)
            if (inst->is_call())
                return false;
    // 简单规范 while 形态可直接被向量化和 IV 强度削弱使用，不应无谓旋转。
    // 多块循环则本就无法匹配这些简单消费者，旋转后反而能向结构化展开暴露
    // 带入口保护的 do-while 形态，所以仅在缺少可收紧域保护时保留规范循环。
    if (loop->hasCanonicalIV() && !hasTightenableDomainGuard(*loop) &&
        !std::getenv("EXP_ROTATE_IV"))
        return false;
    BasicBlock *header = loop->header;
    BasicBlock *preheader = loop->preheader;
    BasicBlock *latch = loop->singleLatch();
    if (!latch)
        return false;

    auto *preTerm = preheader->get_terminator();
    auto *latchTerm = latch->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops() != 1 ||
        preTerm->get_operand(0) != header)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops() != 1 ||
        latchTerm->get_operand(0) != header)
        return false;
    bool hasHeaderPhi = !header->instr_list_.empty() &&
                        header->instr_list_.front()->is_phi();
    if (!hasHeaderPhi)
        return false;

    auto *headerTerm = header->get_terminator();
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops() != 3)
        return false;

    auto *trueSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *falseSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!trueSucc || !falseSucc)
        return false;

    bool trueInLoop = loop->isInLoop(trueSucc);
    bool falseInLoop = loop->isInLoop(falseSucc);
    if (trueInLoop == falseInLoop)
        return false;

    BasicBlock *continueSucc = trueInLoop ? trueSucc : falseSucc;
    BasicBlock *exitSucc = trueInLoop ? falseSucc : trueSucc;
    if (continueSucc->pre_bbs_.size() != 1 || continueSucc->pre_bbs_[0] != header)
        return false;

    std::vector<PhiInst *> headerPhis;
    std::vector<Instruction *> headerInsts;
    for (auto *inst : header->instr_list_) {
        if (inst->is_phi()) {
            headerPhis.push_back(static_cast<PhiInst *>(inst));
            continue;
        }
        if (inst == headerTerm)
            break;
        if (!isSupportedHeaderInst(inst))
            return false;
        headerInsts.push_back(inst);
    }

    std::set<Instruction *> headerDefs;
    for (auto *phi : headerPhis)
        headerDefs.insert(phi);
    for (auto *inst : headerInsts)
        headerDefs.insert(inst);

    bool headerIsOnlyExiting =
        loop->exiting.size() == 1 && loop->exiting[0] == header;
    // 多 exiting 循环旋转后 exitSucc 的 LCSSA 形 phi（各入边同一循环内值）
    // 会变成三路入边拷贝——曾因后端合并不掉而在此 bail。regalloc 的精确
    // SSA 干涉 coalescing（2026-06-12）落地后这些拷贝可正常合并，门槛取消。
    std::set<PhiInst *> phisNeedingExitPhi;
    for (auto *phi : headerPhis) {
        for (auto &use : phi->use_list_) {
            auto *user = use.user_;
            if (!user) continue;
            if (user->parent_ == exitSucc && user->is_phi())
                continue;
            if (loop->isInLoop(user->parent_))
                continue;
            // 循环外的非 phi 使用：当 header 是唯一 exiting 块时，exitSucc
            // 支配所有循环外使用点，可用新建的 exit phi 接管
            if (!user->is_phi() && headerIsOnlyExiting) {
                phisNeedingExitPhi.insert(phi);
                continue;
            }
            return false;
        }
    }
    for (auto *def : headerInsts) {
        for (auto &use : def->use_list_) {
            auto *user = use.user_;
            if (!user) continue;
            if (!isHeaderLocalUse(def, user, header, exitSucc))
                return false;
            if (user->parent_ == exitSucc && user->is_phi()) {
                auto *exitPhi = static_cast<PhiInst *>(user);
                unsigned valueIndex = use.operand_index_;
                if (valueIndex + 1 >= exitPhi->num_ops() ||
                    exitPhi->get_operand(valueIndex + 1) != header)
                    return false;
            }
        }
    }

    std::map<Value *, Value *> preMap;
    std::map<Value *, Value *> latchMap;
    for (auto *phi : headerPhis) {
        Value *preVal = incomingFrom(phi, preheader);
        Value *latchVal = incomingFrom(phi, latch);
        if (!preVal || !latchVal)
            return false;
        preMap[phi] = preVal;
        latchMap[phi] = latchVal;
    }

    auto *origCondInst = dynamic_cast<Instruction *>(headerTerm->get_operand(0));
    if (!origCondInst || headerDefs.find(origCondInst) == headerDefs.end())
        return false;

    for (auto *inst : headerInsts) {
        auto *preClone = cloneInstruction(inst, preheader, preMap);
        auto *latchClone = cloneInstruction(inst, latch, latchMap);
        if (!preClone || !latchClone)
            return false;
        preheader->add_instruction_before_terminator(preClone);
        latch->add_instruction_before_terminator(latchClone);
        preMap[inst] = preClone;
        latchMap[inst] = latchClone;
    }

    Value *origCond = headerTerm->get_operand(0);
    Value *preCond = remapValue(origCond, preMap);
    Value *latchCond = remapValue(origCond, latchMap);
    if (preCond == origCond || latchCond == origCond)
        return false;

    BasicBlock *preExit = splitExitEdge(func, preheader, exitSucc);
    BasicBlock *latchExit = splitExitEdge(func, latch, exitSucc);
    // 原 preheader 旋转后承担零次迭代保护，不能再直接作为循环入口。为继续边新建
    // 单后继 preheader，使旋转后的循环仍保持 simplify form，方便后续 Pass 匹配。
    auto *rotatedPreheader = new BasicBlock(
        func->parent_, continueSucc->name_ + ".preheader", func);
    placeBlockBefore(func, rotatedPreheader, continueSucc);
    new BranchInst(continueSucc, rotatedPreheader);

    for (auto *inst : exitSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
            if (phi->get_operand(i + 1) != header)
                continue;
            Value *oldVal = phi->get_operand(i);
            Value *preVal = remapValue(oldVal, preMap);
            Value *latchVal = remapValue(oldVal, latchMap);
            phi->remove_operands(i, i + 1);
            phi->addIncoming(preVal, preExit);
            phi->addIncoming(latchVal, latchExit);
            break;
        }
    }

    for (auto *phi : headerPhis) {
        if (!phisNeedingExitPhi.count(phi))
            continue;
        auto *exitPhi = PhiInst::create_phi(phi->type_, exitSucc);
        exitPhi->addIncoming(remapValue(phi, preMap), preExit);
        exitPhi->addIncoming(remapValue(phi, latchMap), latchExit);
        exitSucc->add_instruction_front(exitPhi);
        std::vector<std::pair<Instruction *, unsigned>> redirects;
        for (auto &use : phi->use_list_) {
            auto *user = use.user_;
            if (!user || user->is_phi()) continue;
            if (loop->isInLoop(user->parent_)) continue;
            redirects.emplace_back(user, use.operand_index_);
        }
        for (auto &[user, idx] : redirects)
            user->set_operand(idx, exitPhi);
    }

    // exitSucc 若是某个祖先循环的 header，preExit/latchExit 就把原来
    // 经此处的一条回边变成了两条，破坏祖先循环的单 latch 规范形（L1）。
    // 把两条边汇入统一回边块再跳 exitSucc，保持回边数不变。
    for (Loop *anc = loop->parent; anc; anc = anc->parent) {
        if (anc->header != exitSucc)
            continue;
        auto *merge = new BasicBlock(func->parent_,
                                     exitSucc->name_ + ".backedge", func);
        placeBlockBefore(func, merge, exitSucc);
        new BranchInst(exitSucc, merge);
        for (auto *inst : exitSucc->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);
            int idxPre = -1, idxLatch = -1;
            for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i + 1) == preExit)   idxPre = (int)i;
                if (phi->get_operand(i + 1) == latchExit) idxLatch = (int)i;
            }
            if (idxPre < 0 || idxLatch < 0)
                continue;
            Value *vPre = phi->get_operand(idxPre);
            Value *vLatch = phi->get_operand(idxLatch);
            Value *merged = vPre;
            if (vPre != vLatch) {
                auto *mPhi = PhiInst::create_phi(phi->type_, merge);
                mPhi->addIncoming(vPre, preExit);
                mPhi->addIncoming(vLatch, latchExit);
                merge->add_instruction_front(mPhi);
                merged = mPhi;
            }
            phi->remove_operands(std::max(idxPre, idxLatch),
                                 std::max(idxPre, idxLatch) + 1);
            phi->remove_operands(std::min(idxPre, idxLatch),
                                 std::min(idxPre, idxLatch) + 1);
            phi->addIncoming(merged, merge);
        }
        for (auto *src : {preExit, latchExit}) {
            auto *term = src->get_terminator();
            term->set_operand(0, merge);
            src->remove_succ_basic_block(exitSucc);
            exitSucc->remove_pre_basic_block(src);
            src->add_succ_basic_block(merge);
            merge->add_pre_basic_block(src);
        }
        break;
    }

    for (auto *inst : continueSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
            if (phi->get_operand(i + 1) != header)
                continue;
            Value *oldVal = phi->get_operand(i);
            Value *preVal = remapValue(oldVal, preMap);
            Value *latchVal = remapValue(oldVal, latchMap);
            phi->remove_operands(i, i + 1);
            phi->addIncoming(preVal, rotatedPreheader);
            phi->addIncoming(latchVal, latch);
            break;
        }
    }

    for (auto it = headerPhis.rbegin(); it != headerPhis.rend(); ++it) {
        auto *phi = *it;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) == preheader)
                phi->set_operand(i + 1, rotatedPreheader);
        }
        header->remove_instr(phi);
        continueSucc->add_instruction_front(phi);
    }

    BasicBlock *preTrue = trueInLoop ? rotatedPreheader : preExit;
    BasicBlock *preFalse = falseInLoop ? rotatedPreheader : preExit;
    BasicBlock *latchTrue = trueInLoop ? continueSucc : latchExit;
    BasicBlock *latchFalse = falseInLoop ? continueSucc : latchExit;

    replaceWithConditionalBranch(preheader, preCond, preTrue, preFalse);
    replaceWithConditionalBranch(latch, latchCond, latchTrue, latchFalse);

    if (headerTerm->parent_ == header)
        header->delete_instr(headerTerm);
    for (auto it = headerInsts.rbegin(); it != headerInsts.rend(); ++it) {
        if ((*it)->parent_ == header)
            header->delete_instr(*it);
    }

    placeBlockBefore(func, continueSucc, header);
    func->remove_bb(header);

    return true;
}
