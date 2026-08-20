/**
 * @file loopFusion.cpp
 * @brief 循环融合：在边界一致且依赖合法时融合相邻循环，减少循环控制和中间内存访问。
 * @details 先验证相邻形状和边界，再检查标量交叉使用、调用纯度与内存依赖，最后统一重连 CFG 与 PHI。
 */

#include "../../../include/mid/opt/loopFusion.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/opt/loopWorklist.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

/**
 * @brief 判断是否启用循环融合调试诊断。
 * @return 设置 DEBUG_LOOP_FUSION 环境变量时返回 true。
 */
bool debugEnabled() { return std::getenv("DEBUG_LOOP_FUSION") != nullptr; }

// 可无副作用上提到 preheader 的纯指令（无内存访问、无调用、非 phi）。
/**
 * @brief 判断 isHoistableInst 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isHoistableInst(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<SelectInst *>(inst);
}

/**
 * @brief 沿连续 GEP 的基址操作数追溯内存访问根对象。
 * @param ptr 待追溯的指针值。
 * @return 最外层非 GEP 基址值。
 */
Value *rootBase(Value *ptr) {
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->get_operand(0);
    return ptr;
}

/**
 * @brief 表示两个内存访问根对象之间的别名关系。
 */
enum class BaseRelation {
    NoAlias,    ///< 根对象确定互不重叠。
    MayAlias,   ///< 无法证明是否重叠。
    MustAlias   ///< 根对象确定相同。
};

/**
 * @brief 描述循环融合依赖检查中的一次 load 或 store 内存访问。
 */
struct Access {
    GetElementPtrInst *gep = nullptr;    ///< 计算本次访问地址的 GEP。
    Value *base = nullptr;               ///< 穿过 GEP 后得到的根对象。
    bool isStore = false;                ///< 为 true 时该访问会写入内存。
};

/**
 * @brief 取得 load 或 store 指令访问的指针操作数。
 * @param inst 待检查的内存指令。
 * @return load/store 的地址；其他指令返回 nullptr。
 */
Value *accessPtr(Instruction *inst) {
    if (auto *st = dynamic_cast<StoreInst *>(inst))
        return st->get_operand(1);
    if (auto *ld = dynamic_cast<LoadInst *>(inst))
        return ld->get_operand(0);
    return nullptr;
}

/**
 * @brief 收集或查找 collectAccesses 所需的信息。
 * @param L 待检查或变换的循环。
 * @return 返回计算、分析或构造得到的结果。
 */
std::vector<Access> collectAccesses(Loop *L) {
    std::vector<Access> accs;
    for (auto *bb : L->blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_load() && !inst->is_store()) continue;
            Access acc;
            acc.isStore = inst->is_store();
            Value *ptr = accessPtr(inst);
            acc.gep = dynamic_cast<GetElementPtrInst *>(ptr);
            acc.base = ptr ? rootBase(ptr) : nullptr;
            accs.push_back(acc);
        }
    }
    return accs;
}

// v 是否定义在 L 的某个块内。
/**
 * @brief 判断 definedInLoop 所描述的结构、合法性或安全条件是否成立。
 * @param L 待检查或变换的循环。
 * @param v 待检查或映射的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool definedInLoop(Loop *L, Value *v) {
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && inst->parent_ && L->blocks.count(inst->parent_);
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
        for (unsigned i = 1; i < inst->num_ops(); i += 2)
            if (inst->get_operand(i) == oldPred)
                inst->set_operand(i, newPred);
    }
}

} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopFusion::execute(Module *module) {
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    BasicAliasAnalysis basicAA;
    basicAA.analyze(module);
    argAA_ = &argAA;
    basicAA_ = &basicAA;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, nullptr);
    }
    argAA_ = nullptr;
    basicAA_ = nullptr;
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopFusion::execute(Module *module, AnalysisManager &AM) {
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    argAA_ = &argAA;
    basicAA_ = &AM.getBasicAA(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, &AM);
    }
    argAA_ = nullptr;
    basicAA_ = nullptr;
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::runOnFunction(Function *func, AnalysisManager *AM) {
    // 每个候选对依次经过形状、边界、调用纯度、标量使用、内存依赖和收益检查。
    // 融合会使 LoopInfo 失效，因此一次只提交一对循环，再用受影响 header 重建工作列表。
    bool everChanged = false;
    LoopInfo localLoopInfo;
    LoopInfo *loopInfo = nullptr;
    if (AM)
        loopInfo = &AM->getLoopInfo(func);
    else {
        localLoopInfo.analyze(func);
        loopInfo = &localLoopInfo;
    }

    AffectedLoopWorklist worklist;
    worklist.seed(*loopInfo);

    while (true) {
        if (loopInfo->allLoops().empty()) break;

        AffineAnalysis AA(*loopInfo);
        DependenceAnalysis DA(*loopInfo, AA);
        DA.setArgAlias(argAA_);
        LoopAccessAnalysis LA(AA);
        CostModel CM(AA);
        LoopInterchangeAnalysis IA(DA, LA, CM);

        bool fused = false;
        BasicBlock *survivingHeader = nullptr;
        while (Loop *L1 = worklist.take(*loopInfo)) {
            Shape s1 = analyzeShape(L1);
            if (!s1.ok) continue;

            std::vector<BasicBlock *> chain;
            Loop *L2 = walkToSibling(s1, L1, *loopInfo, chain);
            if (!L2) continue;
            Shape s2 = analyzeShape(L2);
            if (!s2.ok) continue;
            std::vector<PhiInst *> bypassPhis;
            std::vector<Instruction *> hoist;
            std::vector<Instruction *> sink;

            auto dbg = [&](const char *why) {
                if (debugEnabled())
                    std::cerr << "[LoopFusion] reject " << func->name_ << ": "
                              << s1.header->name_ << " + "
                              << s2.header->name_ << ": " << why << "\n";
            };

            if (!boundsEqual(s1, s2)) { dbg("bounds differ"); continue; }
            if (!callsArePure(L1, L2)) { dbg("impure call in loop"); continue; }
            if (!planChainMotion(L1, L2, s2, chain, bypassPhis,
                                 hoist, sink)) {
                dbg("intervening code");
                continue;
            }
            if (!headerContentSimple(s2)) { dbg("L2 header content"); continue; }
            if (!phiInitsAvailable(L1, s1, L2, s2, chain)) { dbg("L2 phi init"); continue; }
            if (!exitUsesAvailable(L1, s1, L2, s2, chain)) { dbg("exit phi use"); continue; }
            if (!noScalarCrossUse(L1, L2)) { dbg("scalar cross use"); continue; }
            if (!memoryLegal(L1, L2, AA)) { dbg("memory dependence"); continue; }
            if (const char *why = profitabilityRejection(L1, L2, IA)) {
                dbg(why);
                continue;
            }

            if (debugEnabled())
                std::cerr << "[LoopFusion] fuse " << func->name_ << ": "
                          << s1.header->name_ << " + " << s2.header->name_
                          << "\n";
            applyFusion(func, s1, s2, chain, bypassPhis, hoist, sink);
            fused = true;
            everChanged = true;
            survivingHeader = s1.header;
            break;
        }
        if (!fused) break;

        if (AM) {
            AM->clear(func);
            loopInfo = &AM->getLoopInfo(func);
        } else {
            localLoopInfo.analyze(func);
            loopInfo = &localLoopInfo;
        }
        worklist.addNeighborhood(*loopInfo, survivingHeader);
    }
    return everChanged;
}

/**
 * @brief 分析 Shape 的结构、递推或依赖信息。
 * @param L 待检查或变换的循环。
 * @return 返回计算、分析或构造得到的结果。
 */
LoopFusion::Shape LoopFusion::analyzeShape(Loop *L) const {
    Shape s;
    if (!L || !L->hasCanonicalIV()) return s;
    if (!L->preheader || !L->singleLatch() || !L->singleExit()) return s;
    // while 形：唯一 exiting 是 header，唯一 exit 只被 header 跳到。
    if (L->exiting.size() != 1 || L->exiting[0] != L->header) return s;

    BasicBlock *H = L->header;
    BasicBlock *P = L->preheader;
    BasicBlock *Lat = L->singleLatch();
    BasicBlock *E = L->singleExit();

    auto *preTerm = dynamic_cast<BranchInst *>(P->get_terminator());
    if (!preTerm || preTerm->num_ops() != 1 || preTerm->get_operand(0) != H)
        return s;
    auto *latTerm = dynamic_cast<BranchInst *>(Lat->get_terminator());
    if (!latTerm || latTerm->num_ops() != 1 || latTerm->get_operand(0) != H)
        return s;
    auto *hTerm = dynamic_cast<BranchInst *>(H->get_terminator());
    if (!hTerm || hTerm->num_ops() != 3) return s;
    auto *cmp = dynamic_cast<ICmpInst *>(hTerm->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return s;
    if (cmp->get_operand(0) != L->canonicalIV ||
        cmp->get_operand(1) != L->tripCount)
        return s;
    // guard 结果只允许驱动本 header 的分支：L2.header 将被删除，
    // 其值若被循环其它位置引用会留下悬垂 use。
    for (const auto &use : cmp->use_list_) {
        auto *user = use.user_;
        if (!user || user->parent_ != H) return s;
    }

    auto *body = dynamic_cast<BasicBlock *>(hTerm->get_operand(1));
    auto *exit = dynamic_cast<BasicBlock *>(hTerm->get_operand(2));
    if (!body || !exit || !L->blocks.count(body) || L->blocks.count(exit))
        return s;
    if (exit != E) return s;
    if (E->pre_bbs_.size() != 1 || E->pre_bbs_[0] != H) return s;

    // 上界必须在循环外可用（canonical IV 语义已保证，防御性检查）。
    if (definedInLoop(L, L->tripCount)) return s;

    Value *backedge = nullptr;
    PhiInst *iv = L->canonicalIV;
    for (unsigned i = 1; i < iv->num_ops(); i += 2)
        if (iv->get_operand(i) == Lat)
            backedge = iv->get_operand(i - 1);
    if (!backedge) return s;

    s.preheader = P;
    s.header = H;
    s.latch = Lat;
    s.exit = E;
    s.bodyEntry = body;
    s.iv = iv;
    s.bound = L->tripCount;
    s.backedge = backedge;
    s.guard = cmp;
    s.ok = true;
    return s;
}

/**
 * @brief 沿第一循环出口后的简单 CFG 链寻找相邻同级循环。
 * @param s1 第一循环的规范形态描述。
 * @param L1 第一循环。
 * @param LI 当前函数的循环信息。
 * @param chain 写回两个循环之间可旁路或移动的基本块链。
 * @return 找到时返回相邻同级循环，否则返回 nullptr。
 */
Loop *LoopFusion::walkToSibling(const Shape &s1, Loop *L1, LoopInfo &LI,
                                std::vector<BasicBlock *> &chain) const {
    std::map<BasicBlock *, Loop *> preMap;
    for (auto &Lp : LI.allLoops())
        if (Lp->preheader) preMap[Lp->preheader] = Lp.get();

    chain.clear();
    std::set<BasicBlock *> seen;
    BasicBlock *X = s1.exit;
    while (X && seen.insert(X).second && chain.size() < 8) {
        chain.push_back(X);
        auto it = preMap.find(X);
        if (it != preMap.end()) {
            Loop *B = it->second;
            if (B == L1 || B->parent != L1->parent) return nullptr;
            // 除首块（L1.exit 的前驱已在形态检查中确认只有 L1.header）外，
            // 链上每块都必须只有唯一前驱——删除中间块才不会留下悬边。
            for (size_t i = 1; i < chain.size(); i++)
                if (chain[i]->pre_bbs_.size() != 1 ||
                    chain[i]->pre_bbs_[0] != chain[i - 1])
                    return nullptr;
            return B;
        }
        auto *term = dynamic_cast<BranchInst *>(X->get_terminator());
        if (!term || term->num_ops() != 1) return nullptr;
        X = dynamic_cast<BasicBlock *>(term->get_operand(0));
    }
    return nullptr;
}

/**
 * @brief 判断 boundsEqual 所描述的结构、合法性或安全条件是否成立。
 * @param s1 参数 `s1`，用于本函数的分析、匹配或 IR 构造。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::boundsEqual(const Shape &s1, const Shape &s2) const {
    if (s1.bound == s2.bound) return true;
    auto *c1 = dynamic_cast<ConstantInt *>(s1.bound);
    auto *c2 = dynamic_cast<ConstantInt *>(s2.bound);
    return c1 && c2 && c1->value_ == c2->value_;
}

/**
 * @brief 实现 planChainMotion 对应的局部分析或变换辅助逻辑。
 * @param L1 第一个循环。
 * @param L2 第二个循环。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @param chain 参数 `chain`，用于本函数的分析、匹配或 IR 构造。
 * @param bypassPhis 参数 `bypassPhis`，用于本函数的分析、匹配或 IR 构造。
 * @param hoist 参数 `hoist`，用于本函数的分析、匹配或 IR 构造。
 * @param sink 参数 `sink`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::planChainMotion(
    Loop *L1, Loop *L2, const Shape &s2,
    const std::vector<BasicBlock *> &chain,
    std::vector<PhiInst *> &bypassPhis,
    std::vector<Instruction *> &hoist,
    std::vector<Instruction *> &sink) const {
    std::set<BasicBlock *> chainSet(chain.begin(), chain.end());
    std::unordered_map<Value *, Value *> aliases;
    std::set<Value *> dependsOnL1;

    auto resolveAlias = [&](Value *value) {
        std::set<Value *> seen;
        while (aliases.count(value) && seen.insert(value).second)
            value = aliases.at(value);
        return value;
    };
    auto valueDependsOnL1 = [&](Value *value) {
        value = resolveAlias(value);
        return definedInLoop(L1, value) || dependsOnL1.count(value);
    };

    for (auto *X : chain) {
        for (auto *inst : X->instr_list_) {
            if (inst == X->get_terminator()) continue;

            if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
                // 桥接块只有一个前驱，其中的 PHI 只是 LCSSA/边转发节点。
                // 在桥接块删除前，可安全地用唯一 incoming 值旁路该 PHI。
                if (phi->num_ops() != 2 ||
                    phi->get_operand(1) != X->pre_bbs_.front())
                    return false;
                Value *incoming = resolveAlias(phi->get_operand(0));
                aliases[phi] = incoming;
                if (valueDependsOnL1(incoming))
                    dependsOnL1.insert(phi);
                bypassPhis.push_back(phi);
                continue;
            }

            if (!isHoistableInst(inst))
                return false;
            bool dependent = false;
            for (unsigned i = 0; i < inst->num_ops(); i++) {
                Value *v = resolveAlias(inst->get_operand(i));
                if (dynamic_cast<BasicBlock *>(v)) continue;
                if (dynamic_cast<Constant *>(v) ||
                    dynamic_cast<Argument *>(v) ||
                    dynamic_cast<GlobalVariable *>(v))
                    continue;
                auto *def = dynamic_cast<Instruction *>(v);
                if (!def || !def->parent_) return false;
                if (L2->blocks.count(def->parent_))
                    return false;
                dependent |= valueDependsOnL1(v);
            }
            if (dependent) {
                dependsOnL1.insert(inst);
                sink.push_back(inst);
            } else {
                hoist.push_back(inst);
            }
        }
    }

    // 依赖 L1 最终状态的值不能在融合后的 L2 内初始化或执行，因为此时 L1
    // 只推进到当前迭代；它也不能作为 E2 PHI 入值，因为下沉到 E2 的定义
    // 不支配 E2 的 incoming 边。
    auto invalidDependentUse = [&](Value *value) {
        for (const auto &use : value->use_list_) {
            auto *user = use.user_;
            if (!user || !user->parent_ || chainSet.count(user->parent_))
                continue;
            if (L2->blocks.count(user->parent_))
                return true;
            if (user->parent_ == s2.exit && user->is_phi())
                return true;
        }
        return false;
    };
    for (auto *phi : bypassPhis)
        if (dependsOnL1.count(phi) && invalidDependentUse(phi))
            return false;
    for (auto *inst : sink)
        if (invalidDependentUse(inst))
            return false;

    // 上提值不能依赖下沉值。虽然前面的分类按支配顺序传播依赖，这里仍做
    // 防御检查，拒绝不满足支配关系的异常 SSA。
    std::set<Instruction *> sunkSet(sink.begin(), sink.end());
    for (auto *inst : hoist) {
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            auto *def = dynamic_cast<Instruction *>(
                resolveAlias(inst->get_operand(i)));
            if (def && sunkSet.count(def))
                return false;
        }
    }
    return true;
}

/**
 * @brief 判断 callsArePure 所描述的结构、合法性或安全条件是否成立。
 * @param L1 第一个循环。
 * @param L2 第二个循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::callsArePure(Loop *L1, Loop *L2) const {
    for (auto *L : {L1, L2})
        for (auto *bb : L->blocksOrdered)
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call) continue;
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops() - 1));
                if (!callee || !basicAA_ || !basicAA_->isPure(callee))
                    return false;
            }
    return true;
}

/**
 * @brief 实现 noScalarCrossUse 对应的局部分析或变换辅助逻辑。
 * @param L1 第一个循环。
 * @param L2 第二个循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::noScalarCrossUse(Loop *L1, Loop *L2) const {
    for (auto *bb : L2->blocksOrdered)
        for (auto *inst : bb->instr_list_)
            for (unsigned i = 0; i < inst->num_ops(); i++)
                if (definedInLoop(L1, inst->get_operand(i)))
                    return false;
    return true;
}

/**
 * @brief 实现 headerContentSimple 对应的局部分析或变换辅助逻辑。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::headerContentSimple(const Shape &s2) const {
    // L2.header 将被删除，内容只允许 phi + guard + 终止指令。
    for (auto *inst : s2.header->instr_list_) {
        if (inst->is_phi()) continue;
        if (inst == s2.guard || inst == s2.header->get_terminator()) continue;
        return false;
    }
    return true;
}

// 值 v 能否在融合后的 L1.header/L1.preheader 区域使用：
//   常量/参数/全局          → 永远可用；
//   链内定义的纯指令         → 随融合上提到 L1.preheader；
//   L2.header 的 phi        → 随融合迁入 L1.header（仅 exitUses 允许）；
//   L1.header 的指令        → 仅在 asLatchUse=false 时可用（不能当 phi 初值，
//                             否则初值在 preheader 入边上无定义）；
//   两个循环体（非 header）内 → while 形下不可能支配 L2.header，防御性拒绝；
//   其余（两循环之外）       → 到达 L2 的路径必过 L1.preheader，必支配之。
/**
 * @brief 实现 phiInitsAvailable 对应的局部分析或变换辅助逻辑。
 * @param L1 第一个循环。
 * @param s1 参数 `s1`，用于本函数的分析、匹配或 IR 构造。
 * @param L2 第二个循环。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @param chain 参数 `chain`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::phiInitsAvailable(Loop *L1, const Shape &s1, Loop *L2,
                                   const Shape &s2,
                                   const std::vector<BasicBlock *> &chain) const {
    std::set<BasicBlock *> chainSet(chain.begin(), chain.end());
    for (auto *inst : s2.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst == s2.iv) continue;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 1; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i) != s2.preheader) continue;
            Value *v = phi->get_operand(i - 1);
            if (dynamic_cast<Constant *>(v) || dynamic_cast<Argument *>(v) ||
                dynamic_cast<GlobalVariable *>(v))
                continue;
            auto *def = dynamic_cast<Instruction *>(v);
            if (!def || !def->parent_) return false;
            if (chainSet.count(def->parent_)) continue;
            // 初值必须在 L1.preheader 可用：L1/L2 任何块内定义都不行
            // （L1.header 值在 preheader 入边上同样无定义）。
            if (L1->blocks.count(def->parent_) ||
                L2->blocks.count(def->parent_))
                return false;
        }
    }
    return true;
}

/**
 * @brief 实现 exitUsesAvailable 对应的局部分析或变换辅助逻辑。
 * @param L1 第一个循环。
 * @param s1 参数 `s1`，用于本函数的分析、匹配或 IR 构造。
 * @param L2 第二个循环。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @param chain 参数 `chain`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::exitUsesAvailable(Loop *L1, const Shape &s1, Loop *L2,
                                   const Shape &s2,
                                   const std::vector<BasicBlock *> &chain) const {
    std::set<BasicBlock *> chainSet(chain.begin(), chain.end());
    // E2 的 phi 入边由 H2 改接到 H1；bodyEntry 的 phi 入边由 H2 改接到
    // L1.latch。入边值都必须支配新前驱：H1/H1.phi/L2.header phi（迁入 H1）
    // /链内（上提到 P1）/循环外（支配 P1）均可用，其余拒绝。
    auto checkBlock = [&](BasicBlock *bb) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) break;
            for (unsigned i = 1; i < inst->num_ops(); i += 2) {
                if (inst->get_operand(i) != s2.header) continue;
                Value *v = inst->get_operand(i - 1);
                if (dynamic_cast<Constant *>(v) || dynamic_cast<Argument *>(v) ||
                    dynamic_cast<GlobalVariable *>(v))
                    continue;
                auto *def = dynamic_cast<Instruction *>(v);
                if (!def || !def->parent_) return false;
                if (def->parent_ == s2.header && def->is_phi()) continue;
                if (def->parent_ == s1.header) continue;
                if (chainSet.count(def->parent_)) continue;
                if (L1->blocks.count(def->parent_) ||
                    L2->blocks.count(def->parent_))
                    return false;
            }
        }
        return true;
    };
    if (!checkBlock(s2.exit)) return false;
    if (!checkBlock(s2.bodyEntry)) return false;
    return true;
}

/**
 * @brief 实现 memoryLegal 对应的局部分析或变换辅助逻辑。
 * @param L1 第一个循环。
 * @param L2 第二个循环。
 * @param AA 参数 `AA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopFusion::memoryLegal(Loop *L1, Loop *L2, AffineAnalysis &AA) const {
    std::vector<Access> a1 = collectAccesses(L1);
    std::vector<Access> a2 = collectAccesses(L2);
    PhiInst *iv1 = L1->canonicalIV;
    PhiInst *iv2 = L2->canonicalIV;

    auto relation = [&](Value *a, Value *b) -> BaseRelation {
        if (a == b) return BaseRelation::MustAlias;
        bool ag = dynamic_cast<GlobalVariable *>(a) != nullptr;
        bool bg = dynamic_cast<GlobalVariable *>(b) != nullptr;
        if (ag && bg) return BaseRelation::NoAlias;
        bool aa = dynamic_cast<AllocaInst *>(a) != nullptr;
        bool ab = dynamic_cast<AllocaInst *>(b) != nullptr;
        if (aa && ab) return BaseRelation::NoAlias;
        if ((ag && ab) || (aa && bg)) return BaseRelation::NoAlias;
        if (argAA_ && argAA_->noAlias(a, b)) return BaseRelation::NoAlias;
        return BaseRelation::MayAlias;
    };

    for (const Access &x : a1) {
        for (const Access &y : a2) {
            if (!x.isStore && !y.isStore) continue;   // read-read 无依赖
            BaseRelation rel = relation(x.base, y.base);
            if (rel == BaseRelation::NoAlias) continue;
            if (rel == BaseRelation::MayAlias) return false;
            // MustAlias：需要某一维下标等式强制 i1 <= i2，或证明某维恒不等。
            if (!x.gep || !y.gep) return false;   // 同址标量：迭代关系任意
            if (x.gep->num_ops() != y.gep->num_ops()) return false;
            bool independent = false, safe = false;
            for (unsigned d = 1; d < x.gep->num_ops(); d++) {
                AffineExpr e1 = AA.analyze(x.gep->get_operand(d));
                AffineExpr e2 = AA.analyze(y.gep->get_operand(d));
                if (!e1.valid || !e2.valid) continue;   // 该维不提供保证
                int c1 = e1.coeffOf(iv1);
                int c2 = e2.coeffOf(iv2);
                AffineExpr r1 = e1 - AffineExpr::makeIV(iv1) * c1;
                AffineExpr r2 = e2 - AffineExpr::makeIV(iv2) * c2;
                r1.canonicalize();
                r2.canonicalize();
                if (!r1.isConstant() || !r2.isConstant()) continue;
                long k = static_cast<long>(r2.constant) - r1.constant;
                if (c1 == 0 && c2 == 0) {
                    if (k != 0) independent = true;   // 该维恒不等，绝无别名
                } else if (c1 == 1 && c2 == 1) {
                    // 该维等式 ⟺ i1 - i2 = k；k <= 0 时相依对必有 i1 <= i2，
                    // 融合不翻转任何访存先后。
                    if (k <= 0) safe = true;
                }
                // 其它系数形态不提供保证。
            }
            if (!independent && !safe) return false;
        }
    }
    return true;
}

/**
 * @brief 判断融合是否会阻断已经证明有利的循环交换方案。
 * @param L1 第一候选循环。
 * @param L2 第二候选循环。
 * @param IA 循环交换收益与合法性分析。
 * @return 应拒绝融合时返回原因字符串，融合不影响既有方案时返回 nullptr。
 */
const char *LoopFusion::profitabilityRejection(
    Loop *L1, Loop *L2, LoopInterchangeAnalysis &IA) const {
    // LoopInterchange 紧随本 pass。若任一原循环已经有由依赖与 stride
    // 分析证明有利的交换方案，融合会在其循环体中加入另一段 payload：
    // perfect/single-child nest 随之消失，原方案无法再实施。这里保留已知
    // 有利的粗粒度循环形态，而不按函数名、循环名或固定嵌套层数猜测。
    if (IA.analyzeParallelSink(L1).accepted)
        return "would block L1 parallel sink";
    if (IA.analyzeParallelFloat(L1).accepted)
        return "would block L1 parallel float";
    if (IA.analyzeParallelSink(L2).accepted)
        return "would block L2 parallel sink";
    if (IA.analyzeParallelFloat(L2).accepted)
        return "would block L2 parallel float";
    return nullptr;
}

/**
 * @brief 原地执行 applyFusion 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param func 待分析或改写的函数。
 * @param s1 参数 `s1`，用于本函数的分析、匹配或 IR 构造。
 * @param s2 参数 `s2`，用于本函数的分析、匹配或 IR 构造。
 * @param chain 参数 `chain`，用于本函数的分析、匹配或 IR 构造。
 * @param bypassPhis 参数 `bypassPhis`，用于本函数的分析、匹配或 IR 构造。
 * @param hoist 参数 `hoist`，用于本函数的分析、匹配或 IR 构造。
 * @param sink 参数 `sink`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopFusion::applyFusion(
    Function *func, const Shape &s1, const Shape &s2,
    const std::vector<BasicBlock *> &chain,
    const std::vector<PhiInst *> &bypassPhis,
    const std::vector<Instruction *> &hoist,
    const std::vector<Instruction *> &sink) {
    // 到达这里时所有合法性检查均已完成。提交顺序先搬移链上纯指令，
    // 再把第二个循环体接入第一个循环，最后重定向出口 PHI 并删除失效块。
    BasicBlock *P1 = s1.preheader;
    BasicBlock *H1 = s1.header;
    BasicBlock *Lat1 = s1.latch;
    BasicBlock *E1 = s1.exit;
    BasicBlock *P2 = s2.preheader;
    BasicBlock *H2 = s2.header;
    BasicBlock *Lat2 = s2.latch;
    BasicBlock *E2 = s2.exit;
    BasicBlock *B2 = s2.bodyEntry;

    // 1. 单前驱 LCSSA phi 只转发桥接边上的值，先旁路它们。随后把循环外
    //    不变量上提，把依赖 L1 最终状态的纯计算下沉到融合循环出口。
    for (auto *phi : bypassPhis)
        phi->replace_all_use_with(phi->get_operand(0));
    for (auto *inst : hoist) {
        inst->parent_->remove_instr(inst);
        P1->add_instruction_before_terminator(inst);
    }
    Instruction *exitAnchor = nullptr;
    for (auto *inst : E2->instr_list_) {
        if (!inst->is_phi()) {
            exitAnchor = inst;
            break;
        }
    }
    for (auto *inst : sink) {
        inst->parent_->remove_instr(inst);
        E2->add_instruction_before_inst(inst, exitAnchor);
    }

    // 2. iv2 → iv1：两边迭代区间相同，值语义一致。
    s2.iv->replace_all_use_with(s1.iv);

    // 3. L2.header 的非 IV phi 迁入 L1.header（保持 phi 前缀连续），
    //    preheader 入边 P2→P1。
    Instruction *firstNonPhi = nullptr;
    for (auto *inst : H1->instr_list_) {
        if (!inst->is_phi()) { firstNonPhi = inst; break; }
    }
    std::vector<PhiInst *> moved;
    for (auto *inst : H2->instr_list_)
        if (inst->is_phi() && inst != s2.iv)
            moved.push_back(static_cast<PhiInst *>(inst));
    for (auto *phi : moved) {
        H2->remove_instr(phi);
        H1->add_instruction_before_inst(phi, firstNonPhi);
        for (unsigned i = 1; i < phi->num_ops(); i += 2)
            if (phi->get_operand(i) == P2)
                phi->set_operand(i, P1);
    }

    // 4. L1.header 全部 phi 的 latch 入边 Lat1→Lat2（入边值不变：
    //    原值支配 L1.latch，而 L1.latch 融合后支配 L2.latch）。
    for (auto *inst : H1->instr_list_) {
        if (!inst->is_phi()) break;
        for (unsigned i = 1; i < inst->num_ops(); i += 2)
            if (inst->get_operand(i) == Lat1)
                inst->set_operand(i, Lat2);
    }

    // 5. L1.latch 改跳 L2.bodyEntry；bodyEntry 中 phi 的 H2 入边改 L1.latch。
    auto *lat1Term = dynamic_cast<BranchInst *>(Lat1->get_terminator());
    lat1Term->set_operand(0, B2);
    Lat1->remove_succ_basic_block(H1);
    H1->remove_pre_basic_block(Lat1);
    Lat1->add_succ_basic_block(B2);
    B2->add_pre_basic_block(Lat1);
    retargetPhiPred(B2, H2, Lat1);
    B2->remove_pre_basic_block(H2);

    // 6. L2.latch 改跳 L1.header。
    auto *lat2Term = dynamic_cast<BranchInst *>(Lat2->get_terminator());
    lat2Term->set_operand(0, H1);
    Lat2->remove_succ_basic_block(H2);
    H2->remove_pre_basic_block(Lat2);
    Lat2->add_succ_basic_block(H1);
    H1->add_pre_basic_block(Lat2);

    // 两边都是单 body/latch 循环时，融合暂时形成 H1 -> Lat1 -> Lat2
    // -> H1。把无 phi 的线性 Lat2 拼入 Lat1，恢复标准两块 while 形，
    // 让后续规范 unroller 和其它单 latch 分析直接复用。
    bool collapseLatches =
        (!bypassPhis.empty() || !sink.empty()) &&
        B2 == Lat2 && Lat2->pre_bbs_.size() == 1 &&
        Lat2->pre_bbs_.front() == Lat1;
    if (collapseLatches) {
        for (auto *inst : Lat2->instr_list_) {
            if (inst->is_phi()) {
                collapseLatches = false;
                break;
            }
        }
    }
    if (collapseLatches) {
        std::vector<Instruction *> payload;
        for (auto *inst : Lat2->instr_list_)
            if (inst != lat2Term)
                payload.push_back(inst);
        for (auto *inst : payload) {
            Lat2->remove_instr(inst);
            Lat1->add_instruction_before_terminator(inst);
        }

        lat1Term->set_operand(0, H1);
        Lat1->remove_succ_basic_block(Lat2);
        Lat2->remove_pre_basic_block(Lat1);
        Lat2->remove_succ_basic_block(H1);
        H1->remove_pre_basic_block(Lat2);
        Lat1->add_succ_basic_block(H1);
        H1->add_pre_basic_block(Lat1);
        retargetPhiPred(H1, Lat2, Lat1);
    }

    // 7. L1.header 的 exit 边 E1→E2；E2 中 phi 的 H2 入边改 H1。
    auto *h1Term = dynamic_cast<BranchInst *>(H1->get_terminator());
    for (unsigned i = 1; i < h1Term->num_ops(); i++)
        if (h1Term->get_operand(i) == E1)
            h1Term->set_operand(i, E2);
    H1->remove_succ_basic_block(E1);
    E1->remove_pre_basic_block(H1);
    H1->add_succ_basic_block(E2);
    E2->remove_pre_basic_block(H2);
    E2->add_pre_basic_block(H1);
    retargetPhiPred(E2, H2, H1);

    // 8. 中间块与 L2.header 已不可达，连同内部指令一并回收
    //    （死块内已无可被存活区引用的值：phi 已迁出、iv2 已 RAUW、
    //    guard 的 use 仅限 H2 内、链内纯指令已上提）。
    removeUnreachableBlocks(func);

    // 9. iv2 的步进指令（已 RAUW 成 iv1+1）若随 phi 消亡变死，顺手删除。
    if (auto *inc2 = dynamic_cast<Instruction *>(s2.backedge))
        if (inc2->parent_ && inc2->use_list_.empty())
            inc2->parent_->delete_instr(inc2);
}
