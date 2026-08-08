#pragma once
// IR 语义标记（semantic markers）：把源级语义（如 const 不变性）与分析证明的事实
// （函数纯度、参数只读）持久承载在 Value 上，并在文本 IR 中打印，供后续优化
// （LICM / GVN 等）直接消费，避免各 pass 反复重建相同结论。
//
// 统一用一个 uint32_t 位集合承载（见 Value::sem_flags_），新增一类标记 = 新增一个
// 位 + 一处打印分支，避免给各子类逐个加 bool 的补丁式扩散。

#include <cstdint>

enum class SemFlag : uint32_t {
    None            = 0,
    // (1) const 内存不变性
    ImmutableObject = 1u << 0,  // AllocaInst / GlobalVariable：初始化后内容永不被改写
    ImmutableLoad   = 1u << 1,  // LoadInst：读取 immutable 对象，且处于其初始化区之后
    // (2) 函数 / 参数属性（由 BasicAliasAnalysis 证明后持久化）
    FnPure          = 1u << 2,  // Function：无副作用（既不写内存也无可观察读）
    FnReadOnly      = 1u << 3,  // Function：可能读内存，但从不写
    ArgReadOnly     = 1u << 4,  // Argument：callee 从不经此指针写
    ArgNoCapture    = 1u << 5,  // Argument：指针不逃逸出 callee（暂保留，未启用）
    // (3) 源级结构信息
    SrcConstArray   = 1u << 6,  // AllocaInst：来自源级 `const` 数组声明
    // (4) 整数 IR 语义位
    NoSignedWrap    = 1u << 7,  // add/sub/mul/shl：有符号不溢出
    NoUnsignedWrap  = 1u << 8,  // add/sub/mul/shl：无符号不溢出
    Exact           = 1u << 9,  // ashr/lshr/div：无信息丢失（如被 2^k 整除）
    Disjoint        = 1u << 10, // or：两侧置位集合互斥
    VectorizedEpilogue = 1u << 11, // BasicBlock：该标量循环已作为向量余数循环
    TargetPointerRecurrenceLoop = 1u << 12,
        // BasicBlock：循环已显式构造目标化指针递推，IVSR 不应重写
    HiraRepetitionFolded = 1u << 13,
        // BasicBlock：Hira 已将循环的重复加性递推折叠为单次执行
    MemsetIdiomLoop = 1u << 14, // BasicBlock：preheader 已插入 memset 替换原循环
    KnownNonNegative = 1u << 15, // Value：所有到达该值的路径上均为非负 i32
    ScalarExpansionCompute = 1u << 16,
        // BasicBlock：标量展开后、依赖已由 scratch 分离的计算循环
    WavefrontCoincident = 1u << 17,
        // BasicBlock：wavefront 调度已证明该循环维度内不存在跨迭代依赖
};

inline SemFlag operator|(SemFlag a, SemFlag b) {
    return static_cast<SemFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
