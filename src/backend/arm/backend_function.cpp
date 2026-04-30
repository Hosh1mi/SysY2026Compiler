#include "../../../include/backend/arm/arm_builder.hpp"

void ArmBuilder::analyzeFunction(FuncContext &ctx) {
    assignStackSlots(ctx);
    collectPhiMoves(ctx);
}

void ArmBuilder::assignStackSlots(FuncContext &ctx) {
    for (auto *arg : ctx.func->arguments_) allocateSlot(ctx, arg, typeSize(arg->type_), 8);
    for (auto *bb : ctx.func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_void()) allocateSlot(ctx, inst, std::max<size_t>(4, typeSize(inst->type_)), 8);
            if (auto *call = dynamic_cast<CallInst *>(inst)) ctx.max_call_args = std::max<int>(ctx.max_call_args, static_cast<int>(call->num_ops_ - 1));
        }
    }
    const int call_area = std::max(0, ctx.max_call_args - 8) * 8;
    ctx.frame_size = alignUp(ctx.frame_size + 16 + call_area, 16);
}

void ArmBuilder::collectPhiMoves(FuncContext &ctx) {
    for (auto *bb : ctx.func->basic_blocks_) {
        for (auto *succ : bb->succ_bbs_) ctx.preds[succ].push_back(bb);
    }
    for (auto *bb : ctx.func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) continue;
            for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
                auto *val = phi->get_operand(i);
                auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                ctx.edge_moves[pred].push_back(PhiMove{phi, val});
            }
        }
    }
}

std::string ArmBuilder::emitFunction(Function *func) {
    ArmFuncContext ctx;
    ctx.func = func;
    if (func->is_declaration()) return "";
    analyzeFunction(ctx);
    emitPrologue(ctx);
    emitFunctionBody(ctx);
    emitEpilogue(ctx);
    std::ostringstream out;
    out << ".global " << func->name_ << "\n";
    out << func->name_ << ":\n" << ctx.prologue.str() << ctx.text.str();
    return out.str();
}

void ArmBuilder::emitPrologue(FuncContext &ctx) {
    // 函数序言：保存帧指针和返回地址。
    ctx.prologue << "    stp x29, x30, [sp, #-16]!\n";
    ctx.prologue << "    mov x29, sp\n";
    if (ctx.frame_size > 16) ctx.prologue << "    sub sp, sp, #" << (ctx.frame_size - 16) << "\n";
    // 将形参保存到后续统一访问的栈槽里。
    for (size_t i = 0; i < ctx.func->arguments_.size(); ++i) {
        auto *arg = ctx.func->arguments_[i];
        auto off = ctx.slots[arg].offset;
        if (isFloatTy(arg->type_)) ctx.prologue << "    str s" << i << ", [x29, #-" << (off + 4) << "]\n";
        else ctx.prologue << "    str w" << i << ", [x29, #-" << (off + 4) << "]\n";
    }
}

void ArmBuilder::emitEpilogue(FuncContext &ctx) {
    ctx.text << ".Lreturn_" << ctx.func->name_ << ":\n";
    if (ctx.frame_size > 16) ctx.text << "    add sp, sp, #" << (ctx.frame_size - 16) << "\n";
    ctx.text << "    ldp x29, x30, [sp], #16\n";
    ctx.text << "    ret\n";
}
