#include "../../../include/mid/opt/loopFixedPointEliminate.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct HeaderPhi {
    PhiInst *phi = nullptr;
    Value *initial = nullptr;
    Value *backedge = nullptr;
};

bool describeHeaderPhi(PhiInst *phi, BasicBlock *preheader,
                       BasicBlock *latch, HeaderPhi &result) {
    if (!phi || phi->num_ops_ != 4)
        return false;
    result.phi = phi;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
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

bool isInsideUse(const Use &use, const Loop &loop) {
    auto *user = dynamic_cast<Instruction *>(use.val_);
    return user && user->parent_ && loop.isInLoop(user->parent_);
}

bool isSupportedStateType(Value *value) {
    if (!value || !value->type_)
        return false;
    if (value->type_->tid_ != Type::IntegerTyID)
        return false;
    unsigned bits =
        static_cast<IntegerType *>(value->type_)->num_bits_;
    return bits == 1 || bits == 32;
}

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

    // The count itself must not influence the repeated computation.
    for (const Use &use : info.phi->use_list_) {
        if (use.val_ != update && use.val_ != guard)
            return false;
    }
    for (const Use &use : update->use_list_) {
        if (use.val_ != info.phi)
            return false;
    }
    for (const Use &use : guard->use_list_) {
        auto *user = dynamic_cast<BranchInst *>(use.val_);
        if (!user || user != guardBlock->get_terminator() ||
            use.arg_no_ != 0)
            return false;
    }
    return true;
}

// Header phi whose only user is its own backedge update forms a dead SSA
// cycle. It cannot affect memory or live-outs, so it must not block
// fixed-point detection (e.g. a leftover countdown of the original trip
// count after IndVarSimplify introduced a canonical IV).
bool isDeadSelfRecurrence(const HeaderPhi &info) {
    auto *update = dynamic_cast<Instruction *>(info.backedge);
    if (!update || !info.phi)
        return false;
    for (const Use &use : info.phi->use_list_) {
        if (use.val_ != update)
            return false;
    }
    for (const Use &use : update->use_list_) {
        if (use.val_ != info.phi)
            return false;
    }
    return true;
}

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
bool addLatchIncomingToExitPhis(BasicBlock *exit, BasicBlock *header,
                                BasicBlock *latch,
                                const std::vector<HeaderPhi> &phis) {
    std::vector<std::pair<PhiInst *, Value *>> updates;
    for (auto *inst : exit->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi)
            break;
        Value *fromHeader = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
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

void LoopFixedPointEliminate::execute(Module *module) {
    BasicAliasAnalysis AA;
    AA.analyze(module);
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, AA);
    }
}

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

bool LoopFixedPointEliminate::runOnFunction(Function *func,
                                            BasicAliasAnalysis &AA) {
    bool changed = false;
    for (;;) {
        // A top-tested fixed-point rewrite adds a new latch-to-exit edge.
        // Rebuild the loop forest after every successful rewrite instead of
        // continuing with child Loop objects whose CFG view is now stale.
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
        // Rewriting an outer top-tested loop can make its children appear to
        // satisfy the fixed-point shape through newly extended exit phis.
        // Those children were not independently proven against the original
        // loop nest, so do not cascade into them in the same function run.
        if (!transformed || transformedNonLeaf)
            break;
    }
    if (changed)
        func->set_instr_name();
    return changed;
}

bool LoopFixedPointEliminate::tryTransform(Loop &loop, Function *func,
                                           BasicAliasAnalysis &AA) {
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
        if (!latchBranch || latchBranch->num_ops_ != 3 ||
            latchBranch->get_operand(1) != loop.header ||
            latchBranch->get_operand(2) != exit)
            return false;
        guard = dynamic_cast<ICmpInst *>(latchBranch->get_operand(0));
        if (!guard || guard->parent_ != latch)
            return false;
        guardBlock = latch;
    } else {
        // Top-tested while: header is the sole exiting block; latch is an
        // unconditional backedge. Early exit is inserted on the latch.
        auto *headerBranch =
            dynamic_cast<BranchInst *>(loop.header->get_terminator());
        if (!headerBranch || headerBranch->num_ops_ != 3)
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
        if (!latchTerm || latchTerm->num_ops_ != 1 ||
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

    if (!memoryIsIterationIndependent(loop, AA))
        return false;

    // Outside uses must already flow through exit phis. For while shape the
    // latch gains an exit edge, so those phis are extended below.
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            for (const Use &use : inst->use_list_) {
                if (!isInsideUse(use, loop)) {
                    auto *user = dynamic_cast<Instruction *>(use.val_);
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
