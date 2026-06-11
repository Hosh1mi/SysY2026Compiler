#pragma once
// CFG 公共工具：供各 pass 共享的清理函数。
// （SCCP/CFGSimplify 内部仍各有一份 static 历史副本，后续可统一迁移到这里。）

class Function;

// 删除从入口不可达的基本块。可达性沿 terminator 的基本块操作数传播
// （不信任 succ_bbs_），并同步清理存活块的 phi 入边与 pre/succ 链表。
// 不可达子图必须及时删除：其 phi 会被后续 pass 收缩成自引用指令，
// 使 InstCombine 等基于"def 链有限"假设的改写无法终止。
void removeUnreachableBlocks(Function *func);
