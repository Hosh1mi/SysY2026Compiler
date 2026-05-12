#pragma once
#include "pass.hpp"

class LoopIdiomRecognition : public Pass {
public:
    void execute(Module *module) override;

private:
    // ---------- 循环模式识别 ----------
    /// 识别实现两个 i32 按位逻辑运算的循环函数
    /// 返回对应的 OpID (And/Or/Xor)，若不是则返回 -1
    int detectBitwiseLoopPattern(Function *func);

    /// 识别“按参数移位”函数：根据第二个参数分派，对第一个参数乘/除 2 的幂
    /// 返回值：0=不是，1=左移（shl），2=右移（lshr）
    int detectShiftByParamPattern(Function *func);

    // ---------- 调用折叠 ----------
    void foldBitwiseLoopCalls(Module *module);
    void foldShiftCalls(Module *module);

    // ---------- 算术优化 ----------
    void optimizeSDivByPowerOfTwo(Module *module);
    void constantFolding(Module *module);

    // ---------- 辅助 ----------
    void makeDeclaration(Function *func);
    void safeDeleteInst(Instruction *inst);
    bool isConstInt(Value *v, int val);
    bool getConstIntValue(Value *v, int &val);

    // 返回函数参数类型对应的位宽（假设只有 i32，返回 32）
    int getTypeWidth(Function *func);
};