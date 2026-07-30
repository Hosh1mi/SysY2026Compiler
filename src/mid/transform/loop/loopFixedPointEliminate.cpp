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

// Recognize a finite latch-controlled unit-stride recurrence. The latch guard
// compares the value at the beginning of the just-finished iteration, as in:
//   n.next = n - 1
//   if (n > bound) goto header
bool isFiniteControlPhi(const HeaderPhi &info, const Loop &loop,
                        ICmpInst *guard) {
    auto *initial = dynamic_cast<ConstantInt *>(info.initial);
    auto *update = dynamic_cast<BinaryInst *>(info.backedge);
    if (!initial || !update || update->parent_ != loop.singleLatch())
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
        if (!user || user != loop.singleLatch()->get_terminator() ||
            use.arg_no_ != 0)
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
    LoopInfo LI;
    LI.analyze(func);
    std::vector<Loop *> loops;
    for (const auto &loop : LI.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *lhs, Loop *rhs) {
                  return lhs->depth < rhs->depth;
              });

    bool changed = false;
    for (auto *loop : loops)
        changed |= tryTransform(*loop, func, AA);
    if (changed)
        func->set_instr_name();
    return changed;
}

bool LoopFixedPointEliminate::tryTransform(Loop &loop, Function *func,
                                           BasicAliasAnalysis &AA) {
    BasicBlock *preheader = loop.preheader;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !latch || !exit ||
        loop.exiting.size() != 1 || loop.exiting.front() != latch)
        return false;

    auto *branch = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!branch || branch->num_ops_ != 3 ||
        branch->get_operand(1) != loop.header ||
        branch->get_operand(2) != exit)
        return false;
    auto *guard = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    if (!guard || guard->parent_ != latch)
        return false;

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
        if (!isFiniteControlPhi(phis[i], loop, guard))
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
        if (!isSupportedStateType(phis[i].phi))
            return false;
        state.push_back(phis[i]);
    }

    if (!memoryIsIterationIndependent(loop, AA))
        return false;

    // Reject any loop-local definition other than the control recurrence that
    // escapes by a route not already represented on the latch exit. The
    // existing exit edge remains unchanged, so ordinary live-outs are safe.
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

    Type *i1 = branch->get_operand(0)->type_;
    Value *stateChanged = new ConstantInt(i1, 0);
    for (const HeaderPhi &info : state) {
        auto *changed =
            new ICmpInst(ICmpInst::ICMP_NE, info.backedge, info.phi,
                         latch, true);
        latch->add_instruction_before_inst(changed, branch);
        if (dynamic_cast<ConstantInt *>(stateChanged)) {
            stateChanged = changed;
        } else {
            auto *eitherChanged =
                new BinaryInst(i1, Instruction::Or, stateChanged, changed,
                               latch, true);
            latch->add_instruction_before_inst(eitherChanged, branch);
            stateChanged = eitherChanged;
        }
    }

    auto *continueIfChanged =
        new BinaryInst(i1, Instruction::And, branch->get_operand(0),
                       stateChanged, latch, true);
    latch->add_instruction_before_inst(continueIfChanged, branch);
    branch->set_operand(0, continueIfChanged);

    if (std::getenv("DEBUG_LOOP_FIXED_POINT"))
        std::cerr << "[LoopFixedPointEliminate] function=" << func->name_
                  << " header=" << loop.header->name_
                  << " state=" << state.size()
                  << " blocks=" << loop.blocks.size() << "\n";
    return true;
}
