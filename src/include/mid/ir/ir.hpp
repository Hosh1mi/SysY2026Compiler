#pragma once

// IR 公共聚合头：按依赖顺序引入对象模型和构建器。只需要完整 IR API 的调用方
// 可以包含本文件；实现细节明确的代码应优先包含更小的专用头文件。

#include "type.hpp"
#include "value.hpp"
#include "constant.hpp"
#include "module.hpp"
#include "globalVariable.hpp"
#include "function.hpp"
#include "basicBlock.hpp"
#include "instruction.hpp"
#include "irBuilder.hpp"
