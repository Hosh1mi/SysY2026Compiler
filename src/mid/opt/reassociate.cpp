#include "../../include/mid/opt/reassociate.hpp"
#include <algorithm>
#include <queue>

void Reassociate::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

void Reassociate::runOnFunction(Function *func) {
    valueRank_.clear();
    bbRank_.clear();
    computeRanks(func);

    for (auto bb : func->basic_blocks_) {
        auto it = bb->instr_list_.begin();
        while (it != bb->instr_list_.end()) {
            Instruction *inst = *it;
            ++it;
            if (auto *bin = dynamic_cast<BinaryInst*>(inst)) {
                reassociate(bin);
            }
        }
    }
}

// -----------------------------------------------------------------------
// Rank: assign ranks in BB order (RPO approximation)
// -----------------------------------------------------------------------
void Reassociate::computeRanks(Function *func) {
    int rank = 2;
    // Assign distinct ranks to arguments
    for (auto arg : func->arguments_)
        valueRank_[arg] = ++rank;

    for (auto bb : func->basic_blocks_) {
        int bbRank = ++rank << 16;
        bbRank_[bb] = bbRank;
        int bbLocal = bbRank;
        for (auto inst : bb->instr_list_) {
            if (inst->is_phi())
                valueRank_[inst] = ++bbLocal;
            else if (inst->is_binary())
                valueRank_[inst] = ++bbLocal;
        }
    }
}

int Reassociate::getRank(Value *v) {
    if (dynamic_cast<Constant*>(v)) return 0;
    if (dynamic_cast<Argument*>(v)) return valueRank_[v];
    auto it = valueRank_.find(v);
    if (it != valueRank_.end()) return it->second;
    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst) return 0;
    int maxRank = bbRank_[inst->parent_];
    int r = 0;
    for (unsigned i = 0; i < inst->num_ops_ && r != maxRank; i++)
        r = std::max(r, getRank(inst->get_operand(i)));
    return valueRank_[inst] = r;
}

// -----------------------------------------------------------------------
// Core helpers
// -----------------------------------------------------------------------
void Reassociate::swapOperands(Instruction *inst) {
    Value *tmp = inst->get_operand(0);
    inst->set_operand(0, inst->get_operand(1));
    inst->set_operand(1, tmp);
}

BinaryInst *Reassociate::createBinary(Instruction::OpID op, Value *v1, Value *v2,
                                       BasicBlock *bb, Instruction *before) {
    auto *bin = new BinaryInst(v1->type_, op, v1, v2, bb, true);
    bb->add_instruction_before_inst(bin, before);
    return bin;
}

// -----------------------------------------------------------------------
// Leaf collection: flatten associative ops (single-use chain from root)
// -----------------------------------------------------------------------
void Reassociate::collectLeafOperands(Value *root, Instruction::OpID op,
                                       std::vector<Value*> &ops,
                                       std::unordered_set<Value*> &visited) {
    auto *bin = dynamic_cast<BinaryInst*>(root);
    // Only traverse single-use, same-opcode, same-BB chains
    if (!bin || bin->op_id_ != op || bin->use_list_.size() != 1) {
        if (!visited.count(root)) {
            visited.insert(root);
            ops.push_back(root);
        }
        return;
    }
    collectLeafOperands(bin->get_operand(0), op, ops, visited);
    collectLeafOperands(bin->get_operand(1), op, ops, visited);
}

// -----------------------------------------------------------------------
// Rebuild ADD tree from flat list
// -----------------------------------------------------------------------
Value *Reassociate::rebuildAddTree(std::vector<Value*> &ops, BasicBlock *bb,
                                    Instruction *before) {
    if (ops.size() == 1) return ops[0];
    Value *lhs = ops.back(); ops.pop_back();
    Value *rhs = rebuildAddTree(ops, bb, before);
    return createBinary(Instruction::Add, lhs, rhs, bb, before);
}

static Value *rebuildTree(Instruction::OpID op, std::vector<Value*> &ops,
                          BasicBlock *bb, Instruction *before) {
    if (ops.size() == 1) return ops[0];
    Value *lhs = ops.back(); ops.pop_back();
    Value *rhs = rebuildTree(op, ops, bb, before);
    return Reassociate::createBinary(op, lhs, rhs, bb, before);
}

// -----------------------------------------------------------------------
// Extract factors from a MUL tree
// -----------------------------------------------------------------------
void Reassociate::extractOneUseFactors(Value *v, std::vector<Value*> &factors) {
    auto *bin = dynamic_cast<BinaryInst*>(v);
    if (bin && bin->op_id_ == Instruction::Mul && bin->use_list_.size() == 1) {
        extractOneUseFactors(bin->get_operand(0), factors);
        extractOneUseFactors(bin->get_operand(1), factors);
    } else {
        factors.push_back(v);
    }
}

// -----------------------------------------------------------------------
// removeFactor: extract a factor from a MUL tree
// -----------------------------------------------------------------------
Value *Reassociate::removeFactor(Value *mulTree, Value *factor) {
    auto *bin = dynamic_cast<BinaryInst*>(mulTree);
    if (!bin || bin->op_id_ != Instruction::Mul) return nullptr;

    // Collect all factors
    std::vector<Value*> allFactors;
    extractOneUseFactors(mulTree, allFactors);

    // Find and remove the target factor
    bool found = false;
    bool negate = false;
    for (size_t i = 0; i < allFactors.size(); i++) {
        if (allFactors[i] == factor) {
            found = true;
            allFactors.erase(allFactors.begin() + i);
            break;
        }
        auto *ci1 = dynamic_cast<ConstantInt*>(factor);
        auto *ci2 = dynamic_cast<ConstantInt*>(allFactors[i]);
        if (ci1 && ci2 && ci1->value_ == -ci2->value_) {
            found = negate = true;
            allFactors.erase(allFactors.begin() + i);
            break;
        }
    }
    if (!found) return nullptr;

    // Rebuild remaining mul tree
    Value *result = nullptr;
    for (auto *f : allFactors) {
        if (!result) result = f;
        else {
            result = createBinary(Instruction::Mul, result, f,
                                  bin->parent_, bin);
        }
    }
    if (!result) return nullptr;

    if (negate) {
        auto *bb = bin->parent_;
        auto *zero = new ConstantInt(result->type_, 0);
        result = createBinary(Instruction::Sub, zero, result, bb, bin);
    }
    return result;
}

// -----------------------------------------------------------------------
// optAddTree: optimize a flattened ADD tree
// -----------------------------------------------------------------------
Value *Reassociate::optAddTree(BinaryInst *root, std::vector<ValueEntry> &ops) {
    auto *bb = root->parent_;
    auto *intTy = root->type_;
    bool changed = false;

    // ---- Pass 1: Add-to-mul (X + X + X → 3 * X) ----
    for (size_t i = 0; i + 1 < ops.size(); ) {
        Value *curr = ops[i].operand;
        int freq = 1;
        size_t j = i + 1;
        while (j < ops.size() && ops[j].operand == curr) { freq++; j++; }
        if (freq > 1) {
            ops.erase(ops.begin() + i, ops.begin() + j);
            auto *mul = createBinary(Instruction::Mul, curr,
                new ConstantInt(intTy, freq), bb, root);
            changed = true;
            if (ops.empty()) return mul;
            ops.insert(ops.begin(), {mul, getRank(mul)});
        } else {
            i++;
        }
    }

    // ---- Pass 2: Cancel A + (-A) ----
    for (size_t i = 0; i < ops.size(); ) {
        Value *curr = ops[i].operand;
        // Check if curr is "sub 0, X" (i.e., -X)
        auto *subBin = dynamic_cast<BinaryInst*>(curr);
        Value *negVal = nullptr;
        if (subBin && subBin->is_sub()) {
            auto *c0 = dynamic_cast<ConstantInt*>(subBin->get_operand(0));
            if (c0 && c0->value_ == 0)
                negVal = subBin->get_operand(1);  // negVal = X
        }

        if (negVal) {
            // Search for matching X in ops at same rank
            bool cancelled = false;
            for (size_t j = i + 1; j < ops.size() && ops[j].rank == ops[i].rank; j++) {
                if (ops[j].operand == negVal) {
                    // Found: A + (-A) → cancel both
                    if (j > i) { ops.erase(ops.begin() + j); ops.erase(ops.begin() + i); }
                    else { ops.erase(ops.begin() + i); ops.erase(ops.begin() + j); }
                    changed = true;
                    cancelled = true;
                    break;
                }
            }
            if (!cancelled) {
                for (size_t j = 0; j < i && ops[j].rank == ops[i].rank; j++) {
                    if (ops[j].operand == negVal) {
                        if (j > i) { ops.erase(ops.begin() + j); ops.erase(ops.begin() + i); }
                        else { ops.erase(ops.begin() + i); ops.erase(ops.begin() + j); }
                        changed = true;
                        cancelled = true;
                        break;
                    }
                }
            }
            if (cancelled) {
                if (ops.empty()) return new ConstantInt(intTy, 0);
                continue;
            }
        }
        i++;
    }

    // ---- Pass 3: Factor out common term (A*B + A*C → A*(B+C)) ----
    std::unordered_map<Value*, int> factorFreq;
    Value *bestFactor = nullptr;
    int bestFreq = 0;
    for (auto &op : ops) {
        auto *mulBin = dynamic_cast<BinaryInst*>(op.operand);
        if (!mulBin || mulBin->op_id_ != Instruction::Mul || mulBin->use_list_.size() != 1)
            continue;
        std::vector<Value*> factors;
        extractOneUseFactors(mulBin, factors);
        std::unordered_set<Value*> seen;
        for (auto *f : factors) {
            if (!seen.insert(f).second) continue;
            int cnt = ++factorFreq[f];
            if (cnt > bestFreq) { bestFreq = cnt; bestFactor = f; }
        }
    }

    if (bestFreq > 1) {
        std::vector<Value*> innerAddOps;
        for (size_t i = 0; i < ops.size(); ) {
            auto *mulBin = dynamic_cast<BinaryInst*>(ops[i].operand);
            if (mulBin && mulBin->op_id_ == Instruction::Mul) {
                Value *reduced = removeFactor(ops[i].operand, bestFactor);
                if (reduced) {
                    int count = 0;
                    for (size_t j = ops.size(); j > i; ) {
                        j--;
                        if (ops[j].operand == ops[i].operand) {
                            innerAddOps.push_back(reduced);
                            ops.erase(ops.begin() + j);
                            count++;
                        }
                    }
                    continue;
                }
            }
            i++;
        }

        if (!innerAddOps.empty()) {
            Value *addTree = rebuildAddTree(innerAddOps, bb, root);
            auto *factorMul = createBinary(Instruction::Mul, addTree, bestFactor, bb, root);
            changed = true;
            if (ops.empty()) return factorMul;
            ops.insert(ops.begin(), {factorMul, getRank(factorMul)});
        }
    }

    return changed ? nullptr : nullptr;
    // nullptr means: no replacement, just rewrite the tree in order
}

// -----------------------------------------------------------------------
// reassociate: main entry for a binary instruction
// -----------------------------------------------------------------------
void Reassociate::reassociate(BinaryInst *inst) {
    Instruction::OpID op = inst->op_id_;
    if (op != Instruction::Add && op != Instruction::Mul) return;

    // Canonicalize: constants on RHS only (skip rank-based swap for safety)
    Value *lhs = inst->get_operand(0);
    Value *rhs = inst->get_operand(1);

    if (dynamic_cast<Constant*>(lhs) && !dynamic_cast<Constant*>(rhs))
        swapOperands(inst);
}
