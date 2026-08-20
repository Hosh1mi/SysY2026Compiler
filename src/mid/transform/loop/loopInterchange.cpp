/**
 * @file loopInterchange.cpp
 * @brief 循环交换：依据循环形状与内存依赖合法性交换嵌套循环次序，以改善局部性或暴露并行性。
 * @details 循环交换必须保持词典序依赖合法；克隆/下沉指令前验证副作用、外部使用和退出值可用性。
 */

#include "../../../include/mid/opt/loopInterchange.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/opt/loopWorklist.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_INTERCHANGE") != nullptr;
}

/**
 * @brief 实现 storeSlice 对应的局部分析或变换辅助逻辑。
 * @param B 参数 `B`，用于本函数的分析、匹配或 IR 构造。
 * @return 成功时返回对应对象指针；无法匹配或构造时可能返回 nullptr。
 */
std::vector<Instruction *> storeSlice(BasicBlock *B) {
    std::set<Instruction *> inW;
    std::vector<Instruction *> work;
    for (auto *inst : B->instr_list_)
        if (inst->is_store() && inW.insert(inst).second) work.push_back(inst);
    while (!work.empty()) {
        auto *x = work.back();
        work.pop_back();
        for (unsigned i = 0; i < x->num_ops(); i++) {
            auto *op = dynamic_cast<Instruction *>(x->get_operand(i));
            if (!op || op->parent_ != B || op->is_phi()) continue;
            if (inW.insert(op).second) work.push_back(op);
        }
    }
    std::vector<Instruction *> W;
    for (auto *inst : B->instr_list_)
        if (inW.count(inst)) W.push_back(inst);
    return W;
}

/**
 * @brief 判断 isCloneableType 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isCloneableType(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) || dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst)   || dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) || dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst)   || dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) || dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<StoreInst *>(inst);
}

/**
 * @brief 判断 isDiscardablePureInstruction 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isDiscardablePureInstruction(Instruction *inst) {
    return inst && !inst->isTerminator() && !inst->is_store() &&
           !inst->is_call();
}

/**
 * @brief 判断 isSafeOutsideSunkLoop 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isSafeOutsideSunkLoop(Instruction *inst) {
    // 与待克隆 store 切片交错的其他指令会留在新内层循环之外，所以既不能观察
    // 切片修改的内存，也不能自身有副作用。依赖闭包会把切片所需定义全部纳入，
    // 后续 IV-use 检查再证明余下计算对被下沉循环不变。
    return inst && !inst->isTerminator() && !inst->is_load() &&
           !inst->is_store() && !inst->is_call();
}

/**
 * @brief 实现 latchHasSideEffects 对应的局部分析或变换辅助逻辑。
 * @param latch 循环回边基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool latchHasSideEffects(BasicBlock *latch) {
    if (!latch) return true;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (!isDiscardablePureInstruction(inst)) return true;
    }
    return false;
}

/**
 * @brief 实现 deleteUnusedPureInstructions 对应的局部分析或变换辅助逻辑。
 * @param bb 目标或待修改的基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void deleteUnusedPureInstructions(BasicBlock *bb) {
    if (!bb) return;
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Instruction *> instructions(bb->instr_list_.begin(),
                                                 bb->instr_list_.end());
        for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
            Instruction *inst = *it;
            if (isDiscardablePureInstruction(inst) && inst->use_list_.empty()) {
                bb->delete_instr(inst);
                changed = true;
            }
        }
    }
}

using ValMap = std::unordered_map<Value *, Value *>;

/**
 * @brief 前向声明：重映射交换切片中的值，必要时递归克隆其定义。
 * @param v 待重映射的原值。
 * @param dest 克隆指令的目标基本块。
 * @param vm 原值到克隆值的映射表。
 * @param unionW 需要随 K 维移动的完整指令切片。
 * @param kIV 原 K 维归纳变量。
 * @param localk 交换后当前位置对应的 K 值。
 * @param ok 写回克隆过程是否成功。
 * @return 重映射后的值；失败时返回 nullptr。
 */
Value *cloneValInto(Value *v, BasicBlock *dest, ValMap &vm,
                    const std::set<Instruction *> &unionW, PhiInst *kIV,
                    Value *localk, bool &ok);

/**
 * @brief 把可移动 store 切片中的一条指令克隆到交换后的目标块。
 * @param orig 待克隆的原指令。
 * @param dest 克隆指令的目标基本块。
 * @param vm 原值到克隆值的映射表。
 * @param unionW 需要随 K 维移动的完整指令切片。
 * @param kIV 原 K 维归纳变量。
 * @param localk 交换后当前位置对应的 K 值。
 * @param ok 写回克隆是否仍然成功。
 * @return 成功时返回克隆指令，否则返回 nullptr。
 */
Instruction *cloneInstInto(Instruction *orig, BasicBlock *dest, ValMap &vm,
                           const std::set<Instruction *> &unionW, PhiInst *kIV,
                           Value *localk, bool &ok) {
    auto it = vm.find(orig);
    if (it != vm.end()) return dynamic_cast<Instruction *>(it->second);
    auto C = [&](Value *v) { return cloneValInto(v, dest, vm, unionW, kIV, localk, ok); };

    Instruction *cl = nullptr;
    if (auto *bi = dynamic_cast<BinaryInst *>(orig)) {
        cl = new BinaryInst(bi->type_, bi->op_id_, C(bi->get_operand(0)),
                            C(bi->get_operand(1)), dest);
    } else if (auto *ui = dynamic_cast<UnaryInst *>(orig)) {
        cl = new UnaryInst(ui->type_, ui->op_id_, C(ui->get_operand(0)), dest);
    } else if (auto *ci = dynamic_cast<ICmpInst *>(orig)) {
        cl = new ICmpInst(ci->icmp_op_, C(ci->get_operand(0)), C(ci->get_operand(1)), dest);
    } else if (auto *fi = dynamic_cast<FCmpInst *>(orig)) {
        cl = new FCmpInst(fi->fcmp_op_, C(fi->get_operand(0)), C(fi->get_operand(1)), dest);
    } else if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops(); i++) idxs.push_back(C(gi->get_operand(i)));
        cl = new GetElementPtrInst(C(gi->get_operand(0)), idxs, dest);
    } else if (auto *li = dynamic_cast<LoadInst *>(orig)) {
        cl = new LoadInst(C(li->get_operand(0)), dest);
    } else if (auto *st = dynamic_cast<StoreInst *>(orig)) {
        cl = new StoreInst(C(st->get_operand(0)), C(st->get_operand(1)), dest);
    } else if (auto *zi = dynamic_cast<ZextInst *>(orig)) {
        cl = new ZextInst(zi->op_id_, C(zi->get_operand(0)), zi->type_, dest);
    } else if (auto *fp = dynamic_cast<FpToSiInst *>(orig)) {
        cl = new FpToSiInst(fp->op_id_, C(fp->get_operand(0)), fp->type_, dest);
    } else if (auto *sf = dynamic_cast<SiToFpInst *>(orig)) {
        cl = new SiToFpInst(sf->op_id_, C(sf->get_operand(0)), sf->type_, dest);
    } else if (auto *bc = dynamic_cast<Bitcast *>(orig)) {
        cl = new Bitcast(bc->op_id_, C(bc->get_operand(0)), bc->type_, dest);
    } else {
        ok = false;
        return nullptr;
    }
    if (!ok) return nullptr;
    vm[orig] = cl;
    return cl;
}

/**
 * @brief 将切片操作数重映射到交换后的循环位置，必要时递归克隆其定义。
 * @param v 待重映射的原值。
 * @param dest 克隆指令的目标基本块。
 * @param vm 原值到克隆值的映射表。
 * @param unionW 需要移动的指令切片。
 * @param kIV 原 K 维归纳变量。
 * @param localk 交换后对应的 K 值。
 * @param ok 写回递归克隆是否成功。
 * @return 重映射后的值；失败时返回 nullptr。
 */
Value *cloneValInto(Value *v, BasicBlock *dest, ValMap &vm,
                    const std::set<Instruction *> &unionW, PhiInst *kIV,
                    Value *localk, bool &ok) {
    if (v == kIV) return localk;
    auto it = vm.find(v);
    if (it != vm.end()) return it->second;
    if (dynamic_cast<Constant *>(v) || dynamic_cast<Argument *>(v) ||
        dynamic_cast<GlobalVariable *>(v) || dynamic_cast<BasicBlock *>(v))
        return v;
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return v;
    if (unionW.count(inst))
        return cloneInstInto(inst, dest, vm, unionW, kIV, localk, ok);
    return v;   
}

/**
 * @brief 原地执行 replaceBranchTarget 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param pred 前驱基本块。
 * @param oldT 参数 `oldT`，用于本函数的分析、匹配或 IR 构造。
 * @param newT 参数 `newT`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldT, BasicBlock *newT) {
    auto *term = pred->get_terminator();
    for (unsigned i = 0; i < term->num_ops(); i++)
        if (term->get_operand(i) == oldT) term->set_operand(i, newT);
    pred->remove_succ_basic_block(oldT);
    oldT->remove_pre_basic_block(pred);
    pred->add_succ_basic_block(newT);
    newT->add_pre_basic_block(pred);
}

/**
 * @brief 原地执行 retargetPhiPred 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param succ 后继基本块。
 * @param oldPred 需要替换的原前驱基本块。
 * @param newPred 替换后的新前驱基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void retargetPhiPred(BasicBlock *succ, BasicBlock *oldPred, BasicBlock *newPred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        for (unsigned i = 0; i + 1 < inst->num_ops(); i += 2)
            if (inst->get_operand(i + 1) == oldPred)
                inst->set_operand(i + 1, newPred);
    }
}

/**
 * @brief 原地执行 applyParallelSink 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param func 待分析或改写的函数。
 * @param K 参数 `K`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @param newLoopHeaders 参数 `newLoopHeaders`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyParallelSink(Function *func, Loop *K,
                       const DominatorTreeAnalysis &DT,
                       std::vector<BasicBlock *> &newLoopHeaders) {
    auto reject = [&](const char *reason) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] parallel sink rejected: "
                      << reason << "\n";
        return false;
    };
    Module  *module = func->parent_;
    Type    *i32    = module->int32_ty_;
    PhiInst *kIV    = K->getInductionIV();
    Value   *kBound = K->tripCount;
    BasicBlock *kHeader = K->header;
    BasicBlock *kPre    = K->preheader;
    BasicBlock *kLatch  = K->singleLatch();
    BasicBlock *kExit   = K->singleExit();
    if (!kIV || !kBound || !kHeader || !kPre || !kLatch || !kExit)
        return reject("incomplete canonical loop structure");
    if (kLatch == kHeader || latchHasSideEffects(kLatch)) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] parallel sink requires a distinct, "
                         "side-effect-free latch; header="
                      << kHeader->name_ << " latch=" << kLatch->name_ << "\n";
        return reject("latch is not safe to remove");
    }

    auto *kbr = dynamic_cast<BranchInst *>(kHeader->get_terminator());
    if (!kbr || kbr->num_ops() != 3)
        return reject("header is not conditionally branched");
    auto *t1 = dynamic_cast<BasicBlock *>(kbr->get_operand(1));
    auto *t2 = dynamic_cast<BasicBlock *>(kbr->get_operand(2));
    BasicBlock *bodyEntry = K->blocks.count(t1) ? t1 : (K->blocks.count(t2) ? t2 : nullptr);
    if (!bodyEntry) return reject("loop body entry was not found");

    std::vector<BasicBlock *> storeBlocks;
    std::unordered_map<BasicBlock *, std::vector<Instruction *>> slices;
    std::set<Instruction *> unionW;
    for (auto *bb : K->blocksOrdered) {
        if (bb == kHeader || bb == kLatch) continue;
        bool hasStore = false;
        for (auto *inst : bb->instr_list_) if (inst->is_store()) { hasStore = true; break; }
        if (!hasStore) continue;
        auto W = storeSlice(bb);
        if (W.empty()) continue;
        storeBlocks.push_back(bb);
        slices[bb] = W;
        for (auto *w : W) unionW.insert(w);
    }
    if (storeBlocks.empty()) return reject("loop has no movable store slice");

    // store 的输入可能已被 LICM 提升到嵌套循环 preheader，但它仍是每个 K 迭代
    // 计算一次，必须随 K 一起移动。因此对 K 内非 PHI 定义求依赖闭包，不能依赖
    // LICM 恰好生成的基本块布局。
    std::vector<Instruction *> dependencyWork(unionW.begin(), unionW.end());
    while (!dependencyWork.empty()) {
        Instruction *value = dependencyWork.back();
        dependencyWork.pop_back();
        for (unsigned i = 0; i < value->num_ops(); ++i) {
            auto *dependency =
                dynamic_cast<Instruction *>(value->get_operand(i));
            if (!dependency || !dependency->parent_ ||
                !K->blocks.count(dependency->parent_) || dependency->is_phi() ||
                dependency->isTerminator())
                continue;
            if (unionW.insert(dependency).second)
                dependencyWork.push_back(dependency);
        }
    }

    for (auto *w : unionW)
        if (!isCloneableType(w))
            return reject("store slice contains an unclonable instruction");
  
    for (auto *bb : storeBlocks) {
        auto &W = slices[bb];
        std::set<Instruction *> inW(W.begin(), W.end());
        Instruction *lastW = W.back();
        bool seenLast = false, passedPhi = false;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi()) continue;
            passedPhi = true;
            (void)passedPhi;
            if (inst == lastW) { seenLast = true; break; }
            if (!inW.count(inst) && !isSafeOutsideSunkLoop(inst))
                return reject("store slice crosses a memory or side-effecting instruction");
        }
        if (!seenLast) return reject("store slice endpoint was not found");
    }

    for (auto *w : unionW) {
        if (w->is_store()) continue;
        for (auto &u : w->use_list_) {
            auto *user = u.user_;
            if (!user || !unionW.count(user)) {
                if (debugEnabled()) {
                    std::cerr << "[LoopInterchange] escaping slice value="
                              << w->name_ << " op=" << w->op_id_
                              << " user="
                              << (user ? user->name_ : "<non-instruction>")
                              << " user-block="
                              << (user && user->parent_
                                      ? user->parent_->name_
                                      : "<none>")
                              << "\n  value-ir: " << w->print()
                              << "\n  user-ir: "
                              << (user ? user->print() : "<none>")
                              << "\n";
                }
                return reject("store slice value escapes the slice");
            }
        }
    }
    
    for (auto &u : kIV->use_list_) {
        auto *user = u.user_;
        if (!user) return reject("induction has a non-instruction use");
        if (unionW.count(user)) continue;
        if (user->parent_ == kHeader || user->parent_ == kLatch) continue;
        if (debugEnabled())
            std::cerr << "[LoopInterchange] escaping induction user: "
                      << user->print() << "\n";
        return reject("induction is used outside loop control and store slices");
    }
  
    for (auto *w : unionW) {
        for (unsigned i = 0; i < w->num_ops(); i++) {
            auto *op = dynamic_cast<Instruction *>(w->get_operand(i));
            if (!op || op == kIV || unionW.count(op)) continue;
            if (!DT.dominates(op->parent_, w->parent_))
                return reject("store slice operand does not dominate its use");
        }
    }
    
    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 2000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(module, "li_" + tag + "_" + bbNum(), func);
    };
    auto *c0 = new ConstantInt(i32, 0);
    auto *c1 = new ConstantInt(i32, 1);
    std::vector<BasicBlock *> generatedLoopHeaders;

    for (auto *B : storeBlocks) {
        auto &W = slices[B];
        Instruction *lastW = W.back();
        
        std::vector<Instruction *> tail;
        bool after = false;
        for (auto *inst : B->instr_list_) {
            if (after) tail.push_back(inst);
            if (inst == lastW) after = true;
        }
        auto *origTerm = tail.empty() ? nullptr : tail.back();
        if (!origTerm || !origTerm->isTerminator())
            return reject("store block has no movable terminator tail");

        BasicBlock *bTail = newBB("tail");
        std::vector<BasicBlock *> origSuccs;
        for (unsigned i = 0; i < origTerm->num_ops(); i++)
            if (auto *s = dynamic_cast<BasicBlock *>(origTerm->get_operand(i)))
                origSuccs.push_back(s);

        for (auto *inst : tail) { B->remove_instr(inst); bTail->add_instruction(inst); }
        
        for (auto *s : origSuccs) {
            B->remove_succ_basic_block(s);
            s->remove_pre_basic_block(B);
            bTail->add_succ_basic_block(s);
            s->add_pre_basic_block(bTail);
            retargetPhiPred(s, B, bTail);
        }
        
        BasicBlock *skH = newBB("kH");
        BasicBlock *skB = newBB("kB");
        BasicBlock *skL = newBB("kL");
        generatedLoopHeaders.push_back(skH);

        new BranchInst(skH, B);
        B->add_succ_basic_block(skH);
        skH->add_pre_basic_block(B);
        
        auto *localk = PhiInst::create_phi(i32, skH);
        localk->add_phi_pair_operand(c0, B);
        skH->add_instruction_front(localk);
        auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, localk, kBound, skH);
        new BranchInst(cmp, skB, bTail, skH);
        skH->add_pre_basic_block(skL);   
        
        ValMap vm;
        bool ok = true;
        for (auto *w : W) {
            cloneInstInto(w, skB, vm, unionW, kIV, localk, ok);
            if (!ok) return reject("failed to clone a store slice");
        }
        new BranchInst(skL, skB);
        
        auto *inc = new BinaryInst(i32, Instruction::Add, localk, c1, skL);
        localk->add_phi_pair_operand(inc, skL);
        new BranchInst(skH, skL);
    }

    std::set<Instruction *> pending = unionW;
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            Instruction *w = *it;
            if (w->use_list_.empty()) {
                w->parent_->delete_instr(w);
                it = pending.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
    if (!pending.empty())
        return reject("original store slice remains live after cloning");
    
    replaceBranchTarget(kPre, kHeader, bodyEntry);
    replaceBranchTarget(kLatch, kHeader, kExit);

    removeUnreachableBlocks(func);
    deleteUnusedPureInstructions(kLatch);
    newLoopHeaders = std::move(generatedLoopHeaders);
    return true;
}

// ── parallel float ──────────────────────────────────────────────────────
// Interchange K (outer, carries dependence) with M (inner, parallel).
//
// Before: for K { for M { body } }
// After:  for M { for K { body } }
//
// M must have a canonical header guard.  Its phi nodes and guard move to a
// new outer header (mOuter), while the old header becomes the entry to the
// interchanged body.  Guards that depend on K remain in their body blocks.

/**
 * @brief 原地执行 applyInterchange 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param func 待分析或改写的函数。
 * @param K 参数 `K`，用于本函数的分析、匹配或 IR 构造。
 * @param M 参数 `M`，用于本函数的分析、匹配或 IR 构造。
 * @param newOuterHeader 参数 `newOuterHeader`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyInterchange(Function *func, Loop *K, Loop *M,
                      BasicBlock *&newOuterHeader) {
    // 交换不是简单互换两个 header：需要重建外/内层入口、回边和退出关系，
    // 并按新前驱重写两层 PHI。旧块只在所有新边就绪后才清理。
    Module *module = func->parent_;
    auto reject = [&](const char *reason) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] full interchange rejected: "
                      << reason << "\n";
        return false;
    };

    BasicBlock *kHeader = K->header;
    BasicBlock *kPre    = K->preheader;
    BasicBlock *kLatch  = K->singleLatch();
    BasicBlock *kExit   = K->singleExit();
    if (!kHeader || !kPre || !kLatch || !kExit)
        return reject("incomplete outer structure");

    BasicBlock *mHeader = M->header;
    BasicBlock *mPre    = M->preheader;
    BasicBlock *mLatch  = M->singleLatch();
    if (!mHeader || !mPre || !mLatch)
        return reject("incomplete inner structure");

    auto *kBranch = dynamic_cast<BranchInst *>(kHeader->get_terminator());
    auto *mPreBranch = dynamic_cast<BranchInst *>(mPre->get_terminator());
    BasicBlock *kBodySucc = nullptr;
    if (kBranch && kBranch->num_ops() == 3) {
        for (unsigned i = 1; i < kBranch->num_ops(); ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(kBranch->get_operand(i));
            if (succ && K->isInLoop(succ)) kBodySucc = succ;
        }
    }
    if (!mPreBranch || mPreBranch->num_ops() != 1 ||
        mPreBranch->get_operand(0) != mHeader || kBodySucc != mPre)
        return reject("loops are not a directly nested guarded pair");

    // 交换后内层 preheader 移到 K 外，旧内层 latch 变成新外层控制块。
    // 因此连接块不能含每轮 K 的计算，latch 也只能含归纳更新；否则必须先
    // 分裂/克隆这些工作，不能直接重定向边。
    for (auto *inst : mPre->instr_list_)
        if (inst != mPreBranch)
            return reject("inner preheader contains outer-iteration work");

    const InductionDescriptor *mControl = M->getInductionDescriptor();
    if (!mControl || !mControl->update || mControl->update->parent_ != mLatch)
        return reject("inner latch has no isolated induction update");
    for (auto *inst : mLatch->instr_list_)
        if (inst != mControl->update && !inst->isTerminator())
            return reject("inner latch contains loop body work");

    if (K->children.size() != 1)
        return reject("outer does not have exactly one child");

    auto hasPhi = [](BasicBlock *bb) -> bool {
        return !bb->instr_list_.empty() && bb->instr_list_.front()->is_phi();
    };
    if (hasPhi(kExit) || hasPhi(kLatch) || hasPhi(mLatch)) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] phi blocks outer-exit="
                      << kExit->name_ << ":" << hasPhi(kExit)
                      << " outer-latch=" << kLatch->name_ << ":"
                      << hasPhi(kLatch) << " inner-latch=" << mLatch->name_
                      << ":" << hasPhi(mLatch) << "\n";
        return reject("exit or latch contains phi nodes");
    }

    std::vector<PhiInst *> mPhis;
    for (auto *inst : mHeader->instr_list_) {
        if (!inst->is_phi()) break;
        mPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (mPhis.empty()) return reject("inner header has no phi nodes");

    auto *mBranch = dynamic_cast<BranchInst *>(mHeader->get_terminator());
    auto *mGuard = mBranch && mBranch->num_ops() == 3
                       ? dynamic_cast<ICmpInst *>(mBranch->get_operand(0))
                       : nullptr;
    BasicBlock *mBodySucc = nullptr;
    BasicBlock *mExitSucc = nullptr;
    if (mBranch && mBranch->num_ops() == 3) {
        for (unsigned i = 1; i < mBranch->num_ops(); ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(mBranch->get_operand(i));
            if (!succ) continue;
            if (M->isInLoop(succ)) mBodySucc = succ;
            else                   mExitSucc = succ;
        }
    }
    PhiInst *mIV = M->getInductionIV();
    if (!mIV || !mGuard ||
        mGuard->icmp_op_ != ICmpInst::ICMP_SLT ||
        mGuard->get_operand(0) != mIV ||
        !mBodySucc || !mExitSucc)
        return reject("inner loop is not guarded by its canonical induction");

    for (auto *inst : mHeader->instr_list_) {
        if (inst->is_phi() || inst == mGuard || inst == mBranch) continue;
        return reject("inner header contains instructions outside its guard");
    }

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 3000); };

    auto *mOuter = new BasicBlock(module, "li_float_" + bbNum(), func);

    for (auto *phi : mPhis) {
        mHeader->remove_instr(phi);
        mOuter->add_instruction(phi);
    }

    // 原内层 guard 必须迁移为新外层 guard；若仍留在旧 header，内层 IV
    // 越界后只会跳过 body，而旧外层循环会继续推进并无法终止。
    mHeader->remove_instr(mGuard);
    mOuter->add_instruction(mGuard);
    for (auto *succ : std::vector<BasicBlock *>(mHeader->succ_bbs_)) {
        mHeader->remove_succ_basic_block(succ);
        succ->remove_pre_basic_block(mHeader);
    }
    mHeader->delete_instr(mBranch);
    new BranchInst(mBodySucc, mHeader);
    new BranchInst(mGuard, mPre, kExit, mOuter);

    for (auto *phi : mPhis) {
        for (unsigned i = 0; i < phi->num_ops(); i += 2)
            if (phi->get_operand(i + 1) == mPre)
                phi->set_operand(i + 1, kPre);
    }
    for (auto *inst : kHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops(); i += 2)
            if (phi->get_operand(i + 1) == kPre)
                phi->set_operand(i + 1, mPre);
    }

    auto isInMExits = [&](BasicBlock *bb) -> bool {
        return std::find(M->exits.begin(), M->exits.end(), bb) != M->exits.end();
    };

    // 1. K_pre → mOuter (was → kHeader)
    replaceBranchTarget(kPre, kHeader, mOuter);

    // 2. K_header: body → mHeader, exit → mLatch
    {
        BasicBlock *bodySucc = nullptr, *exitSucc = nullptr;
        auto *term = kBranch;
        for (unsigned i = 1; i < term->num_ops(); i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ) continue;
            if (K->isInLoop(succ)) bodySucc = succ;
            else                   exitSucc = succ;
        }
        if (bodySucc) replaceBranchTarget(kHeader, bodySucc, mHeader);
        if (exitSucc) replaceBranchTarget(kHeader, exitSucc, mLatch);
    }

    // 3. M_pre: enter → kHeader, skip → mLatch
    {
        BasicBlock *enterSucc = nullptr, *skipSucc = nullptr;
        for (auto *succ : mPre->succ_bbs_) {
            if (isInMExits(succ)) skipSucc = succ;
            else                  enterSucc = succ;
        }
        if (enterSucc) replaceBranchTarget(mPre, enterSucc, kHeader);
        if (skipSucc)  replaceBranchTarget(mPre, skipSucc,  mLatch);
    }

    // 4. Blocks inside M that branch to mLatch → branch to kLatch
    {
        std::vector<BasicBlock *> predsToRetarget;
        for (auto *pred : mLatch->pre_bbs_) {
            if (pred == mLatch) continue;
            if (!M->isInLoop(pred)) continue;
            predsToRetarget.push_back(pred);
        }
        for (auto *pred : predsToRetarget)
            replaceBranchTarget(pred, mLatch, kLatch);
    }

    // 5. M_latch: continue → mOuter, exit → kExit
    {
        BasicBlock *continueSucc = nullptr, *exitSucc = nullptr;
        for (auto *succ : mLatch->succ_bbs_) {
            if (isInMExits(succ)) exitSucc = succ;
            else                  continueSucc = succ;
        }
        if (continueSucc) replaceBranchTarget(mLatch, continueSucc, mOuter);
        if (exitSucc)     replaceBranchTarget(mLatch, exitSucc,     kExit);
    }

    // 前驱改变后同步改写 PHI 的 predecessor 操作数，保持 value/block 成对。
    retargetPhiPred(kExit, kHeader, mLatch);
    retargetPhiPred(kLatch, mLatch, mHeader);
    for (auto *phi : mPhis)
        retargetPhiPred(mPre, mHeader, mOuter);

    removeUnreachableBlocks(func);
    newOuterHeader = mOuter;
    return true;
}
} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopInterchange::execute(Module *module) {
    AnalysisManager AM;
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    argAA_ = &argAA;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, AM);
    }
    argAA_ = nullptr;
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopInterchange::execute(Module *module, AnalysisManager &AM) {
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    argAA_ = &argAA;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    argAA_ = nullptr;
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopInterchange::runOnFunction(Function *func, AnalysisManager &AM) {
    // 两阶段分别尝试把并行循环下沉、把携带归约的循环上浮。
    // 每次 CFG 改写后清空分析并按受影响 header 重建邻域，避免复用旧 Loop*。
    bool everChanged = false;
    LoopInfo &initialLoopInfo = AM.getLoopInfo(func);
    AffectedLoopWorklist sinkWorklist;
    AffectedLoopWorklist floatWorklist;
    sinkWorklist.seed(initialLoopInfo);
    floatWorklist.seed(initialLoopInfo);

    while (true) {
        LoopInfo &LI = AM.getLoopInfo(func);
        DominatorTreeAnalysis &DT = AM.getDominatorTree(func);
        if (LI.allLoops().empty()) break;

        AffineAnalysis     AA(LI);
        DependenceAnalysis DA(LI, AA);
        DA.setArgAlias(argAA_);
        LoopAccessAnalysis LA(AA);
        CostModel          CM(AA);
        LoopInterchangeAnalysis IA(DA, LA, CM);

        auto dbg = [&](Loop *K, const char *why) {
            if (debugEnabled())
                std::cerr << "[LoopInterchange] reject K=" << func->name_ << "/"
                          << (K->header ? K->header->name_ : "?")
                          << " depth=" << K->depth << ": " << why << "\n";
        };

        bool transformed = false;
        std::vector<BasicBlock *> affectedHeaders;
        while (Loop *K = sinkWorklist.take(LI)) {
            ParallelSinkAnalysisResult analysis = IA.analyzeParallelSink(K);
            if (!analysis.accepted) {
                if (!K->children.empty()) dbg(K, analysis.reason);
                continue;
            }

            if (debugEnabled())
                std::cerr << "[LoopInterchange] sink candidate K=" << func->name_ << "/"
                          << K->header->name_ << " stride "
                          << analysis.cost.before << "->"
                          << analysis.cost.after << "\n";
            if (K->parent) affectedHeaders.push_back(K->parent->header);
            for (Loop *child : K->children)
                affectedHeaders.push_back(child->header);
            std::vector<BasicBlock *> newLoopHeaders;
            if (applyParallelSink(func, K, DT, newLoopHeaders)) {
                affectedHeaders.insert(affectedHeaders.end(),
                                       newLoopHeaders.begin(),
                                       newLoopHeaders.end());
                everChanged = true;
                transformed = true;
                break;
            }
            affectedHeaders.clear();
            if (debugEnabled())
                std::cerr << "[LoopInterchange] applyParallelSink bailed\n";
        }

        // 第二阶段：K 携带依赖而子循环 M 可并行。交换后归约循环更靠内，
        // 缩短归约变量复用距离，同时把并行维提升到外层。
        if (!transformed) {
            while (Loop *K = floatWorklist.take(LI)) {
                ParallelFloatAnalysisResult analysis =
                    IA.analyzeParallelFloat(K);
                if (!analysis.accepted) {
                    if (!K->children.empty()) dbg(K, analysis.reason);
                    continue;
                }

                if (debugEnabled())
                    std::cerr << "[LoopInterchange] float candidate K=" << func->name_ << "/"
                              << K->header->name_ << " M="
                              << analysis.inner->header->name_ << "\n";
                affectedHeaders.push_back(K->header);
                affectedHeaders.push_back(analysis.inner->header);
                if (K->parent)
                    affectedHeaders.push_back(K->parent->header);
                for (Loop *child : K->children)
                    affectedHeaders.push_back(child->header);
                BasicBlock *newOuterHeader = nullptr;
                if (applyInterchange(func, K, analysis.inner,
                                     newOuterHeader)) {
                    affectedHeaders.push_back(newOuterHeader);
                    everChanged = true;
                    transformed = true;
                    break;
                }
                affectedHeaders.clear();
                if (debugEnabled())
                    std::cerr << "[LoopInterchange] applyInterchange bailed\n";
            }
        }

        if (!transformed) break;

        AM.clear(func);
        LoopInfo &updatedLoopInfo = AM.getLoopInfo(func);
        for (BasicBlock *header : affectedHeaders) {
            sinkWorklist.addNeighborhood(updatedLoopInfo, header);
            floatWorklist.addNeighborhood(updatedLoopInfo, header);
        }
    }
    return everChanged;
}
