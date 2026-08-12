#include "../../../include/mid/opt/loopRotate.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

void LoopRotate::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses LoopRotate::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

static Value *remapValue(Value *value, const std::map<Value *, Value *> &valueMap) {
    auto it = valueMap.find(value);
    return it == valueMap.end() ? value : it->second;
}

static Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

static bool isSupportedHeaderInst(Instruction *inst) {
    if (inst->is_phi() || inst->isTerminator())
        return false;
    if (dynamic_cast<BinaryInst *>(inst) ||
        dynamic_cast<UnaryInst *>(inst) ||
        dynamic_cast<ICmpInst *>(inst))
        return true;
    return false;
}

static void removeTerminatorAndCfgEdges(BasicBlock *bb) {
    auto *term = bb->get_terminator();
    std::vector<BasicBlock *> succs = bb->succ_bbs_;
    for (auto *succ : succs)
        succ->remove_pre_basic_block(bb);
    bb->succ_bbs_.clear();
    if (term)
        bb->delete_instr(term);
}

static void replaceWithConditionalBranch(BasicBlock *bb, Value *cond,
                                         BasicBlock *trueSucc,
                                         BasicBlock *falseSucc) {
    removeTerminatorAndCfgEdges(bb);
    new BranchInst(cond, trueSucc, falseSucc, bb);
}

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

static bool isHeaderLocalUse(Instruction *def, Instruction *user,
                             BasicBlock *header, BasicBlock *exit) {
    if (user->parent_ == header)
        return true;
    if (user->parent_ == exit && user->is_phi())
        return true;
    return false;
}

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

BasicBlock *LoopRotate::splitExitEdge(Function *func, BasicBlock *pred,
                                      BasicBlock *exit) {
    auto *split = new BasicBlock(func->parent_,
                                 exit->name_ + ".from." + pred->name_,
                                 func);
    placeBlockBefore(func, split, exit);
    new BranchInst(exit, split);
    return split;
}

bool LoopRotate::rotateLoop(Loop *loop, Function *func) {
    if (!loop || !loop->header || !loop->preheader)
        return false;
    // Calls are control and memory barriers whose continuation can carry
    // values through exit phis.  The current cloning/remapping logic handles
    // only the header's scalar expressions, so do not rotate a loop that
    // contains a call.
    for (auto *bb : loop->blocks)
        for (auto *inst : bb->instr_list_)
            if (inst->is_call())
                return false;
    // Keep simple canonical while loops in the form consumed directly by
    // vectorization and IV strength reduction.  A canonical IV alone is not a
    // reason to reject rotation for a multi-block body: internal control flow
    // already prevents those simple-loop consumers from matching, while
    // rotation exposes a guarded do-while form to the structured unroller.
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
    // Keep the rotated loop in simplify form.  The original preheader becomes
    // the zero-trip guard, so its continue edge needs a new dedicated
    // single-successor preheader rather than targeting the loop body directly.
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
