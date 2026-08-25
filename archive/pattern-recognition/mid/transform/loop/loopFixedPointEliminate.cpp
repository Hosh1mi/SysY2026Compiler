/**
 * @file loopFixedPointEliminate.cpp
 * @brief 循环不动点消除：识别达到不动点或无效自递推的循环状态并消除冗余迭代。
 * @details 区分控制 PHI、平移状态和自递推状态，同时用别名分析排除跨迭代相关内存后才缩短循环。
 */

#include "../../../include/mid/opt/loopFixedPointEliminate.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

/**
 * @brief 描述循环头 PHI 的初始入值和回边入值，用于固定点证明。
 */
struct HeaderPhi {
    PhiInst *phi = nullptr;         ///< 被检查的循环头 PHI。
    Value *initial = nullptr;       ///< 从预头进入循环的初始值。
    Value *backedge = nullptr;      ///< 从回边进入下一迭代的递推值。
};

/**
 * @brief 实现 describeHeaderPhi 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param preheader 循环预头基本块。
 * @param latch 循环回边基本块。
 * @param result 用于写回匹配或计算结果的输出参数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool describeHeaderPhi(PhiInst *phi, BasicBlock *preheader,
                       BasicBlock *latch, HeaderPhi &result) {
    if (!phi || phi->num_ops() != 4)
        return false;
    result.phi = phi;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        auto *source =
            dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (source == preheader)
            result.initial = phi->get_operand(i);
        else if (source == latch)
            result.backedge = phi->get_operand(i);
        else
            return false;
    }
    return result.initial && result.backedge;
}

/**
 * @brief 判断 isInsideUse 所描述的结构、合法性或安全条件是否成立。
 * @param use 参数 `use`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isInsideUse(const Use &use, const Loop &loop) {
    auto *user = use.user_;
    return user && user->parent_ && loop.isInLoop(user->parent_);
}

/**
 * @brief 判断 isSupportedStateType 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isSupportedStateType(Value *value) {
    if (!value || !value->type_)
        return false;
    if (value->type_->tid_ != Type::IntegerTyID)
        return false;
    unsigned bits =
        static_cast<IntegerType *>(value->type_)->num_bits_;
    return bits == 1 || bits == 32;
}

/**
 * @brief 判断 isTranslationState 所描述的结构、合法性或安全条件是否成立。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isTranslationState(const HeaderPhi &info, const Loop &loop) {
    auto *update = dynamic_cast<BinaryInst *>(info.backedge);
    if (!update)
        return false;

    auto isInvariant = [&](Value *value) {
        auto *inst = dynamic_cast<Instruction *>(value);
        return !inst || !loop.isInLoop(inst);
    };

    if (update->is_add()) {
        if (update->get_operand(0) == info.phi)
            return isInvariant(update->get_operand(1));
        if (update->get_operand(1) == info.phi)
            return isInvariant(update->get_operand(0));
    }
    return update->is_sub() && update->get_operand(0) == info.phi &&
           isInvariant(update->get_operand(1));
}

/**
 * @brief 实现 normalizeGuard 对应的局部分析或变换辅助逻辑。
 * @param compare 参数 `compare`，用于本函数的分析、匹配或 IR 构造。
 * @param candidate 参数 `candidate`，用于本函数的分析、匹配或 IR 构造。
 * @param predicate 参数 `predicate`，用于本函数的分析、匹配或 IR 构造。
 * @param bound 参数 `bound`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool normalizeGuard(ICmpInst *compare, Value *candidate,
                    ICmpInst::ICmpOp &predicate, ConstantInt *&bound) {
    if (!compare || !candidate)
        return false;
    predicate = compare->icmp_op_;
    if (compare->get_operand(0) == candidate) {
        bound = dynamic_cast<ConstantInt *>(compare->get_operand(1));
        return bound != nullptr;
    }
    if (compare->get_operand(1) != candidate)
        return false;
    bound = dynamic_cast<ConstantInt *>(compare->get_operand(0));
    if (!bound)
        return false;
    switch (predicate) {
    case ICmpInst::ICMP_SGT:
        predicate = ICmpInst::ICMP_SLT;
        break;
    case ICmpInst::ICMP_SGE:
        predicate = ICmpInst::ICMP_SLE;
        break;
    case ICmpInst::ICMP_SLT:
        predicate = ICmpInst::ICMP_SGT;
        break;
    case ICmpInst::ICMP_SLE:
        predicate = ICmpInst::ICMP_SGE;
        break;
    default:
        return false;
    }
    return true;
}

// Recognize a finite unit-stride count recurrence whose only role is the
// trip-count guard in guardBlock (latch for do-while, header for while):
//   n.next = n +/- 1
//   if (n </> bound) continue
/**
 * @brief 判断 isFiniteControlPhi 所描述的结构、合法性或安全条件是否成立。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param guard 参数 `guard`，用于本函数的分析、匹配或 IR 构造。
 * @param guardBlock 参数 `guardBlock`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isFiniteControlPhi(const HeaderPhi &info, const Loop &loop,
                        ICmpInst *guard, BasicBlock *guardBlock) {
    auto *initial = dynamic_cast<ConstantInt *>(info.initial);
    auto *update = dynamic_cast<BinaryInst *>(info.backedge);
    if (!initial || !update || update->parent_ != loop.singleLatch())
        return false;
    if (!guard || !guardBlock || guard->parent_ != guardBlock)
        return false;

    int step = 0;
    if (update->op_id_ == Instruction::Add) {
        auto *rhs = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) != info.phi || !rhs)
            return false;
        step = rhs->value_;
    } else if (update->op_id_ == Instruction::Sub) {
        auto *rhs = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) != info.phi || !rhs)
            return false;
        step = -rhs->value_;
    } else {
        return false;
    }
    if (step != 1 && step != -1)
        return false;

    ICmpInst::ICmpOp predicate;
    ConstantInt *bound = nullptr;
    if (!normalizeGuard(guard, info.phi, predicate, bound))
        return false;

    if (step == -1) {
        if (predicate != ICmpInst::ICMP_SGT ||
            initial->value_ <= bound->value_ ||
            bound->value_ == std::numeric_limits<int>::min())
            return false;
    } else {
        if (predicate != ICmpInst::ICMP_SLT ||
            initial->value_ >= bound->value_ ||
            bound->value_ == std::numeric_limits<int>::max())
            return false;
    }

    // 计数器只能控制循环次数；若参与重复计算，直接跳到不动点会改变中间状态语义。
    for (const Use &use : info.phi->use_list_) {
        if (use.user_ != update && use.user_ != guard)
            return false;
    }
    for (const Use &use : update->use_list_) {
        if (use.user_ != info.phi)
            return false;
    }
    for (const Use &use : guard->use_list_) {
        auto *user = dynamic_cast<BranchInst *>(use.user_);
        if (!user || user != guardBlock->get_terminator() ||
            use.operand_index_ != 0)
            return false;
    }
    return true;
}

// Header phi whose only user is its own backedge update forms a dead SSA
// cycle. It cannot affect memory or live-outs, so it must not block
// fixed-point detection (e.g. a leftover countdown of the original trip
// count after IndVarSimplify introduced a canonical IV).
/**
 * @brief 判断 isDeadSelfRecurrence 所描述的结构、合法性或安全条件是否成立。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isDeadSelfRecurrence(const HeaderPhi &info) {
    auto *update = dynamic_cast<Instruction *>(info.backedge);
    if (!update || !info.phi)
        return false;
    for (const Use &use : info.phi->use_list_) {
        if (use.user_ != update)
            return false;
    }
    for (const Use &use : update->use_list_) {
        if (use.user_ != info.phi)
            return false;
    }
    return true;
}

/**
 * @brief 实现 memoryIsIterationIndependent 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param AA 参数 `AA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool memoryIsIterationIndependent(const Loop &loop, BasicAliasAnalysis &AA) {
    std::vector<LoadInst *> loads;
    std::vector<StoreInst *> stores;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_call())
                return false;
            if (auto *load = dynamic_cast<LoadInst *>(inst))
                loads.push_back(load);
            else if (auto *store = dynamic_cast<StoreInst *>(inst))
                stores.push_back(store);
        }
    }

    for (auto *store : stores) {
        Value *storePtr = store->get_operand(1);
        for (auto *load : loads) {
            if (AA.alias(storePtr, load->get_operand(0)) !=
                AliasResult::NoAlias)
                return false;
        }
    }
    return true;
}

/**
 * @brief 原地执行 replaceTerminatorWithCond 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param bb 目标或待修改的基本块。
 * @param cond 参数 `cond`，用于本函数的分析、匹配或 IR 构造。
 * @param trueSucc 参数 `trueSucc`，用于本函数的分析、匹配或 IR 构造。
 * @param falseSucc 参数 `falseSucc`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceTerminatorWithCond(BasicBlock *bb, Value *cond,
                               BasicBlock *trueSucc,
                               BasicBlock *falseSucc) {
    auto *term = bb->get_terminator();
    std::vector<BasicBlock *> succs = bb->succ_bbs_;
    for (auto *succ : succs)
        succ->remove_pre_basic_block(bb);
    bb->succ_bbs_.clear();
    if (term)
        bb->delete_instr(term);
    new BranchInst(cond, trueSucc, falseSucc, bb);
}

// When early-exiting from the latch of a while loop, each exit phi that
// currently receives a header value must also receive the matching
// end-of-iteration (backedge) value along the new latch edge.
/**
 * @brief 实现 addLatchIncomingToExitPhis 对应的局部分析或变换辅助逻辑。
 * @param exit 参数 `exit`，用于本函数的分析、匹配或 IR 构造。
 * @param header 循环头基本块。
 * @param latch 循环回边基本块。
 * @param phis 参数 `phis`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool addLatchIncomingToExitPhis(BasicBlock *exit, BasicBlock *header,
                                BasicBlock *latch,
                                const std::vector<HeaderPhi> &phis) {
    std::vector<std::pair<PhiInst *, Value *>> updates;
    for (auto *inst : exit->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi)
            break;
        Value *fromHeader = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) == header) {
                fromHeader = phi->get_operand(i);
                break;
            }
        }
        if (!fromHeader)
            return false;

        Value *fromLatch = fromHeader;
        for (const HeaderPhi &info : phis) {
            if (info.phi == fromHeader) {
                fromLatch = info.backedge;
                break;
            }
        }
        updates.emplace_back(phi, fromLatch);
    }
    for (auto &[phi, fromLatch] : updates)
        phi->addIncoming(fromLatch, latch);
    return true;
}

/**
 * @brief 构造本轮循环状态是否发生变化的汇总布尔条件。
 * @param i1 布尔结果类型。
 * @param latch 比较与 OR 链的插入基本块。
 * @param state 所有需要比较初值和回边值的状态 PHI 描述。
 * @return 任一状态发生变化时为真的 IR 条件；没有状态时返回 nullptr。
 */
Value *buildStateChanged(Type *i1, BasicBlock *latch,
                         Instruction *before,
                         const std::vector<HeaderPhi> &state) {
    Value *stateChanged = new ConstantInt(i1, 0);
    for (const HeaderPhi &info : state) {
        auto *changed =
            new ICmpInst(ICmpInst::ICMP_NE, info.backedge, info.phi,
                         latch, true);
        latch->add_instruction_before_inst(changed, before);
        if (dynamic_cast<ConstantInt *>(stateChanged)) {
            stateChanged = changed;
        } else {
            auto *eitherChanged =
                new BinaryInst(i1, Instruction::Or, stateChanged, changed,
                               latch, true);
            latch->add_instruction_before_inst(eitherChanged, before);
            stateChanged = eitherChanged;
        }
    }
    return stateChanged;
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopFixedPointEliminate::execute(Module *module) {
    BasicAliasAnalysis AA;
    AA.analyze(module);
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, AA);
    }
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses
LoopFixedPointEliminate::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    BasicAliasAnalysis AA;
    AA.analyze(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AA);
    }
    return changed ? PreservedAnalyses::none()
                   : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AA 参数 `AA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFixedPointEliminate::runOnFunction(Function *func,
                                            BasicAliasAnalysis &AA) {
    bool changed = false;
    for (;;) {
        // 顶测循环的不动点改写会新增 latch→exit 边。每次成功后立即重建
        // 循环森林，不能继续使用 CFG 视图已经过期的子循环 Loop 指针。
        LoopInfo LI;
        LI.analyze(func);
        std::vector<Loop *> loops;
        for (const auto &loop : LI.allLoops())
            loops.push_back(loop.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *lhs, Loop *rhs) {
                      return lhs->depth < rhs->depth;
                  });

        bool transformed = false;
        bool transformedNonLeaf = false;
        for (auto *loop : loops) {
            if (!tryTransform(*loop, func, AA))
                continue;
            changed = true;
            transformed = true;
            transformedNonLeaf = !loop->children.empty();
            break;
        }
        // 改写顶测外层循环后，新扩展的出口 PHI 可能让子循环表面上也匹配不动点。
        // 这些子循环未基于原循环嵌套独立完成证明，因此本轮函数处理中不能级联改写。
        if (!transformed || transformedNonLeaf)
            break;
    }
    if (changed)
        func->set_instr_name();
    return changed;
}

/**
 * @brief 尝试执行 Transform 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @param AA 参数 `AA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFixedPointEliminate::tryTransform(Loop &loop, Function *func,
                                           BasicAliasAnalysis &AA) {
    // 先统一顶测/底测循环的退出条件，再分类控制 PHI、平移状态和不动点状态。
    // 只有内存跨迭代独立且退出 PHI 可补齐时，才在 latch 插入提前退出。
    BasicBlock *preheader = loop.preheader;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !latch || !exit || loop.exiting.size() != 1)
        return false;

    BasicBlock *exiting = loop.exiting.front();
    const bool latchExiting = exiting == latch;
    const bool headerExiting = exiting == loop.header;
    if (!latchExiting && !headerExiting)
        return false;
    ICmpInst *guard = nullptr;
    BasicBlock *guardBlock = nullptr;
    BranchInst *latchBranch = nullptr;

    if (latchExiting) {
        latchBranch =
            dynamic_cast<BranchInst *>(latch->get_terminator());
        if (!latchBranch || latchBranch->num_ops() != 3 ||
            latchBranch->get_operand(1) != loop.header ||
            latchBranch->get_operand(2) != exit)
            return false;
        guard = dynamic_cast<ICmpInst *>(latchBranch->get_operand(0));
        if (!guard || guard->parent_ != latch)
            return false;
        guardBlock = latch;
    } else {
        // 顶测 while 只有 header 退出，latch 是无条件回边；新的提前退出检查放在 latch。
        auto *headerBranch =
            dynamic_cast<BranchInst *>(loop.header->get_terminator());
        if (!headerBranch || headerBranch->num_ops() != 3)
            return false;
        auto *trueSucc =
            dynamic_cast<BasicBlock *>(headerBranch->get_operand(1));
        auto *falseSucc =
            dynamic_cast<BasicBlock *>(headerBranch->get_operand(2));
        if (!trueSucc || !falseSucc)
            return false;
        bool trueInLoop = loop.isInLoop(trueSucc);
        bool falseInLoop = loop.isInLoop(falseSucc);
        if (trueInLoop == falseInLoop)
            return false;
        BasicBlock *exitSucc = trueInLoop ? falseSucc : trueSucc;
        if (exitSucc != exit)
            return false;

        auto *latchTerm =
            dynamic_cast<BranchInst *>(latch->get_terminator());
        if (!latchTerm || latchTerm->num_ops() != 1 ||
            latchTerm->get_operand(0) != loop.header)
            return false;

        guard = dynamic_cast<ICmpInst *>(headerBranch->get_operand(0));
        if (!guard || guard->parent_ != loop.header)
            return false;
        guardBlock = loop.header;
        latchBranch = latchTerm;
    }

    std::vector<HeaderPhi> phis;
    for (auto *inst : loop.header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi)
            break;
        HeaderPhi info;
        if (!describeHeaderPhi(phi, preheader, latch, info))
            return false;
        phis.push_back(info);
    }
    if (phis.empty())
        return false;

    int controlIndex = -1;
    for (size_t i = 0; i < phis.size(); ++i) {
        if (!isFiniteControlPhi(phis[i], loop, guard, guardBlock))
            continue;
        if (controlIndex >= 0)
            return false;
        controlIndex = static_cast<int>(i);
    }
    if (controlIndex < 0)
        return false;

    std::vector<HeaderPhi> state;
    for (size_t i = 0; i < phis.size(); ++i) {
        if (static_cast<int>(i) == controlIndex)
            continue;
        if (isDeadSelfRecurrence(phis[i]))
            continue;
        if (!isSupportedStateType(phis[i].phi))
            return false;
        state.push_back(phis[i]);
    }

    // 平移递推 x'=x±c 只有 c=0 时才不变；为它增加逐轮相等判断还会遮蔽
    // 后续归约分析看到的仿射结构，因此只保留真正可能收敛的状态映射。
    for (const HeaderPhi &info : state)
        if (isTranslationState(info, loop))
            return false;

    if (!memoryIsIterationIndependent(loop, AA))
        return false;

    // 外部 use 必须已经经过出口 PHI。while 形态会新增 latch→exit 边，因此下面还要
    // 为这些 PHI 补充来自 latch 的最终状态入值。
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            for (const Use &use : inst->use_list_) {
                if (!isInsideUse(use, loop)) {
                    auto *user = use.user_;
                    if (!user || user->parent_ != exit ||
                        !user->is_phi())
                        return false;
                }
            }
        }
    }

    Type *i1 = guard->type_;
    if (latchExiting) {
        Value *stateChanged =
            buildStateChanged(i1, latch, latchBranch, state);
        auto *continueIfChanged =
            new BinaryInst(i1, Instruction::And,
                           latchBranch->get_operand(0), stateChanged,
                           latch, true);
        latch->add_instruction_before_inst(continueIfChanged,
                                           latchBranch);
        latchBranch->set_operand(0, continueIfChanged);
    } else {
        if (!addLatchIncomingToExitPhis(exit, loop.header, latch, phis))
            return false;
        Value *stateChanged =
            buildStateChanged(i1, latch, latchBranch, state);
        replaceTerminatorWithCond(latch, stateChanged, loop.header,
                                  exit);
    }

    if (std::getenv("DEBUG_LOOP_FIXED_POINT"))
        std::cerr << "[LoopFixedPointEliminate] function=" << func->name_
                  << " header=" << loop.header->name_
                  << " state=" << state.size()
                  << " blocks=" << loop.blocks.size()
                  << (headerExiting ? " shape=while\n" : " shape=do\n");
    return true;
}
