#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>

// LoopInterchange (matmul-only):
// 识别 3 层 i→j→k 矩阵乘法模式：
//     for i:
//       for j:
//         sum = 0
//         for k: sum += C[i][k] * A[k][j]
//         D[i][j] = sum
// 变换为：
//     for i:
//       clear  __mm_tmp[0..N)
//       for k:
//         c_ik = C[i][k]
//         for j: __mm_tmp[j] += c_ik * A[k][j]
//       for j: D[i][j] = __mm_tmp[j]
// 将列访问 A[k][j] 转为顺序访问，cache line 利用率 1/16 → 16/16。
class LoopInterchange : public Pass {
public:
    void execute(Module *module) override;

private:
    struct Loop {
        BasicBlock *header = nullptr;
        BasicBlock *latch  = nullptr;
        BasicBlock *preheader = nullptr;
        BasicBlock *exit   = nullptr;
        BasicBlock *body_entry = nullptr;  // 条件为真时跳进的第一个 body 块
        PhiInst    *iv_phi = nullptr;       // 计数 IV phi
        Value      *bound  = nullptr;       // 上界（icmp 右操作数）
        std::set<BasicBlock *> blocks;
    };

    void runOnFunction(Function *func);
    std::vector<Loop> findLoops(Function *func);
    std::vector<BasicBlock *> computeRPO(Function *func);
    void computeDominators(const std::vector<BasicBlock *> &rpo);
    BasicBlock *intersect(BasicBlock *a, BasicBlock *b);
    bool dominates(BasicBlock *a, BasicBlock *b);

    // Returns true and fills loop.iv_phi/bound/exit/body_entry if loop is a
    // canonical "for (iv=0; iv<bound; iv++)" with iv_phi as the only counting
    // induction variable.
    bool analyzeCanonicalLoop(Loop &loop);

    bool tryInterchange(Loop &i_loop, Loop &j_loop, Loop &k_loop, Module *module);

    GlobalVariable *getOrCreateTempBuffer(Module *module);

    std::map<BasicBlock *, BasicBlock *> idom_;
    std::map<BasicBlock *, int>          rpoIdx_;
    GlobalVariable                      *temp_buf_ = nullptr;  // @__mm_tmp[1024]
};
