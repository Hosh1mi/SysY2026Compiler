#include "../../include/mid/opt/loopRotate.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <set>

void LoopRotate::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

static Value *remapValue(Value *value, const std::map<Value *, Value *> &valueMap) {
    auto it = valueMap.find(value);
    return it == valueMap.end() ? value : it->second;
}

static Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
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

bool LoopRotate::runOnFunction(Function *func) {
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
    BasicBlock *header = loop->header;
    BasicBlock *preheader = loop->preheader;
    BasicBlock *latch = loop->singleLatch();
    if (!latch)
        return false;

    auto *preTerm = preheader->get_terminator();
    auto *latchTerm = latch->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != header)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
        latchTerm->get_operand(0) != header)
        return false;
    bool hasBackedgePhi = false;
    for (auto *inst : latch->instr_list_) {
        if (inst->is_phi()) {
            hasBackedgePhi = true;
            break;
        }
        if (!inst->is_phi())
            break;
    }
    if (!hasBackedgePhi)
        return false;

    auto *headerTerm = header->get_terminator();
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3)
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

    for (auto *phi : headerPhis) {
        for (auto &use : phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user) continue;
            if (user->parent_ == exitSucc && user->is_phi())
                continue;
            if (loop->isInLoop(user->parent_))
                continue;
            return false;
        }
    }
    for (auto *def : headerInsts) {
        for (auto &use : def->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user) continue;
            if (!isHeaderLocalUse(def, user, header, exitSucc))
                return false;
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

    for (auto *inst : exitSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
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

    for (auto *inst : continueSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
            if (phi->get_operand(i + 1) != header)
                continue;
            Value *oldVal = phi->get_operand(i);
            Value *preVal = remapValue(oldVal, preMap);
            Value *latchVal = remapValue(oldVal, latchMap);
            phi->remove_operands(i, i + 1);
            phi->addIncoming(preVal, preheader);
            phi->addIncoming(latchVal, latch);
            break;
        }
    }

    for (auto it = headerPhis.rbegin(); it != headerPhis.rend(); ++it) {
        auto *phi = *it;
        header->remove_instr(phi);
        continueSucc->add_instruction_front(phi);
    }

    BasicBlock *preTrue = trueInLoop ? continueSucc : preExit;
    BasicBlock *preFalse = falseInLoop ? continueSucc : preExit;
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
