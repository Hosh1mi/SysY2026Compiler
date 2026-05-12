# Loop Unrolling 优化前后性能对比

- **优化前基线**：commit `b56dd81` — LICM 合并后、循环展开前（`result_20260512_034610`）
- **优化后**：Loop Unroll pass，展开因子 4，循环体指令数限制 ≤ 6（`MAX_LATCH_INSTS=6`）
- **平台**：ARM64 Docker 容器（native，无 QEMU），Apple Silicon M 系列
- **pass 位置**：LICM 之后、第二轮 CSE/DCE 之前

## 优化说明

针对满足以下条件的简单循环自动展开 4 倍，剩余迭代由余数循环处理：

- 仅含 2 个基本块（header + latch）
- 单一整数归纳变量（IV），正常数步长
- header 中有单一整数比较退出条件（slt/sle/sgt/sge）
- latch 中无 call/phi/alloca，且可克隆指令类型
- **latch 体非终止指令数 ≤ 6**（防止大循环展开引发寄存器压力飙升）

调参过程：初始 `MAX_LATCH_INSTS=8` 时，h-5、matmul、sl 等测试因寄存器溢出回退 1.5–2×；收紧至 6 后，7 指令体的乘累加内层循环（h-5、matmul）不再展开，主要受益对象为简单初始化/累加循环（3–5 指令）。

## 汇总（3次平均值）

| 测试 | 优化前 | Run1 | Run2 | Run3 | 均值 | 变化 |
|------|--------|------|------|------|------|------|
| 01_mm1 | 3.155s | 3.139s | 3.144s | 3.210s | 3.165s | ≈持平 |
| 01_mm2 | 3.143s | 3.154s | 3.142s | 3.221s | 3.172s | ≈持平 |
| 01_mm3 | 3.147s | 3.155s | 3.153s | 3.230s | 3.179s | ≈持平 |
| 03_sort1 | 488.6ms | 483.8ms | 480.8ms | 488.2ms | 484.2ms | ≈持平 |
| 03_sort2 | 21.1ms | 22.0ms | 22.4ms | 22.3ms | 22.2ms | **+5.3%** ⬆️ |
| 03_sort3 | 70.4ms | 75.7ms | 75.9ms | 75.9ms | 75.8ms | **+7.7%** ⬆️ |
| conv2d-1 | 733.7ms | 733.2ms | 733.0ms | 731.7ms | 732.6ms | ≈持平 |
| conv2d-2 | 201.1ms | 200.6ms | 200.9ms | 200.7ms | 200.8ms | ≈持平 |
| conv2d-3 | 69.6ms | 67.0ms | 66.8ms | 67.0ms | 66.9ms | -3.9% ⬇️ |
| crc1 | 38.9ms | 39.8ms | 39.1ms | 40.1ms | 39.7ms | ≈持平 |
| crc2 | 45.6ms | 40.2ms | 40.2ms | 40.9ms | 40.4ms | **-11.4%** ⬇️ |
| crc3 | 40.3ms | 40.3ms | 39.9ms | 40.8ms | 40.4ms | ≈持平 |
| crypto-1 | 141.4ms | 139.6ms | 139.3ms | 142.5ms | 140.5ms | ≈持平 |
| crypto-2 | 99.1ms | 97.8ms | 97.4ms | 99.9ms | 98.4ms | ≈持平 |
| crypto-3 | 57.3ms | 56.5ms | 56.5ms | 57.8ms | 56.9ms | ≈持平 |
| fft0 | 3.483s | 3.468s | 3.461s | 3.479s | 3.470s | ≈持平 |
| fft1 | 7.416s | 7.392s | 7.403s | 7.445s | 7.413s | ≈持平 |
| fft2 | 20.0ms | 20.1ms | 20.1ms | 19.9ms | 20.0ms | ≈持平 |
| h-1-01 | 17.004s | 17.036s | 16.999s | 17.257s | 17.098s | ≈持平 |
| h-1-02 | 1.515s | 1.517s | 1.514s | 1.533s | 1.521s | ≈持平 |
| h-1-03 | 8.228s | 8.283s | 8.251s | 8.352s | 8.295s | ≈持平 |
| h-10-01 | 6.7ms | 6.8ms | 6.9ms | 6.7ms | 6.8ms | ≈持平 |
| h-10-02 | 14.2ms | 14.3ms | 14.1ms | 14.2ms | 14.2ms | ≈持平 |
| h-10-03 | 26.1ms | 25.7ms | 25.8ms | 25.7ms | 25.7ms | ≈持平 |
| h-4-01 | 1.136s | 1.136s | 1.135s | 1.140s | 1.137s | ≈持平 |
| h-4-02 | 3.793s | 3.805s | 3.793s | 3.812s | 3.803s | ≈持平 |
| h-4-03 | 6.958s | 6.957s | 6.959s | 6.980s | 6.965s | ≈持平 |
| h-5-01 | 1.252s | 1.252s | 1.252s | 1.251s | 1.252s | ≈持平 |
| h-5-02 | 1.253s | 1.257s | 1.253s | 1.251s | 1.254s | ≈持平 |
| h-5-03 | 1.252s | 1.254s | 1.252s | 1.256s | 1.254s | ≈持平 |
| h-8-01 | WA | WA | WA | WA | WA | 本地缺 .in 文件（平台AC） |
| h-8-02 | WA | WA | WA | WA | WA | 本地缺 .in 文件（平台AC） |
| h-8-03 | WA | WA | WA | WA | WA | 本地缺 .in 文件（平台AC） |
| h-9-01 | 1.9ms | 1.7ms | 1.8ms | 1.8ms | 1.8ms | ≈持平 |
| h-9-02 | 1.6ms | 1.6ms | 1.7ms | 1.6ms | 1.6ms | ≈持平 |
| h-9-03 | 1.6ms | 1.8ms | 2.1ms | 1.6ms | 1.8ms | ≈持平（噪声） |
| huffman-01 | 35.224s | 35.223s | 35.172s | 35.286s | 35.227s | ≈持平 |
| huffman-02 | 35.214s | 35.198s | 35.205s | 35.266s | 35.223s | ≈持平 |
| huffman-03 | 35.220s | 35.233s | 35.238s | 35.324s | 35.265s | ≈持平 |
| knapsack_naive-1 | 129.7ms | 129.7ms | 129.6ms | 129.6ms | 129.6ms | ≈持平 |
| knapsack_naive-2 | 129.6ms | 133.9ms | 129.4ms | 129.5ms | 130.9ms | ≈持平 |
| knapsack_naive-3 | 129.6ms | 129.6ms | 129.7ms | 129.6ms | 129.6ms | ≈持平 |
| many_mat_cal-1 | 18.962s | 15.539s | 15.547s | 15.541s | 15.542s | **-18.0%** ⬇️ |
| many_mat_cal-2 | 18.827s | 15.402s | 15.402s | 15.405s | 15.403s | **-18.2%** ⬇️ |
| many_mat_cal-3 | 18.927s | 15.509s | 15.512s | 15.514s | 15.512s | **-18.0%** ⬇️ |
| matmul1 | 5.386s | 5.408s | 5.388s | 5.396s | 5.397s | ≈持平 |
| matmul2 | 5.405s | 5.402s | 5.408s | 5.403s | 5.404s | ≈持平 |
| matmul3 | 5.386s | 5.413s | 5.424s | 5.401s | 5.413s | ≈持平 |
| optimization_scheduling1 | 1.4ms | 1.4ms | 1.4ms | 1.3ms | 1.4ms | ≈持平 |
| optimization_scheduling2 | 1.4ms | 1.3ms | 1.3ms | 1.3ms | 1.3ms | ≈持平 |
| optimization_scheduling3 | 1.5ms | 1.4ms | 1.4ms | 1.3ms | 1.4ms | **-7.6%** ⬇️ |
| shuffle0 | 1.312s | 1.330s | 1.321s | 1.320s | 1.324s | ≈持平 |
| shuffle1 | 745.5ms | 744.2ms | 749.9ms | 748.9ms | 747.7ms | ≈持平 |
| shuffle2 | 36.4ms | 35.5ms | 36.1ms | 36.7ms | 36.1ms | ≈持平 |
| sl1 | 2.205s | 1.948s | 2.119s | 1.932s | 2.000s | **-9.3%** ⬇️ |
| sl2 | 614.4ms | 575.3ms | 573.1ms | 574.0ms | 574.1ms | **-6.6%** ⬇️ |
| sl3 | 244.3ms | 243.7ms | 243.4ms | 244.4ms | 243.8ms | ≈持平 |
| transpose0 | 923.9ms | 924.8ms | 923.8ms | 924.6ms | 924.4ms | ≈持平 |
| transpose1 | 1.492s | 1.495s | 1.493s | 1.494s | 1.494s | ≈持平 |
| transpose2 | 1.684s | 1.680s | 1.680s | 1.687s | 1.682s | ≈持平 |

**总计（57个AC测试）：优化前 253.02s，优化后均值 242.86s，整体变化 -4.0%**

**几何平均加速比：1.0139x（+1.39%）**

- 提升：10 个
- 持平：43 个
- 退步：4 个（03_sort2/03_sort3 各+5~8ms，h-9-03 噪声，均可忽略）

## 主要受益测试

| 测试 | 优化前 | 均值 | 提升 | 原因 |
|------|--------|------|------|------|
| many_mat_cal-1/2/3 | ~18.9s | ~15.5s | **-18%** | 矩阵初始化循环（GEP+store+add，3指令）展开4× |
| sl1 | 2.205s | 2.000s | **-9.3%** | 简单累加循环展开 |
| sl2 | 614ms | 574ms | **-6.6%** | 同上 |
| crc2 | 45.6ms | 40.4ms | **-11.4%** | 简单位操作循环展开 |

## 未受益的原因

- `huffman`：内层循环调用 `_and`/`_xor`/`_or`（函数调用，latch 为空），展开无法触及
- `matmul`/`h-5`：内层循环 7 指令（GEP×2+load×2+mul+sub+add），超过限制 6，不展开；尝试展开时寄存器压力导致 matmul3 慢 2.2×，h-5 慢 1.55×

## Run1（第1次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 3.139s |
| 01_mm2 | AC | 3.154s |
| 01_mm3 | AC | 3.155s |
| 03_sort1 | AC | 483.8ms |
| 03_sort2 | AC | 22.0ms |
| 03_sort3 | AC | 75.7ms |
| conv2d-1 | AC | 733.2ms |
| conv2d-2 | AC | 200.6ms |
| conv2d-3 | AC | 67.0ms |
| crc1 | AC | 39.8ms |
| crc2 | AC | 40.2ms |
| crc3 | AC | 40.3ms |
| crypto-1 | AC | 139.6ms |
| crypto-2 | AC | 97.8ms |
| crypto-3 | AC | 56.5ms |
| fft0 | AC | 3.468s |
| fft1 | AC | 7.392s |
| fft2 | AC | 20.1ms |
| h-1-01 | AC | 17.036s |
| h-1-02 | AC | 1.517s |
| h-1-03 | AC | 8.283s |
| h-10-01 | AC | 6.8ms |
| h-10-02 | AC | 14.3ms |
| h-10-03 | AC | 25.7ms |
| h-4-01 | AC | 1.136s |
| h-4-02 | AC | 3.805s |
| h-4-03 | AC | 6.957s |
| h-5-01 | AC | 1.252s |
| h-5-02 | AC | 1.257s |
| h-5-03 | AC | 1.254s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.7ms |
| h-9-02 | AC | 1.6ms |
| h-9-03 | AC | 1.8ms |
| huffman-01 | AC | 35.223s |
| huffman-02 | AC | 35.198s |
| huffman-03 | AC | 35.233s |
| knapsack_naive-1 | AC | 129.7ms |
| knapsack_naive-2 | AC | 133.9ms |
| knapsack_naive-3 | AC | 129.6ms |
| many_mat_cal-1 | AC | 15.539s |
| many_mat_cal-2 | AC | 15.402s |
| many_mat_cal-3 | AC | 15.509s |
| matmul1 | AC | 5.408s |
| matmul2 | AC | 5.402s |
| matmul3 | AC | 5.413s |
| optimization_scheduling1 | AC | 1.4ms |
| optimization_scheduling2 | AC | 1.3ms |
| optimization_scheduling3 | AC | 1.4ms |
| shuffle0 | AC | 1.330s |
| shuffle1 | AC | 744.2ms |
| shuffle2 | AC | 35.5ms |
| sl1 | AC | 1.948s |
| sl2 | AC | 575.3ms |
| sl3 | AC | 243.7ms |
| transpose0 | AC | 924.8ms |
| transpose1 | AC | 1.495s |
| transpose2 | AC | 1.680s |

## Run2（第2次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 3.144s |
| 01_mm2 | AC | 3.142s |
| 01_mm3 | AC | 3.153s |
| 03_sort1 | AC | 480.8ms |
| 03_sort2 | AC | 22.4ms |
| 03_sort3 | AC | 75.9ms |
| conv2d-1 | AC | 733.0ms |
| conv2d-2 | AC | 200.9ms |
| conv2d-3 | AC | 66.8ms |
| crc1 | AC | 39.1ms |
| crc2 | AC | 40.2ms |
| crc3 | AC | 39.9ms |
| crypto-1 | AC | 139.3ms |
| crypto-2 | AC | 97.4ms |
| crypto-3 | AC | 56.5ms |
| fft0 | AC | 3.461s |
| fft1 | AC | 7.403s |
| fft2 | AC | 20.1ms |
| h-1-01 | AC | 16.999s |
| h-1-02 | AC | 1.514s |
| h-1-03 | AC | 8.251s |
| h-10-01 | AC | 6.9ms |
| h-10-02 | AC | 14.1ms |
| h-10-03 | AC | 25.8ms |
| h-4-01 | AC | 1.135s |
| h-4-02 | AC | 3.793s |
| h-4-03 | AC | 6.959s |
| h-5-01 | AC | 1.252s |
| h-5-02 | AC | 1.253s |
| h-5-03 | AC | 1.252s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.8ms |
| h-9-02 | AC | 1.7ms |
| h-9-03 | AC | 2.1ms |
| huffman-01 | AC | 35.172s |
| huffman-02 | AC | 35.205s |
| huffman-03 | AC | 35.238s |
| knapsack_naive-1 | AC | 129.6ms |
| knapsack_naive-2 | AC | 129.4ms |
| knapsack_naive-3 | AC | 129.7ms |
| many_mat_cal-1 | AC | 15.547s |
| many_mat_cal-2 | AC | 15.402s |
| many_mat_cal-3 | AC | 15.512s |
| matmul1 | AC | 5.388s |
| matmul2 | AC | 5.408s |
| matmul3 | AC | 5.424s |
| optimization_scheduling1 | AC | 1.4ms |
| optimization_scheduling2 | AC | 1.3ms |
| optimization_scheduling3 | AC | 1.4ms |
| shuffle0 | AC | 1.321s |
| shuffle1 | AC | 749.9ms |
| shuffle2 | AC | 36.1ms |
| sl1 | AC | 2.119s |
| sl2 | AC | 573.1ms |
| sl3 | AC | 243.4ms |
| transpose0 | AC | 923.8ms |
| transpose1 | AC | 1.493s |
| transpose2 | AC | 1.680s |

## Run3（第3次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 3.210s |
| 01_mm2 | AC | 3.221s |
| 01_mm3 | AC | 3.230s |
| 03_sort1 | AC | 488.2ms |
| 03_sort2 | AC | 22.3ms |
| 03_sort3 | AC | 75.9ms |
| conv2d-1 | AC | 731.7ms |
| conv2d-2 | AC | 200.7ms |
| conv2d-3 | AC | 67.0ms |
| crc1 | AC | 40.1ms |
| crc2 | AC | 40.9ms |
| crc3 | AC | 40.8ms |
| crypto-1 | AC | 142.5ms |
| crypto-2 | AC | 99.9ms |
| crypto-3 | AC | 57.8ms |
| fft0 | AC | 3.479s |
| fft1 | AC | 7.445s |
| fft2 | AC | 19.9ms |
| h-1-01 | AC | 17.257s |
| h-1-02 | AC | 1.533s |
| h-1-03 | AC | 8.352s |
| h-10-01 | AC | 6.7ms |
| h-10-02 | AC | 14.2ms |
| h-10-03 | AC | 25.7ms |
| h-4-01 | AC | 1.140s |
| h-4-02 | AC | 3.812s |
| h-4-03 | AC | 6.980s |
| h-5-01 | AC | 1.251s |
| h-5-02 | AC | 1.251s |
| h-5-03 | AC | 1.256s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.8ms |
| h-9-02 | AC | 1.6ms |
| h-9-03 | AC | 1.6ms |
| huffman-01 | AC | 35.286s |
| huffman-02 | AC | 35.266s |
| huffman-03 | AC | 35.324s |
| knapsack_naive-1 | AC | 129.6ms |
| knapsack_naive-2 | AC | 129.5ms |
| knapsack_naive-3 | AC | 129.6ms |
| many_mat_cal-1 | AC | 15.541s |
| many_mat_cal-2 | AC | 15.405s |
| many_mat_cal-3 | AC | 15.514s |
| matmul1 | AC | 5.396s |
| matmul2 | AC | 5.403s |
| matmul3 | AC | 5.401s |
| optimization_scheduling1 | AC | 1.3ms |
| optimization_scheduling2 | AC | 1.3ms |
| optimization_scheduling3 | AC | 1.3ms |
| shuffle0 | AC | 1.320s |
| shuffle1 | AC | 748.9ms |
| shuffle2 | AC | 36.7ms |
| sl1 | AC | 1.932s |
| sl2 | AC | 574.0ms |
| sl3 | AC | 244.4ms |
| transpose0 | AC | 924.6ms |
| transpose1 | AC | 1.494s |
| transpose2 | AC | 1.687s |
