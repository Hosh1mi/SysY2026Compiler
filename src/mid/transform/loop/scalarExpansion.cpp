/**
 * @file scalarExpansion.cpp
 * @brief 标量扩展：为循环携带标量创建按迭代索引访问的临时数组，解除可证明的伪依赖。
 * @details 把标量状态按迭代编号展开到临时数组前，需证明迭代计数、存储生命周期和新增空间成本均可接受。
 */

#include "../../../include/mid/opt/scalarExpansion.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <set>
#include <unordered_map>
#include <vector>

namespace {

/**
 * @brief 判断 isScratchAlloca 所描述的结构、合法性或安全条件是否成立。
 * @param alloca 参数 `alloca`，用于本函数的分析、匹配或 IR 构造。
 * @param size 参数 `size`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isScratchAlloca(AllocaInst *alloca, int size) {
    if (!alloca || !alloca->isLoopExpansionScratch()) return false;
    auto *arr = dynamic_cast<ArrayType *>(alloca->allocated_type());
    return arr && static_cast<int>(arr->num_elements_) == size;
}

/**
 * @brief 在函数入口查找尺寸匹配且当前无 use 的标量展开 scratch。
 * @param func 待搜索的函数。
 * @param size 所需数组元素数量。
 * @param reserved 本轮已占用、不可复用的 scratch 集合。
 * @return 找到时返回可复用 alloca，否则返回 nullptr。
 */
AllocaInst *findUnusedScratch(Function *func, int size,
                              const std::set<AllocaInst *> &reserved) {
    if (!func || func->basic_blocks_.empty()) return nullptr;
    for (auto *inst : func->basic_blocks_.front()->instr_list_) {
        auto *alloca = dynamic_cast<AllocaInst *>(inst);
        if (!isScratchAlloca(alloca, size)) continue;
        if (reserved.count(alloca)) continue;
        if (!alloca->use_list_.empty()) continue;
        return alloca;
    }
    return nullptr;
}

/**
 * @brief 在函数入口创建并标记一个标量展开临时数组。
 * @param func 目标函数。
 * @param size 临时数组元素数量。
 * @param counter 用于生成唯一名称的计数器。
 * @return 新建的 scratch alloca。
 */
AllocaInst *createScratch(Function *func, int size, int &counter) {
    Module *module = func->parent_;
    auto *arr = module->get_array_type(module->int32_ty_, size);
    auto *entry = func->basic_blocks_.front();
    auto *alloca = new AllocaInst(arr, entry, true);
    alloca->markLoopExpansionScratch();
    alloca->name_ = "scalar.expansion.tmp." + std::to_string(counter++);
    entry->add_instruction_front(alloca);
    return alloca;
}

/**
 * @brief 查找基本块中的第一条非 PHI 指令。
 * @param bb 待搜索的基本块。
 * @return 找到时返回指令，否则返回 nullptr。
 */
Instruction *firstNonPhi(BasicBlock *bb) {
    for (auto *inst : bb->instr_list_) {
        if (!inst->is_phi()) return inst;
    }
    return nullptr;
}

/**
 * @brief 从循环 header 条件分支中取得位于循环内的主体入口。
 * @param loop 待分析的循环。
 * @return 找到时返回循环内后继，否则返回 nullptr。
 */
BasicBlock *loopBodyEntry(Loop *loop) {
    if (!loop || !loop->header) return nullptr;
    auto *br = dynamic_cast<BranchInst *>(loop->header->get_terminator());
    if (!br || br->num_ops() != 3) return nullptr;
    auto *t = dynamic_cast<BasicBlock *>(br->get_operand(1));
    auto *f = dynamic_cast<BasicBlock *>(br->get_operand(2));
    if (t && loop->blocks.count(t)) return t;
    if (f && loop->blocks.count(f)) return f;
    return nullptr;
}

/**
 * @brief 原地执行 replaceBranchTarget 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param pred 前驱基本块。
 * @param oldT 参数 `oldT`，用于本函数的分析、匹配或 IR 构造。
 * @param newT 参数 `newT`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldT, BasicBlock *newT) {
    auto *term = pred ? pred->get_terminator() : nullptr;
    if (!term || !term->is_br()) return;
    bool changed = false;
    for (unsigned i = 0; i < term->num_ops(); i++) {
        if (term->get_operand(i) == oldT) {
            term->set_operand(i, newT);
            changed = true;
        }
    }
    if (!changed) return;
    pred->remove_succ_basic_block(oldT);
    oldT->remove_pre_basic_block(pred);
    pred->add_succ_basic_block(newT);
    newT->add_pre_basic_block(pred);
}

/**
 * @brief 原地执行 retargetPhiPred 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param succ 后继基本块。
 * @param oldPred 需要替换的原前驱基本块。
 * @param newPred 替换后的新前驱基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void retargetPhiPred(BasicBlock *succ, BasicBlock *oldPred, BasicBlock *newPred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        for (unsigned i = 0; i + 1 < inst->num_ops(); i += 2) {
            if (inst->get_operand(i + 1) == oldPred)
                inst->set_operand(i + 1, newPred);
        }
    }
}

/**
 * @brief 在指定指令前插入 scratch[index] 的地址计算。
 * @param scratch 临时数组 alloca。
 * @param zero 数组首维使用的零下标。
 * @param index 元素下标。
 * @param bb 指令所属基本块。
 * @param before 插入位置。
 * @return 新建并插入的 GEP 指令。
 */
GetElementPtrInst *insertScratchGEP(AllocaInst *scratch, Value *zero,
                                    Value *index, BasicBlock *bb,
                                    Instruction *before) {
    auto *gep = new GetElementPtrInst(scratch, {zero, index}, bb, true);
    bb->add_instruction_before_inst(gep, before);
    return gep;
}

/**
 * @brief 在指定指令前插入对 scratch[index] 的读取。
 * @param scratch 临时数组 alloca。
 * @param zero 数组首维使用的零下标。
 * @param index 元素下标。
 * @param bb 指令所属基本块。
 * @param before 插入位置。
 * @return 新建并插入的 load 指令。
 */
LoadInst *insertScratchLoad(AllocaInst *scratch, Value *zero, Value *index,
                            BasicBlock *bb, Instruction *before) {
    auto *gep = insertScratchGEP(scratch, zero, index, bb, before);
    auto *load = new LoadInst(gep, bb, true);
    bb->add_instruction_before_inst(load, before);
    return load;
}

/**
 * @brief 原地执行 insertScratchStore 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param scratch 参数 `scratch`，用于本函数的分析、匹配或 IR 构造。
 * @param zero 参数 `zero`，用于本函数的分析、匹配或 IR 构造。
 * @param index 参数 `index`，用于本函数的分析、匹配或 IR 构造。
 * @param stored 参数 `stored`，用于本函数的分析、匹配或 IR 构造。
 * @param bb 目标或待修改的基本块。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void insertScratchStore(AllocaInst *scratch, Value *zero, Value *index,
                        Value *stored, BasicBlock *bb) {
    auto *term = bb->get_terminator();
    auto *gep = new GetElementPtrInst(scratch, {zero, index}, bb, true);
    bb->add_instruction_before_terminator(gep);
    auto *store = new StoreInst(stored, gep, bb, true);
    bb->add_instruction_before_terminator(store);
}

/**
 * @brief 关联一个可跨展开迭代分布的标量归约与其临时存储数组。
 */
struct DistributedReduction {
    ScalarReductionInfo reduction;       ///< 原循环中的标量归约描述。
    AllocaInst *scratch = nullptr;        ///< 保存各展开分片局部结果的临时数组。
};

/**
 * @brief 汇总新建计数循环的控制流基本块和归纳变量。
 */
struct CountedLoopBlocks {
    BasicBlock *header = nullptr;    ///< 执行迭代条件判断的循环头。
    BasicBlock *body = nullptr;      ///< 执行每次迭代有效工作的循环体。
    BasicBlock *latch = nullptr;     ///< 更新归纳变量并跳回头部的回边块。
    PhiInst *iv = nullptr;           ///< 新计数循环的归纳变量 PHI。
};

/**
 * @brief 创建带唯一编号名称的标量展开基本块。
 * @param module 所属模块。
 * @param func 所属函数。
 * @param tag 基本块名称前缀。
 * @param counter 唯一编号计数器。
 * @return 新建基本块。
 */
BasicBlock *newBlock(Module *module, Function *func, const std::string &tag,
                     int &counter) {
    return new BasicBlock(module, tag + "." + std::to_string(counter++), func);
}

/**
 * @brief 构造 createCountedLoop 所描述的新 IR，并返回或记录构造结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param func 待分析或改写的函数。
 * @param prefix 参数 `prefix`，用于本函数的分析、匹配或 IR 构造。
 * @param bound 参数 `bound`，用于本函数的分析、匹配或 IR 构造。
 * @param predecessor 前驱基本块。
 * @param exit 参数 `exit`，用于本函数的分析、匹配或 IR 构造。
 * @param counter 参数 `counter`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
CountedLoopBlocks createCountedLoop(Module *module, Function *func,
                                    const std::string &prefix, Value *bound,
                                    BasicBlock *predecessor, BasicBlock *exit,
                                    int &counter) {
    CountedLoopBlocks loop;
    loop.header = newBlock(module, func, prefix + ".h", counter);
    loop.body = newBlock(module, func, prefix + ".body", counter);
    loop.latch = newBlock(module, func, prefix + ".latch", counter);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);

    loop.iv = PhiInst::create_phi(module->int32_ty_, loop.header);
    loop.iv->add_phi_pair_operand(zero, predecessor);
    loop.header->add_instruction_front(loop.iv);
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, loop.iv, bound, loop.header);
    new BranchInst(cmp, loop.body, exit, loop.header);

    new BranchInst(loop.latch, loop.body);
    auto *inc = new BinaryInst(module->int32_ty_, Instruction::Add,
                               loop.iv, one, loop.latch);
    loop.iv->add_phi_pair_operand(inc, loop.latch);
    new BranchInst(loop.header, loop.latch);
    return loop;
}

/**
 * @brief 原地执行 replaceUsesInLoop 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param oldPhi 参数 `oldPhi`，用于本函数的分析、匹配或 IR 构造。
 * @param replacement 参数 `replacement`，用于本函数的分析、匹配或 IR 构造。
 * @param deadStore 参数 `deadStore`，用于本函数的分析、匹配或 IR 构造。
 * @param parentLoop 参数 `parentLoop`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceUsesInLoop(PhiInst *oldPhi, Value *replacement, StoreInst *deadStore,
                       Loop *parentLoop) {
    auto uses = oldPhi->use_list_;
    for (const auto &use : uses) {
        auto *user = use.user_;
        if (!user || user == oldPhi || user == deadStore) continue;
        if (!user->parent_ || !parentLoop->blocks.count(user->parent_)) continue;
        user->set_operand(use.operand_index_, replacement);
    }
}

using ValueMap = std::unordered_map<Value *, Value *>;

/**
 * @brief 查询标量展开克隆阶段的值映射。
 * @param value 待重映射的原值。
 * @param map 原值到克隆值的映射表。
 * @return 命中时返回克隆值，否则返回原值。
 */
Value *remapValue(Value *value, const ValueMap &map) {
    auto found = map.find(value);
    return found == map.end() ? value : found->second;
}

/**
 * @brief 判断 isCloneableBodyInstruction 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isCloneableBodyInstruction(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<SelectInst *>(inst);
}

/**
 * @brief 克隆归约循环主体中的一条受支持纯指令。
 * @param orig 待克隆的原指令。
 * @param dest 克隆指令的目标基本块。
 * @param map 已建立的值映射。
 * @return 成功时返回克隆指令，不支持该类型时返回 nullptr。
 */
Instruction *cloneBodyInstruction(Instruction *orig, BasicBlock *dest,
                                  const ValueMap &map) {
    auto R = [&](Value *value) { return remapValue(value, map); };
    Instruction *clone = nullptr;
    if (auto *binary = dynamic_cast<BinaryInst *>(orig)) {
        clone = new BinaryInst(binary->type_, binary->op_id_,
                               R(binary->get_operand(0)),
                               R(binary->get_operand(1)), dest);
    } else if (auto *unary = dynamic_cast<UnaryInst *>(orig)) {
        clone = new UnaryInst(unary->type_, unary->op_id_,
                              R(unary->get_operand(0)), dest);
    } else if (auto *cmp = dynamic_cast<ICmpInst *>(orig)) {
        clone = new ICmpInst(cmp->icmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest);
    } else if (auto *cmp = dynamic_cast<FCmpInst *>(orig)) {
        clone = new FCmpInst(cmp->fcmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest);
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops(); ++i)
            indices.push_back(R(gep->get_operand(i)));
        clone = new GetElementPtrInst(R(gep->get_operand(0)), indices, dest);
    } else if (auto *load = dynamic_cast<LoadInst *>(orig)) {
        clone = new LoadInst(R(load->get_operand(0)), dest);
    } else if (auto *zext = dynamic_cast<ZextInst *>(orig)) {
        clone = new ZextInst(zext->op_id_, R(zext->get_operand(0)),
                             zext->type_, dest);
    } else if (auto *cast = dynamic_cast<FpToSiInst *>(orig)) {
        clone = new FpToSiInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->type_, dest);
    } else if (auto *cast = dynamic_cast<SiToFpInst *>(orig)) {
        clone = new SiToFpInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->type_, dest);
    } else if (auto *cast = dynamic_cast<Bitcast *>(orig)) {
        clone = new Bitcast(cast->op_id_, R(cast->get_operand(0)),
                            cast->type_, dest);
    } else if (auto *select = dynamic_cast<SelectInst *>(orig)) {
        clone = new SelectInst(R(select->get_operand(0)),
                               R(select->get_operand(1)),
                               R(select->get_operand(2)), dest);
    }
    if (clone) clone->copySemFlagsFrom(orig);
    return clone;
}

/**
 * @brief 递归克隆生成归约初值所需的纯表达式依赖树。
 * @param value 待克隆或复用的原值。
 * @param dest 克隆指令的目标基本块。
 * @param map 原值到克隆值的缓存映射。
 * @param visiting 当前递归栈，用于拒绝值环。
 * @param scope 限定需要克隆定义的循环范围。
 * @return 克隆或复用后的值；遇到不支持依赖时返回 nullptr。
 */
Value *clonePureValue(Value *value, BasicBlock *dest, ValueMap &map,
                      std::set<Value *> &visiting, Loop *scope) {
    if (!value) return nullptr;
    auto mapped = map.find(value);
    if (mapped != map.end()) return mapped->second;
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return value;

    auto *inst = dynamic_cast<Instruction *>(value);
    if (inst && scope && !scope->blocks.count(inst->parent_)) return value;
    if (!inst || !isCloneableBodyInstruction(inst) ||
        !visiting.insert(value).second)
        return nullptr;

    for (unsigned i = 0; i < inst->num_ops(); ++i) {
        if (dynamic_cast<BasicBlock *>(inst->get_operand(i))) continue;
        if (!clonePureValue(inst->get_operand(i), dest, map, visiting, scope))
            return nullptr;
    }
    auto *clone = cloneBodyInstruction(inst, dest, map);
    if (!clone) return nullptr;
    map[value] = clone;
    visiting.erase(value);
    return clone;
}

} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void ScalarExpansion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void ScalarExpansion::runOnFunction(Function *func) {
    for (int iter = 0; iter < 32; iter++) {
        LoopInfo LI;
        LI.analyze(func);
        if (LI.allLoops().empty()) return;

        AffineAnalysis     AA(LI);
        DependenceAnalysis DA(LI, AA);
        CostModel          CM(AA);
        ReductionAnalysis  RA(AA);
        LoopAccessAnalysis LA(AA);
        LoopInterchangeAnalysis IA(DA, LA, CM);

        bool changed = false;
        for (auto &L_ptr : LI.allLoops()) {
            Loop *L = L_ptr.get();
            if (!L->children.empty()) continue;
            ScalarReductionNestInfo info{};
            if (!RA.detectScalarExpandableNest(L, info)) continue;
            if (!RA.isScalarExpansionMemoryLegal(info)) continue;
            if (!isLegalAndProfitable(info, IA)) continue;
            if (apply(info, func->parent_)) {
                changed = true;
                break;
            }
        }
        if (!changed) return;
    }
}

/**
 * @brief 判断 isLegalAndProfitable 所描述的结构、合法性或安全条件是否成立。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @param IA 参数 `IA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool ScalarExpansion::isLegalAndProfitable(const ScalarReductionNestInfo &info,
                                           LoopInterchangeAnalysis &IA) {
    PhiInst *L_iv = info.inner_loop->getInductionIV();
    PhiInst *P_iv = info.parent_loop->getInductionIV();
    std::vector<GetElementPtrInst *> geps = info.body_geps;
    for (auto &r : info.reductions) geps.push_back(r.gep_store);
    return IA.estimateCost(geps, L_iv, P_iv).profitable();
}

/**
 * @brief 原地执行 apply 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool ScalarExpansion::apply(const ScalarReductionNestInfo &info, Module *module) {
    // 先验证整个待复制区域，再分配或复用 scratch；此前不得改动 CFG。
    // 随后按迭代索引展开标量状态、克隆内层体并在父循环出口归并最终值。
    Loop *P = info.parent_loop;
    Loop *L = info.inner_loop;
    Function *func = P->header->parent_;
    Type *i32 = module->int32_ty_;
    BasicBlock *P_preheader = P->preheader;
    BasicBlock *P_exit = P->singleExit();
    BasicBlock *L_header = L->header;
    BasicBlock *L_latch = L->singleLatch();
    PhiInst *P_iv = P->getInductionIV();
    PhiInst *L_iv = L->getInductionIV();
    if (!P_preheader || !P_exit || !L_header || !L_latch ||
        !P_iv || !L_iv || !info.parent_bound || !info.inner_bound)
        return false;

    auto *preTerm = P_preheader->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops() != 1 ||
        preTerm->get_operand(0) != P->header)
        return false;

    BasicBlock *bodyEntry = loopBodyEntry(L);
    if (!bodyEntry) return false;

    std::vector<BasicBlock *> originalBlocks;
    for (auto *bb : L->blocksOrdered) {
        if (bb != L_header) originalBlocks.push_back(bb);
    }
    if (originalBlocks.empty()) return false;

    // 修改 CFG 或分配 scratch 前先验证整个克隆区域；失败候选不能留下
    // 部分循环分布结果或无用临时数组。
    for (auto *bb : originalBlocks) {
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!term) return false;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            if (!isCloneableBodyInstruction(inst)) return false;
        }
    }

    std::vector<BasicBlock *> oldExitPreds;
    for (auto *pred : P_exit->pre_bbs_)
        if (P->blocks.count(pred)) oldExitPreds.push_back(pred);
    if (oldExitPreds.size() != 1) return false;
    BasicBlock *oldExitPred = oldExitPreds.front();

    std::set<AllocaInst *> reserved;
    std::vector<DistributedReduction> reductions;
    for (const auto &reduction : info.reductions) {
        AllocaInst *scratch = findUnusedScratch(func, reduction.inner_dim,
                                                reserved);
        if (!scratch)
            scratch = createScratch(func, reduction.inner_dim,
                                    scratch_counter_);
        reserved.insert(scratch);
        reductions.push_back({reduction, scratch});
    }

    auto block = [&](const std::string &name) {
        return newBlock(module, func, name, block_counter_);
    };
    BasicBlock *clearHeader = block("ldist.clear.h");
    BasicBlock *clearBody = block("ldist.clear.body");
    BasicBlock *clearLatch = block("ldist.clear.latch");
    BasicBlock *outerHeader = block("ldist.interchanged.outer.h");
    BasicBlock *outerBody = block("ldist.interchanged.outer.body");
    BasicBlock *outerLatch = block("ldist.interchanged.outer.latch");
    BasicBlock *innerHeader = block("ldist.interchanged.inner.h");
    BasicBlock *innerLatch = block("ldist.interchanged.inner.latch");
    BasicBlock *storeHeader = block("ldist.storeback.h");
    BasicBlock *storeBody = block("ldist.storeback.body");
    BasicBlock *storeLatch = block("ldist.storeback.latch");

    innerHeader->setSemFlag(SemFlag::ScalarExpansionCompute);
    innerLatch->setSemFlag(SemFlag::ScalarExpansionCompute);

    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);
    Value *parentInit = P->inductionInit;
    if (!parentInit) return false;

    auto *clearIV = PhiInst::create_phi(i32, clearHeader);
    clearIV->add_phi_pair_operand(zero, P_preheader);
    clearHeader->add_instruction_front(clearIV);
    auto *clearCmp = new ICmpInst(ICmpInst::ICMP_SLT, clearIV,
                                  info.parent_bound, clearHeader);
    new BranchInst(clearCmp, clearBody, outerHeader, clearHeader);
    std::vector<std::pair<AllocaInst *, Value *>> initialValues;
    for (const auto &entry : reductions) {
        ValueMap initMap;
        initMap[P_iv] = clearIV;
        std::set<Value *> visiting;
        Value *initialValue = clonePureValue(entry.reduction.sum_init,
                                             clearBody, initMap, visiting, P);
        if (!initialValue) return false;
        initialValues.push_back({entry.scratch, initialValue});
    }
    new BranchInst(clearLatch, clearBody);
    for (const auto &[scratch, initialValue] : initialValues) {
        insertScratchStore(scratch, zero, clearIV, initialValue,
                           clearBody);
    }
    auto *clearNext = new BinaryInst(i32, Instruction::Add, clearIV, one,
                                     clearLatch);
    clearIV->add_phi_pair_operand(clearNext, clearLatch);
    new BranchInst(clearHeader, clearLatch);

    // 交换执行维度：原内层成为外层，原父维成为内层。必须克隆完整的旧内层 CFG，
    // 而不只是算术指令，才能保留条件汇合和任意内部控制流的 SSA 结构。
    auto *outerIV = PhiInst::create_phi(i32, outerHeader);
    outerIV->add_phi_pair_operand(zero, clearHeader);
    outerHeader->add_instruction_front(outerIV);
    auto *outerCmp = new ICmpInst(ICmpInst::ICMP_SLT, outerIV,
                                  info.inner_bound, outerHeader);
    new BranchInst(outerCmp, outerBody, storeHeader, outerHeader);
    new BranchInst(innerHeader, outerBody);

    auto *innerIV = PhiInst::create_phi(i32, innerHeader);
    innerIV->add_phi_pair_operand(parentInit, outerBody);
    innerHeader->add_instruction_front(innerIV);

    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto *oldBlock : originalBlocks) {
        blockMap[oldBlock] = block("ldist.clone");
        blockMap[oldBlock]->setSemFlag(SemFlag::ScalarExpansionCompute);
    }
    BasicBlock *clonedEntry = blockMap[bodyEntry];

    ValueMap valueMap;
    valueMap[L_iv] = outerIV;
    valueMap[P_iv] = innerIV;
    for (const auto &entry : reductions) {
        auto *gep = new GetElementPtrInst(entry.scratch, {zero, innerIV},
                                          clonedEntry);
        auto *load = new LoadInst(gep, clonedEntry);
        valueMap[entry.reduction.sum_phi] = load;
    }

    std::set<PhiInst *> reductionPhis;
    for (const auto &entry : reductions)
        reductionPhis.insert(entry.reduction.sum_phi);

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (!inst->is_phi()) break;
            auto *oldPhi = static_cast<PhiInst *>(inst);
            if (reductionPhis.count(oldPhi)) continue;
            auto *newPhi = PhiInst::create_phi(oldPhi->type_, newBB);
            newPhi->copySemFlagsFrom(oldPhi);
            newBB->add_instruction_front(newPhi);
            valueMap[oldPhi] = newPhi;
        }
    }

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            Instruction *clone = cloneBodyInstruction(inst, newBB, valueMap);
            if (!clone) return false;
            valueMap[inst] = clone;
        }
    }

    auto mappedBlock = [&](BasicBlock *oldBlock) -> BasicBlock * {
        if (oldBlock == L_header) return innerLatch;
        auto found = blockMap.find(oldBlock);
        return found == blockMap.end() ? nullptr : found->second;
    };

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (!inst->is_phi()) break;
            auto *oldPhi = static_cast<PhiInst *>(inst);
            if (reductionPhis.count(oldPhi)) continue;
            auto *newPhi = static_cast<PhiInst *>(valueMap[oldPhi]);
            for (unsigned i = 0; i < oldPhi->num_ops(); i += 2) {
                auto *oldPred = static_cast<BasicBlock *>(
                    oldPhi->get_operand(i + 1));
                if (oldPred == L_header || oldPred == L->preheader) continue;
                BasicBlock *newPred = mappedBlock(oldPred);
                if (!newPred) return false;
                newPhi->add_phi_pair_operand(
                    remapValue(oldPhi->get_operand(i), valueMap), newPred);
            }
        }
    }

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        if (oldBlock == L_latch) {
            for (const auto &entry : reductions) {
                auto *gep = new GetElementPtrInst(entry.scratch,
                                                  {zero, innerIV}, newBB);
                new StoreInst(remapValue(entry.reduction.sum_latch, valueMap),
                              gep, newBB);
            }
        }

        auto *oldBranch = static_cast<BranchInst *>(oldBlock->get_terminator());
        if (oldBranch->num_ops() == 1) {
            BasicBlock *dest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(0)));
            if (!dest) return false;
            new BranchInst(dest, newBB);
        } else {
            BasicBlock *trueDest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(1)));
            BasicBlock *falseDest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(2)));
            if (!trueDest || !falseDest) return false;
            new BranchInst(remapValue(oldBranch->get_operand(0), valueMap),
                           trueDest, falseDest, newBB);
        }
    }

    auto *innerCmp = new ICmpInst(ICmpInst::ICMP_SLT, innerIV,
                                  info.parent_bound, innerHeader);
    new BranchInst(innerCmp, clonedEntry, outerLatch, innerHeader);
    auto *innerNext = new BinaryInst(i32, Instruction::Add, innerIV, one,
                                     innerLatch);
    innerIV->add_phi_pair_operand(innerNext, innerLatch);
    new BranchInst(innerHeader, innerLatch);
    auto *outerNext = new BinaryInst(i32, Instruction::Add, outerIV, one,
                                     outerLatch);
    outerIV->add_phi_pair_operand(outerNext, outerLatch);
    new BranchInst(outerHeader, outerLatch);

    auto *storeIV = PhiInst::create_phi(i32, storeHeader);
    storeIV->add_phi_pair_operand(parentInit, outerHeader);
    storeHeader->add_instruction_front(storeIV);
    auto *storeCmp = new ICmpInst(ICmpInst::ICMP_SLT, storeIV,
                                  info.parent_bound, storeHeader);
    new BranchInst(storeCmp, storeBody, P_exit, storeHeader);
    for (const auto &entry : reductions) {
        auto *loadGEP = new GetElementPtrInst(entry.scratch,
                                              {zero, storeIV}, storeBody);
        auto *value = new LoadInst(loadGEP, storeBody);
        std::vector<Value *> destinationIndices;
        auto *originalGEP = entry.reduction.gep_store;
        unsigned last = originalGEP->num_ops() - 1;
        for (unsigned i = 1; i < originalGEP->num_ops(); ++i)
            destinationIndices.push_back(
                i == last ? static_cast<Value *>(storeIV)
                          : originalGEP->get_operand(i));
        auto *destination = new GetElementPtrInst(
            entry.reduction.base_store, destinationIndices, storeBody);
        new StoreInst(value, destination, storeBody);
    }
    new BranchInst(storeLatch, storeBody);
    auto *storeNext = new BinaryInst(i32, Instruction::Add, storeIV, one,
                                     storeLatch);
    storeIV->add_phi_pair_operand(storeNext, storeLatch);
    new BranchInst(storeHeader, storeLatch);

    // 替代循环巢、值映射和出口结果全部就绪后，最后才切换旧入口完成提交。
    preTerm->set_operand(0, clearHeader);
    P_preheader->remove_succ_basic_block(P->header);
    P->header->remove_pre_basic_block(P_preheader);
    P_preheader->add_succ_basic_block(clearHeader);
    clearHeader->add_pre_basic_block(P_preheader);
    retargetPhiPred(P_exit, oldExitPred, storeHeader);
    removeUnreachableBlocks(func);
    return true;
}
