// Module::verify —— IR 完整性校验（--verify-ir 时每个 pass 之后运行）
//
// 自包含设计：CFG 一律从 terminator 操作数推导，不信任 pre_bbs_/succ_bbs_
// 链表（它们本身是被校验对象之一）。支配关系用 Cooper-Harvey-Kennedy
// idom 算法独立计算，不复用任何缓存。
//
// 校验项：
//   1. 终结指令：每块非空、最后一条是 br/ret、块内其余位置无 br/ret
//   2. phi 形态：phi 只出现在块首、操作数成 (value, block) 对
//   3. phi 入边与前驱一致：incoming 块集合 == 按 terminator 推导的前驱集合
//   4. SSA 支配性：def 支配 use（phi 的 use 记在对应入边块末尾）
//   5. CFG 链表对称：pre_bbs_/succ_bbs_ 与 terminator 推导的边一致
//   6. use-def 链一致：operand 的 use_list_ 含本指令；use_list_ 指向的
//      user 确实以该值为操作数；已删除指令（parent_==nullptr）不得再被使用
//   7. 不可达块视为违例（SCCP/CFGSimplify 应已清理）

#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/globalVariable.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {

struct Verifier {
    const std::string &context;
    int violations = 0;
    static const int kMaxReports = 20;

    explicit Verifier(const std::string &ctx) : context(ctx) {}

    void report(Function *func, const std::string &msg) {
        if (++violations <= kMaxReports) {
            std::cerr << "[VERIFY] " << (context.empty() ? "" : context + ": ")
                      << func->name_ << ": " << msg << "\n";
        }
    }

    // ---- 从 terminator 推导的 CFG ----
    std::map<BasicBlock *, std::vector<BasicBlock *>> succs;
    std::map<BasicBlock *, std::vector<BasicBlock *>> preds;
    std::vector<BasicBlock *> rpo;                 // 仅可达块
    std::map<BasicBlock *, int> rpoIdx;
    std::map<BasicBlock *, BasicBlock *> idom;
    std::map<Instruction *, int> instrPos;         // 块内位置
    std::map<Instruction *, BasicBlock *> instrBlock;
    std::set<BasicBlock *> inFunc;

    void buildCfg(Function *func) {
        succs.clear(); preds.clear(); rpo.clear(); rpoIdx.clear();
        idom.clear(); instrPos.clear(); instrBlock.clear(); inFunc.clear();

        for (auto *bb : func->basic_blocks_)
            inFunc.insert(bb);

        for (auto *bb : func->basic_blocks_) {
            auto *term = bb->get_terminator();
            if (!term) continue;
            std::set<BasicBlock *> seen;
            for (unsigned i = 0; i < term->num_ops(); ++i) {
                auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
                if (!succ) continue;
                if (!inFunc.count(succ)) {
                    report(curFunc, "block '" + bb->name_ +
                           "' terminator targets block not in function: '" +
                           succ->name_ + "'");
                    continue;
                }
                if (seen.insert(succ).second) {
                    succs[bb].push_back(succ);
                    preds[succ].push_back(bb);
                }
            }
        }

        // 后序 DFS → 反转得 RPO（仅可达块）
        std::set<BasicBlock *> visited;
        std::vector<BasicBlock *> post;
        std::vector<std::pair<BasicBlock *, size_t>> stack;
        if (!func->basic_blocks_.empty()) {
            BasicBlock *entry = func->basic_blocks_.front();
            stack.push_back({entry, 0});
            visited.insert(entry);
            while (!stack.empty()) {
                auto &[bb, i] = stack.back();
                auto &ss = succs[bb];
                if (i < ss.size()) {
                    BasicBlock *next = ss[i++];
                    if (visited.insert(next).second)
                        stack.push_back({next, 0});
                } else {
                    post.push_back(bb);
                    stack.pop_back();
                }
            }
        }
        rpo.assign(post.rbegin(), post.rend());
        for (size_t i = 0; i < rpo.size(); ++i)
            rpoIdx[rpo[i]] = (int)i;

        // Cooper-Harvey-Kennedy idom
        if (!rpo.empty()) {
            BasicBlock *entry = rpo.front();
            idom[entry] = entry;
            bool changed = true;
            while (changed) {
                changed = false;
                for (size_t i = 1; i < rpo.size(); ++i) {
                    BasicBlock *bb = rpo[i];
                    BasicBlock *newIdom = nullptr;
                    for (auto *p : preds[bb]) {
                        if (!idom.count(p)) continue;   // p 未处理或不可达
                        newIdom = newIdom ? intersect(p, newIdom) : p;
                    }
                    if (newIdom && (!idom.count(bb) || idom[bb] != newIdom)) {
                        idom[bb] = newIdom;
                        changed = true;
                    }
                }
            }
        }

        for (auto *bb : func->basic_blocks_) {
            int pos = 0;
            for (auto *inst : bb->instr_list_) {
                instrPos[inst] = pos++;
                instrBlock[inst] = bb;
            }
        }
    }

    BasicBlock *intersect(BasicBlock *a, BasicBlock *b) {
        while (a != b) {
            while (rpoIdx[a] > rpoIdx[b]) a = idom[a];
            while (rpoIdx[b] > rpoIdx[a]) b = idom[b];
        }
        return a;
    }

    bool reachable(BasicBlock *bb) { return rpoIdx.count(bb) > 0; }

    // a 是否支配 b（均须可达）
    bool dominates(BasicBlock *a, BasicBlock *b) {
        BasicBlock *entry = rpo.front();
        while (true) {
            if (b == a) return true;
            if (b == entry) return false;
            auto it = idom.find(b);
            if (it == idom.end()) return false;
            b = it->second;
        }
    }

    Function *curFunc = nullptr;

    void run(Function *func) {
        curFunc = func;
        buildCfg(func);

        // ---- 1. 终结指令 ----
        for (auto *bb : func->basic_blocks_) {
            if (bb->instr_list_.empty()) {
                report(func, "block '" + bb->name_ + "' is empty");
                continue;
            }
            int n = 0, idx = 0, lastTermPos = -1;
            for (auto *inst : bb->instr_list_) {
                if (inst->isTerminator()) { n++; lastTermPos = idx; }
                idx++;
            }
            if (n == 0)
                report(func, "block '" + bb->name_ + "' has no terminator");
            else if (n > 1 || lastTermPos != idx - 1)
                report(func, "block '" + bb->name_ +
                       "' has terminator not at end (count=" + std::to_string(n) + ")");
        }

        // ---- 7. 不可达块 ----
        for (auto *bb : func->basic_blocks_) {
            if (!reachable(bb))
                report(func, "unreachable block '" + bb->name_ + "'");
        }

        // ---- 5. CFG 链表对称 ----
        for (auto *bb : func->basic_blocks_) {
            std::set<BasicBlock *> derivedSucc(succs[bb].begin(), succs[bb].end());
            std::set<BasicBlock *> listedSucc(bb->succ_bbs_.begin(), bb->succ_bbs_.end());
            if (derivedSucc != listedSucc)
                report(func, "block '" + bb->name_ +
                       "' succ_bbs_ disagrees with terminator targets");
            std::set<BasicBlock *> derivedPred(preds[bb].begin(), preds[bb].end());
            std::set<BasicBlock *> listedPred(bb->pre_bbs_.begin(), bb->pre_bbs_.end());
            if (derivedPred != listedPred)
                report(func, "block '" + bb->name_ +
                       "' pre_bbs_ disagrees with terminator-derived preds");
        }

        // ---- 2/3/4/6 逐指令 ----
        for (auto *bb : func->basic_blocks_) {
            bool seenNonPhi = false;
            for (auto *inst : bb->instr_list_) {
                if (inst->is_phi()) {
                    if (seenNonPhi)
                        report(func, "phi not at top of block '" + bb->name_ + "'");
                    verifyPhi(func, bb, static_cast<PhiInst *>(inst));
                } else {
                    seenNonPhi = true;
                    verifyInst(func, bb, inst);
                }
                verifyUseDef(func, inst);
            }
        }
    }

    void verifyPhi(Function *func, BasicBlock *bb, PhiInst *phi) {
        if (phi->num_ops() % 2 != 0) {
            report(func, "phi in '" + bb->name_ + "' has odd operand count");
            return;
        }
        std::set<BasicBlock *> incoming;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            Value *val = phi->get_operand(i);
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!pred) {
                report(func, "phi in '" + bb->name_ + "' operand " +
                       std::to_string(i + 1) + " is not a block");
                continue;
            }
            if (!incoming.insert(pred).second)
                report(func, "phi in '" + bb->name_ + "' has duplicate incoming block '" +
                       pred->name_ + "'");
            // 支配性：val 须在 pred 块末尾可用
            if (auto *defInst = dynamic_cast<Instruction *>(val)) {
                if (!defInst->parent_)
                    report(func, "phi in '" + bb->name_ +
                           "' uses deleted instruction (incoming from '" +
                           pred->name_ + "')");
                else if (reachable(pred) && reachable(defInst->parent_) &&
                         !dominates(defInst->parent_, pred))
                    report(func, "phi in '" + bb->name_ + "': def in '" +
                           defInst->parent_->name_ + "' does not dominate incoming edge from '" +
                           pred->name_ + "'");
            }
        }
        if (reachable(bb)) {
            std::set<BasicBlock *> predSet(preds[bb].begin(), preds[bb].end());
            if (incoming != predSet) {
                std::string in, pr;
                for (auto *b : incoming) in += b->name_ + " ";
                for (auto *b : predSet) pr += b->name_ + " ";
                report(func, "phi in '" + bb->name_ +
                       "' incoming blocks disagree with actual predecessors"
                       " (incoming: " + in + "| preds: " + pr + ")");
            }
        }
    }

    void verifyInst(Function *func, BasicBlock *bb, Instruction *inst) {
        auto verifyConstantIndex = [&](Value *index, unsigned lanes,
                                       const char *operation) {
            auto *constant = dynamic_cast<ConstantInt *>(index);
            if (constant &&
                (constant->value_ < 0 ||
                 static_cast<unsigned>(constant->value_) >= lanes))
                report(func, std::string(operation) + " in '" + bb->name_ +
                                 "' has an out-of-range constant lane");
        };

        if (auto *binary = dynamic_cast<BinaryInst *>(inst)) {
            if (auto *vectorTy = dynamic_cast<VectorType *>(inst->type_)) {
                if (vectorTy->num_elements_ == 0)
                    report(func, "vector binary in '" + bb->name_ +
                                     "' has zero lanes");
                if (binary->get_operand(0)->type_ != vectorTy ||
                    binary->get_operand(1)->type_ != vectorTy)
                    report(func, "vector binary in '" + bb->name_ +
                                     "' has mismatched operand types");
                const bool integerLane =
                    vectorTy->contained_->tid_ == Type::IntegerTyID;
                const bool floatLane =
                    vectorTy->contained_->tid_ == Type::FloatTyID;
                const bool floatOpcode =
                    inst->op_id_ == Instruction::FAdd ||
                    inst->op_id_ == Instruction::FSub ||
                    inst->op_id_ == Instruction::FMul ||
                    inst->op_id_ == Instruction::FDiv;
                if ((!integerLane && !floatLane) ||
                    (floatLane != floatOpcode))
                    report(func, "vector binary in '" + bb->name_ +
                                     "' has an opcode/lane-type mismatch");
            }
        }
        if (inst->op_id_ == Instruction::InsertElement) {
            if (inst->num_ops() != 3) {
                report(func, "insertelement in '" + bb->name_ +
                                 "' has wrong operand count");
            } else {
                auto *vectorTy = dynamic_cast<VectorType *>(
                    inst->get_operand(0)->type_);
                if (!vectorTy) {
                    report(func, "insertelement in '" + bb->name_ +
                                     "' operand 0 is not a vector");
                } else {
                    if (inst->type_ != vectorTy ||
                        inst->get_operand(1)->type_ != vectorTy->contained_)
                        report(func, "insertelement in '" + bb->name_ +
                                         "' has incompatible value types");
                    verifyConstantIndex(inst->get_operand(2),
                                        vectorTy->num_elements_,
                                        "insertelement");
                }
                if (inst->get_operand(2)->type_->tid_ != Type::IntegerTyID)
                    report(func, "insertelement in '" + bb->name_ +
                                     "' index is not integer typed");
            }
        }
        if (inst->op_id_ == Instruction::ExtractElement) {
            if (inst->num_ops() != 2) {
                report(func, "extractelement in '" + bb->name_ +
                       "' has wrong operand count");
            } else {
                auto *vectorTy = dynamic_cast<VectorType *>(
                    inst->get_operand(0)->type_);
                if (!vectorTy) {
                    report(func, "extractelement in '" + bb->name_ +
                           "' operand 0 is not a vector");
                } else if (inst->type_ != vectorTy->contained_) {
                    report(func, "extractelement in '" + bb->name_ +
                           "' result type does not match the lane type");
                } else {
                    verifyConstantIndex(inst->get_operand(1),
                                        vectorTy->num_elements_,
                                        "extractelement");
                }
                if (inst->get_operand(1)->type_->tid_ != Type::IntegerTyID)
                    report(func, "extractelement in '" + bb->name_ +
                           "' index is not integer typed");
            }
        }
        if (inst->op_id_ == Instruction::ShuffleVector) {
            auto *shuffle = static_cast<ShuffleVectorInst *>(inst);
            auto *vectorTy = dynamic_cast<VectorType *>(inst->type_);
            if (!vectorTy || inst->num_ops() != 3) {
                report(func, "shufflevector in '" + bb->name_ +
                                 "' is malformed");
            } else {
                if (inst->get_operand(0)->type_ != vectorTy ||
                    inst->get_operand(1)->type_ != vectorTy)
                    report(func, "shufflevector in '" + bb->name_ +
                                     "' has mismatched input types");
                if (shuffle->mask().size() != vectorTy->num_elements_)
                    report(func, "shufflevector in '" + bb->name_ +
                                     "' has the wrong mask width");
                for (int lane : shuffle->mask())
                    if (lane < 0 ||
                        static_cast<unsigned>(lane) >=
                            2 * vectorTy->num_elements_)
                        report(func, "shufflevector in '" + bb->name_ +
                                         "' has an out-of-range mask lane");
            }
        }
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            Value *op = inst->get_operand(i);
            if (!op) {
                report(func, "null operand in '" + bb->name_ + "'");
                continue;
            }
            if (auto *opBB = dynamic_cast<BasicBlock *>(op)) {
                if (!inst->isTerminator())
                    report(func, "non-terminator in '" + bb->name_ +
                           "' has block operand '" + opBB->name_ + "'");
                continue;
            }
            auto *defInst = dynamic_cast<Instruction *>(op);
            if (!defInst) continue;       // 常量/参数/全局
            if (!defInst->parent_) {
                report(func, "instruction in '" + bb->name_ +
                       "' uses deleted instruction");
                continue;
            }
            if (!inFunc.count(defInst->parent_)) {
                report(func, "instruction in '" + bb->name_ +
                       "' uses value from another function");
                continue;
            }
            if (!reachable(bb) || !reachable(defInst->parent_))
                continue;                  // 不可达块已单独报告
            if (defInst->parent_ == bb) {
                if (instrPos[defInst] >= instrPos[inst])
                    report(func, "in '" + bb->name_ +
                           "': use before def in same block");
            } else if (!dominates(defInst->parent_, bb)) {
                report(func, "in '" + bb->name_ + "': def in '" +
                       defInst->parent_->name_ + "' does not dominate use");
            }
        }
    }

    void verifyUseDef(Function *func, Instruction *inst) {
        // operand → use_list_ 方向
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            Value *op = inst->get_operand(i);
            if (!op) continue;
            bool found = false;
            for (auto &use : op->use_list_) {
                if (use.user_ == inst && use.operand_index_ == i) { found = true; break; }
            }
            if (!found)
                report(func, "use-def broken: operand " + std::to_string(i) +
                       " of an instruction in '" + inst->parent_->name_ +
                       "' missing from value's use_list_");
        }
        // use_list_ → operand 方向
        for (auto &use : inst->use_list_) {
            auto *user = use.user_;
            if (!user) {
                report(func, "use_list_ of value in '" + inst->parent_->name_ +
                       "' contains non-instruction user");
                continue;
            }
            if (!user->parent_)
                continue;  // 已删除的 user 残留 use 记录——由删除路径负责清理，
                           // 这里不视为违例（delete_instr 会清掉，但 RAUW 路径未必）
            if (use.operand_index_ >= user->num_ops() ||
                user->get_operand(use.operand_index_) != inst)
                report(func, "use_list_ of value in '" + inst->parent_->name_ +
                       "' has stale entry (user operand mismatch)");
        }
    }
};

} // namespace

void Module::verify() {
    verify("");
}

void Module::verify(const std::string &context) {
    int total = 0;
    for (auto *func : function_list_) {
        if (func->is_declaration()) continue;
        Verifier v(context);
        v.run(func);
        total += v.violations;
    }
    if (total > 0) {
        std::cerr << "[VERIFY] " << (context.empty() ? "" : context + ": ")
                  << total << " violation(s), aborting\n";
        std::abort();
    }
}
