#include "../../include/mid/opt/loopInvariantCodeMotion.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

void LICM::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LICM::execute(Module *module, AnalysisManager &AM) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, AM);
    }
    return PreservedAnalyses::none();
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

static bool isLICMHoistDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_LICM_HOIST") != nullptr;
    return enabled;
}

static std::string formatLICMValue(Value *val) {
    if (!val) return "<null>";
    if (auto *inst = dynamic_cast<Instruction *>(val))
        return "%" + inst->name_;
    if (auto *global = dynamic_cast<GlobalVariable *>(val))
        return "@" + global->name_;
    if (!val->name_.empty())
        return val->name_;
    return "<anon>";
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

static void debugHoistedInst(Instruction *inst, BasicBlock *loopHeader,
                             BasicBlock *preheader) {
    if (!isLICMHoistDebugEnabled()) return;

    const char *kind = "inst";
    const char *reason = "invariant";
    if (inst->is_load()) {
        kind = "load";
        reason = "no-modref";
    } else if (inst->is_call()) {
        kind = "call";
        reason = "pure";
    } else if (inst->is_gep()) {
        kind = "gep";
    }

    std::cerr << "[LICM:HOIST] function="
              << (preheader && preheader->parent_ ? preheader->parent_->name_ : "<null>")
              << " loop_header=" << (loopHeader ? loopHeader->name_ : "<null>")
              << " kind=" << kind
              << " reason=" << reason
              << " inst=" << formatLICMValue(inst);

    if (inst->is_load()) {
        std::cerr << " ptr=" << formatLICMValue(inst->get_operand(0));
    } else if (inst->is_call()) {
        auto *call = static_cast<CallInst *>(inst);
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        std::cerr << " callee=" << (callee ? callee->name_ : "<null>");
    }
    std::cerr << "\n";
}

bool LICM::isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                        const std::set<Instruction*>& toHoist, const Loop *loop,
                        ScalarEvolution *SE) {
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

    if (SE && loop) {
        const SCEV *s = SE->getSCEV(val);
        if (s && s->kind() != SCEVKind::CouldNotCompute &&
            SE->isLoopInvariant(s, const_cast<Loop *>(loop))) {
            debugSCEVInvariant(val, loop->header, s);
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

bool LICM::runOnLoop(const Loop &loop, ScalarEvolution *SE,
                     const BasicAliasAnalysis *BAA) {
    BasicBlock *preheader = loop.preheader;
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
                                     &loop, SE)) {
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
                debugHoistedInst(inst, loop.header, preheader);
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

bool LICM::eliminateTrivialHeaderPhis(const Loop &loop) {
    // 单 latch 是 LoopSimplify 的后置条件；多 latch 时"来自 latch 的入边"
    // 不唯一，保守跳过
    BasicBlock *latch = loop.singleLatch();
    if (!latch) return false;
    bool anyChanged = false;
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
                if (inc_bb == latch) {
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
                anyChanged = true;
                it = loop.header->instr_list_.begin();
                continue;
            }
            ++it;
        }
    }
    return anyChanged;
}

void LICM::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return;

    eliminateSinglePredPhis(func);

    // 由内向外处理。本 pass 不改 CFG，但 changed 后会失效 AM 缓存的
    // LoopInfo，Loop* 不能跨失效持有——先收集稳定的 header 列表，
    // 每轮按 header 重查当前 LoopInfo。
    std::vector<BasicBlock *> headers;
    {
        LoopInfo &LI = AM.getLoopInfo(func);
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(), [](Loop *a, Loop *b) {
            return a->depth > b->depth;
        });
        for (auto *l : loops)
            headers.push_back(l->header);
    }

    for (auto *header : headers) {
        LoopInfo &LI = AM.getLoopInfo(func);
        Loop *loop = nullptr;
        for (auto &l : LI.allLoops()) {
            if (l->header == header) {
                loop = l.get();
                break;
            }
        }
        if (!loop) continue;

        ScalarEvolution &SE = AM.getScalarEvolution(func);
        BasicAliasAnalysis &BAA = AM.getBasicAA(func->parent_);

        bool changed = runOnLoop(*loop, &SE, &BAA);
        bool phiChanged = eliminateTrivialHeaderPhis(*loop);
        if (changed || phiChanged) {
            PreservedAnalyses pa;
            pa.preserveBasicAA();
            AM.invalidateFunction(func, pa);
        }
    }
}
