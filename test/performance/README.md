# Performance tests

每组只保留按输入规模实测最大的样例。

| 测试集 | 热路径 / 测试意图 |
|---|---|
| 00_bitset | 位图扫描与集合运算 |
| 01_mm | 朴素矩阵乘法 |
| 02_mv | 矩阵向量乘 |
| 03_sort | 大规模比较排序 |
| 04_spmv | 稀疏矩阵向量乘 |
| bitonic_sort | Bitonic 排序网络 |
| brainfuck-mandelbrot-nerf | Brainfuck 解释器与 Mandelbrot |
| conv | 一维卷积 |
| conv2d | 二维卷积 |
| crc | CRC 校验循环 |
| crypto | 加密轮函数与字节处理 |
| dead-code-elimination | 大规模死代码消除 |
| dijkstra | Dijkstra 最短路 |
| fft | FFT 蝶形运算 |
| fib_rand | 递归 Fibonacci |
| flood_fill | 网格 Flood Fill |
| floyd | Floyd-Warshall 全源最短路 |
| gameoflife-p61glidergun | 生命游戏滑翔机炮 |
| gaussian | 高斯消元 |
| h-1 | 大数组访问与循环 |
| h-2 | 条件分支与循环 |
| h-3 | 大数组前缀处理 |
| h-4 | 索引与边界分支 |
| h-5 | 大规模数值循环 |
| h-6 | 长循环归约 |
| h-7 | 线性扫描 |
| h-8 | 动态规划 |
| h-9 | 递归与记忆化 |
| h-10 | 动态规划状态转移 |
| h-11 | 大规模递推计算 |
| h-conv_pooling | 卷积、池化与激活函数 |
| hoist | 循环不变代码外提 |
| huffman | Huffman 编码树构造 |
| if-combine | 条件合并与分支热路径 |
| instruction-combining | 指令组合模式 |
| integer-divide-optimization | 整数除法强度削弱 |
| knapsack_naive | 0/1 背包穷举转移 |
| large_loop_array | 大数组循环访存 |
| lcs | 最长公共子序列 DP |
| loop_opt | 循环优化与归约 |
| ludcmp | LU 分解内层乘加 |
| many_mat_cal | 多矩阵连续计算 |
| matmul | 矩阵乘法 |
| median | 中位数选择 |
| mm_block | 分块矩阵乘法 |
| mm1000 | 大矩阵乘法 |
| nussinov | Nussinov RNA DP |
| optimization_scheduling | 指令调度压力 |
| recursive_call | 递归调用开销 |
| shuffle | 随机重排与访存 |
| sl | 链表遍历 |
| sort_search | 排序后查找 |
| stencil | 网格 stencil 邻域计算 |
| tensor_g1_elementwise_int | 整数张量逐元素运算 |
| tensor_g2_elementwise_float | 浮点张量逐元素运算 |
| tensor_g3_matmul_int | 整数张量矩阵乘 |
| tensor_g4_matmul_float | 浮点张量矩阵乘 |
| tensor_g5_stencil_int | 整数张量 stencil |
| tensor3d | 三维张量索引与遍历 |
| transpose | 矩阵转置 |
