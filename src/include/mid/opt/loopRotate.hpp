#pragma once
// LoopRotate —— 将 while 形态旋转为 do-while。
//
// 把出口测试移到 latch，形成更利于 LICM / unroll 等的规范形态。
//
// 典型支持形式：
//   while (i < n) { body; i++; } → do { body; i++; } while (i < n)
//   header 出口测试下沉到 latch，入口侧补上首次 guard
//
// 含 call 时常跳过；已是规范 IV 且主要为 IRCE 保留 guard 时也可能不旋转。

#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

#include <map>
#include <vector>

class LoopRotate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopRotate"; }

private:
    bool runOnFunction(Function *func);
    bool rotateLoop(Loop *loop, Function *func);

    Instruction *cloneInstruction(Instruction *inst, BasicBlock *dest,
                                  const std::map<Value *, Value *> &valueMap);
    BasicBlock *splitExitEdge(Function *func, BasicBlock *pred,
                              BasicBlock *exit);
};
