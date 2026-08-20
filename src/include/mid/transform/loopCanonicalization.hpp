/**
 * @file loopCanonicalization.hpp
 * @brief 声明循环规范化入口，把自然循环整理为后续区域变换所要求的标准 CFG 形态。
 * @details 关键约束是任何新增 CFG 边都必须有匹配的 PHI 入值，且调用方在结构变化后应失效循环与支配分析。
 */

#pragma once

class Function;

/**
 * @brief 规范化函数内所有自然循环的控制流结构。
 * @param function 待处理的函数；声明函数和空函数不会传入此接口。
 * @return 至少插入或重连了一个基本块时返回 true，否则返回 false。
 *
 * 变换会依次保证每个循环拥有专用预头、单一回边和专用退出边。插入新块时会
 * 同步修复分支目标、CFG 前驱/后继列表及 PHI 入边。调用方应在返回 true 后使
 * LoopInfo、支配树等依赖 CFG 的分析失效。
 */
bool canonicalizeLoopForm(Function *function);
