/**
 * @file triangularRemapSourceCompose.cpp
 * @brief 三角重映射源组合：按需合成有序三角拷贝的数据来源，改写消费者载入以绕过可消除的中间副本。
 * @details 沿有序拷贝链追踪元素来源，只版本化已匹配的消费者载入，并为无法证明的路径保留原读取。
 */

// Demand-driven lowering for a proven sequence of ordered triangular copies.
// The original consumer is preserved; only its matched array load is versioned.

#include "../../../include/mid/opt/triangularRemapSourceCompose.hpp"

#include "../../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace {

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_TRIANGULAR_REMAP_SOURCE") != nullptr;
}

/**
 * @brief 旁路连续 bitcast，取得未转换的源值。
 * @param value 待追溯的值。
 * @return 最内层非 bitcast 值。
 */
Value *stripBitcast(Value *value) {
    while (auto *bitcast = dynamic_cast<Bitcast *>(value))
        value = bitcast->get_operand(0);
    return value;
}

/**
 * @brief 穿过 bitcast 和 GEP 链取得指针的根对象。
 * @param value 待追溯的指针值。
 * @return 最外层根对象。
 */
Value *pointerRoot(Value *value) {
    value = stripBitcast(value);
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(value))
        value = stripBitcast(gep->get_operand(0));
    return value;
}

/**
 * @brief 判断 isI32 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isI32(Value *value, Module *module) {
    return value && value->type_ == module->int32_ty_;
}

/**
 * @brief 判断 isConstant 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param expected 参数 `expected`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isConstant(Value *value, int expected) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == expected;
}

/**
 * @brief 匹配 AddOne 所描述的 IR 结构并提取结果。
 * @param value 待检查、映射或物化的 IR 值。
 * @param base 参数 `base`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchAddOne(Value *value, Value *base) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;
    return (add->get_operand(0) == base && isConstant(add->get_operand(1), 1)) ||
           (add->get_operand(1) == base && isConstant(add->get_operand(0), 1));
}

/**
 * @brief 匹配 ScaledPlus 所描述的 IR 结构并提取结果。
 * @param value 待检查、映射或物化的 IR 值。
 * @param scaled 参数 `scaled`，用于本函数的分析、匹配或 IR 构造。
 * @param unit 参数 `unit`，用于本函数的分析、匹配或 IR 构造。
 * @param scaleOut 参数 `scaleOut`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchScaledPlus(Value *value, Value *scaled, Value *unit,
                     Value **scaleOut) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;

    Value *productValue = nullptr;
    if (add->get_operand(0) == unit)
        productValue = add->get_operand(1);
    else if (add->get_operand(1) == unit)
        productValue = add->get_operand(0);
    else
        return false;

    auto *product = dynamic_cast<BinaryInst *>(productValue);
    if (!product || !product->is_mul())
        return false;
    if (product->get_operand(0) == scaled)
        *scaleOut = product->get_operand(1);
    else if (product->get_operand(1) == scaled)
        *scaleOut = product->get_operand(0);
    else
        return false;
    return true;
}

/**
 * @brief 匹配 TriangularBound 所描述的 IR 结构并提取结果。
 * @param bound 参数 `bound`，用于本函数的分析、匹配或 IR 构造。
 * @param outerIV 参数 `outerIV`，用于本函数的分析、匹配或 IR 构造。
 * @param rowsize 参数 `rowsize`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchTriangularBound(Value *bound, Value *outerIV, Value *rowsize) {
    auto *select = dynamic_cast<SelectInst *>(bound);
    if (!select)
        return false;

    Value *next = select->get_operand(1);
    Value *other = select->get_operand(2);
    if (!matchAddOne(next, outerIV) || other != rowsize)
        return false;

    auto *compare = dynamic_cast<ICmpInst *>(select->get_operand(0));
    if (!compare)
        return false;
    if (compare->icmp_op_ == ICmpInst::ICMP_SLT)
        return compare->get_operand(0) == next &&
               compare->get_operand(1) == rowsize;
    if (compare->icmp_op_ == ICmpInst::ICMP_SGT)
        return compare->get_operand(0) == rowsize &&
               compare->get_operand(1) == next;
    return false;
}

/**
 * @brief 描述一维平坦数组或二维数组中的单个元素地址计算。
 */
struct ElementAccess {
    GetElementPtrInst *gep = nullptr;  ///< 生成元素地址的 GEP 指令。
    Value *base = nullptr;             ///< GEP 的数组或平坦缓冲区基址。
    Value *index = nullptr;            ///< 去除零维索引后的实际元素下标。
    bool flat = true;                  ///< 是否使用单下标平坦指针形式。
};

/**
 * @brief 实现 parseElementAccess 对应的局部分析或变换辅助逻辑。
 * @param pointer 参数 `pointer`，用于本函数的分析、匹配或 IR 构造。
 * @param access 参数 `access`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool parseElementAccess(Value *pointer, ElementAccess &access) {
    auto *gep = dynamic_cast<GetElementPtrInst *>(stripBitcast(pointer));
    if (!gep)
        return false;
    auto *pointerType = dynamic_cast<PointerType *>(gep->get_operand(0)->type_);
    if (!pointerType)
        return false;

    access = ElementAccess{};
    access.gep = gep;
    access.base = gep->get_operand(0);
    if (pointerType->contained_->tid_ == Type::ArrayTyID) {
        if (gep->num_ops() != 3 || !isConstant(gep->get_operand(1), 0))
            return false;
        access.index = gep->get_operand(2);
        access.flat = false;
    } else {
        if (gep->num_ops() != 2)
            return false;
        access.index = gep->get_operand(1);
        access.flat = true;
    }
    return true;
}

/**
 * @brief 实现 globalArrayExtent 对应的局部分析或变换辅助逻辑。
 * @param root 参数 `root`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 返回计算、分析或构造得到的结果。
 */
int globalArrayExtent(Value *root, Module *module) {
    auto *global = dynamic_cast<GlobalVariable *>(root);
    auto *pointerType = global
                            ? dynamic_cast<PointerType *>(global->type_)
                            : nullptr;
    auto *arrayType = pointerType
                          ? dynamic_cast<ArrayType *>(pointerType->contained_)
                          : nullptr;
    if (!arrayType || arrayType->contained_ != module->int32_ty_ ||
        arrayType->num_elements_ >
            static_cast<unsigned>(std::numeric_limits<int>::max()))
        return -1;
    return static_cast<int>(arrayType->num_elements_);
}

/**
 * @brief 实现 valueAvailableAt 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param block 目标或待检查的基本块。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool valueAvailableAt(Value *value, BasicBlock *block,
                      const DominatorTreeAnalysis &DT) {
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return true;
    if (!instruction->parent_)
        return false;
    return instruction->parent_ == block ||
           DT.dominates(instruction->parent_, block);
}

/**
 * @brief 原地执行 removeTerminatorAndEdges 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param block 目标或待检查的基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void removeTerminatorAndEdges(BasicBlock *block) {
    auto *terminator = block ? block->get_terminator() : nullptr;
    if (!terminator)
        return;
    std::vector<BasicBlock *> successors = block->succ_bbs_;
    for (auto *successor : successors)
        successor->remove_pre_basic_block(block);
    block->succ_bbs_.clear();
    block->delete_instr(terminator);
}

/**
 * @brief 原地执行 retargetPhiPredecessor 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param block 目标或待检查的基本块。
 * @param oldPredecessor 参数 `oldPredecessor`，用于本函数的分析、匹配或 IR 构造。
 * @param newPredecessor 参数 `newPredecessor`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void retargetPhiPredecessor(BasicBlock *block, BasicBlock *oldPredecessor,
                            BasicBlock *newPredecessor) {
    for (auto *instruction : block->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        for (unsigned index = 1; index < phi->num_ops(); index += 2) {
            if (phi->get_operand(index) == oldPredecessor)
                phi->set_operand(index, newPredecessor);
        }
    }
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
    auto *branch = from
                       ? dynamic_cast<BranchInst *>(from->get_terminator())
                       : nullptr;
    if (!branch)
        return;
    for (unsigned index = 0; index < branch->num_ops(); ++index) {
        if (branch->get_operand(index) == oldTarget)
            branch->set_operand(index, newTarget);
    }
    from->remove_succ_basic_block(oldTarget);
    oldTarget->remove_pre_basic_block(from);
    from->add_succ_basic_block(newTarget);
    newTarget->add_pre_basic_block(from);
}

/**
 * @brief 判断 hasPhi 所描述的结构、合法性或安全条件是否成立。
 * @param block 目标或待检查的基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasPhi(BasicBlock *block) {
    return block && !block->instr_list_.empty() &&
           block->instr_list_.front()->is_phi();
}

/**
 * @brief 汇总三角矩阵重映射双层循环的控制变量和唯一数据搬运访问。
 */
struct RemapPattern {
    Loop *outer = nullptr;          ///< 重映射过程的外层循环。
    Loop *inner = nullptr;          ///< 执行单行元素搬运的内层循环。
    PhiInst *outerIV = nullptr;     ///< 外层行索引归纳变量。
    PhiInst *innerIV = nullptr;     ///< 内层列索引归纳变量。
    Value *rowsize = nullptr;       ///< 当前重映射行对应的有效长度。
    Value *colsize = nullptr;       ///< 原矩阵的列数或行跨度。
    Value *matrixBase = nullptr;    ///< 元素 GEP 直接使用的矩阵基址。
    Value *matrixRoot = nullptr;    ///< 穿过地址转换后用于别名分析的根对象。
    bool matrixFlat = true;         ///< 矩阵地址是否采用平坦单下标形式。
    LoadInst *load = nullptr;       ///< 从原位置读取元素的唯一 load。
    StoreInst *store = nullptr;     ///< 将元素写入重映射位置的唯一 store。
};

/**
 * @brief 判断 isNonTrappingStructuralInstruction 所描述的结构、合法性或安全条件是否成立。
 * @param instruction 待分析或改写的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isNonTrappingStructuralInstruction(Instruction *instruction) {
    if (!instruction)
        return false;
    if (instruction->is_phi() || instruction->is_br() ||
        dynamic_cast<ICmpInst *>(instruction) ||
        dynamic_cast<SelectInst *>(instruction) ||
        dynamic_cast<GetElementPtrInst *>(instruction) ||
        dynamic_cast<Bitcast *>(instruction))
        return true;
    auto *binary = dynamic_cast<BinaryInst *>(instruction);
    return binary &&
           (binary->is_add() || binary->is_sub() || binary->is_mul());
}

/**
 * @brief 匹配 Remap 所描述的 IR 结构并提取结果。
 * @param outer 参数 `outer`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchRemap(Loop *outer, Module *module, RemapPattern &pattern) {
    if (!outer || outer->children.size() != 1 || !outer->canonicalIV ||
        !outer->preheader || !outer->singleLatch() || !outer->singleExit())
        return false;
    Loop *inner = outer->children.front();
    if (!inner || !inner->children.empty() || !inner->canonicalIV ||
        !inner->preheader || !inner->singleLatch() || !inner->singleExit())
        return false;

    LoadInst *load = nullptr;
    StoreInst *store = nullptr;
    for (auto *block : inner->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_call())
                return false;
            if (instruction->is_load()) {
                if (load)
                    return false;
                load = static_cast<LoadInst *>(instruction);
            } else if (instruction->is_store()) {
                if (store)
                    return false;
                store = static_cast<StoreInst *>(instruction);
            }
        }
    }
    if (!load || !store || store->get_operand(0) != load)
        return false;

    ElementAccess readAccess;
    ElementAccess writeAccess;
    if (!parseElementAccess(load->get_operand(0), readAccess) ||
        !parseElementAccess(store->get_operand(1), writeAccess) ||
        readAccess.base != writeAccess.base ||
        readAccess.flat != writeAccess.flat)
        return false;

    Value *rowsize = nullptr;
    Value *colsize = nullptr;
    if (!matchScaledPlus(readAccess.index, outer->canonicalIV,
                         inner->canonicalIV, &rowsize) ||
        !matchScaledPlus(writeAccess.index, inner->canonicalIV,
                         outer->canonicalIV, &colsize) ||
        !isI32(rowsize, module) || !isI32(colsize, module))
        return false;

    if (outer->tripCount != colsize ||
        !matchTriangularBound(inner->tripCount, outer->canonicalIV, rowsize))
        return false;
    if (auto *instruction = dynamic_cast<Instruction *>(rowsize);
        instruction && outer->isInLoop(instruction))
        return false;
    if (auto *instruction = dynamic_cast<Instruction *>(colsize);
        instruction && outer->isInLoop(instruction))
        return false;

    for (auto *block : outer->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction == load || instruction == store)
                continue;
            if (!isNonTrappingStructuralInstruction(instruction))
                return false;
        }
    }

    pattern = RemapPattern{};
    pattern.outer = outer;
    pattern.inner = inner;
    pattern.outerIV = outer->canonicalIV;
    pattern.innerIV = inner->canonicalIV;
    pattern.rowsize = rowsize;
    pattern.colsize = colsize;
    pattern.matrixBase = readAccess.base;
    pattern.matrixRoot = pointerRoot(readAccess.base);
    pattern.matrixFlat = readAccess.flat;
    pattern.load = load;
    pattern.store = store;
    return pattern.matrixRoot != nullptr;
}

/**
 * @brief 判断 hasLiveOutValue 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasLiveOutValue(Loop *loop) {
    for (auto *block : loop->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            for (const Use &use : instruction->use_list_) {
                auto *user = use.user_;
                if (user && !loop->isInLoop(user))
                    return true;
            }
        }
    }
    return false;
}

/**
 * @brief 匹配 RowsizeLoad 所描述的 IR 结构并提取结果。
 * @param rowsize 参数 `rowsize`，用于本函数的分析、匹配或 IR 构造。
 * @param index 参数 `index`，用于本函数的分析、匹配或 IR 构造。
 * @param baseOut 参数 `baseOut`，用于本函数的分析、匹配或 IR 构造。
 * @param rootOut 参数 `rootOut`，用于本函数的分析、匹配或 IR 构造。
 * @param flatOut 参数 `flatOut`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchRowsizeLoad(Value *rowsize, PhiInst *index, Value **baseOut,
                      Value **rootOut, bool *flatOut) {
    auto *load = dynamic_cast<LoadInst *>(rowsize);
    ElementAccess access;
    if (!load || !parseElementAccess(load->get_operand(0), access) ||
        access.index != index)
        return false;
    *baseOut = access.base;
    *rootOut = pointerRoot(access.base);
    *flatOut = access.flat;
    return *rootOut != nullptr;
}

/**
 * @brief 匹配 ExtentDivision 所描述的 IR 结构并提取结果。
 * @param colsize 参数 `colsize`，用于本函数的分析、匹配或 IR 构造。
 * @param rowsize 参数 `rowsize`，用于本函数的分析、匹配或 IR 构造。
 * @param extentOut 参数 `extentOut`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchExtentDivision(Value *colsize, Value *rowsize, Value **extentOut) {
    auto *division = dynamic_cast<BinaryInst *>(colsize);
    if (!division || !division->is_div() ||
        division->get_operand(1) != rowsize)
        return false;
    *extentOut = division->get_operand(0);
    return true;
}

/**
 * @brief 实现 loopBodyHasOnlyRowsizeRead 对应的局部分析或变换辅助逻辑。
 * @param top 参数 `top`，用于本函数的分析、匹配或 IR 构造。
 * @param remap 参数 `remap`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool loopBodyHasOnlyRowsizeRead(Loop *top, const RemapPattern &remap) {
    for (auto *block : top->blocksOrdered) {
        if (remap.outer->isInLoop(block))
            continue;
        for (auto *instruction : block->instr_list_) {
            if (instruction == remap.rowsize ||
                instruction == remap.colsize)
                continue;
            if (!isNonTrappingStructuralInstruction(instruction))
                return false;
        }
    }
    return true;
}

/**
 * @brief 实现 simpleConsumerPreheader 对应的局部分析或变换辅助逻辑。
 * @param consumer 参数 `consumer`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool simpleConsumerPreheader(Loop *consumer) {
    BasicBlock *preheader = consumer ? consumer->preheader : nullptr;
    if (!preheader || hasPhi(preheader) || preheader->instr_list_.size() != 1)
        return false;
    auto *branch = dynamic_cast<BranchInst *>(preheader->get_terminator());
    return branch && branch->num_ops() == 1 &&
           branch->get_operand(0) == consumer->header;
}

/**
 * @brief 实现 callMayAccessMatrix 对应的局部分析或变换辅助逻辑。
 * @param call 参数 `call`，用于本函数的分析、匹配或 IR 构造。
 * @param matrixRoot 参数 `matrixRoot`，用于本函数的分析、匹配或 IR 构造。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool callMayAccessMatrix(CallInst *call, Value *matrixRoot,
                        const BasicAliasAnalysis &BAA) {
    if (!call)
        return true;
    auto *callee = dynamic_cast<Function *>(
        call->get_operand(call->num_ops() - 1));
    if (!callee)
        return true;
    if (!callee->is_declaration())
        return BAA.getCallModRef(call, matrixRoot) != ModRefInfo::NoModRef;

    for (unsigned index = 0; index + 1 < call->num_ops(); ++index) {
        Value *argument = call->get_operand(index);
        if (!dynamic_cast<PointerType *>(argument->type_))
            continue;
        if (BAA.alias(argument, matrixRoot) != AliasResult::NoAlias)
            return true;
    }
    // SysY 外部声明都是运行时接口；若调用没有任何可能别名到 matrixRoot 的指针参数，
    // 就没有途径访问源语言中的该数组对象。
    return false;
}

/**
 * @brief 实现 matrixUnobservedAfter 对应的局部分析或变换辅助逻辑。
 * @param start 参数 `start`，用于本函数的分析、匹配或 IR 构造。
 * @param matrixRoot 参数 `matrixRoot`，用于本函数的分析、匹配或 IR 构造。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matrixUnobservedAfter(BasicBlock *start, Value *matrixRoot,
                           const BasicAliasAnalysis &BAA) {
    std::queue<BasicBlock *> worklist;
    std::set<BasicBlock *> visited;
    worklist.push(start);
    while (!worklist.empty()) {
        BasicBlock *block = worklist.front();
        worklist.pop();
        if (!block || !visited.insert(block).second)
            continue;
        for (auto *instruction : block->instr_list_) {
            if ((instruction->is_load() || instruction->is_store()) &&
                BAA.getModRefInfo(instruction, matrixRoot) !=
                    ModRefInfo::NoModRef)
                return false;
            if (instruction->is_call() &&
                callMayAccessMatrix(static_cast<CallInst *>(instruction),
                                    matrixRoot, BAA))
                return false;
        }
        for (auto *successor : block->succ_bbs_)
            worklist.push(successor);
    }
    return true;
}

/**
 * @brief 描述紧随重映射之后、可直接改读源矩阵的单循环消费者。
 */
struct ConsumerPattern {
    Loop *loop = nullptr;               ///< 顺序遍历重映射结果的消费者循环。
    LoadInst *load = nullptr;           ///< 消费者中读取重映射矩阵的唯一 load。
    BasicBlock *bodyEntry = nullptr;     ///< 条件成立时进入的消费者循环体入口。
};

/**
 * @brief 匹配 Consumer 所描述的 IR 结构并提取结果。
 * @param loop 待检查或变换的循环。
 * @param length 参数 `length`，用于本函数的分析、匹配或 IR 构造。
 * @param matrixRoot 参数 `matrixRoot`，用于本函数的分析、匹配或 IR 构造。
 * @param fastOrigin 参数 `fastOrigin`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @param consumer 参数 `consumer`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchConsumer(Loop *loop, Value *length, Value *matrixRoot,
                   BasicBlock *fastOrigin, const DominatorTreeAnalysis &DT,
                   const BasicAliasAnalysis &BAA,
                   ConsumerPattern &consumer) {
    if (!loop || !loop->children.empty() || !loop->canonicalIV ||
        loop->tripCount != length || !loop->singleLatch() ||
        !loop->singleExit() || !simpleConsumerPreheader(loop))
        return false;

    auto *branch = dynamic_cast<BranchInst *>(loop->header->get_terminator());
    auto *compare = branch && branch->num_ops() == 3
                        ? dynamic_cast<ICmpInst *>(branch->get_operand(0))
                        : nullptr;
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        compare->get_operand(0) != loop->canonicalIV ||
        compare->get_operand(1) != length)
        return false;
    auto *bodyEntry = dynamic_cast<BasicBlock *>(branch->get_operand(1));
    auto *exit = dynamic_cast<BasicBlock *>(branch->get_operand(2));
    if (!bodyEntry || !loop->isInLoop(bodyEntry) || exit != loop->singleExit() ||
        hasPhi(bodyEntry))
        return false;

    LoadInst *matrixLoad = nullptr;
    for (auto *block : loop->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_call() || instruction->is_store())
                return false;
            if (!instruction->is_load())
                continue;
            if (BAA.getModRefInfo(instruction, matrixRoot) ==
                ModRefInfo::NoModRef)
                continue;
            if (matrixLoad)
                return false;
            ElementAccess access;
            if (!parseElementAccess(instruction->get_operand(0), access) ||
                pointerRoot(access.base) != matrixRoot ||
                access.index != loop->canonicalIV)
                return false;
            matrixLoad = static_cast<LoadInst *>(instruction);
        }
    }
    if (!matrixLoad || matrixLoad->parent_ == loop->header ||
        !DT.dominates(bodyEntry, matrixLoad->parent_))
        return false;

    for (auto *instruction : loop->header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        Value *initial = nullptr;
        for (unsigned index = 0; index + 1 < phi->num_ops(); index += 2) {
            if (phi->get_operand(index + 1) == loop->preheader) {
                initial = phi->get_operand(index);
                break;
            }
        }
        if (!initial || !valueAvailableAt(initial, fastOrigin, DT))
            return false;
    }

    consumer = ConsumerPattern{};
    consumer.loop = loop;
    consumer.load = matrixLoad;
    consumer.bodyEntry = bodyEntry;
    return true;
}

/**
 * @brief 描述“生成行长序列、重映射矩阵、顺序消费矩阵”的完整可组合模式。
 */
struct Pattern {
    Loop *sequence = nullptr;       ///< 生成每行长度信息的前置循环。
    RemapPattern remap;             ///< 中间矩阵重映射循环的匹配结果。
    ConsumerPattern consumer;       ///< 可改为直接读取源矩阵的后继消费者。
    Value *length = nullptr;        ///< 行长序列与消费者循环共用的元素总数。
    Value *extent = nullptr;        ///< 重映射矩阵可访问的总元素范围。
    Value *rowsizeBase = nullptr;   ///< 行长表元素 GEP 直接使用的基址。
    Value *rowsizeRoot = nullptr;   ///< 行长表用于别名分析的根对象。
    bool rowsizeFlat = true;        ///< 行长表是否采用平坦单下标形式。
    int rowsizeExtent = -1;         ///< 静态可知的行长表元素容量。
    int matrixExtent = -1;          ///< 静态可知的矩阵元素容量。
};

/**
 * @brief 匹配 Pattern 所描述的 IR 结构并提取结果。
 * @param function 待分析或改写的函数。
 * @param LI 参数 `LI`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchPattern(Function *function, LoopInfo &LI,
                  const DominatorTreeAnalysis &DT,
                  const BasicAliasAnalysis &BAA, Pattern &pattern) {
    // 沿“有序重映射序列 → 相邻消费者”匹配整条数据流，并证明中间矩阵之后不可观察。
    // 所有长度、extent、全局数组界限和支配条件确认后，才记录可版本化的消费者。
    Module *module = function->parent_;
    for (Loop *top : LI.topLevelLoops()) {
        if (!top || top->children.size() != 1 || !top->canonicalIV ||
            !top->preheader || !top->singleLatch() || !top->singleExit() ||
            hasLiveOutValue(top))
            continue;

        RemapPattern remap;
        if (!matchRemap(top->children.front(), module, remap) ||
            remap.outer->parent != top || !loopBodyHasOnlyRowsizeRead(top, remap))
            continue;

        Value *rowsizeBase = nullptr;
        Value *rowsizeRoot = nullptr;
        bool rowsizeFlat = true;
        if (!matchRowsizeLoad(remap.rowsize, top->canonicalIV, &rowsizeBase,
                              &rowsizeRoot, &rowsizeFlat))
            continue;
        Value *extent = nullptr;
        if (!matchExtentDivision(remap.colsize, remap.rowsize, &extent) ||
            !isI32(extent, module) || !isI32(top->tripCount, module) ||
            !valueAvailableAt(extent, top->preheader, DT) ||
            !valueAvailableAt(top->tripCount, top->preheader, DT) ||
            !valueAvailableAt(remap.matrixBase, top->preheader, DT))
            continue;

        auto *matrixGlobal = dynamic_cast<GlobalVariable *>(remap.matrixRoot);
        auto *rowsizeGlobal = dynamic_cast<GlobalVariable *>(rowsizeRoot);
        if (!matrixGlobal || !rowsizeGlobal || matrixGlobal == rowsizeGlobal)
            continue;
        int matrixExtent = globalArrayExtent(matrixGlobal, module);
        int rowsizeExtent = globalArrayExtent(rowsizeGlobal, module);
        if (matrixExtent < 0 || rowsizeExtent < 0)
            continue;

        BasicBlock *consumerPreheader = top->singleExit();
        ConsumerPattern matchedConsumer;
        Loop *consumerLoop = nullptr;
        for (Loop *candidate : LI.topLevelLoops()) {
            if (candidate == top || candidate->preheader != consumerPreheader)
                continue;
            if (!matchConsumer(candidate, top->tripCount, remap.matrixRoot,
                               top->preheader, DT, BAA, matchedConsumer))
                continue;
            consumerLoop = candidate;
            break;
        }
        if (!consumerLoop ||
            !matrixUnobservedAfter(consumerLoop->singleExit(),
                                   remap.matrixRoot, BAA))
            continue;

        pattern = Pattern{};
        pattern.sequence = top;
        pattern.remap = remap;
        pattern.consumer = std::move(matchedConsumer);
        pattern.length = top->tripCount;
        pattern.extent = extent;
        pattern.rowsizeBase = rowsizeBase;
        pattern.rowsizeRoot = rowsizeRoot;
        pattern.rowsizeFlat = rowsizeFlat;
        pattern.rowsizeExtent = rowsizeExtent;
        pattern.matrixExtent = matrixExtent;
        return true;
    }
    return false;
}

/**
 * @brief 按一维平坦或数组首维形式构造元素地址。
 * @param base 数组或平坦缓冲区基址。
 * @param index 元素索引。
 * @param flat 为 true 时使用单下标，否则使用 {0,index}。
 * @param block 指令插入基本块。
 * @return 新建的元素 GEP 指令。
 */
GetElementPtrInst *elementGEP(Value *base, Value *index, bool flat,
                              BasicBlock *block) {
    if (flat)
        return new GetElementPtrInst(base, {index}, block);
    auto *zero = new ConstantInt(block->parent_->parent_->int32_ty_, 0);
    return new GetElementPtrInst(base, {zero, index}, block);
}

/**
 * @brief 保存运行期边界守卫构造完成后可进入快速路径的基本块。
 */
struct GuardBlocks {
    BasicBlock *ready = nullptr;  ///< 所有安全检查通过后的快速路径前驱块。
};

/**
 * @brief 构造 buildGuard 所描述的新 IR，并返回或记录构造结果。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @param function 待分析或改写的函数。
 * @param fastTarget 参数 `fastTarget`，用于本函数的分析、匹配或 IR 构造。
 * @param counter 参数 `counter`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
GuardBlocks buildGuard(Pattern &pattern, Function *function,
                       BasicBlock *fastTarget, int &counter) {
    Module *module = function->parent_;
    Type *i32 = module->int32_ty_;
    Type *i1 = module->int1_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);
    auto block = [&](const char *suffix) {
        return new BasicBlock(module,
                              "remap.guard." + std::string(suffix) + "." +
                                  std::to_string(counter),
                              function);
    };
    BasicBlock *entry = block("entry");
    BasicBlock *loopPreheader = block("preheader");
    BasicBlock *header = block("header");
    BasicBlock *body = block("body");
    BasicBlock *latch = block("latch");
    BasicBlock *fallback = block("fallback");
    BasicBlock *ready = block("ready");
    ++counter;

    auto *rowsizeBound =
        new ConstantInt(i32, pattern.rowsizeExtent);
    auto *matrixBound = new ConstantInt(i32, pattern.matrixExtent);
    auto *lengthNonNegative =
        new ICmpInst(ICmpInst::ICMP_SGE, pattern.length, zero, entry);
    auto *lengthFitsRows =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.length, rowsizeBound, entry);
    auto *lengthFitsMatrix =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.length, matrixBound, entry);
    auto *extentNonNegative =
        new ICmpInst(ICmpInst::ICMP_SGE, pattern.extent, zero, entry);
    auto *extentFitsMatrix =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.extent, matrixBound, entry);
    Value *safe = new BinaryInst(i1, Instruction::And, lengthNonNegative,
                                 lengthFitsRows, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, lengthFitsMatrix, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, extentNonNegative, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, extentFitsMatrix, entry);
    new BranchInst(safe, loopPreheader, fallback, entry);
    new BranchInst(header, loopPreheader);

    auto *index = PhiInst::create_phi(i32, header);
    index->add_phi_pair_operand(zero, loopPreheader);
    header->add_instruction_front(index);
    auto *more = new ICmpInst(ICmpInst::ICMP_SLT, index, pattern.length, header);
    new BranchInst(more, body, ready, header);

    auto *rowsizePointer = elementGEP(pattern.rowsizeBase, index,
                                      pattern.rowsizeFlat, body);
    auto *rowsize = new LoadInst(rowsizePointer, body);
    auto *positive = new ICmpInst(ICmpInst::ICMP_SGT, rowsize, zero, body);
    new BranchInst(positive, latch, fallback, body);

    auto *next = new BinaryInst(i32, Instruction::Add, index, one, latch);
    index->add_phi_pair_operand(next, latch);
    new BranchInst(header, latch);

    new BranchInst(pattern.sequence->header, fallback);
    new BranchInst(fastTarget, ready);

    BasicBlock *originalPreheader = pattern.sequence->preheader;
    removeTerminatorAndEdges(originalPreheader);
    retargetPhiPredecessor(pattern.sequence->header, originalPreheader,
                           fallback);
    new BranchInst(entry, originalPreheader);
    return GuardBlocks{ready};
}

/**
 * @brief 汇总按重映射关系追踪源元素所生成的 CFG 入口、出口和结果地址。
 */
struct TraceBlocks {
    BasicBlock *entry = nullptr;   ///< 源位置追踪循环的入口块。
    BasicBlock *done = nullptr;    ///< 追踪完成并产生结果地址的汇合块。
    Value *sourcePointer = nullptr; ///< 在 done 块可用的原始矩阵元素地址。
};

/**
 * @brief 构造 buildTrace 所描述的新 IR，并返回或记录构造结果。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @param function 待分析或改写的函数。
 * @param merge 参数 `merge`，用于本函数的分析、匹配或 IR 构造。
 * @param counter 参数 `counter`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
TraceBlocks buildTrace(Pattern &pattern, Function *function,
                       BasicBlock *merge, int &counter) {
    Module *module = function->parent_;
    Type *i32 = module->int32_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);

    auto block = [&](const char *suffix) {
        return new BasicBlock(module,
                              "remap.trace." + std::string(suffix) + "." +
                                  std::to_string(counter),
                              function);
    };
    BasicBlock *entry = block("entry");
    BasicBlock *remapHeader = block("remap.header");
    BasicBlock *remapBody = block("remap.body");
    BasicBlock *positiveColumns = block("columns");
    BasicBlock *chainHeader = block("chain.header");
    BasicBlock *chainRange = block("chain.range");
    BasicBlock *chainCoordinates = block("chain.coordinates");
    BasicBlock *chainTriangle = block("chain.triangle");
    BasicBlock *chainOuterOrder = block("chain.outer-order");
    BasicBlock *chainSameOuter = block("chain.same-outer");
    BasicBlock *chainInnerOrder = block("chain.inner-order");
    BasicBlock *chainStep = block("chain.step");
    BasicBlock *chainDone = block("chain.done");
    BasicBlock *noRemap = block("no-remap");
    BasicBlock *remapLatch = block("remap.latch");
    BasicBlock *done = block("done");
    ++counter;

    new BranchInst(remapHeader, entry);
    auto *iteration = PhiInst::create_phi(i32, remapHeader);
    auto *source = PhiInst::create_phi(i32, remapHeader);
    iteration->add_phi_pair_operand(zero, entry);
    source->add_phi_pair_operand(pattern.consumer.loop->canonicalIV, entry);
    remapHeader->add_instruction_front(source);
    remapHeader->add_instruction_front(iteration);
    auto *hasRemap = new ICmpInst(ICmpInst::ICMP_SLT, iteration,
                                  pattern.length, remapHeader);
    new BranchInst(hasRemap, remapBody, done, remapHeader);

    auto *last = new BinaryInst(i32, Instruction::Sub, pattern.length, one,
                                remapBody);
    auto *reverseIndex = new BinaryInst(i32, Instruction::Sub, last, iteration,
                                        remapBody);
    auto *rowsizePointer = elementGEP(pattern.rowsizeBase, reverseIndex,
                                      pattern.rowsizeFlat, remapBody);
    auto *rowsize = new LoadInst(rowsizePointer, remapBody);
    auto *colsize = new BinaryInst(i32, Instruction::SDiv, pattern.extent,
                                   rowsize, remapBody);
    auto *hasColumns = new ICmpInst(ICmpInst::ICMP_SGT, colsize, zero,
                                    remapBody);
    new BranchInst(hasColumns, positiveColumns, noRemap, remapBody);

    auto *covered = new BinaryInst(i32, Instruction::Mul, rowsize, colsize,
                                   positiveColumns);
    new BranchInst(chainHeader, positiveColumns);

    auto *position = PhiInst::create_phi(i32, chainHeader);
    auto *previousOuter = PhiInst::create_phi(i32, chainHeader);
    auto *previousInner = PhiInst::create_phi(i32, chainHeader);
    position->add_phi_pair_operand(source, positiveColumns);
    previousOuter->add_phi_pair_operand(colsize, positiveColumns);
    previousInner->add_phi_pair_operand(zero, positiveColumns);
    chainHeader->add_instruction_front(previousInner);
    chainHeader->add_instruction_front(previousOuter);
    chainHeader->add_instruction_front(position);
    new BranchInst(chainRange, chainHeader);

    auto *outside = new ICmpInst(ICmpInst::ICMP_SGE, position, covered,
                                 chainRange);
    new BranchInst(outside, chainDone, chainCoordinates, chainRange);

    auto *outer = new BinaryInst(i32, Instruction::SRem, position, colsize,
                                 chainCoordinates);
    auto *inner = new BinaryInst(i32, Instruction::SDiv, position, colsize,
                                 chainCoordinates);
    new BranchInst(chainTriangle, chainCoordinates);

    auto *outsideTriangle = new ICmpInst(ICmpInst::ICMP_SGT, inner, outer,
                                         chainTriangle);
    new BranchInst(outsideTriangle, chainDone, chainOuterOrder, chainTriangle);

    auto *tooLateOuter = new ICmpInst(ICmpInst::ICMP_SGT, outer,
                                      previousOuter, chainOuterOrder);
    new BranchInst(tooLateOuter, chainDone, chainSameOuter, chainOuterOrder);

    auto *sameOuter = new ICmpInst(ICmpInst::ICMP_EQ, outer, previousOuter,
                                   chainSameOuter);
    new BranchInst(sameOuter, chainInnerOrder, chainStep, chainSameOuter);

    auto *tooLateInner = new ICmpInst(ICmpInst::ICMP_SGE, inner,
                                      previousInner, chainInnerOrder);
    new BranchInst(tooLateInner, chainDone, chainStep, chainInnerOrder);

    auto *nextPosition = new BinaryInst(i32, Instruction::Mul, outer, rowsize,
                                        chainStep);
    nextPosition = new BinaryInst(i32, Instruction::Add, nextPosition, inner,
                                  chainStep);
    position->add_phi_pair_operand(nextPosition, chainStep);
    previousOuter->add_phi_pair_operand(outer, chainStep);
    previousInner->add_phi_pair_operand(inner, chainStep);
    new BranchInst(chainHeader, chainStep);

    new BranchInst(remapLatch, chainDone);
    new BranchInst(remapLatch, noRemap);
    auto *nextSource = PhiInst::create_phi(i32, remapLatch);
    nextSource->add_phi_pair_operand(position, chainDone);
    nextSource->add_phi_pair_operand(source, noRemap);
    remapLatch->add_instruction_front(nextSource);
    auto *nextIteration = new BinaryInst(i32, Instruction::Add, iteration, one,
                                         remapLatch);
    iteration->add_phi_pair_operand(nextIteration, remapLatch);
    source->add_phi_pair_operand(nextSource, remapLatch);
    new BranchInst(remapHeader, remapLatch);

    auto *sourcePointer = elementGEP(pattern.remap.matrixBase, source,
                                     pattern.remap.matrixFlat, done);
    new BranchInst(merge, done);
    return TraceBlocks{entry, done, sourcePointer};
}

/**
 * @brief 实现 versionConsumer 对应的局部分析或变换辅助逻辑。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @param guard 参数 `guard`，用于本函数的分析、匹配或 IR 构造。
 * @param consumerEntry 参数 `consumerEntry`，用于本函数的分析、匹配或 IR 构造。
 * @param function 待分析或改写的函数。
 * @param counter 参数 `counter`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void versionConsumer(Pattern &pattern, GuardBlocks guard,
                     BasicBlock *consumerEntry, Function *function,
                     int &counter) {
    // 克隆消费者控制流时，仅把匹配到的矩阵 load 改为按来源追踪得到的值。
    // 未命中的指令沿用普通值映射，原消费者则保留为守卫失败时的完整回退版本。
    Module *module = function->parent_;
    Type *i1 = module->int1_ty_;
    auto *falseValue = new ConstantInt(i1, 0);
    auto *trueValue = new ConstantInt(i1, 1);

    std::string suffix = std::to_string(counter++);
    auto *dispatch = new BasicBlock(module, "remap.consumer.dispatch." + suffix,
                                    function);
    auto *slow = new BasicBlock(module, "remap.consumer.slow." + suffix,
                                function);
    auto *merge = new BasicBlock(module, "remap.consumer.merge." + suffix,
                                 function);

    redirectEdge(pattern.consumer.loop->preheader,
                 pattern.consumer.loop->header, consumerEntry);
    retargetPhiPredecessor(pattern.consumer.loop->header,
                           pattern.consumer.loop->preheader, consumerEntry);
    auto *entryMode = PhiInst::create_phi(i1, consumerEntry);
    entryMode->add_phi_pair_operand(falseValue,
                                    pattern.consumer.loop->preheader);
    entryMode->add_phi_pair_operand(trueValue, guard.ready);
    consumerEntry->add_instruction_front(entryMode);
    new BranchInst(pattern.consumer.loop->header, consumerEntry);

    auto *fastMode = PhiInst::create_phi(i1, pattern.consumer.loop->header);
    fastMode->add_phi_pair_operand(entryMode, consumerEntry);
    fastMode->add_phi_pair_operand(fastMode,
                                   pattern.consumer.loop->singleLatch());
    pattern.consumer.loop->header->add_instruction_front(fastMode);

    TraceBlocks trace = buildTrace(pattern, function, merge, counter);
    auto *slowPointer = elementGEP(pattern.remap.matrixBase,
                                   pattern.consumer.loop->canonicalIV,
                                   pattern.remap.matrixFlat, slow);
    new BranchInst(merge, slow);

    auto *pointer = PhiInst::create_phi(slowPointer->type_, merge);
    pointer->add_phi_pair_operand(slowPointer, slow);
    pointer->add_phi_pair_operand(trace.sourcePointer, trace.done);
    merge->add_instruction_front(pointer);
    new BranchInst(pattern.consumer.bodyEntry, merge);

    new BranchInst(fastMode, trace.entry, slow, dispatch);
    redirectEdge(pattern.consumer.loop->header, pattern.consumer.bodyEntry,
                 dispatch);
    pattern.consumer.load->set_operand(0, pointer);
}

/**
 * @brief 原地执行 applyTransform 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param function 待分析或改写的函数。
 * @param pattern 参数 `pattern`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyTransform(Function *function, Pattern &pattern) {
    // 保留原消费者作为回退路径，新建守卫后只版本化已匹配的 load 来源追踪路径。
    // 因此运行时条件不满足时仍执行原程序，不会依赖特定输入才能保持正确。
    auto *preheaderBranch = dynamic_cast<BranchInst *>(
        pattern.sequence->preheader->get_terminator());
    if (!preheaderBranch || preheaderBranch->num_ops() != 1 ||
        preheaderBranch->get_operand(0) != pattern.sequence->header)
        return false;

    int counter = 0;
    auto *consumerEntry = new BasicBlock(
        function->parent_, "remap.consumer.entry." + std::to_string(counter++),
        function);
    GuardBlocks guard = buildGuard(pattern, function, consumerEntry, counter);
    versionConsumer(pattern, guard, consumerEntry, function, counter);
    function->set_instr_name();
    if (debugEnabled())
        std::cerr << "[TriangularRemapSourceCompose] versioned source tracing in "
                  << function->name_ << "\n";
    return true;
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void TriangularRemapSourceCompose::execute(Module *module) {
    AnalysisManager AM;
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            runOnFunction(function, AM);
    }
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses
TriangularRemapSourceCompose::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            changed |= runOnFunction(function, AM);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool TriangularRemapSourceCompose::runOnFunction(Function *function,
                                                 AnalysisManager &AM) {
    if (!function || function->basic_blocks_.empty())
        return false;

    LoopInfo &loopInfo = AM.getLoopInfo(function);
    DominatorTreeAnalysis &DT = AM.getDominatorTree(function);
    BasicAliasAnalysis &aliasAnalysis = AM.getBasicAA(function->parent_);
    Pattern pattern;
    if (!matchPattern(function, loopInfo, DT, aliasAnalysis, pattern))
        return false;
    return applyTransform(function, pattern);
}
