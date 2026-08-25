/**
 * @file loopInvariantReduction.cpp
 * @brief 循环不变量归约：识别含循环不变量部分的归约表达式，并拆分或重写递推以减少循环内计算。
 * @details 先匹配归约链并验证私有存储与 CFG，再把不变量部分移到外层，避免改变浮点或有序归约语义。
 */

#include "../../../include/mid/opt/loopInvariantReduction.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

using ValueMap = std::unordered_map<Value *, Value *>;

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    static bool enabled = std::getenv("DEBUG_LOOP_INVARIANT_REDUCTION") != nullptr;
    return enabled;
}

/**
 * @brief 生成 debugReject 对应的调试诊断，不参与程序语义。
 * @param func 待分析或改写的函数。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void debugReject(Function *func, const char *reason) {
    if (!debugEnabled()) return;
    std::cerr << "[LoopInvariantReduction] reject "
              << (func ? func->name_ : "<null>") << ": " << reason << "\n";
}

/**
 * @brief 生成 debugApply 对应的调试诊断，不参与程序语义。
 * @param func 待分析或改写的函数。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void debugApply(Function *func) {
    if (!debugEnabled()) return;
    std::cerr << "[LoopInvariantReduction] apply "
              << (func ? func->name_ : "<null>")
              << ": hoisted private stores and extracted rowSum\n";
}

/**
 * @brief 查询克隆值映射，未命中的循环外值保持不变。
 * @param value 待重映射的原值。
 * @param map 原值到克隆值的映射表。
 * @return 映射存在时返回克隆值，否则返回原值。
 */
Value *remap(Value *value, const ValueMap &map) {
    auto it = map.find(value);
    return it == map.end() ? value : it->second;
}

/**
 * @brief 判断 isCloneable 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isCloneable(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<StoreInst *>(inst) ||
           dynamic_cast<SelectInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst);
}

/**
 * @brief 克隆候选归约区域中的一条受支持指令并重映射操作数。
 * @param orig 待克隆的原指令。
 * @param dest 克隆指令的目标基本块。
 * @param map 已建立的值映射。
 * @return 成功时返回尚未插入链表的克隆指令，否则返回 nullptr。
 */
Instruction *cloneInstruction(Instruction *orig, BasicBlock *dest,
                              const ValueMap &map) {
    auto R = [&](Value *value) { return remap(value, map); };
    Instruction *clone = nullptr;

    if (auto *binary = dynamic_cast<BinaryInst *>(orig)) {
        clone = new BinaryInst(binary->type_, binary->op_id_,
                               R(binary->get_operand(0)),
                               R(binary->get_operand(1)), dest, true);
    } else if (auto *unary = dynamic_cast<UnaryInst *>(orig)) {
        clone = new UnaryInst(unary->type_, unary->op_id_,
                              R(unary->get_operand(0)), dest, true);
    } else if (auto *cmp = dynamic_cast<ICmpInst *>(orig)) {
        clone = new ICmpInst(cmp->icmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest, true);
    } else if (auto *cmp = dynamic_cast<FCmpInst *>(orig)) {
        clone = new FCmpInst(cmp->fcmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest, true);
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops(); ++i)
            indices.push_back(R(gep->get_operand(i)));
        clone = new GetElementPtrInst(R(gep->get_operand(0)), indices,
                                      dest, true);
    } else if (auto *load = dynamic_cast<LoadInst *>(orig)) {
        clone = new LoadInst(R(load->get_operand(0)), dest, true);
    } else if (auto *store = dynamic_cast<StoreInst *>(orig)) {
        clone = new StoreInst(R(store->get_operand(0)),
                              R(store->get_operand(1)), dest, true);
    } else if (auto *select = dynamic_cast<SelectInst *>(orig)) {
        clone = new SelectInst(R(select->get_operand(0)),
                               R(select->get_operand(1)),
                               R(select->get_operand(2)), orig->type_);
        clone->parent_ = dest;
    } else if (auto *zext = dynamic_cast<ZextInst *>(orig)) {
        clone = new ZextInst(zext->op_id_, R(zext->get_operand(0)),
                             zext->type_, dest, true);
    } else if (auto *cast = dynamic_cast<Bitcast *>(orig)) {
        clone = new Bitcast(cast->op_id_, R(cast->get_operand(0)),
                            cast->type_, dest, true);
    }

    if (clone) clone->copySemFlagsFrom(orig);
    return clone;
}

/**
 * @brief 原地执行 removeTerminatorEdges 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param bb 目标或待修改的基本块。
 * @param term 参数 `term`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void removeTerminatorEdges(BasicBlock *bb, BranchInst *term) {
    if (!bb || !term) return;
    for (unsigned i = 0; i < term->num_ops(); ++i) {
        auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
        if (!succ) continue;
        bb->remove_succ_basic_block(succ);
        succ->remove_pre_basic_block(bb);
    }
    bb->delete_instr(term);
}

/**
 * @brief 原地执行 retargetPhiPred 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param succ 后继基本块。
 * @param oldPred 需要替换的原前驱基本块。
 * @param newPred 替换后的新前驱基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void retargetPhiPred(BasicBlock *succ, BasicBlock *oldPred,
                     BasicBlock *newPred) {
    if (!succ) return;
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 1; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i) == oldPred)
                phi->set_operand(i, newPred);
        }
    }
}

/**
 * @brief 实现 validateCFG 对应的局部分析或变换辅助逻辑。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool validateCFG(Function *func) {
    if (!func || func->basic_blocks_.empty()) return false;
    std::set<BasicBlock *> blocks(func->basic_blocks_.begin(),
                                  func->basic_blocks_.end());
    for (auto *bb : func->basic_blocks_) {
        auto *term = bb->get_terminator();
        if (!term) {
            if (debugEnabled())
                std::cerr << "[LoopInvariantReduction] invalid CFG "
                          << func->name_ << ": block without terminator "
                          << bb->name_ << "\n";
            return false;
        }
        for (auto *succ : bb->succ_bbs_) {
            if (!blocks.count(succ) ||
                std::find(succ->pre_bbs_.begin(), succ->pre_bbs_.end(), bb) ==
                    succ->pre_bbs_.end()) {
                if (debugEnabled())
                    std::cerr << "[LoopInvariantReduction] invalid CFG "
                              << func->name_ << ": missing reciprocal edge "
                              << bb->name_ << " -> " << (succ ? succ->name_ : "<null>")
                              << "\n";
                return false;
            }
        }
        for (auto *pred : bb->pre_bbs_) {
            if (!blocks.count(pred) ||
                std::find(pred->succ_bbs_.begin(), pred->succ_bbs_.end(), bb) ==
                    pred->succ_bbs_.end()) {
                if (debugEnabled())
                    std::cerr << "[LoopInvariantReduction] invalid CFG "
                              << func->name_ << ": missing reciprocal edge "
                              << (pred ? pred->name_ : "<null>") << " -> "
                              << bb->name_ << "\n";
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief 沿 GEP 链追溯指针对应的栈上 alloca 根对象。
 * @param value 待追溯的指针值。
 * @return 找到时返回根 alloca；指针不是栈对象派生值时返回 nullptr。
 */
AllocaInst *pointerBase(Value *value) {
    std::set<Value *> seen;
    while (value && seen.insert(value).second) {
        if (auto *alloca = dynamic_cast<AllocaInst *>(value)) return alloca;
        auto *gep = dynamic_cast<GetElementPtrInst *>(value);
        if (!gep) return nullptr;
        value = gep->get_operand(0);
    }
    return nullptr;
}

/**
 * @brief 判断 isPrivateAlloca 所描述的结构、合法性或安全条件是否成立。
 * @param alloca 参数 `alloca`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isPrivateAlloca(AllocaInst *alloca) {
    if (!alloca) return false;
    std::queue<Value *> worklist;
    std::set<Value *> seen;
    worklist.push(alloca);
    seen.insert(alloca);

    while (!worklist.empty()) {
        Value *value = worklist.front();
        worklist.pop();
        for (const auto &use : value->use_list_) {
            auto *user = use.user_;
            if (!user) return false;

            if (auto *gep = dynamic_cast<GetElementPtrInst *>(user)) {
                if (use.operand_index_ != 0) return false;
                if (seen.insert(gep).second) worklist.push(gep);
                continue;
            }
            if (dynamic_cast<LoadInst *>(user)) {
                if (use.operand_index_ != 0) return false;
                continue;
            }
            if (dynamic_cast<StoreInst *>(user)) {
                if (use.operand_index_ != 1) return false;
                continue;
            }
            return false;
        }
    }
    return true;
}

/**
 * @brief 汇总可将内层不变量归约并入外层的嵌套循环匹配结果。
 */
struct Candidate {
    Function *func = nullptr;                   ///< 候选循环所属函数。
    Loop *outer = nullptr;                      ///< 承载最终归约结果的外层循环。
    Loop *inner = nullptr;                      ///< 待消除或简化的内层归约循环。
    AllocaInst *array = nullptr;                ///< 内层循环重复读取的栈数组。
    BasicBlock *outerBody = nullptr;            ///< 从外层头进入内层前的循环体块。
    BasicBlock *innerPreheader = nullptr;        ///< 内层循环预头。
    BasicBlock *innerExit = nullptr;             ///< 内层循环唯一出口。
    PhiInst *outerIV = nullptr;                  ///< 外层循环归纳变量。
    PhiInst *outerSum = nullptr;                 ///< 外层累加状态 PHI。
    PhiInst *innerIV = nullptr;                  ///< 内层循环归纳变量。
    PhiInst *innerSum = nullptr;                 ///< 内层累加状态 PHI。
    BinaryInst *innerIVUpdate = nullptr;          ///< 内层归纳变量的回边更新。
    BinaryInst *innerSumUpdate = nullptr;         ///< 内层累加状态的回边更新。
    LoadInst *innerLoad = nullptr;                ///< 参与内层归约的数组读取。
    BinaryInst *oldOuterIVUpdate = nullptr;       ///< 改写前的外层归纳变量更新。
    ICmpInst *innerCompare = nullptr;             ///< 控制内层循环退出的比较。
    Value *innerBound = nullptr;                  ///< 内层循环的迭代上界。
    BinaryInst *oldModulo = nullptr;              ///< 原外层累加结果上的取模指令。
    Value *modulus = nullptr;                     ///< 归约结果使用的模数。
    std::vector<BasicBlock *> chainBlocks;         ///< 外层体至内层预头间的直线 CFG 链。
};

/**
 * @brief 查找基本块中具有指定前驱入边的第一个 PHI。
 * @param bb 待搜索的基本块。
 * @param pred 指定的前驱基本块。
 * @return 找到时返回 PHI，否则返回 nullptr。
 */
PhiInst *findPhiFrom(BasicBlock *bb, BasicBlock *pred) {
    if (!bb) return nullptr;
    for (auto *inst : bb->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 1; i < phi->num_ops(); i += 2)
            if (phi->get_operand(i) == pred) return phi;
    }
    return nullptr;
}

/**
 * @brief 收集或查找 findIncoming 所需的信息。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param pred 前驱基本块。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool findIncoming(PhiInst *phi, BasicBlock *pred, Value *&value) {
    if (!phi) return false;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred) {
            value = phi->get_operand(i);
            return true;
        }
    }
    return false;
}

/**
 * @brief 实现 containsOuterValue 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param region 参数 `region`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool containsOuterValue(Value *value, Loop *loop,
                        const std::set<Instruction *> &region) {
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && loop->blocks.count(inst->parent_) && !region.count(inst);
}

/**
 * @brief 分析 Chain 的结构、递推或依赖信息。
 * @param c 参数 `c`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool analyzeChain(Candidate &c) {
    auto *term = dynamic_cast<BranchInst *>(c.outerBody->get_terminator());
    if (!term || term->num_ops() != 3) return false;

    BasicBlock *first = nullptr;
    BasicBlock *continuation = nullptr;
    for (unsigned i = 1; i < 3; ++i) {
        auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
        if (!succ) return false;
        if (succ == c.innerPreheader) continuation = succ;
        else first = succ;
    }
    if (!first || !continuation) return false;
    std::queue<BasicBlock *> worklist;
    std::set<BasicBlock *> seen;
    worklist.push(c.outerBody);
    seen.insert(c.outerBody);

    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        c.chainBlocks.push_back(bb);

        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            if (!isCloneable(inst)) return false;
            if (inst->is_call() || inst->is_load() || inst->is_alloca())
                return false;
            if (auto *store = dynamic_cast<StoreInst *>(inst)) {
                if (pointerBase(store->get_operand(1)) != c.array)
                    return false;
            }
        }

        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br) return false;
        unsigned firstSucc = br->num_ops() == 1 ? 0 : 1;
        unsigned lastSucc = br->num_ops() == 1 ? 0 : 2;
        for (unsigned i : {firstSucc, lastSucc}) {
            auto *succ = dynamic_cast<BasicBlock *>(br->get_operand(i));
            if (!succ) return false;
            if (succ == continuation) continue;
            if (!c.outer->blocks.count(succ) || succ == c.inner->header)
                return false;
            if (seen.insert(succ).second) worklist.push(succ);
        }
    }

    std::set<Instruction *> regionInsts;
    for (auto *bb : c.chainBlocks)
        for (auto *inst : bb->instr_list_)
            if (!inst->isTerminator()) regionInsts.insert(inst);

    for (auto *bb : c.chainBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->isTerminator()) continue;
            unsigned limit = inst->is_call() ? inst->num_ops() - 1 : inst->num_ops();
            for (unsigned i = 0; i < limit; ++i) {
                if (containsOuterValue(inst->get_operand(i), c.outer,
                                       regionInsts))
                    return false;
            }
            for (const auto &use : inst->use_list_) {
                auto *user = use.user_;
                if (!user || regionInsts.count(user)) continue;
                if (user->isTerminator()) continue;
                if (user->parent_ == c.outer->header) continue;
                if (user->parent_ && c.outer->blocks.count(user->parent_))
                    return false;
            }
        }
    }

    std::set<Instruction *> available;
    for (auto *bb : c.chainBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->isTerminator()) continue;
            for (unsigned i = 0; i < inst->num_ops(); ++i) {
                auto *operand = dynamic_cast<Instruction *>(inst->get_operand(i));
                if (operand && regionInsts.count(operand) &&
                    !available.count(operand))
                    return false;
            }
            available.insert(inst);
        }
    }
    return true;
}

/**
 * @brief 分析 Inner 的结构、递推或依赖信息。
 * @param c 参数 `c`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool analyzeInner(Candidate &c) {
    if (!c.inner || !c.inner->header || !c.inner->singleLatch() ||
        !c.inner->singleExit())
        return false;
    if (!c.inner->hasInductionIV()) return false;
    c.innerIV = c.inner->getInductionIV();
    c.innerExit = c.inner->singleExit();

    auto *headerBr = dynamic_cast<BranchInst *>(c.inner->header->get_terminator());
    if (!headerBr || headerBr->num_ops() != 3) return false;
    for (unsigned i = 1; i < 3; ++i) {
        auto *value = dynamic_cast<BasicBlock *>(headerBr->get_operand(i));
        if (!value) return false;
        if (!c.inner->blocks.count(value)) {
            auto *cmp = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
            if (!cmp) return false;
            c.innerCompare = cmp;
            c.innerBound = cmp->get_operand(0) == c.innerIV
                                ? cmp->get_operand(1)
                                : cmp->get_operand(0);
        }
    }
    if (!c.innerCompare || !c.innerBound) return false;
    if (c.innerCompare->get_operand(0) != c.innerIV) return false;

    for (auto *inst : c.inner->header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi != c.innerIV) {
            if (c.innerSum) return false;
            c.innerSum = phi;
        }
    }
    if (!c.innerSum) return false;

    auto *latch = c.inner->singleLatch();
    int loadCount = 0;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (!isCloneable(inst) || inst->is_store() || inst->is_call())
            return false;
        if (auto *load = dynamic_cast<LoadInst *>(inst)) {
            ++loadCount;
            c.innerLoad = load;
            if (pointerBase(load->get_operand(0)) != c.array)
                return false;
        }
        if (auto *binary = dynamic_cast<BinaryInst *>(inst)) {
            bool usesIV = binary->get_operand(0) == c.innerIV ||
                          binary->get_operand(1) == c.innerIV;
            bool usesSum = binary->get_operand(0) == c.innerSum ||
                           binary->get_operand(1) == c.innerSum;
            if (usesIV) {
                if (c.innerIVUpdate) return false;
                c.innerIVUpdate = binary;
            } else if (usesSum) {
                if (c.innerSumUpdate) return false;
                c.innerSumUpdate = binary;
            }
        }
    }
    if (loadCount != 1 || !c.innerLoad || !c.innerIVUpdate ||
        !c.innerSumUpdate)
        return false;
    if (c.innerSumUpdate->get_operand(0) != c.innerLoad &&
        c.innerSumUpdate->get_operand(1) != c.innerLoad)
        return false;

    Value *innerInit = nullptr;
    if (!findIncoming(c.innerSum, c.inner->preheader, innerInit) ||
        innerInit != c.outerSum)
        return false;
    Value *ivInit = nullptr;
    if (!findIncoming(c.innerIV, c.inner->preheader, ivInit)) return false;
    auto *zero = dynamic_cast<ConstantInt *>(ivInit);
    if (!zero || zero->value_ != 0) return false;

    auto *one = dynamic_cast<ConstantInt *>(
        c.innerIVUpdate->get_operand(0) == c.innerIV
            ? c.innerIVUpdate->get_operand(1)
            : c.innerIVUpdate->get_operand(0));
    if (!one || one->value_ != 1) return false;

    if (c.innerSumUpdate->op_id_ != Instruction::Add) return false;
    BinaryInst *oldModulo = nullptr;
    for (auto *inst : c.innerExit->instr_list_) {
        if (inst->is_phi()) continue;
        if (inst->isTerminator()) continue;
        auto *binary = dynamic_cast<BinaryInst *>(inst);
        if (!binary) return false;
        if (binary->op_id_ == Instruction::SRem) {
            if (oldModulo) return false;
            oldModulo = binary;
        } else if (binary->op_id_ == Instruction::Add) {
            bool usesIV = binary->get_operand(0) == c.outerIV ||
                          binary->get_operand(1) == c.outerIV;
            if (!usesIV || c.oldOuterIVUpdate) return false;
            c.oldOuterIVUpdate = binary;
        } else {
            return false;
        }
    }
    if (!oldModulo || !c.oldOuterIVUpdate) return false;
    auto *outerOne = dynamic_cast<ConstantInt *>(
        c.oldOuterIVUpdate->get_operand(0) == c.outerIV
            ? c.oldOuterIVUpdate->get_operand(1)
            : c.oldOuterIVUpdate->get_operand(0));
    if (!outerOne || outerOne->value_ != 1) return false;
    Value *modInput = oldModulo->get_operand(0);
    PhiInst *exitPhi = nullptr;
    if (modInput != c.innerSum) {
        exitPhi = dynamic_cast<PhiInst *>(modInput);
        if (!exitPhi || exitPhi->parent_ != c.innerExit) return false;
        Value *exitIncoming = nullptr;
        if (!findIncoming(exitPhi, c.inner->header, exitIncoming) ||
            exitIncoming != c.innerSum)
            return false;
    }
    c.oldModulo = oldModulo;
    c.modulus = oldModulo->get_operand(1);

    auto unavailableInRow = [&](Value *value) {
        auto *inst = dynamic_cast<Instruction *>(value);
        return inst && c.outer->blocks.count(inst->parent_);
    };
    if (unavailableInRow(c.innerBound) || unavailableInRow(c.modulus))
        return false;

    ValueMap availableInLatch;
    availableInLatch[c.innerIV] = c.innerIV;
    availableInLatch[c.innerSum] = c.innerSum;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            auto *operand = dynamic_cast<Instruction *>(inst->get_operand(i));
            if (operand && c.inner->blocks.count(operand->parent_) &&
                !availableInLatch.count(operand))
                return false;
        }
        availableInLatch[inst] = inst;
    }

    Value *outerSumIncoming = nullptr;
    Value *outerIVIncoming = nullptr;
    if (!findIncoming(c.outerSum, c.innerExit, outerSumIncoming) ||
        outerSumIncoming != c.oldModulo ||
        !findIncoming(c.outerIV, c.innerExit, outerIVIncoming) ||
        outerIVIncoming != c.oldOuterIVUpdate)
        return false;

    for (const auto &use : c.innerSum->use_list_) {
        auto *user = use.user_;
        if (!user) return false;
        if (c.inner->blocks.count(user->parent_)) continue;
        if (user->parent_ == c.innerExit &&
            (user == exitPhi || user == c.oldModulo))
            continue;
        return false;
    }
    return true;
}

/**
 * @brief 实现 validateArrayUses 对应的局部分析或变换辅助逻辑。
 * @param c 参数 `c`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool validateArrayUses(const Candidate &c) {
    std::set<BasicBlock *> afterOuter;
    std::queue<BasicBlock *> afterWorklist;
    afterWorklist.push(c.outer->singleExit());
    afterOuter.insert(c.outer->singleExit());
    while (!afterWorklist.empty()) {
        auto *bb = afterWorklist.front();
        afterWorklist.pop();
        for (auto *succ : bb->succ_bbs_)
            if (afterOuter.insert(succ).second)
                afterWorklist.push(succ);
    }

    std::queue<Value *> worklist;
    std::set<Value *> seen;
    worklist.push(c.array);
    seen.insert(c.array);
    while (!worklist.empty()) {
        auto *value = worklist.front();
        worklist.pop();
        for (const auto &use : value->use_list_) {
            auto *user = use.user_;
            if (!user) return false;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(user)) {
                if (use.operand_index_ != 0) return false;
                if (seen.insert(gep).second) worklist.push(gep);
                continue;
            }
            if (auto *load = dynamic_cast<LoadInst *>(user)) {
                if (use.operand_index_ != 0 ||
                    (c.outer->blocks.count(load->parent_) &&
                     !c.inner->blocks.count(load->parent_)))
                    return false;
                continue;
            }
            if (auto *store = dynamic_cast<StoreInst *>(user)) {
                bool inChain = std::find(c.chainBlocks.begin(),
                                         c.chainBlocks.end(),
                                         store->parent_) != c.chainBlocks.end();
                if (use.operand_index_ != 1 ||
                    (!inChain && c.outer->blocks.count(store->parent_)) ||
                    (!inChain && afterOuter.count(store->parent_)))
                    return false;
                continue;
            }
            return false;
        }
    }
    return true;
}

/**
 * @brief 收集或查找 findCandidate 所需的信息。
 * @param func 待分析或改写的函数。
 * @param LI 参数 `LI`，用于本函数的分析、匹配或 IR 构造。
 * @param c 参数 `c`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool findCandidate(Function *func, LoopInfo &LI, Candidate &c) {
    // 候选必须同时匹配双层循环 CFG、唯一外层累加 PHI、私有临时数组及内层归约链。
    // 这里只构造完整 Candidate，不修改 IR；数组使用验证通过后才交给 applyCandidate。
    for (auto &outerPtr : LI.allLoops()) {
        auto *outer = outerPtr.get();
        if (outer->children.size() != 1 || !outer->preheader ||
            !outer->singleLatch() || !outer->singleExit())
            continue;
        auto *inner = outer->children.front();
        if (!inner->preheader || inner->preheader->pre_bbs_.empty()) continue;

        auto *outerBr = dynamic_cast<BranchInst *>(outer->header->get_terminator());
        if (!outerBr || outerBr->num_ops() != 3) continue;
        BasicBlock *outerBody = nullptr;
        for (unsigned i = 1; i < 3; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(outerBr->get_operand(i));
            if (succ && outer->blocks.count(succ) && succ != inner->header)
                outerBody = succ;
        }
        if (!outerBody) continue;
        auto *bodyBr = dynamic_cast<BranchInst *>(outerBody->get_terminator());
        if (!bodyBr || bodyBr->num_ops() != 3) continue;
        bool reachesInnerPreheader = false;
        for (unsigned i = 1; i < 3; ++i)
            reachesInnerPreheader |=
                bodyBr->get_operand(i) == inner->preheader;
        if (!reachesInnerPreheader) continue;

        auto *outerIV = outer->getInductionIV();
        if (!outerIV) continue;
        PhiInst *outerSum = nullptr;
        for (auto *inst : outer->header->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);
            if (phi != outerIV) {
                if (outerSum) { outerSum = nullptr; break; }
                outerSum = phi;
            }
        }
        if (!outerSum) continue;

        c = {};
        c.func = func;
        c.outer = outer;
        c.inner = inner;
        c.outerBody = outerBody;
        c.innerPreheader = inner->preheader;
        c.outerIV = outerIV;
        c.outerSum = outerSum;

        // 在内层所有 load 中追踪唯一私有数组基址；出现第二个基址或非私有
        // alloca 都会破坏后续把数组中间态替换为标量链的前提。
        AllocaInst *array = nullptr;
        for (auto *bb : inner->blocksOrdered) {
            for (auto *inst : bb->instr_list_) {
                auto *load = dynamic_cast<LoadInst *>(inst);
                if (!load) continue;
                auto *base = pointerBase(load->get_operand(0));
                if (!base || (array && array != base)) {
                    array = nullptr;
                    break;
                }
                array = base;
            }
        }
        if (!array || !isPrivateAlloca(array)) continue;
        c.array = array;
        if (!analyzeInner(c)) continue;
        if (!analyzeChain(c)) continue;
        if (!validateArrayUses(c)) continue;
        return true;
    }
    return false;
}

/**
 * @brief 原地执行 applyCandidate 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param c 参数 `c`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyCandidate(Candidate &c) {
    // 提交阶段先建立替代循环骨架和 PHI，再克隆计算链并重连旧入口/出口。
    // 新旧 CFG 的切换放在最后，确保此前的任何校验失败都不会留下半成品。
    Module *module = c.func->parent_;
    Type *i32 = module->int32_ty_;
    auto *one = new ConstantInt(i32, 1);
    auto *zero = new ConstantInt(i32, 0);

    // 先构建尚未接入旧入口的替代循环骨架，等 PHI 和克隆链完整后再切换 CFG。
    auto *rowJoin = new BasicBlock(module, "lir.row.join", c.func);
    auto *rowPreheader = new BasicBlock(module, "lir.row.preheader", c.func);
    auto *rowHeader = new BasicBlock(module, "lir.row.header", c.func);
    auto *rowBody = new BasicBlock(module, "lir.row.body", c.func);
    auto *rowExit = new BasicBlock(module, "lir.row.exit", c.func);
    new BranchInst(rowPreheader, rowJoin);
    new BranchInst(rowHeader, rowPreheader);

    auto *rowIV = PhiInst::create_phi(i32, rowHeader);
    rowIV->add_phi_pair_operand(zero, rowPreheader);
    rowHeader->add_instruction_front(rowIV);
    auto *rowSum = PhiInst::create_phi(i32, rowHeader);
    rowSum->add_phi_pair_operand(zero, rowPreheader);
    rowHeader->add_instruction_front(rowSum);

    auto *rowCmp = new ICmpInst(c.innerCompare->icmp_op_, rowIV,
                                c.innerBound,
                                rowHeader);
    new BranchInst(rowCmp, rowBody, rowExit, rowHeader);

    ValueMap rowMap;
    rowMap[c.innerIV] = rowIV;
    rowMap[c.innerSum] = rowSum;
    BinaryInst *newIVUpdate = nullptr;
    BinaryInst *newSumUpdate = nullptr;
    for (auto *inst : c.inner->singleLatch()->instr_list_) {
        if (inst->isTerminator()) continue;
        if (!isCloneable(inst) || inst->is_store() || inst->is_call() ||
            inst->is_phi())
            return false;
        auto *clone = cloneInstruction(inst, rowBody, rowMap);
        if (!clone) return false;
        rowBody->add_instruction(clone);
        rowMap[inst] = clone;
        if (inst == c.innerIVUpdate)
            newIVUpdate = dynamic_cast<BinaryInst *>(clone);
        if (inst == c.innerSumUpdate)
            newSumUpdate = dynamic_cast<BinaryInst *>(clone);
    }
    if (!newIVUpdate || !newSumUpdate) return false;
    rowIV->add_phi_pair_operand(newIVUpdate, rowBody);
    rowSum->add_phi_pair_operand(newSumUpdate, rowBody);
    new BranchInst(rowHeader, rowBody);

    auto *rowResult = PhiInst::create_phi(i32, rowExit);
    rowResult->add_phi_pair_operand(rowSum, rowHeader);
    rowExit->add_instruction_front(rowResult);
    new BranchInst(c.outer->header, rowExit);
    ValueMap chainMap;
    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto *old : c.chainBlocks)
        blockMap[old] = new BasicBlock(module, "lir.hoisted", c.func);

    for (auto *old : c.chainBlocks) {
        auto *dest = blockMap[old];
        for (auto *inst : old->instr_list_) {
            if (inst->isTerminator()) continue;
            if (!isCloneable(inst) || inst->is_phi() || inst->is_call())
                return false;
            auto *clone = cloneInstruction(inst, dest, chainMap);
            if (!clone) return false;
            dest->add_instruction(clone);
            chainMap[inst] = clone;
        }
    }
    for (auto *old : c.chainBlocks) {
        auto *oldBr = dynamic_cast<BranchInst *>(old->get_terminator());
        if (!oldBr) return false;
        auto *dest = blockMap[old];
        if (oldBr->num_ops() == 1) {
            auto *target = static_cast<BasicBlock *>(oldBr->get_operand(0));
            new BranchInst(target == c.innerPreheader ? rowJoin
                                                       : blockMap[target], dest);
        } else {
            auto *trueTarget = static_cast<BasicBlock *>(oldBr->get_operand(1));
            auto *falseTarget = static_cast<BasicBlock *>(oldBr->get_operand(2));
            auto mapTarget = [&](BasicBlock *target) {
                return target == c.innerPreheader ? rowJoin : blockMap[target];
            };
            new BranchInst(remap(oldBr->get_operand(0), chainMap),
                           mapTarget(trueTarget), mapTarget(falseTarget), dest);
        }
    }

    auto *oldPreTerm = dynamic_cast<BranchInst *>(c.outer->preheader->get_terminator());
    if (!oldPreTerm) return false;
    auto *hoistedEntry = blockMap[c.chainBlocks.front()];
    removeTerminatorEdges(c.outer->preheader, oldPreTerm);
    new BranchInst(hoistedEntry, c.outer->preheader);
    auto *oldBodyTerm = dynamic_cast<BranchInst *>(c.outerBody->get_terminator());
    if (!oldBodyTerm) return false;
    removeTerminatorEdges(c.outerBody, oldBodyTerm);
    new BranchInst(c.innerExit, c.outerBody);
    auto *oldExitTerm = dynamic_cast<BranchInst *>(c.innerExit->get_terminator());
    if (!oldExitTerm) return false;
    removeTerminatorEdges(c.innerExit, oldExitTerm);
    std::vector<Instruction *> oldExitInsts;
    for (auto *inst : c.innerExit->instr_list_)
        if (!inst->isTerminator()) oldExitInsts.push_back(inst);
    for (auto *inst : oldExitInsts)
        c.innerExit->delete_instr(inst);

    auto *newAdd = new BinaryInst(i32, Instruction::Add, c.outerSum,
                                  rowResult, c.innerExit);
    auto *newRem = new BinaryInst(i32, Instruction::SRem, newAdd,
                                  c.modulus, c.innerExit);
    auto *newIV = new BinaryInst(i32, Instruction::Add, c.outerIV, one,
                                 c.innerExit);
    newRem->copySemFlagsFrom(c.oldModulo);
    newIV->copySemFlagsFrom(c.oldOuterIVUpdate);
    for (unsigned i = 0; i + 1 < c.outerSum->num_ops(); i += 2)
        if (c.outerSum->get_operand(i + 1) == c.innerExit)
            c.outerSum->set_operand(i, newRem);
    for (unsigned i = 0; i + 1 < c.outerIV->num_ops(); i += 2)
        if (c.outerIV->get_operand(i + 1) == c.innerExit)
            c.outerIV->set_operand(i, newIV);
    new BranchInst(c.outer->header, c.innerExit);
    retargetPhiPred(c.outer->header, c.outer->preheader, rowExit);
    removeUnreachableBlocks(c.func);
    debugApply(c.func);
    return true;
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool runOnFunction(Function *func) {
    if (debugEnabled())
        std::cerr << "[LoopInvariantReduction] analyze "
                  << (func ? func->name_ : "<null>") << "\n";
    if (!validateCFG(func)) {
        debugReject(func, "invalid CFG");
        return false;
    }
    LoopInfo LI;
    LI.analyze(func);
    Candidate c;
    if (!findCandidate(func, LI, c)) {
        debugReject(func, "no supported private invariant reduction nest");
        return false;
    }
    return applyCandidate(c);
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopInvariantReduction::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopInvariantReduction::execute(Module *module,
                                                  AnalysisManager &AM) {
    if (debugEnabled()) std::cerr << "[LoopInvariantReduction] execute\n";
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        if (runOnFunction(func)) {
            changed = true;
            AM.invalidateFunction(func, PreservedAnalyses::none());
        }
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
