#include "../../include/mid/opt/indVarStrengthReduce.hpp"
#include <stack>

void IndVarStrengthReduce::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

void IndVarStrengthReduce::runOnFunction(Function *func) {
    dom.clear();
    idom.clear();
    computeDominators(func);
    auto loops = findLoops(func);

    Module *module = func->parent_;
    for (auto &loop : loops) {
        processLoop(loop, func, module);
    }
}

// -----------------------------------------------------------------------
// 支配树计算
// -----------------------------------------------------------------------
void IndVarStrengthReduce::computeDominators(Function *func) {
    auto entryBB = func->basic_blocks_.front();

    std::set<BasicBlock *> allBlocks;
    for (auto bb : func->basic_blocks_) allBlocks.insert(bb);

    for (auto bb : func->basic_blocks_) {
        if (bb == entryBB)
            dom[bb] = {entryBB};
        else
            dom[bb] = allBlocks;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb == entryBB) continue;
            std::set<BasicBlock *> newDom = allBlocks;
            bool first = true;
            for (auto pred : bb->pre_bbs_) {
                if (first) {
                    newDom = dom[pred];
                    first = false;
                } else {
                    std::set<BasicBlock *> temp;
                    std::set_intersection(newDom.begin(), newDom.end(),
                                         dom[pred].begin(), dom[pred].end(),
                                         std::inserter(temp, temp.begin()));
                    newDom = temp;
                }
            }
            newDom.insert(bb);
            if (newDom != dom[bb]) {
                dom[bb] = newDom;
                changed = true;
            }
        }
    }

    for (auto bb : func->basic_blocks_) {
        if (bb == entryBB) {
            idom[bb] = nullptr;
            continue;
        }
        for (auto d : dom[bb]) {
            if (d == bb) continue;
            bool isIdom = true;
            for (auto other : dom[bb]) {
                if (other == bb || other == d) continue;
                if (dom[d].count(other)) {
                    isIdom = false;
                    break;
                }
            }
            if (isIdom) {
                idom[bb] = d;
                break;
            }
        }
    }
}

// -----------------------------------------------------------------------
// 循环检测：找回边 → 自然循环
// -----------------------------------------------------------------------
std::vector<IndVarStrengthReduce::Loop> IndVarStrengthReduce::findLoops(Function *func) {
    std::vector<Loop> loops;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!dom[bb].count(succ)) continue;
            if (bb == succ) continue;

            Loop loop;
            loop.header = succ;
            loop.latch = bb;

            std::set<BasicBlock *> loopBlocks;
            loopBlocks.insert(succ);

            if (bb != succ) {
                std::stack<BasicBlock *> worklist;
                worklist.push(bb);
                loopBlocks.insert(bb);

                while (!worklist.empty()) {
                    auto cur = worklist.top();
                    worklist.pop();
                    for (auto pred : cur->pre_bbs_) {
                        if (!loopBlocks.count(pred)) {
                            loopBlocks.insert(pred);
                            worklist.push(pred);
                        }
                    }
                }
            }

            loop.blocks = loopBlocks;

            BasicBlock *externalPred = nullptr;
            int extCount = 0;
            for (auto pred : succ->pre_bbs_) {
                if (!loopBlocks.count(pred)) {
                    externalPred = pred;
                    extCount++;
                }
            }
            loop.preheader = (extCount == 1) ? externalPred : nullptr;

            loops.push_back(loop);
        }
    }

    return loops;
}

// -----------------------------------------------------------------------
// 判断值是否在循环中不变
// -----------------------------------------------------------------------
bool IndVarStrengthReduce::isLoopInvariant(Value *val, const std::set<BasicBlock *> &loopBlocks) {
    if (dynamic_cast<Constant *>(val)) return true;
    if (dynamic_cast<Argument *>(val)) return true;
    if (dynamic_cast<GlobalVariable *>(val)) return true;

    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst) return false;

    return !loopBlocks.count(inst->parent_);
}

// -----------------------------------------------------------------------
// 检查 value 通过 phi 链是否最终归结为 target
// -----------------------------------------------------------------------
static bool resolvesTo(Value *val, Value *target, std::set<Value *> &visited) {
    if (val == target) return true;
    if (visited.count(val)) return false;

    auto *phi = dynamic_cast<PhiInst *>(val);
    if (!phi) return false;

    visited.insert(val);
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto subVisited = visited;
        if (!resolvesTo(phi->get_operand(i), target, subVisited))
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// 获取 value 在 preheader 中对应的可用值
// 若 value 是循环中的 phi，返回来自 preheader 的入边值；否则返回自身
// -----------------------------------------------------------------------
static Value *getEntryValue(Value *val, const std::set<BasicBlock *> &loopBlocks,
                             std::set<Value *> &visited) {
    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst || !loopBlocks.count(inst->parent_)) return val;

    auto *phi = dynamic_cast<PhiInst *>(val);
    if (!phi) return nullptr;
    if (!visited.insert(val).second) return val;

    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        Value *incoming = phi->get_operand(i);
        auto *inInst = dynamic_cast<Instruction *>(incoming);
        if (!inInst || !loopBlocks.count(inInst->parent_))
            return incoming;
    }
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        Value *result = getEntryValue(phi->get_operand(i), loopBlocks, visited);
        auto *rInst = dynamic_cast<Instruction *>(result);
        if (!rInst || !loopBlocks.count(rInst->parent_))
            return result;
    }
    return val;
}

// -----------------------------------------------------------------------
// 识别 SSA 基本归纳变量：phi(init, add/sub(X, stride)) 其中 X 归结为 phi
// -----------------------------------------------------------------------
std::vector<IndVarStrengthReduce::BasicIV> IndVarStrengthReduce::findBasicIVs(Loop &loop) {
    std::vector<BasicIV> ivs;

    // 1) SSA 基本归纳变量：phi(init, add/sub(X, stride)) 其中 X 归结为 phi
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);

        if (phi->type_->tid_ != Type::IntegerTyID) continue;

        Value *insideVal = nullptr;
        Value *outsideVal = nullptr;
        BasicBlock *insideBB = nullptr;

        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (loop.blocks.count(pred)) {
                if (insideVal) { insideVal = nullptr; break; }
                insideVal = phi->get_operand(i);
                insideBB = pred;
            } else {
                if (outsideVal) { outsideVal = nullptr; break; }
                outsideVal = phi->get_operand(i);
            }
        }

        if (!insideVal || !outsideVal || !insideBB) continue;

        auto *insideInst = dynamic_cast<Instruction *>(insideVal);
        if (!insideInst) continue;
        if (!insideInst->is_add() && !insideInst->is_sub()) continue;

        Value *op0 = insideInst->get_operand(0);
        Value *op1 = insideInst->get_operand(1);

        Value *stride = nullptr;
        bool isAdd = insideInst->is_add();

        {
            std::set<Value *> vis;
            if (resolvesTo(op0, phi, vis) && isLoopInvariant(op1, loop.blocks))
                stride = op1;
        }
        if (!stride && isAdd) {
            std::set<Value *> vis;
            if (resolvesTo(op1, phi, vis) && isLoopInvariant(op0, loop.blocks))
                stride = op0;
        }

        if (!stride || !dynamic_cast<ConstantInt *>(stride)) continue;

        BasicIV iv;
        iv.phi = phi;
        iv.initVal = outsideVal;
        iv.stride = stride;
        iv.isAdd = isAdd;
        iv.latch = insideBB;
        iv.updateInst = insideInst;
        ivs.push_back(iv);
    }

    return ivs;
}

// -----------------------------------------------------------------------
// 确保循环有唯一 preheader
// -----------------------------------------------------------------------
BasicBlock *IndVarStrengthReduce::ensurePreheader(Loop &loop, Function *func, Module *module) {
    if (loop.preheader) return loop.preheader;

    auto *preheader = new BasicBlock(module, "label_ivsr_ph", func);

    std::vector<BasicBlock *> externalPreds;
    for (auto pred : loop.header->pre_bbs_) {
        if (!loop.blocks.count(pred))
            externalPreds.push_back(pred);
    }

    for (auto pred : externalPreds) {
        auto *term = pred->get_terminator();
        for (unsigned i = 0; i < term->num_ops_; i++) {
            if (term->get_operand(i) == loop.header)
                term->set_operand(i, preheader);
        }
        pred->remove_succ_basic_block(loop.header);
        pred->add_succ_basic_block(preheader);
        preheader->add_pre_basic_block(pred);
        loop.header->remove_pre_basic_block(pred);
    }

    auto *builder = new IRStmtBuilder(preheader, module);
    builder->create_br(loop.header);
    delete builder;

    preheader->add_succ_basic_block(loop.header);
    loop.header->add_pre_basic_block(preheader);

    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (auto extPred : externalPreds) {
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i + 1) == extPred) {
                    Value *val = phi->get_operand(i);
                    phi->add_phi_pair_operand(val, preheader);
                    phi->remove_operands(i, i + 1);
                    break;
                }
            }
        }
    }

    return preheader;
}

// -----------------------------------------------------------------------
// 计算 GEP 中 IV 推进 1 时，地址需要增加多少元素
// 例如 A[N][M] 中 IV 在索引 1 (i) 时步长为 M，在索引 2 (j) 时步长为 1
// -----------------------------------------------------------------------
static int computeElementStride(GetElementPtrInst *gep, unsigned ivOpIdx) {
    Type *ty = static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
    for (unsigned i = 1; i < ivOpIdx; i++) {
        if (auto *arrTy = dynamic_cast<ArrayType *>(ty))
            ty = arrTy->contained_;
        else
            return 1;
    }
    int stride = 0;
    if (auto *arrTy = dynamic_cast<ArrayType *>(ty)) {
        stride = arrTy->num_elements_;
        Type *inner = arrTy->contained_;
        while (auto *ia = dynamic_cast<ArrayType *>(inner)) {
            stride *= ia->num_elements_;
            inner = ia->contained_;
        }
    }
    return stride > 0 ? stride : 1;
}

// -----------------------------------------------------------------------
// 主体：对循环中的派生 IV 进行强度削弱
// -----------------------------------------------------------------------
void IndVarStrengthReduce::processLoop(Loop &loop, Function *func, Module *module) {
    // Skip loops that already contain vector operations — converting
    // GEPs to pointer phis in these loops causes register spills because
    // the new phis compete with vector registers.
    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            if (inst->type_->tid_ == Type::VectorTyID) return;
        }
    }

    auto ivs = findBasicIVs(loop);
    if (ivs.empty()) return;

    BasicBlock *preheader = ensurePreheader(loop, func, module);
    auto *builder = new IRStmtBuilder(preheader, module);

    for (auto &iv : ivs) {
        struct GEPCandidate {
            GetElementPtrInst *gep;
            unsigned ivOpIdx;
        };
        std::vector<GEPCandidate> candidates;

        for (auto bb : loop.blocks) {
            for (auto inst : bb->instr_list_) {
                if (!inst->is_gep()) continue;
                auto *gep = static_cast<GetElementPtrInst *>(inst);

                unsigned ivOpIdx = 0;
                bool found = false;
                for (unsigned i = 1; i < gep->num_ops_; i++) {
                    std::set<Value *> vis;
                    if (resolvesTo(gep->get_operand(i), iv.phi, vis)) {
                        ivOpIdx = i;
                        found = true;
                        break;
                    }
                }
                if (!found) continue;

                bool ok = true;
                for (unsigned i = 1; i < gep->num_ops_; i++) {
                    if (i == ivOpIdx) continue; 
                    if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
        candidates.push_back({gep, ivOpIdx});
            }
        }

        // 对每个候选 GEP 执行强度削弱替换
        for (auto &c : candidates) {
            auto *gep = c.gep;
            if (!gep->parent_) continue;

            std::vector<Value *> initIndices;
            bool failed = false;
            for (unsigned i = 1; i < gep->num_ops_; i++) {
                Value *idx = gep->get_operand(i);
                if (i == c.ivOpIdx) {
                    initIndices.push_back(iv.initVal);
                } else {
                    // 被保留的索引必须在 preheader 中可用：若是 phi 则取其 preheader 入边值
                    auto entryVis = std::set<Value *>{};
                    Value *entryVal = getEntryValue(idx, loop.blocks, entryVis);
                    if(!entryVal) {
                        failed = true;
                        break;
                    }
                    initIndices.push_back(entryVal);
                }
            }
            if (failed) continue;
            // 在 preheader 中创建初始 GEP（先构造后移入，保证位置在 br 之前）
            builder->set_insert_point(preheader);
            auto *initGEP = builder->create_gep(gep->get_operand(0), initIndices);
            preheader->remove_instr(initGEP);
            preheader->add_instruction_before_terminator(initGEP);

            // 创建 phi 并插入循环头前端
            auto *addrPhi = PhiInst::create_phi(gep->type_, loop.header);
            loop.header->remove_instr(addrPhi);
            loop.header->add_instruction_front(addrPhi);

            addrPhi->addIncoming(initGEP, preheader);

            // 在 latch 中创建步进指令（在 br 之前）
            int elemStride = computeElementStride(gep, c.ivOpIdx);
            // 将标量步长归一化到 addrPhi 所指类型的元素单位
            Type *addrTy = static_cast<PointerType*>(addrPhi->type_)->contained_;
            while (auto *arrTy = dynamic_cast<ArrayType*>(addrTy)) {
                elemStride /= arrTy->num_elements_;
                addrTy = arrTy->contained_;
            }
            if (elemStride <= 0) elemStride = 1;
            // Account for the IV's actual stride (e.g. vector loops advance by VF=4).
            int ivStrideVal = 1;
            if (auto *ci = dynamic_cast<ConstantInt*>(iv.stride))
                ivStrideVal = ci->value_;
            int effectiveStride = ivStrideVal * elemStride;
            if (!iv.isAdd) effectiveStride = -effectiveStride;
            builder->set_insert_point(iv.latch);
            auto *incrGEP = builder->create_gep(addrPhi, {new ConstantInt(module->int32_ty_, effectiveStride)});
            iv.latch->remove_instr(incrGEP);
            iv.latch->add_instruction_before_terminator(incrGEP);

            addrPhi->addIncoming(incrGEP, iv.latch);

            // 多 latch 场景（如 continue）：其他 latch 的前驱用 phi 自身
            for (auto pred : loop.header->pre_bbs_) {
                if (pred == preheader || pred == iv.latch) continue;
                if (loop.blocks.count(pred)) addrPhi->addIncoming(addrPhi, pred);
            }

            gep->replace_all_use_with(addrPhi);
            gep->parent_->delete_instr(gep);
        }

    }

    delete builder;
}
