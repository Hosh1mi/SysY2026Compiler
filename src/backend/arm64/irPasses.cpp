#include "../../include/backend/arm64/irPasses.hpp"

#include "../../include/backend/arm64/preRAScheduler.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

void rebuildCfgLinks(Module *module) {
    for (auto *function : module->function_list_) {
        if (function->is_declaration())
            continue;
        for (auto *block : function->basic_blocks_) {
            block->pre_bbs_.clear();
            block->succ_bbs_.clear();
        }
        for (auto *block : function->basic_blocks_) {
            auto *terminator = block->get_terminator();
            if (!terminator)
                continue;
            for (unsigned i = 0; i < terminator->num_ops_; ++i) {
                auto *successor = dynamic_cast<BasicBlock *>(terminator->get_operand(i));
                if (!successor)
                    continue;
                block->add_succ_basic_block(successor);
                successor->add_pre_basic_block(block);
            }
        }
    }
}

void sinkCmpToBranch(Module *module) {
    for (auto *function : module->function_list_) {
        if (function->is_declaration())
            continue;
        for (auto *block : function->basic_blocks_) {
            auto *terminator = block->get_terminator();
            if (!terminator || !terminator->is_br() || terminator->num_ops_ != 3)
                continue;
            auto *compare = dynamic_cast<ICmpInst *>(terminator->get_operand(0));
            if (!compare || compare->parent_ != block || compare->use_list_.size() != 1)
                continue;
            auto &instructions = block->instr_list_;
            auto compareIt = std::find(instructions.begin(), instructions.end(), compare);
            if (compareIt == instructions.end())
                continue;
            auto next = std::next(compareIt);
            if (next != instructions.end() && *next == terminator)
                continue;
            instructions.erase(compareIt);
            auto terminatorIt = std::find(instructions.begin(), instructions.end(), terminator);
            instructions.insert(terminatorIt, compare);
        }
    }
}

void updatePhiIncomingBlock(BasicBlock *target, BasicBlock *oldPredecessor,
                            BasicBlock *newPredecessor) {
    for (auto *instruction : target->instr_list_) {
        if (!instruction->is_phi())
            continue;
        for (unsigned i = 1; i < instruction->num_ops_; i += 2) {
            if (instruction->get_operand(i) == oldPredecessor)
                instruction->set_operand(i, newPredecessor);
        }
    }
}

BasicBlock *splitEdge(BasicBlock *predecessor, BasicBlock *target,
                      BranchInst *branch, unsigned operandIndex, int &counter) {
    Function *function = predecessor->parent_;
    auto *edge = new BasicBlock(function->parent_,
                                "label_sink_load_" + std::to_string(counter++),
                                function);
    predecessor->remove_succ_basic_block(target);
    target->remove_pre_basic_block(predecessor);
    branch->set_operand(operandIndex, edge);
    predecessor->add_succ_basic_block(edge);
    edge->add_pre_basic_block(predecessor);
    updatePhiIncomingBlock(target, predecessor, edge);
    new BranchInst(target, edge);
    function->invalidateDominatorInfo();
    return edge;
}

bool sinkGlobalLoadsPastEarlyExit(Module *module) {
    bool changed = false;
    int edgeCounter = 0;
    for (auto *function : module->function_list_) {
        if (function->is_declaration() || function->basic_blocks_.empty())
            continue;
        std::vector<BasicBlock *> blocks = function->basic_blocks_;
        for (auto *block : blocks) {
            auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
            if (!branch || branch->num_ops_ != 3)
                continue;
            auto *trueBlock = dynamic_cast<BasicBlock *>(branch->get_operand(1));
            auto *falseBlock = dynamic_cast<BasicBlock *>(branch->get_operand(2));
            if (!trueBlock || !falseBlock)
                continue;

            std::map<BasicBlock *, BasicBlock *> splitForTarget;
            std::vector<LoadInst *> loads;
            for (auto *instruction : block->instr_list_) {
                if (instruction == branch)
                    break;
                auto *load = dynamic_cast<LoadInst *>(instruction);
                if (load && dynamic_cast<GlobalVariable *>(load->get_operand(0)))
                    loads.push_back(load);
            }

            for (auto *load : loads) {
                if (load->parent_ != block)
                    continue;
                BasicBlock *target = nullptr;
                bool safe = !load->use_list_.empty();
                for (const auto &use : load->use_list_) {
                    auto *user = dynamic_cast<Instruction *>(use.val_);
                    if (!user || !user->parent_ || user->parent_ == block) {
                        safe = false;
                        break;
                    }
                    bool inTrue = function->dominates(trueBlock, user->parent_);
                    bool inFalse = function->dominates(falseBlock, user->parent_);
                    if (inTrue == inFalse) {
                        safe = false;
                        break;
                    }
                    BasicBlock *useTarget = inTrue ? trueBlock : falseBlock;
                    if (!target)
                        target = useTarget;
                    else if (target != useTarget) {
                        safe = false;
                        break;
                    }
                }
                if (!safe || !target)
                    continue;

                unsigned operandIndex = target == trueBlock ? 1u : 2u;
                BasicBlock *edge = nullptr;
                auto found = splitForTarget.find(target);
                if (found != splitForTarget.end()) {
                    edge = found->second;
                } else {
                    edge = splitEdge(block, target, branch, operandIndex, edgeCounter);
                    splitForTarget[target] = edge;
                    changed = true;
                }
                block->remove_instr(load);
                edge->add_instruction_before_terminator(load);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace

Arm64IRPreparationPipeline::Arm64IRPreparationPipeline(
    bool enableOptimizations, bool enableScheduling, bool dumpPreRA,
    std::ostream &diagnostics)
    : enableOptimizations_(enableOptimizations),
      enableScheduling_(enableScheduling), dumpPreRA_(dumpPreRA),
      diagnostics_(diagnostics) {}

void Arm64IRPreparationPipeline::run(Module *module) const {
    rebuildCfgLinks(module);
    if (enableOptimizations_ && sinkGlobalLoadsPastEarlyExit(module))
        rebuildCfgLinks(module);
    sinkCmpToBranch(module);
    if (enableOptimizations_ && enableScheduling_)
        PreRAScheduler(dumpPreRA_, &diagnostics_).run(module);
}
