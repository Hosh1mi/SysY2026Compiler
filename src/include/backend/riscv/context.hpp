#pragma once
// RISC-V 单函数代码生成上下文（朴素 / -O0 路径）。
//
// 朴素策略：为每个产生结果的 IR 值（含函数形参与 alloca）分配一个栈槽，所有
// 计算都"从槽加载操作数 → 用固定临时寄存器计算 → 写回结果槽"，不跨指令保留
// 寄存器。这样既简单又正确，构成后端框架的基线；寄存器分配等优化在此之上叠加。
//
// 栈帧布局（以帧指针 s0 为基准，s0 = 进入函数时的 sp）：
//   高地址  ┌───────────────────────┐  <- s0（= 调用者 sp）
//          │ 入参溢出区（调用者放置）│  s0+0, s0+8, ...
//          ├───────────────────────┤  <- s0
//          │ 保存的 ra             │  s0-8
//          │ 保存的 s0             │  s0-16
//          │ 局部值/alloca 槽       │  s0-16-...（向下增长）
//          │ phi 并行拷贝临时槽     │
//          ├───────────────────────┤
//          │ 出参溢出区            │  sp+0, sp+8, ...
//   低地址  └───────────────────────┘  <- sp（函数内固定不变）

#include "../../mid/ir/ir.hpp"
#include "machine.hpp"
#include "target.hpp"

#include <map>
#include <string>
#include <vector>

class RiscvFuncContext {
public:
    RiscvFuncContext(Function *f, riscv::MFunction &mfunc, bool enableRegAlloc);
    void generate();

private:
    // 值在栈上的存储类别，决定 load/store 选用 lw/sw、ld/sd 还是 flw/fsw。
    enum class SlotKind { Int32, Pointer, Float };

    // ── 栈槽分配（预扫描阶段填充）──────────────────────────────────────
    void planFrame();
    SlotKind slotKindOf(Value *v) const;
    int sizeOfType(Type *ty) const;

    // ── 发射辅助 ───────────────────────────────────────────────────────
    void emit(const std::string &text);
    void emitLabel(const std::string &label);
    void emitMem(const std::string &text, bool load);
    void emitCall(const std::string &text);
    void emitTerminator(const std::string &text);

    // s0 相对栈帧寻址的 load/store，自动处理超出 imm12 范围的偏移。
    void emitLoadSlot(const std::string &reg, int off, SlotKind kind);
    void emitStoreSlot(const std::string &reg, int off, SlotKind kind);
    static const char *memMnemonic(SlotKind kind, bool load);

    // ── 取值到寄存器 / 写回栈槽 ────────────────────────────────────────
    // 把整型/指针值 v 加载到 reg（常量 li、全局 la、alloca addi、否则读槽）。
    void loadInt(Value *v, const std::string &reg);
    // 把地址类值 v（指针）加载到 reg。
    void loadAddr(Value *v, const std::string &reg);
    // 把浮点值 v 加载到浮点寄存器 freg。
    void loadFloat(Value *v, const std::string &freg);
    // 把寄存器写回 v 的结果槽。
    void storeResult(Value *v, const std::string &reg);

    std::string blockLabel(BasicBlock *bb) const;
    int slotOf(Value *v);  // 返回 v 的 s0 相对偏移（必要时即时分配）

    // ── 各类指令选择 ───────────────────────────────────────────────────
    void emitPrologue();
    void emitEpilogue();
    void emitBlock(BasicBlock *bb);
    void emitInstruction(Instruction *inst);
    void emitBinary(Instruction *inst);
    void emitFloatBinary(Instruction *inst);
    void emitICmp(ICmpInst *inst);
    void emitFCmp(FCmpInst *inst);
    void emitBranch(BranchInst *inst);
    void emitReturn(ReturnInst *inst);
    void emitCall(CallInst *inst);
    void emitGep(GetElementPtrInst *inst);
    void emitLoad(LoadInst *inst);
    void emitStore(StoreInst *inst);
    void emitConvert(Instruction *inst);
    void emitSelect(SelectInst *inst);

    // phi 解析：在从 pred 跳转到 succ 的边上做并行拷贝（两遍：先读全部源到临时
    // 槽，再写各 phi 结果槽，避免同块内 phi 互相覆盖）。
    void emitPhiCopies(BasicBlock *pred, BasicBlock *succ);
    bool succHasPhi(BasicBlock *succ) const;

    Function *func_;
    riscv::MFunction &mfunc_;
    bool enableRegAlloc_;

    std::map<Value *, int> slots_;  // Value* → s0 相对偏移（负）
    int frameSize_ = 0;
    int localCursor_ = 0;   // 已分配的局部字节数（自 s0-16 向下）
    int outArgBytes_ = 0;   // 出参溢出区字节数
    int phiTempBase_ = 0;   // phi 临时槽区起始（s0 相对偏移）
    int phiTempBytes_ = 0;  // phi 临时槽区字节数

    int edgeCounter_ = 0;   // 关键边/select 拷贝块的唯一标号

    BasicBlock *curBlock_ = nullptr;  // 当前正在发射的基本块（phi 拷贝定位用）
};
