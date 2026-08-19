// 典型示例：
//   优化前：(%c + %a) + %b。
//   优化后：(%a + %b) + %c，操作数依据稳定 rank 排列。
// 统一的表达式形态更容易与其它位置的同值计算匹配，并可缩短部分依赖链。

#include "../../include/mid/opt/reassociate.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <queue>

// Reassociate 为交换律/结合律表达式建立稳定的操作数顺序，使公共子表达式更易
// 暴露。通用路径按支配关系计算 rank 并重建表达式树；i32 加减法另有线性形式，
// 将系数和常量统一收集后，仅在重建成本更低时生成新树。

namespace {

constexpr unsigned kMaxLinearDepth = 16;
constexpr unsigned kMaxLinearVisits = 128;
constexpr unsigned kMaxLinearTerms = 48;

struct LinearTerm {
    Value *value;
    std::uint32_t coefficient;
};

struct LinearForm {
    std::uint32_t constant = 0;
    std::vector<LinearTerm> terms;
    std::unordered_map<Value *, size_t> termIndex;
    std::unordered_set<BinaryInst *> expanded;
    unsigned visits = 0;
};

// 线性化路径仅处理具有模 2^32 语义的 i32 值。
static bool isI32(Value *value) {
    auto *type = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return type && type->num_bits_ == 32;
}

// 将无符号位模式按二补码解释为有符号常量，避免宿主溢出参与推导。
static int signedBits(std::uint32_t bits) {
    std::int32_t value;
    static_assert(sizeof(value) == sizeof(bits), "i32 bit width mismatch");
    std::memcpy(&value, &bits, sizeof(value));
    return static_cast<int>(value);
}

// 向线性形式合并一项；相同 SSA 值的系数按模 2^32 累加。
static bool addLinearTerm(LinearForm &form, Value *value,
                          std::uint32_t coefficient) {
    if (coefficient == 0)
        return true;
    auto found = form.termIndex.find(value);
    if (found != form.termIndex.end()) {
        auto &term = form.terms[found->second];
        term.coefficient += coefficient;
        return true;
    }
    if (form.terms.size() >= kMaxLinearTerms)
        return false;
    form.termIndex[value] = form.terms.size();
    form.terms.push_back({value, coefficient});
    return true;
}

// 递归展开 add/sub/常量乘法，收集“系数 * 叶子 + 常量”的规范表示。
static bool collectLinear(Value *value, std::uint32_t scale, BasicBlock *bb,
                          unsigned depth, LinearForm &form) {
    if (++form.visits > kMaxLinearVisits || depth > kMaxLinearDepth)
        return false;

    if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
        form.constant +=
            scale * static_cast<std::uint32_t>(constant->value_);
        return true;
    }

    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || binary->parent_ != bb || !isI32(binary))
        return addLinearTerm(form, value, scale);

    switch (binary->op_id_) {
    case Instruction::Add:
        form.expanded.insert(binary);
        return collectLinear(binary->get_operand(0), scale, bb, depth + 1,
                             form) &&
               collectLinear(binary->get_operand(1), scale, bb, depth + 1,
                             form);
    case Instruction::Sub:
        form.expanded.insert(binary);
        return collectLinear(binary->get_operand(0), scale, bb, depth + 1,
                             form) &&
               collectLinear(binary->get_operand(1), 0u - scale, bb,
                             depth + 1, form);
    case Instruction::Mul: {
        ConstantInt *constant =
            dynamic_cast<ConstantInt *>(binary->get_operand(1));
        Value *other = binary->get_operand(0);
        if (!constant) {
            constant = dynamic_cast<ConstantInt *>(binary->get_operand(0));
            other = binary->get_operand(1);
        }
        if (!constant)
            return addLinearTerm(form, value, scale);
        form.expanded.insert(binary);
        return collectLinear(
            other,
            scale * static_cast<std::uint32_t>(constant->value_), bb,
            depth + 1, form);
    }
    default:
        return addLinearTerm(form, value, scale);
    }
}

// 给原算术节点估算目标相关的相对代价。
static int arithmeticCost(BinaryInst *inst) {
    if (!inst)
        return 0;
    return inst->op_id_ == Instruction::Mul ? 2 : 1;
}

// 统计重建后能够因无其它使用而删除的原表达式成本。
static int removableCost(BinaryInst *root, const LinearForm &form) {
    std::unordered_map<BinaryInst *, bool> memo;
    std::unordered_set<BinaryInst *> active;
    std::function<bool(BinaryInst *)> isRemovable =
        [&](BinaryInst *inst) -> bool {
        if (inst == root)
            return true;
        auto known = memo.find(inst);
        if (known != memo.end())
            return known->second;
        if (!active.insert(inst).second)
            return false;

        bool removable = true;
        for (auto &use : inst->use_list_) {
            auto *user = dynamic_cast<BinaryInst *>(use.user_);
            if (!user || !form.expanded.count(user) || !isRemovable(user)) {
                removable = false;
                break;
            }
        }
        active.erase(inst);
        memo[inst] = removable;
        return removable;
    };

    int cost = 0;
    for (auto *inst : form.expanded)
        if (isRemovable(inst))
            cost += arithmeticCost(inst);
    return cost;
}

// 估算从线性形式重新生成乘法与加法树的成本。
static int rebuiltCost(const LinearForm &form) {
    int valueCount = form.constant != 0 ? 1 : 0;
    int cost = 0;
    bool hasNonNegativeSeed = form.constant != 0;

    for (const auto &term : form.terms) {
        if (term.coefficient == 0)
            continue;
        ++valueCount;
        if (term.coefficient != 1 &&
            term.coefficient != UINT32_MAX) {
            cost += 2;
            hasNonNegativeSeed = true;
        } else if (term.coefficient == 1) {
            hasNonNegativeSeed = true;
        }
    }

    if (valueCount == 0)
        return 0;
    cost += valueCount - 1;
    if (!hasNonNegativeSeed)
        ++cost;
    return cost;
}

// 在 root 前按规范顺序生成线性表达式，并保留 i32 环绕语义。
static Value *buildLinear(const LinearForm &form, BinaryInst *root) {
    BasicBlock *bb = root->parent_;
    Type *type = root->type_;
    std::vector<Value *> positive;
    std::vector<Value *> negative;

    for (const auto &term : form.terms) {
        std::uint32_t coefficient = term.coefficient;
        if (coefficient == 0)
            continue;
        if (coefficient == UINT32_MAX) {
            negative.push_back(term.value);
            continue;
        }
        if (coefficient == 1) {
            positive.push_back(term.value);
            continue;
        }
        auto *scaled = Reassociate::createBinary(
            Instruction::Mul, term.value,
            new ConstantInt(type, signedBits(coefficient)), bb, root);
        positive.push_back(scaled);
    }

    Value *result = nullptr;
    if (form.constant != 0)
        result = new ConstantInt(type, signedBits(form.constant));

    for (Value *term : positive) {
        if (!result) {
            result = term;
            continue;
        }
        result = Reassociate::createBinary(Instruction::Add, result, term,
                                           bb, root);
    }
    for (Value *term : negative) {
        if (!result)
            result = new ConstantInt(type, 0);
        result = Reassociate::createBinary(Instruction::Sub, result, term,
                                           bb, root);
    }
    if (!result)
        result = new ConstantInt(type, 0);
    return result;
}

// 仅当线性重建严格降低成本时替换原根节点。
static bool tryReassociateLinear(BinaryInst *root) {
    if (!root || !root->parent_ || !isI32(root) ||
        (root->op_id_ != Instruction::Add &&
         root->op_id_ != Instruction::Sub))
        return false;

    LinearForm form;
    if (!collectLinear(root, 1, root->parent_, 0, form))
        return false;

    form.terms.erase(
        std::remove_if(form.terms.begin(), form.terms.end(),
                       [](const LinearTerm &term) {
                           return term.coefficient == 0;
                       }),
        form.terms.end());

    int oldCost = removableCost(root, form);
    int newCost = rebuiltCost(form);
    if (std::getenv("DEBUG_REASSOCIATE_LINEAR")) {
        std::cerr << "[ReassociateLinear] " << root->print()
                  << " old=" << oldCost << " new=" << newCost
                  << " terms=" << form.terms.size() << "\n";
    }
    if (newCost >= oldCost)
        return false;

    Value *replacement = buildLinear(form, root);
    root->replace_all_use_with(replacement);
    root->parent_->delete_instr(root);
    return true;
}

} // namespace

// 模块入口：逐函数完成 rank 计算和表达式重排。
void Reassociate::execute(Module *module) {
    changed_ = false;
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

PreservedAnalyses Reassociate::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    execute(module);
    return changed_ ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

// 先建立值 rank，再访问每条可结合二元指令并尝试局部重建。
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

// 返回值的稳定排序等级；常量最低，指令依据所在块与依赖层级排序。
int Reassociate::getRank(Value *v) {
    if (dynamic_cast<Constant*>(v)) return 0;
    if (dynamic_cast<Argument*>(v)) return valueRank_[v];
    auto it = valueRank_.find(v);
    if (it != valueRank_.end()) return it->second;
    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst) return 0;
    int maxRank = bbRank_[inst->parent_];
    int r = 0;
    for (unsigned i = 0; i < inst->num_ops() && r != maxRank; i++)
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
    changed_ = true;
}

// 在指定位置创建同类型二元指令，供重建树复用统一插入逻辑。
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
    // Only traverse single-use, same-opcode chains
    if (!bin || bin->op_id_ != op || bin->use_list_.size() != 1) {
        ops.push_back(root);
        return;
    }
    // Guard against cycles (should not happen in valid SSA, but be safe)
    if (!visited.insert(root).second) return;
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

// 将已排序叶子按较平衡的形态重新组合，缩短依赖链。
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

    return nullptr;
}

// -----------------------------------------------------------------------
// reassociate: main entry for a binary instruction
// -----------------------------------------------------------------------
void Reassociate::reassociate(BinaryInst *inst) {
    // The scalar tree builder materializes coefficients as ConstantInt.  A
    // vector coefficient must instead be a ConstantVector splat; until that
    // variant is profitable, leave vector algebra to InstCombine/SLP.
    if (inst->type_->tid_ == Type::VectorTyID)
        return;
    if (tryReassociateLinear(inst)) {
        changed_ = true;
        return;
    }

    Instruction::OpID op = inst->op_id_;
    if (op != Instruction::Add && op != Instruction::Mul) return;

    // Canonicalize: constants on RHS
    Value *lhs = inst->get_operand(0);
    Value *rhs = inst->get_operand(1);

    if (dynamic_cast<Constant*>(lhs) && !dynamic_cast<Constant*>(rhs))
        swapOperands(inst);

    // ---- Flatten ADD tree, optimize, rebuild ----
    if (op != Instruction::Add) return;

    BasicBlock *curBB = inst->parent_;
    std::vector<Value*> leafOps;
    std::unordered_set<Value*> visited;

    collectLeafOperands(inst->get_operand(0), op, leafOps, visited);
    collectLeafOperands(inst->get_operand(1), op, leafOps, visited);

    for (auto *v : leafOps) {
        auto *vi = dynamic_cast<Instruction*>(v);
        if (vi && vi->parent_ && vi->parent_ != curBB) return;
    }

    if (leafOps.size() == 1) {
        inst->replace_all_use_with(leafOps[0]);
        inst->parent_->delete_instr(inst);
        changed_ = true;
        return;
    }

    // Step 2: sort by rank to group identical terms for add-to-mul
    std::vector<ValueEntry> ops;
    for (auto *v : leafOps) ops.push_back({v, getRank(v)});
    std::stable_sort(ops.begin(), ops.end(),
        [](const ValueEntry &a, const ValueEntry &b) { return a.rank > b.rank; });

    // Add-to-mul: X+X+X → 3*X
    Value *optResult = nullptr;
    bool optimized = false;
    for (size_t i = 0; i + 1 < ops.size(); ) {
        Value *curr = ops[i].operand;
        int freq = 1;
        size_t j = i + 1;
        while (j < ops.size() && ops[j].operand == curr) { freq++; j++; }
        if (freq > 1) {
            ops.erase(ops.begin() + i, ops.begin() + j);
            auto *mul = createBinary(Instruction::Mul, curr,
                new ConstantInt(inst->type_, freq), curBB, inst);
            optimized = true;
            if (ops.empty()) { optResult = mul; break; }
            ops.insert(ops.begin(), {mul, getRank(mul)});
        } else {
            i++;
        }
    }

    if (optResult) {
        inst->replace_all_use_with(optResult);
        inst->parent_->delete_instr(inst);
        changed_ = true;
        return;
    }

    // Step 3: Factor out common term (A*B + A*C → A*(B+C))
    {
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
                Value *reduced = removeFactor(ops[i].operand, bestFactor);
                if (reduced) {
                    for (size_t j = ops.size(); j > i; ) {
                        j--;
                        if (ops[j].operand == ops[i].operand) {
                            innerAddOps.push_back(reduced);
                            ops.erase(ops.begin() + j);
                        }
                    }
                } else {
                    i++;
                }
            }

            if (!innerAddOps.empty()) {
                Value *addTree = rebuildAddTree(innerAddOps, curBB, inst);
                auto *factorMul = createBinary(Instruction::Mul, addTree, bestFactor, curBB, inst);
                optimized = true;
                if (ops.empty()) { optResult = factorMul; }
                else { ops.insert(ops.begin(), {factorMul, getRank(factorMul)}); }
            }
        }
    }

    if (optResult) {
        inst->replace_all_use_with(optResult);
        inst->parent_->delete_instr(inst);
        changed_ = true;
        return;
    }

    // Only rebuild if we actually optimized something; otherwise leave original alone
    if (!optimized) return;

    std::vector<Value*> sortedOps;
    for (auto &e : ops) sortedOps.push_back(e.operand);
    Value *newTree = rebuildTree(op, sortedOps, curBB, inst);
    if (newTree && newTree != inst) {
        inst->replace_all_use_with(newTree);
        inst->parent_->delete_instr(inst);
        changed_ = true;
    }
}
