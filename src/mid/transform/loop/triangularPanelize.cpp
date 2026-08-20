/**
 * @file triangularPanelize.cpp
 * @brief 三角分块（Panel 化）：把已证明合法的三角标量递推分块为多向量面板，保持前缀归约顺序。
 * @details 面板内相邻 j 值向量化，而 k 前缀归约保持顺序；不满足二维 i32 基址或最小规模时放弃。
 */

// This pass converts a proved triangular scalar recurrence into a multi-vector
// panel.  The prefix reduction stays ordered in k while executing adjacent,
// independent j values in vectors; intra-panel dependences remain scalar.

#include "../../../include/mid/opt/triangularPanelize.hpp"

#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

constexpr int kVectorWidth = 4;
constexpr int kPanelWidth = 12;
constexpr int kVectorsPerPanel = kPanelWidth / kVectorWidth;
constexpr unsigned kMinimumRowExtent = 64;

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_TRIANGULAR_PANELIZE") != nullptr;
}

/**
 * @brief 描述可分块执行的三角循环及其行、列索引关系。
 */
struct Pattern {
    Loop *outer = nullptr;   ///< 包含目标内层三角循环的外层循环。
    PhiInst *j = nullptr;    ///< 目标循环的列方向归纳变量。
    Value *row = nullptr;    ///< 决定当前三角行有效列数的行索引。
    Value *base = nullptr;   ///< 面板化内存访问所使用的根地址。
};

/**
 * @brief 旁路最多若干层只有一个入值的转发 PHI。
 * @param value 待剥离转发层的值。
 * @return 第一个不是单入值 PHI 的实际值。
 */
Value *stripSingleIncomingPhi(Value *value) {
    for (int depth = 0; depth < 8; ++depth) {
        auto *phi = dynamic_cast<PhiInst *>(value);
        if (!phi || phi->num_ops() != 2) break;
        value = phi->get_operand(0);
    }
    return value;
}

/**
 * @brief 判断 isSubOne 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param base 参数 `base`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isSubOne(Value *value, Value *base) {
    auto *sub = dynamic_cast<BinaryInst *>(value);
    auto *one = sub && sub->is_sub()
                    ? dynamic_cast<ConstantInt *>(sub->get_operand(1))
                    : nullptr;
    return sub && sub->get_operand(0) == base && one && one->value_ == 1;
}

/**
 * @brief 判断 isTwoDimensionalI32Base 所描述的结构、合法性或安全条件是否成立。
 * @param base 参数 `base`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isTwoDimensionalI32Base(Value *base) {
    auto *pointer = base ? dynamic_cast<PointerType *>(base->type_) : nullptr;
    auto *row = pointer ? dynamic_cast<ArrayType *>(pointer->contained_)
                        : nullptr;
    return row && row->num_elements_ >= kMinimumRowExtent &&
           row->contained_->tid_ == Type::IntegerTyID &&
           static_cast<IntegerType *>(row->contained_)->num_bits_ == 32;
}

/**
 * @brief 匹配 esGEP 所描述的 IR 结构并提取结果。
 * @param gep 参数 `gep`，用于本函数的分析、匹配或 IR 构造。
 * @param base 参数 `base`，用于本函数的分析、匹配或 IR 构造。
 * @param first 参数 `first`，用于本函数的分析、匹配或 IR 构造。
 * @param second 参数 `second`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchesGEP(GetElementPtrInst *gep, Value *base, Value *first,
                Value *second) {
    return gep && gep->num_ops() == 3 && gep->get_operand(0) == base &&
           gep->get_operand(1) == first && gep->get_operand(2) == second;
}

/**
 * @brief 匹配 Pattern 所描述的 IR 结构并提取结果。
 * @param outer 参数 `outer`，用于本函数的分析、匹配或 IR 构造。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchPattern(Loop *outer, Pattern &pattern) {
    // 匹配严格限制为两层三角计数循环和单一 i32 归约，随后核对二维数组访问形状。
    // Pattern 只保存经验证的定义和边界，真正的向量面板 CFG 由 applyPattern 创建。
    if (!outer || outer->children.size() != 1 || !outer->preheader ||
        !outer->canonicalIV || !outer->singleLatch() ||
        !outer->singleExit())
        return false;
    Loop *inner = outer->children.front();
    if (!inner || !inner->children.empty() || !inner->preheader ||
        !inner->canonicalIV || !inner->singleLatch() ||
        !inner->singleExit() || inner->tripCount != outer->canonicalIV)
        return false;

    auto *outerBranch = dynamic_cast<BranchInst *>(
        outer->header->get_terminator());
    auto *innerBranch = dynamic_cast<BranchInst *>(
        inner->header->get_terminator());
    if (!outerBranch || outerBranch->num_ops() != 3 || !innerBranch ||
        innerBranch->num_ops() != 3)
        return false;

    PhiInst *sum = nullptr;
    for (auto *inst : inner->header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == inner->canonicalIV) continue;
        if (sum || phi->type_->tid_ != Type::IntegerTyID ||
            static_cast<IntegerType *>(phi->type_)->num_bits_ != 32)
            return false;
        sum = phi;
    }
    if (!sum) return false;

    for (auto *inst : outer->header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst != outer->canonicalIV) return false;
    }

    Value *sumInit = nullptr;
    Value *sumUpdate = nullptr;
    for (unsigned i = 0; i + 1 < sum->num_ops(); i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(sum->get_operand(i + 1));
        if (!pred) return false;
        if (inner->blocks.count(pred)) {
            if (sumUpdate) return false;
            sumUpdate = sum->get_operand(i);
        } else {
            if (sumInit) return false;
            sumInit = sum->get_operand(i);
        }
    }
    auto *initialLoad = dynamic_cast<LoadInst *>(sumInit);
    auto *initialGEP = initialLoad
                           ? dynamic_cast<GetElementPtrInst *>(
                                 initialLoad->get_operand(0))
                           : nullptr;
    auto *sub = dynamic_cast<BinaryInst *>(sumUpdate);
    auto *mul = sub && sub->is_sub() && sub->get_operand(0) == sum
                    ? dynamic_cast<BinaryInst *>(sub->get_operand(1))
                    : nullptr;
    if (!initialGEP || !mul || !mul->is_mul()) return false;

    auto *load0 = dynamic_cast<LoadInst *>(mul->get_operand(0));
    auto *load1 = dynamic_cast<LoadInst *>(mul->get_operand(1));
    if (!load0 || !load1) return false;
    auto *gep0 = dynamic_cast<GetElementPtrInst *>(load0->get_operand(0));
    auto *gep1 = dynamic_cast<GetElementPtrInst *>(load1->get_operand(0));

    Value *j = outer->canonicalIV;
    Value *k = inner->canonicalIV;
    Value *row = outer->tripCount;
    Value *base = initialGEP->get_operand(0);
    if (!isTwoDimensionalI32Base(base) ||
        !matchesGEP(initialGEP, base, row, j))
        return false;

    auto rowByK = [&](GetElementPtrInst *gep) {
        return matchesGEP(gep, base, row, k);
    };
    auto kByPreviousJ = [&](GetElementPtrInst *gep) {
        return gep && gep->num_ops() == 3 && gep->get_operand(0) == base &&
               gep->get_operand(1) == k &&
               isSubOne(gep->get_operand(2), j);
    };
    if (!((rowByK(gep0) && kByPreviousJ(gep1)) ||
          (rowByK(gep1) && kByPreviousJ(gep0))))
        return false;

    StoreInst *resultStore = nullptr;
    int loadCount = 0;
    int storeCount = 0;
    for (auto *block : outer->blocksOrdered) {
        for (auto *inst : block->instr_list_) {
            if (inst->is_call()) return false;
            if (inst->is_load()) ++loadCount;
            if (inst->is_store()) {
                ++storeCount;
                resultStore = static_cast<StoreInst *>(inst);
            }
        }
    }
    if (loadCount != 4 || storeCount != 1 || !resultStore ||
        resultStore->get_operand(1) != initialGEP)
        return false;

    auto *divide = dynamic_cast<BinaryInst *>(resultStore->get_operand(0));
    if (!divide || !divide->is_div() ||
        stripSingleIncomingPhi(divide->get_operand(0)) != sum)
        return false;
    auto *diagonalLoad = dynamic_cast<LoadInst *>(divide->get_operand(1));
    auto *diagonalGEP = diagonalLoad
                            ? dynamic_cast<GetElementPtrInst *>(
                                  diagonalLoad->get_operand(0))
                            : nullptr;
    if (!matchesGEP(diagonalGEP, base, j, j)) return false;

    pattern = {outer, outer->canonicalIV, row, base};
    return true;
}

/**
 * @brief 实现 redirectEdge 对应的局部分析或变换辅助逻辑。
 * @param from 参数 `from`，用于本函数的分析、匹配或 IR 构造。
 * @param oldTarget 需要替换的原分支目标。
 * @param newTarget 替换后的新分支目标。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void redirectEdge(BasicBlock *from, BasicBlock *oldTarget,
                  BasicBlock *newTarget) {
    auto *term = from ? from->get_terminator() : nullptr;
    if (!term || !term->is_br()) return;
    for (unsigned i = 0; i < term->num_ops(); ++i)
        if (term->get_operand(i) == oldTarget)
            term->set_operand(i, newTarget);
    from->remove_succ_basic_block(oldTarget);
    oldTarget->remove_pre_basic_block(from);
    from->add_succ_basic_block(newTarget);
    newTarget->add_pre_basic_block(from);
}

/**
 * @brief 生成 base 加常量偏移的列索引。
 * @param module 所属模块。
 * @param base 基础索引值。
 * @param offset 常量偏移；为零时不生成指令。
 * @param block 指令插入基本块。
 * @return 偏移后的索引值。
 */
Value *addOffset(Module *module, Value *base, int offset,
                 BasicBlock *block) {
    if (offset == 0) return base;
    auto *constant = new ConstantInt(module->int32_ty_, offset);
    return new BinaryInst(module->int32_ty_, Instruction::Add,
                          base, constant, block);
}

/**
 * @brief 构造二维矩阵指定行列元素的地址。
 * @param base 矩阵基址。
 * @param row 行索引。
 * @param column 列索引。
 * @param block 指令插入基本块。
 * @return 新建的二维 GEP 指令。
 */
GetElementPtrInst *matrixAddress(Value *base, Value *row, Value *column,
                                 BasicBlock *block) {
    return new GetElementPtrInst(base, {row, column}, block);
}

/**
 * @brief 将标量广播到固定宽度向量的所有 lane。
 * @param module 所属模块。
 * @param scalar 待广播的标量值。
 * @param block 指令插入基本块。
 * @return 由 insertelement 链构成的广播向量。
 */
Value *emitSplat(Module *module, Value *scalar, BasicBlock *block) {
    auto *vectorType = module->get_vector_type(module->int32_ty_,
                                                kVectorWidth);
    Value *vector = new ConstantZero(vectorType);
    for (int lane = 0; lane < kVectorWidth; ++lane) {
        auto *index = new ConstantInt(module->int32_ty_, lane);
        vector = new InsertElementInst(vector, scalar, index, block);
    }
    return vector;
}

/**
 * @brief 原地执行 applyPattern 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @param blockCounter 参数 `blockCounter`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyPattern(const Pattern &pattern, int &blockCounter) {
    // 创建面板主循环、多个向量 lane 和标量余数路径；k 方向归约仍保持串行顺序。
    // 新 CFG 完成后再把原循环入口切换过来，并在出口恢复最终标量累加值。
    Loop *outer = pattern.outer;
    Function *function = outer->header->parent_;
    Module *module = function->parent_;
    BasicBlock *preheader = outer->preheader;
    BasicBlock *originalHeader = outer->header;
    auto *preTerm = preheader ? preheader->get_terminator() : nullptr;
    if (!preTerm || preTerm->num_ops() != 1 ||
        preTerm->get_operand(0) != originalHeader)
        return false;

    int incomingIndex = -1;
    for (unsigned i = 0; i + 1 < pattern.j->num_ops(); i += 2)
        if (pattern.j->get_operand(i + 1) == preheader)
            incomingIndex = static_cast<int>(i);
    if (incomingIndex < 0) return false;

    auto block = [&](const char *suffix) {
        return new BasicBlock(module,
                              "tri.panel." + std::string(suffix) + "." +
                                  std::to_string(blockCounter++),
                              function);
    };
    BasicBlock *panelHeader = block("header");
    BasicBlock *panelInit = block("init");
    BasicBlock *prefixHeader = block("prefix.header");
    BasicBlock *prefixBody = block("prefix.body");
    BasicBlock *finalize = block("finalize");
    BasicBlock *panelLatch = block("latch");
    BasicBlock *scalarEntry = block("scalar.entry");

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    auto *panelStep = new ConstantInt(module->int32_ty_, kPanelWidth);
    auto *lastLaneOffset =
        new ConstantInt(module->int32_ty_, kPanelWidth - 1);
    auto *vectorType = module->get_vector_type(module->int32_ty_,
                                                kVectorWidth);
    auto *vectorPointerType = module->get_pointer_type(vectorType);

    auto *panelJ = PhiInst::create_phi(module->int32_ty_, panelHeader);
    panelJ->add_phi_pair_operand(zero, preheader);
    panelHeader->add_instruction_front(panelJ);
    auto *lastColumn = new BinaryInst(module->int32_ty_, Instruction::Add,
                                      panelJ, lastLaneOffset, panelHeader);
    auto *hasPanel = new ICmpInst(ICmpInst::ICMP_SLT, lastColumn,
                                  pattern.row, panelHeader);
    new BranchInst(hasPanel, panelInit, scalarEntry, panelHeader);

    // 每个 panel 一次加载多个相邻向量作为累加初值，列方向连续访问保持单位步长。
    std::array<Value *, kVectorsPerPanel> initialVectors{};
    for (int part = 0; part < kVectorsPerPanel; ++part) {
        Value *column = addOffset(module, panelJ, part * kVectorWidth,
                                  panelInit);
        auto *initialPointer = matrixAddress(pattern.base, pattern.row,
                                             column, panelInit);
        auto *vectorPointer = new Bitcast(Instruction::BitCast,
                                          initialPointer, vectorPointerType,
                                          panelInit);
        initialVectors[part] = new LoadInst(vectorPointer, panelInit);
    }
    new BranchInst(prefixHeader, panelInit);

    auto *prefixK = PhiInst::create_phi(module->int32_ty_, prefixHeader);
    prefixK->add_phi_pair_operand(zero, panelInit);
    std::array<PhiInst *, kVectorsPerPanel> accumulators{};
    for (int part = kVectorsPerPanel - 1; part >= 0; --part) {
        accumulators[part] = PhiInst::create_phi(vectorType, prefixHeader);
        accumulators[part]->add_phi_pair_operand(initialVectors[part],
                                                 panelInit);
        prefixHeader->add_instruction_front(accumulators[part]);
    }
    prefixHeader->add_instruction_front(prefixK);
    auto *hasPrefix = new ICmpInst(ICmpInst::ICMP_SLT, prefixK, panelJ,
                                   prefixHeader);
    new BranchInst(hasPrefix, prefixBody, finalize, prefixHeader);

    // k 方向仍逐项执行前缀归约，但同一个 rowFactor 会广播到 panel 的所有列，
    // 从而并行更新多个向量累加器而不改变归约次序。
    auto *rowFactorPointer = matrixAddress(pattern.base, pattern.row,
                                           prefixK, prefixBody);
    auto *rowFactor = new LoadInst(rowFactorPointer, prefixBody);
    Value *broadcast = emitSplat(module, rowFactor, prefixBody);
    auto *previousColumn = new BinaryInst(module->int32_ty_,
                                          Instruction::Sub, panelJ, one,
                                          prefixBody);
    std::array<Value *, kVectorsPerPanel> nextAccumulators{};
    for (int part = 0; part < kVectorsPerPanel; ++part) {
        Value *coefficientColumn = addOffset(
            module, previousColumn, part * kVectorWidth, prefixBody);
        auto *coefficientPointer = matrixAddress(
            pattern.base, prefixK, coefficientColumn, prefixBody);
        auto *coefficientVectorPointer = new Bitcast(
            Instruction::BitCast, coefficientPointer, vectorPointerType,
            prefixBody);
        auto *coefficients = new LoadInst(coefficientVectorPointer,
                                          prefixBody);
        auto *product = new BinaryInst(vectorType, Instruction::Mul,
                                       broadcast, coefficients, prefixBody);
        nextAccumulators[part] = new BinaryInst(
            vectorType, Instruction::Sub, accumulators[part], product,
            prefixBody);
    }
    auto *nextK = new BinaryInst(module->int32_ty_, Instruction::Add,
                                 prefixK, one, prefixBody);
    prefixK->add_phi_pair_operand(nextK, prefixBody);
    for (int part = 0; part < kVectorsPerPanel; ++part)
        accumulators[part]->add_phi_pair_operand(nextAccumulators[part],
                                                 prefixBody);
    new BranchInst(prefixHeader, prefixBody);

    // 向量前缀只包含 panel 之前的贡献。先抽取各 lane，再按列顺序补上 panel 内部
    // 的三角依赖，保证后面的列能看到前面列刚计算出的结果。
    std::array<Value *, kPanelWidth> external{};
    for (int lane = 0; lane < kPanelWidth; ++lane) {
        auto *index = new ConstantInt(module->int32_ty_,
                                      lane % kVectorWidth);
        external[lane] = new ExtractElementInst(
            accumulators[lane / kVectorWidth], index, finalize);
    }

    for (int lane = 0; lane < kPanelWidth; ++lane) {
        Value *value = external[lane];
        Value *column = addOffset(module, panelJ, lane, finalize);
        for (int internal = 0; internal < lane; ++internal) {
            Value *innerK = addOffset(module, panelJ, internal, finalize);
            auto *factorPointer = matrixAddress(pattern.base, pattern.row,
                                                innerK, finalize);
            auto *factor = new LoadInst(factorPointer, finalize);
            Value *coefficientColumn =
                addOffset(module, column, -1, finalize);
            auto *coefficientPointer = matrixAddress(
                pattern.base, innerK, coefficientColumn, finalize);
            auto *coefficient = new LoadInst(coefficientPointer, finalize);
            auto *internalProduct = new BinaryInst(
                module->int32_ty_, Instruction::Mul, factor, coefficient,
                finalize);
            value = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   value, internalProduct, finalize);
        }
        auto *diagonalPointer = matrixAddress(pattern.base, column, column,
                                              finalize);
        auto *diagonal = new LoadInst(diagonalPointer, finalize);
        auto *solved = new BinaryInst(module->int32_ty_, Instruction::SDiv,
                                      value, diagonal, finalize);
        auto *destination = matrixAddress(pattern.base, pattern.row, column,
                                          finalize);
        new StoreInst(solved, destination, finalize);
    }
    new BranchInst(panelLatch, finalize);

    auto *nextPanel = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     panelJ, panelStep, panelLatch);
    panelJ->add_phi_pair_operand(nextPanel, panelLatch);
    new BranchInst(panelHeader, panelLatch);

    new BranchInst(originalHeader, scalarEntry);

    // 新 CFG 构造完成后才提交入口切换，并让原标量循环从 panelJ 开始处理余数列。
    pattern.j->set_operand(incomingIndex, panelJ);
    pattern.j->set_operand(incomingIndex + 1, scalarEntry);
    redirectEdge(preheader, originalHeader, panelHeader);
    function->set_instr_name();
    return true;
}

} // namespace

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool TriangularPanelize::runOnFunction(Function *function) {
    bool changed = false;
    int blockCounter = 0;
    for (int iteration = 0; iteration < 8; ++iteration) {
        LoopInfo loopInfo;
        loopInfo.analyze(function);
        Pattern pattern;
        bool applied = false;
        for (const auto &ownedLoop : loopInfo.allLoops()) {
            Loop *loop = ownedLoop.get();
            if (!matchPattern(loop, pattern)) continue;
            if (applyPattern(pattern, blockCounter)) {
                applied = true;
                changed = true;
                if (debugEnabled())
                    std::cerr << "[TriangularPanelize] transformed func="
                              << function->name_ << "\n";
            }
            break;
        }
        if (!applied) break;
    }
    return changed;
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void TriangularPanelize::execute(Module *module) {
    for (auto *function : module->function_list_)
        if (!function->is_declaration()) runOnFunction(function);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses TriangularPanelize::execute(Module *module,
                                              AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *function : module->function_list_)
        if (!function->is_declaration()) changed |= runOnFunction(function);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
