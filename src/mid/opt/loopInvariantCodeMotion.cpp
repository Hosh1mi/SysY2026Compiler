#include "../../include/mid/opt/loopInvariantCodeMotion.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <queue>

void LICM::execute(Module *module) {
    BasicAliasAnalysis BAA;
    BAA.analyze(module);
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, BAA);
    }
}

// ── Loop detection ─────────────────────────────────────────────────────────

std::vector<LICM::Loop> LICM::findLoops(Function *func) {
    // 相同 header 的多条回边属于同一个 loop。若每条回边各自形成独立 loop，
    // 其不完整的 body 会让 getPreheader 认为有多个"外部前驱"而返回 nullptr，
    // 导致 LICM 静默跳过整个循环。
    std::map<BasicBlock*, Loop> headerToLoop;
    auto &idom = domInfo_->idom;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!idom.count(succ)) continue;
            if (!domInfo_->dominates(succ, bb)) continue;

            auto &loop = headerToLoop[succ];
            loop.header = succ;
            if (!loop.latch) loop.latch = bb;

            loop.blocks.insert(succ);
            std::queue<BasicBlock*> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front(); wl.pop();
                if (!loop.blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop.blocks.count(pred)) wl.push(pred);
            }
        }
    }

    std::vector<Loop> loops;
    for (auto &kv : headerToLoop)
        loops.push_back(std::move(kv.second));
    return loops;
}

BasicBlock *LICM::getPreheader(const Loop &loop) {
    BasicBlock *pre = nullptr;
    for (auto pred : loop.header->pre_bbs_) {
        if (loop.blocks.count(pred)) continue;
        if (pre) return nullptr;
        pre = pred;
    }
    return pre;
}

// ── Invariance / safety checks ─────────────────────────────────────────────

static bool isLICMSCEVDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_LICM_SCEV") != nullptr;
    return enabled;
}

static bool isLICMPurityDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_LICM_PURITY") != nullptr;
    return enabled;
}

static void debugSCEVInvariant(Value *val, BasicBlock *loopHeader, const SCEV *s) {
    if (!isLICMSCEVDebugEnabled()) return;

    std::cerr << "[LICM:SCEV] loop_header="
              << (loopHeader ? loopHeader->name_ : "<null>")
              << " value=";
    if (auto *inst = dynamic_cast<Instruction *>(val)) {
        std::cerr << "%" << inst->name_;
    } else if (auto *global = dynamic_cast<GlobalVariable *>(val)) {
        std::cerr << "@" << global->name_;
    } else if (val) {
        std::cerr << (val->name_.empty() ? "<anon>" : val->name_);
    } else {
        std::cerr << "<null>";
    }
    std::cerr << " scev=" << (s ? s->print() : "<null>") << "\n";
}

bool LICM::isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                        const std::set<Instruction*>& toHoist, const Loop *loop,
                        ScalarEvolution *SE, ::Loop *analysisLoop) {
    bool available = false;
    if (dynamic_cast<Constant*>(val) ||
        dynamic_cast<GlobalVariable*>(val) ||
        dynamic_cast<Argument*>(val)) {
        available = true;
    }

    auto inst = dynamic_cast<Instruction*>(val);
    if (!available) {
        if (!inst) {
            available = true;
        } else if (toHoist.count(inst)) {
            available = true;
        } else {
            available = !loopBlocks.count(inst->parent_);
        }
    }

    if (!available)
        return false;

    if (SE && analysisLoop) {
        const SCEV *s = SE->getSCEV(val);
        if (s && s->kind() != SCEVKind::CouldNotCompute &&
            SE->isLoopInvariant(s, analysisLoop)) {
            debugSCEVInvariant(val, loop ? loop->header : nullptr, s);
        }
    }

    return true;
}

bool LICM::isSafeToHoist(Instruction *inst, const Loop &loop,
                          const BasicAliasAnalysis &BAA) {
    if (inst->is_phi() || inst->is_br() || inst->is_ret() ||
        inst->is_store() || inst->is_alloca())
        return false;

    if (inst->is_call()) {
        if (inst->is_void()) return false;
        auto *call = static_cast<CallInst *>(inst);
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        return callee && BAA.isPure(callee);
    }

    if (inst->is_load()) {
        Value *ptr = inst->get_operand(0);
        for (auto *bb : loop.blocks) {
            for (auto *other : bb->instr_list_) {
                if (isModSet(BAA.getModRefInfo(other, ptr)))
                    return false;
            }
        }
        return true;
    }

    return true;
}

// ── Hoisting ──────────────────────────────────────────────────────────────

bool LICM::runOnLoop(const Loop &loop, ScalarEvolution *SE, ::Loop *analysisLoop,
                     const BasicAliasAnalysis *BAA) {
    BasicBlock *preheader = getPreheader(loop);
    if (!preheader) return false;
    if (!BAA) return false;

    bool changed = false;
#if 0
    // ── GEP splitting ──────────────────────────────────────────────
    // Split multi-dimensional GEPs at the invariant/variant boundary.
    // The invariant prefix will be hoisted by the main loop below.
    {
        std::set<Instruction*> emptyHoist; // no instructions hoisted yet
        for (auto *bb : loop.blocks) {
            std::vector<Instruction*> insts(bb->instr_list_.begin(),
                                            bb->instr_list_.end());
            for (auto *inst : insts) {
                auto *gep = dynamic_cast<GetElementPtrInst*>(inst);
                if (!gep) continue;
                unsigned numIdx = gep->num_ops_ - 1;
                if (numIdx < 2) continue;

                // Find first variant index
                unsigned splitIdx = 0;
                for (unsigned i = 1; i <= numIdx; i++) {
                    if (!isInvariant(gep->get_operand(i), loop.blocks,
                                     emptyHoist, &loop))
                        break;
                    splitIdx = i;
                }
                if (splitIdx == 0 || splitIdx == numIdx) continue;

                // Build invariant prefix GEP
                std::vector<Value*> prefixIdxs;
                for (unsigned i = 1; i <= splitIdx; i++)
                    prefixIdxs.push_back(gep->get_operand(i));
                auto *prefixGEP = new GetElementPtrInst(
                    gep->get_operand(0), prefixIdxs, bb, true);

                bool inserted = bb->add_instruction_before_inst(prefixGEP, inst);
                if (!inserted) {
                    prefixGEP->remove_use_of_ops();
                    delete prefixGEP;
                    continue;
                }

                // Build variant suffix GEP using the prefix as base
                std::vector<Value*> variantIdxs;
                for (unsigned i = splitIdx + 1; i <= numIdx; i++)
                    variantIdxs.push_back(gep->get_operand(i));
                auto *suffixGEP = GetElementPtrInst::create_split_suffix_gep(
                    prefixGEP, variantIdxs, bb, true);

                inserted = bb->add_instruction_before_inst(suffixGEP, inst);
                if (!inserted) {
                    bb->delete_instr(prefixGEP);
                    suffixGEP->remove_use_of_ops();
                    delete suffixGEP;
                    continue;
                }

                // Replace original with suffix; prefix/suffix are already inserted
                // immediately before the original GEP in BB order.
                inst->replace_all_use_with(suffixGEP);
                bb->delete_instr(inst);
                changed = true;
            }
        }
    }
#endif
    bool progress = true;
    while (progress) {
        progress = false;

        std::set<Instruction*> toHoist;
        for (auto bb : loop.blocks) {
            for (auto inst : bb->instr_list_) {
                if (!isSafeToHoist(inst, loop, *BAA)) continue;

                bool allInvariant = true;
                unsigned operandLimit = inst->is_call() ? inst->num_ops_ - 1
                                                        : inst->num_ops_;
                for (unsigned i = 0; i < operandLimit; i++) {
                    if (!isInvariant(inst->get_operand(i), loop.blocks, toHoist,
                                     &loop, SE, analysisLoop)) {
                        allInvariant = false;
                        break;
                    }
                }
                if (allInvariant) toHoist.insert(inst);
            }
        }

        if (toHoist.empty()) break;

        for (auto bb : loop.blocks) {
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                Instruction *inst = *it;
                if (!toHoist.count(inst)) { ++it; continue; }

                it = bb->instr_list_.end();
                bb->remove_instr(inst);
                inst->parent_ = preheader;
                preheader->add_instruction_before_terminator(inst);
                if (inst->is_call() && isLICMPurityDebugEnabled()) {
                    auto *call = static_cast<CallInst *>(inst);
                    auto *callee = dynamic_cast<Function *>(
                        call->get_operand(call->num_ops_ - 1));
                    std::cerr << "[LICM:PURITY] function=" << preheader->parent_->name_
                              << " loop_header=" << loop.header->name_
                              << " call=%" << inst->name_
                              << " callee=" << (callee ? callee->name_ : "<null>")
                              << "\n";
                }

                it = bb->instr_list_.begin();
                progress = true;
                changed  = true;
            }
        }
    }

    return changed;
}

void LICM::eliminateSinglePredPhis(Function *func) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb->pre_bbs_.size() != 1) continue;
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                auto phi = dynamic_cast<PhiInst*>(*it);
                if (!phi) break;
                Value *incoming = phi->get_operand(0);
                phi->replace_all_use_with(incoming);
                bb->delete_instr(phi);
                changed = true;
                it = bb->instr_list_.begin();
            }
        }
    }
}

void LICM::eliminateTrivialHeaderPhis(const Loop &loop) {
    bool changed = true;
    while (changed) {
        changed = false;
        auto it = loop.header->instr_list_.begin();
        while (it != loop.header->instr_list_.end()) {
            auto phi = dynamic_cast<PhiInst*>(*it);
            if (!phi) break;

            Value *non_latch_val = nullptr;
            bool self_loop = true;
            bool valid = true;

            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                Value *inc_bb  = phi->get_operand(i + 1);
                Value *inc_val = phi->get_operand(i);
                if (inc_bb == loop.latch) {
                    if (inc_val != phi) self_loop = false;
                } else {
                    if (!non_latch_val) non_latch_val = inc_val;
                    else if (non_latch_val != inc_val) valid = false;
                }
            }

            if (valid && self_loop && non_latch_val) {
                auto nv_inst = dynamic_cast<Instruction*>(non_latch_val);
                if (nv_inst && loop.blocks.count(nv_inst->parent_)) {
                    ++it;
                    continue;
                }
                phi->replace_all_use_with(non_latch_val);
                loop.header->delete_instr(phi);
                changed = true;
                it = loop.header->instr_list_.begin();
                continue;
            }
            ++it;
        }
    }
}

void LICM::runOnFunction(Function *func, const BasicAliasAnalysis &BAA) {
    if (func->basic_blocks_.empty()) return;

    eliminateSinglePredPhis(func);

    domInfo_ = &func->getDominatorInfo();
    auto loops = findLoops(func);

    std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b) {
        return a.blocks.size() < b.blocks.size();
    });

    for (auto &loop : loops) {
        LoopInfo LI;
        LI.analyze(func);
        ScalarEvolution SE(LI);

        ::Loop *analysisLoop = nullptr;
        for (auto &ownedLoop : LI.allLoops()) {
            if (ownedLoop->header == loop.header) {
                analysisLoop = ownedLoop.get();
                break;
            }
        }

        runOnLoop(loop, &SE, analysisLoop, &BAA);
        eliminateTrivialHeaderPhis(loop);
    }
}
