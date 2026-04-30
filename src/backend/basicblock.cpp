#include "../../include/backend/arm_builder.hpp"

void ArmBuilder::emitFunctionBody(ArmFuncContext &ctx) {
    for (auto *bb : ctx.func->basic_blocks_) emitBasicBlock(ctx, bb);
}

void ArmBuilder::emitBasicBlock(ArmFuncContext &ctx, BasicBlock *bb) {
    ctx.current_bb = bb;
    ctx.text << escapeLabel(bb->name_) << ":\n";
    for (auto *inst : bb->instr_list_) emitInstruction(ctx, inst);
    for (auto *succ : bb->succ_bbs_) emitPhiCopiesForSuccessor(ctx, succ);
}

void ArmBuilder::emitParallelMoves(ArmFuncContext &ctx, const std::vector<ArmFuncContext::PhiMove> &moves) {
    if (moves.empty()) return;
    std::vector<ArmFuncContext::PhiMove> pending = moves;
    while (!pending.empty()) {
        bool progress = false;
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            bool blocked = false;
            for (auto &other : pending) {
                if (&other == &(*it)) continue;
                if (other.dst == it->src) { blocked = true; break; }
            }
            if (!blocked) {
                emitValueToReg(ctx, it->src, "x16", isFloatTy(it->src->type_));
                emitStoreFromReg(ctx, it->dst, isFloatTy(it->src->type_) ? "s16" : "w16", isFloatTy(it->src->type_));
                pending.erase(it);
                progress = true;
                break;
            }
        }
        if (progress) continue;
        auto m = pending.front();
        // 遇到环时，使用临时寄存器打破并行搬运的依赖环。
        emitValueToReg(ctx, m.src, "x17", isFloatTy(m.src->type_));
        ctx.text << "    mov x18, x17\n";
        emitStoreFromReg(ctx, m.dst, isFloatTy(m.src->type_) ? "s18" : "w18", isFloatTy(m.src->type_));
        pending.erase(pending.begin());
    }
}

void ArmBuilder::emitPhiCopiesForSuccessor(ArmFuncContext &ctx, BasicBlock *succ) {
    auto it = ctx.edge_moves.find(ctx.current_bb);
    if (it == ctx.edge_moves.end()) return;
    std::vector<ArmFuncContext::PhiMove> moves;
    for (auto &m : it->second) {
        if (!m.dst || !m.src) continue;
        auto *phi = dynamic_cast<PhiInst *>(m.dst);
        if (!phi || phi->parent_ != succ) continue;
        for (unsigned i = 1; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i) == ctx.current_bb) {
                moves.push_back(m);
                break;
            }
        }
    }
    emitParallelMoves(ctx, moves);
}
