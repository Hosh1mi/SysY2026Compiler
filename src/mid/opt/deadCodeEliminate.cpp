#include "../../include/mid/opt/deadCodeEliminate.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace {

Function *calledFunction(Instruction *inst) {
    auto *call = dynamic_cast<CallInst *>(inst);
    if (!call || call->num_ops_ == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
}

Function *findFunction(Module *module, const std::string &name) {
    for (auto *func : module->function_list_)
        if (func->name_ == name)
            return func;
    return nullptr;
}

bool isParallelBody(Function *func) {
    return func && func->name_.rfind("__sysy_par_body_", 0) == 0;
}

void markParallelBodiesForCall(Module *module, CallInst *call,
                               std::vector<Function *> &worklist,
                               std::set<Function *> &reachable) {
    auto *callee = calledFunction(call);
    if (!callee || callee->name_ != "__sysy_parallel_for")
        return;

    auto mark = [&](Function *func) {
        if (func && reachable.insert(func).second)
            worklist.push_back(func);
    };

    if (call->num_ops_ >= 2) {
        if (auto *id = dynamic_cast<ConstantInt *>(call->get_operand(0))) {
            mark(findFunction(module, "__sysy_par_body_" +
                                      std::to_string(id->value_)));
            return;
        }
    }

    for (auto *func : module->function_list_) {
        if (isParallelBody(func))
            mark(func);
    }
}

} // namespace

void DeadCodeEliminate::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses DeadCodeEliminate::execute(Module *module,
                                             AnalysisManager &AM) {
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        changed |= runOnFunction(func, BAA);
    }
    changed |= eliminateUnreachableFunctions(module);
    changed |= eliminateUnusedGlobals(module);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool DeadCodeEliminate::eliminateUnreachableFunctions(Module *module) {
    Function *entry = module->getMainFunc();
    if (!entry)
        return false;

    std::set<Function *> reachable;
    std::vector<Function *> worklist;
    reachable.insert(entry);
    worklist.push_back(entry);

    while (!worklist.empty()) {
        Function *func = worklist.back();
        worklist.pop_back();
        if (!func || func->is_declaration())
            continue;

        for (auto *bb : func->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call)
                    continue;

                Function *callee = calledFunction(call);
                if (callee && reachable.insert(callee).second)
                    worklist.push_back(callee);
                markParallelBodiesForCall(module, call, worklist, reachable);
            }
        }
    }

    auto &funcs = module->function_list_;
    auto oldSize = funcs.size();
    funcs.erase(std::remove_if(funcs.begin(), funcs.end(),
        [&](Function *func) {
            return !func->is_declaration() && !reachable.count(func);
        }), funcs.end());
    return funcs.size() != oldSize;
}

bool DeadCodeEliminate::eliminateUnusedGlobals(Module *module) {
    std::set<GlobalVariable *> used;

    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        for (auto *bb : func->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                for (unsigned i = 0; i < inst->num_ops_; ++i) {
                    if (auto *global =
                            dynamic_cast<GlobalVariable *>(inst->get_operand(i)))
                        used.insert(global);
                }
            }
        }
    }

    auto &globals = module->global_list_;
    auto oldSize = globals.size();
    globals.erase(std::remove_if(globals.begin(), globals.end(),
        [&](GlobalVariable *global) {
            return !used.count(global);
        }), globals.end());
    return globals.size() != oldSize;
}

bool DeadCodeEliminate::runOnFunction(Function *func,
                                      const BasicAliasAnalysis &BAA) {
    bool changed = false;
    bool localChanged = false;
    do {
        localChanged = false;
        localChanged |= eliminateTrivialPhis(func);
        localChanged |= eliminateDeadInstructions(func, BAA);
        changed |= localChanged;
    } while (localChanged);
    return changed;
}

bool DeadCodeEliminate::hasRequiredEffect(
    Instruction *inst, const BasicAliasAnalysis &BAA) const {
    switch (inst->op_id_) {
        case Instruction::Ret:
        case Instruction::Br:
        case Instruction::Store:
            return true;
        case Instruction::Call: {
            Function *callee = calledFunction(inst);
            if (!callee)
                return true;
            return !BAA.isPure(callee);
        }
        default:
            return false;
    }
}

bool DeadCodeEliminate::eliminateDeadInstructions(
    Function *func, const BasicAliasAnalysis &BAA) {
    // Mark from observable roots so mutually-referential dead SSA cycles do
    // not keep themselves alive through non-empty use lists.
    std::set<Instruction *> live;
    std::vector<Instruction *> worklist;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (hasRequiredEffect(inst, BAA) && live.insert(inst).second)
                worklist.push_back(inst);
        }
    }

    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            auto *operand =
                dynamic_cast<Instruction *>(inst->get_operand(i));
            if (operand && operand->parent_ && live.insert(operand).second)
                worklist.push_back(operand);
        }
    }

    std::vector<Instruction *> dead;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!live.count(inst))
                dead.push_back(inst);
        }
    }

    bool changed = false;
    for (auto *inst : dead) {
        BasicBlock *parent = inst->parent_;
        if (parent && parent->delete_instr(inst))
            changed = true;
    }
    return changed;
}

bool DeadCodeEliminate::eliminateTrivialPhis(Function *func) {
    std::set<PhiInst *> worklist;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi())
                worklist.insert(static_cast<PhiInst *>(inst));
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        PhiInst *phi = *worklist.begin();
        worklist.erase(worklist.begin());
        if (!phi->parent_)
            continue;

        std::vector<PhiInst *> usersToRevisit;
        for (auto use : phi->use_list_) {
            auto *user = dynamic_cast<PhiInst *>(use.val_);
            if (user && user->parent_)
                usersToRevisit.push_back(user);
        }

        Value *common = nullptr;
        bool nonTrivial = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            Value *incoming = phi->get_operand(i);
            if (incoming == phi)
                continue;
            if (!common) {
                common = incoming;
            } else if (common != incoming) {
                nonTrivial = true;
                break;
            }
        }

        if (nonTrivial || !common)
            continue;

        phi->replace_all_use_with(common);
        BasicBlock *parent = phi->parent_;
        if (parent && parent->delete_instr(phi)) {
            changed = true;
            for (auto *user : usersToRevisit) {
                if (user->parent_)
                    worklist.insert(user);
            }
        }
    }
    return changed;
}
