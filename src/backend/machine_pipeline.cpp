// 组装本地 MachineFunction 流水线。这里的顺序属于后端约定，保持线性排列，
// 便于直接观察各表示阶段之间的转换。
#include "../include/backend/machine_pipeline.hpp"

#include "../include/backend/cfg_optimizations.hpp"
#include "../include/backend/codegen.hpp"
#include "../include/backend/frame_lowering.hpp"
#include "../include/backend/post_ra_optimizations.hpp"
#include "../include/backend/pre_ra_optimizations.hpp"
#include "../include/backend/regalloc.hpp"
#include "../include/backend/scheduler.hpp"
#include "../include/backend/spill_optimization.hpp"

namespace backend::aarch64 {
namespace {

bool runPhysicalCleanup(MachineFunction &function) {
	bool changed = false;
	for (;;) {
		bool roundChanged = false;
		roundChanged |= PostRASpillSlotOptimizer::run(function);
		roundChanged |= PostRACopyPropagation::run(function);
		roundChanged |= PostRARedundantCopyElimination::run(function);
		if (!roundChanged)
			return changed;
		changed = true;
	}
}

} // namespace

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          const BackendOptions &options) {
	const bool optimize = options.optimizationLevel >= 1;

	if (optimize) {
		// 复用块内相同常量，但不跨越调用延长其活跃区间。
		// 例如：两条 `MOVi32 #7` 共用第一条指令定义的虚拟寄存器。
		pipeline.addPass("MachineConstantCSE", &MachineConstantCSE::run);
		// 如果标量常量只供 DUP 广播使用，就直接在向量寄存器组中构造它。
		// 例如：`MOVi32 #255; DUP` 改成一条可编码的向量立即数指令。
		pipeline.addPass("AArch64VectorImmediateSelection",
		                 &AArch64VectorImmediateSelection::run);
		// 在后续分析依赖指令形状前，统一无效运算的表示。
		// 例如：`add %x, #0` 改成 COPY，并删除物理寄存器自复制。
		pipeline.addPass("AArch64PreRAPeephole",
		                 &AArch64PreRAPeephole::run);
		// 地址仍为 SSA 值时，合并同一基址上可以配对的访存。
		// 例如：偏移 0 和 4 的两次加载改成一条成对加载。
		pipeline.addPass("AArch64LoadStoreOptimization",
		                 &AArch64LoadStoreOptimization::run);
		// 固定栈加载超过寄存器组容量时，在块内延后单次使用值的定义。
		// 例如：把入口参数加载移动到它的唯一使用点之前。
		pipeline.addPass("MachineSSALocalSink", &MachineSSALocalSink::run);
		// 单次使用的值只允许越过单前驱边下沉，避免引入新的路径语义。
		// 例如：把分支块中的 `MOVi32 #c` 移到唯一使用它的后继块。
		pipeline.addPass("SinglePredecessorMaterializationSink",
		                 &SinglePredecessorMaterializationSink::run);
		// 把产生地址的 SSA 指令折叠进可编码的访存操作数。
		// 例如：`%p = base + 16; load [%p]` 改成 `load [base, #16]`。
		pipeline.addPass("AArch64PreRAAddressingFolder",
		                 &PreRAAddressingFolder::run);
		// 直接复用原比较产生的标志，避免先物化布尔值再比较。
		// 例如：CMP+CSET+CMP+Bcc 改成使用原条件的 CMP+Bcc。
		pipeline.addPass("AArch64ConditionOptimizer",
		                 &AArch64ConditionOptimizer::run);
		// 复用块内距离较近的纯表达式，并限制距离以控制寄存器压力。
		// 例如：重复的 `add %a, %b` 直接使用前一次计算结果。
		pipeline.addPass("MachineExpressionCSE", &MachineExpressionCSE::run);
		// 删除结果无使用且没有副作用的虚拟寄存器定义。
		// 例如：删除 CSE 后遗留的无用常量构造。
		pipeline.addPass("MachineDCE", &DeadMachineInstructionElimination::run);
	}

	// PHI 消除属于表示转换，不是可选优化：拆分多后继入边，并把 PHI
	// 转换成各条入边上的复制。
	pipeline.addPass("PHIElimination", &PhiElimination::run);

	if (optimize) {
		// PHI 消除会产生新的单前驱边块，因此再次下沉物化指令。
		pipeline.addPass("PostPhiMaterializationSink",
		                 &SinglePredecessorMaterializationSink::run);
		// 把循环不变量整数常量移动到真实的循环前置块。
		// 例如：循环内的 `MOVi32 #c` 改为进入循环前只执行一次。
		pipeline.addPass("PostPhiConstantHoisting", &PostPhiConstantHoisting::run);

		// 外提可能让相同常量汇聚到同一块，再次合并新出现的重复定义。
		pipeline.addPass("MachineConstantCSEAfterHoisting",
		                 &MachineConstantCSE::run);
		// 如果外提后的值最终只有一个受控使用点，再下沉以缩短活跃区间。
		pipeline.addPass("PostPhiMaterializationSinkAfterHoisting",
		                 &SinglePredecessorMaterializationSink::run);
		// 改用直接测试数值的分支形式，并删除多余的标志计算。
		// 例如：`and x, #8; cmp ..., #0; b.eq` 改成 `tbz x, #3`。
		pipeline.addPass("AArch64BranchFolding", &AArch64BranchFolding::run);
		// 用尾零数量批量执行经过严格匹配的连续折半循环。
		// 例如：多次 `x >>= 1; n++` 改成一次变量移位和一次加法。
		pipeline.addPass("AArch64ExactHalvingLoopOptimizer",
		                 &AArch64ExactHalvingLoopOptimizer::run);
	}

	// 删除从入口不可达的基本块，同时清理只属于这些块的虚拟寄存器。
	pipeline.addPass("UnreachableMachineBlockElimination",
	                 &UnreachableMachineBlockElimination::run);

	if (optimize) {
		// 在寄存器、标志和内存别名依赖约束下，对屏障分隔的区域做列表调度；
		// 默认按延迟调度，只有寄存器压力严格改善时才采用压力优先顺序。
		pipeline.addPass("A53PreRAScheduler", &A53MachineScheduler::run);
	}

	// 寄存器分配及其必需的物理寄存器修复在所有优化等级下执行。
	pipeline.addPass("GraphColoringRegisterAllocator",
	                 &GraphColoringRegisterAllocator::run);
	// 物理寄存器确定后解析参数和调用复制组；遇到 x0<-x1、x1<-x0
	// 这样的环时，用临时寄存器断环。
	pipeline.addPass("PostRAParallelCopyResolver",
	                 &PostRAParallelCopyResolver::run);
	// 修复破坏式指令的物理寄存器绑定约束。
	// 例如：INS 的旧向量与结果颜色不同时，在 INS 前插入 COPY。
	pipeline.addPass("AArch64FinalizeTiedOperands",
	                 &PostRAInstructionExpansion::run);
	if (optimize) {
		// 迭代执行溢出槽转发、物理复制传播和自复制删除，因为一种清理
		// 可能继续暴露另一种清理机会。
		pipeline.addPass("PostRAPhysicalCleanup", &runPhysicalCleanup);
		// 在不改变调用者/被调用者保存类别的前提下，按寄存器编号奇偶
		// 重新着色相互独立的标量浮点乘法值链。
		pipeline.addPass("A53FPRegisterBalancing",
		                 &A53FPRegisterBalancing::run);
	} else {
		// 即使在 O0，寄存器分配也可能产生物理寄存器自复制。
		pipeline.addPass("PostRARedundantCopyElimination",
		                 &PostRARedundantCopyElimination::run);
	}

	// 确定栈偏移、消除帧索引，并生成函数序言和尾声。
	pipeline.addPass("AArch64FrameLowering", &AArch64FrameLowering::run);
	if (optimize) {
		// 溢出槽和物理基址确定后重新优化地址。
		// 例如：配对相邻栈访问，或把 `load; base += n` 折叠成后索引访存。
		pipeline.addPass("AArch64PostRAAddressingOptimizer",
		                 &PostRAAddressingOptimizer::run);
		// 构造优先布局链、删除空转发块，并通过删除或反转分支让选定后继
		// 成为顺序落空路径。
		pipeline.addPass("MachineBlockPlacement", &MachineBlockPlacement::run);
	}
	// 目标寄存器确定后，把整数常量伪指令展开成最短的 MOVZ/MOVN/MOVK
	// 或逻辑立即数序列。
	pipeline.addPass("AArch64ExpandConstantMaterialization",
	                 &PostRAInstructionExpansion::expandConstantMaterializations);
	if (optimize) {
		// 使用物理寄存器依赖和最终指令延迟再次调度。
		pipeline.addPass("A53PostRAScheduler", &A53MachineScheduler::run);
	}
	// 分支范围修复不可省略：反转越界条件分支，再通过可编码的无条件
	// 跳转到原目标。
	pipeline.addPass("AArch64BranchRelaxation", &AArch64BranchRelaxation::run);
}

} // namespace backend::aarch64
