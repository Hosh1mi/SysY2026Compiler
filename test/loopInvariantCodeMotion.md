# LICM 优化前后性能对比

- **优化前基线**：commit `12485ed` — `test: update performance test results on native arm64`
- **优化后**：LICM pass（循环不变量外提），顺序独立跑3次
- **平台**：ARM64 Docker 容器（native，无 QEMU）

## 汇总（3次平均值）

| 测试 | 优化前 | Run1 | Run2 | Run3 | 均值 | 变化 |
|------|--------|------|------|------|------|------|
| 01_mm1 | 5.021s | 4.342s | 4.399s | 4.312s | 4.351s | **-13.3%** ⬇️ |
| 01_mm2 | 5.073s | 4.337s | 4.308s | 4.285s | 4.310s | **-15.0%** ⬇️ |
| 01_mm3 | 5.090s | 4.348s | 4.339s | 4.309s | 4.332s | **-14.9%** ⬇️ |
| 03_sort1 | 850.8ms | 627.5ms | 662.4ms | 621.6ms | 637.2ms | **-25.1%** ⬇️ |
| 03_sort2 | 35.6ms | 28.6ms | 28.7ms | 29.1ms | 28.8ms | **-19.1%** ⬇️ |
| 03_sort3 | 122.1ms | 95.1ms | 92.1ms | 91.5ms | 92.9ms | **-23.9%** ⬇️ |
| conv2d-1 | 1.018s | 1.040s | 1.028s | 1.021s | 1.029s | +1.2% ⬆️ |
| conv2d-2 | 278.6ms | 282.9ms | 278.8ms | 277.7ms | 279.8ms | ≈持平 |
| conv2d-3 | 92.5ms | 94.2ms | 92.4ms | 92.1ms | 92.9ms | ≈持平 |
| crc1 | 70.7ms | 51.8ms | 51.0ms | 51.4ms | 51.4ms | **-27.3%** ⬇️ |
| crc2 | 70.6ms | 51.5ms | 50.8ms | 50.9ms | 51.0ms | **-27.7%** ⬇️ |
| crc3 | 70.7ms | 51.4ms | 50.6ms | 50.7ms | 50.9ms | **-28.0%** ⬇️ |
| crypto-1 | 286.1ms | 193.8ms | 191.3ms | 191.2ms | 192.1ms | **-32.9%** ⬇️ |
| crypto-2 | 201.3ms | 136.1ms | 133.9ms | 134.1ms | 134.7ms | **-33.1%** ⬇️ |
| crypto-3 | 115.5ms | 78.2ms | 77.0ms | 77.3ms | 77.5ms | **-32.9%** ⬇️ |
| fft0 | 5.579s | 5.824s | 5.701s | 5.694s | 5.740s | +2.9% ⬆️ |
| fft1 | 11.840s | 12.430s | 11.944s | 11.975s | 12.116s | +2.3% ⬆️ |
| fft2 | 30.5ms | 32.1ms | 30.9ms | 31.2ms | 31.4ms | +2.9% ⬆️ |
| h-1-01 | 17.158s | 17.864s | 17.611s | 17.679s | 17.718s | +3.3% ⬆️ |
| h-1-02 | 1.535s | 1.596s | 1.596s | 1.569s | 1.587s | +3.4% ⬆️ |
| h-1-03 | 8.315s | 8.636s | 8.520s | 8.511s | 8.556s | +2.9% ⬆️ |
| h-10-01 | 11.5ms | 9.9ms | 9.8ms | 10.0ms | 9.9ms | **-14.0%** ⬇️ |
| h-10-02 | 25.1ms | 21.9ms | 21.7ms | 22.0ms | 21.9ms | **-12.8%** ⬇️ |
| h-10-03 | 47.0ms | 41.2ms | 40.2ms | 41.2ms | 40.9ms | **-13.0%** ⬇️ |
| h-4-01 | 1.550s | 1.425s | 1.403s | 1.401s | 1.410s | **-9.1%** ⬇️ |
| h-4-02 | 5.196s | 4.491s | 4.432s | 4.421s | 4.448s | **-14.4%** ⬇️ |
| h-4-03 | 9.496s | 8.770s | 8.696s | 8.643s | 8.703s | **-8.4%** ⬇️ |
| h-5-01 | 3.339s | 2.138s | 2.082s | 2.053s | 2.091s | **-37.4%** ⬇️ |
| h-5-02 | 3.340s | 2.161s | 2.075s | 2.052s | 2.096s | **-37.3%** ⬇️ |
| h-5-03 | 3.342s | 2.180s | 2.061s | 2.062s | 2.101s | **-37.1%** ⬇️ |
| h-8-01 | WA | WA | WA | WA | WA | 预存在bug |
| h-8-02 | WA | WA | WA | WA | WA | 预存在bug |
| h-8-03 | WA | WA | WA | WA | WA | 预存在bug |
| h-9-01 | 1.9ms | 1.8ms | 1.7ms | 1.7ms | 1.8ms | **-6.0%** ⬇️ |
| h-9-02 | 1.5ms | 1.7ms | 1.5ms | 1.5ms | 1.6ms | ≈持平 |
| h-9-03 | 1.7ms | 1.7ms | 1.6ms | 1.6ms | 1.6ms | **-3.6%** ⬇️ |
| huffman-01 | 58.139s | 46.158s | 45.180s | 45.074s | 45.471s | **-21.8%** ⬇️ |
| huffman-02 | 57.754s | 45.857s | 45.035s | 44.921s | 45.271s | **-21.6%** ⬇️ |
| huffman-03 | 58.160s | 45.855s | 45.047s | 45.175s | 45.359s | **-22.0%** ⬇️ |
| knapsack_naive-1 | 160.7ms | 169.7ms | 160.5ms | 161.0ms | 163.7ms | +1.9% ⬆️ |
| knapsack_naive-2 | 160.3ms | 172.5ms | 161.2ms | 160.6ms | 164.8ms | +2.8% ⬆️ |
| knapsack_naive-3 | 160.1ms | 172.1ms | 160.4ms | 160.4ms | 164.3ms | +2.6% ⬆️ |
| many_mat_cal-1 | 1.06min | 1.04min | 59.127s | 59.137s | 1.00min | **-4.9%** ⬇️ |
| many_mat_cal-2 | 1.05min | 1.10min | 59.385s | 59.137s | 1.02min | **-2.9%** ⬇️ |
| many_mat_cal-3 | 1.06min | 1.11min | 59.265s | 59.056s | 1.03min | **-2.6%** ⬇️ |
| matmul1 | 8.579s | 8.517s | 7.187s | 7.134s | 7.612s | **-11.3%** ⬇️ |
| matmul2 | 8.590s | 8.666s | 7.207s | 7.171s | 7.681s | **-10.6%** ⬇️ |
| matmul3 | 8.572s | 8.779s | 7.144s | 7.145s | 7.689s | **-10.3%** ⬇️ |
| optimization_scheduling1 | 1.2ms | 1.5ms | 1.3ms | 1.4ms | 1.4ms | +12.9% ⬆️ |
| optimization_scheduling2 | 1.3ms | 1.4ms | 1.3ms | 1.4ms | 1.4ms | +1.3% ⬆️ |
| optimization_scheduling3 | 1.2ms | 1.5ms | 1.3ms | 1.4ms | 1.4ms | +14.3% ⬆️ |
| shuffle0 | 2.047s | 2.175s | 1.903s | 1.906s | 1.994s | **-2.6%** ⬇️ |
| shuffle1 | 1.008s | 1.150s | 1.005s | 1.004s | 1.053s | +4.5% ⬆️ |
| shuffle2 | 38.7ms | 41.3ms | 38.4ms | 38.7ms | 39.5ms | +1.9% ⬆️ |
| sl1 | 2.915s | 2.862s | 2.438s | 2.365s | 2.555s | **-12.4%** ⬇️ |
| sl2 | 861.1ms | 835.2ms | 714.1ms | 711.4ms | 753.6ms | **-12.5%** ⬇️ |
| sl3 | 354.6ms | 353.7ms | 305.1ms | 305.4ms | 321.4ms | **-9.4%** ⬇️ |
| transpose0 | 1.825s | 1.793s | 1.572s | 1.572s | 1.646s | **-9.8%** ⬇️ |
| transpose1 | 2.946s | 2.927s | 2.580s | 2.583s | 2.697s | **-8.5%** ⬇️ |
| transpose2 | 3.803s | 2.888s | 2.500s | 2.495s | 2.628s | **-30.9%** ⬇️ |

**总计（57个AC测试）：优化前 8.25min，优化后均值 7.32min，整体变化 -11.3%**

- 提升：39 个
- 持平：3 个
- 退步：15 个

## Run1（第1次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 4.342s |
| 01_mm2 | AC | 4.337s |
| 01_mm3 | AC | 4.348s |
| 03_sort1 | AC | 627.5ms |
| 03_sort2 | AC | 28.6ms |
| 03_sort3 | AC | 95.1ms |
| conv2d-1 | AC | 1.040s |
| conv2d-2 | AC | 282.9ms |
| conv2d-3 | AC | 94.2ms |
| crc1 | AC | 51.8ms |
| crc2 | AC | 51.5ms |
| crc3 | AC | 51.4ms |
| crypto-1 | AC | 193.8ms |
| crypto-2 | AC | 136.1ms |
| crypto-3 | AC | 78.2ms |
| fft0 | AC | 5.824s |
| fft1 | AC | 12.430s |
| fft2 | AC | 32.1ms |
| h-1-01 | AC | 17.864s |
| h-1-02 | AC | 1.596s |
| h-1-03 | AC | 8.636s |
| h-10-01 | AC | 9.9ms |
| h-10-02 | AC | 21.9ms |
| h-10-03 | AC | 41.2ms |
| h-4-01 | AC | 1.425s |
| h-4-02 | AC | 4.491s |
| h-4-03 | AC | 8.770s |
| h-5-01 | AC | 2.138s |
| h-5-02 | AC | 2.161s |
| h-5-03 | AC | 2.180s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.8ms |
| h-9-02 | AC | 1.7ms |
| h-9-03 | AC | 1.7ms |
| huffman-01 | AC | 46.158s |
| huffman-02 | AC | 45.857s |
| huffman-03 | AC | 45.855s |
| knapsack_naive-1 | AC | 169.7ms |
| knapsack_naive-2 | AC | 172.5ms |
| knapsack_naive-3 | AC | 172.1ms |
| many_mat_cal-1 | AC | 1.04min |
| many_mat_cal-2 | AC | 1.10min |
| many_mat_cal-3 | AC | 1.11min |
| matmul1 | AC | 8.517s |
| matmul2 | AC | 8.666s |
| matmul3 | AC | 8.779s |
| optimization_scheduling1 | AC | 1.5ms |
| optimization_scheduling2 | AC | 1.4ms |
| optimization_scheduling3 | AC | 1.5ms |
| shuffle0 | AC | 2.175s |
| shuffle1 | AC | 1.150s |
| shuffle2 | AC | 41.3ms |
| sl1 | AC | 2.862s |
| sl2 | AC | 835.2ms |
| sl3 | AC | 353.7ms |
| transpose0 | AC | 1.793s |
| transpose1 | AC | 2.927s |
| transpose2 | AC | 2.888s |

## Run2（第2次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 4.399s |
| 01_mm2 | AC | 4.308s |
| 01_mm3 | AC | 4.339s |
| 03_sort1 | AC | 662.4ms |
| 03_sort2 | AC | 28.7ms |
| 03_sort3 | AC | 92.1ms |
| conv2d-1 | AC | 1.028s |
| conv2d-2 | AC | 278.8ms |
| conv2d-3 | AC | 92.4ms |
| crc1 | AC | 51.0ms |
| crc2 | AC | 50.8ms |
| crc3 | AC | 50.6ms |
| crypto-1 | AC | 191.3ms |
| crypto-2 | AC | 133.9ms |
| crypto-3 | AC | 77.0ms |
| fft0 | AC | 5.701s |
| fft1 | AC | 11.944s |
| fft2 | AC | 30.9ms |
| h-1-01 | AC | 17.611s |
| h-1-02 | AC | 1.596s |
| h-1-03 | AC | 8.520s |
| h-10-01 | AC | 9.8ms |
| h-10-02 | AC | 21.7ms |
| h-10-03 | AC | 40.2ms |
| h-4-01 | AC | 1.403s |
| h-4-02 | AC | 4.432s |
| h-4-03 | AC | 8.696s |
| h-5-01 | AC | 2.082s |
| h-5-02 | AC | 2.075s |
| h-5-03 | AC | 2.061s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.7ms |
| h-9-02 | AC | 1.5ms |
| h-9-03 | AC | 1.6ms |
| huffman-01 | AC | 45.180s |
| huffman-02 | AC | 45.035s |
| huffman-03 | AC | 45.047s |
| knapsack_naive-1 | AC | 160.5ms |
| knapsack_naive-2 | AC | 161.2ms |
| knapsack_naive-3 | AC | 160.4ms |
| many_mat_cal-1 | AC | 59.127s |
| many_mat_cal-2 | AC | 59.385s |
| many_mat_cal-3 | AC | 59.265s |
| matmul1 | AC | 7.187s |
| matmul2 | AC | 7.207s |
| matmul3 | AC | 7.144s |
| optimization_scheduling1 | AC | 1.3ms |
| optimization_scheduling2 | AC | 1.3ms |
| optimization_scheduling3 | AC | 1.3ms |
| shuffle0 | AC | 1.903s |
| shuffle1 | AC | 1.005s |
| shuffle2 | AC | 38.4ms |
| sl1 | AC | 2.438s |
| sl2 | AC | 714.1ms |
| sl3 | AC | 305.1ms |
| transpose0 | AC | 1.572s |
| transpose1 | AC | 2.580s |
| transpose2 | AC | 2.500s |

## Run3（第3次） 详细数据

| 测试 | 结果 | 耗时 |
|------|------|------|
| 01_mm1 | AC | 4.312s |
| 01_mm2 | AC | 4.285s |
| 01_mm3 | AC | 4.309s |
| 03_sort1 | AC | 621.6ms |
| 03_sort2 | AC | 29.1ms |
| 03_sort3 | AC | 91.5ms |
| conv2d-1 | AC | 1.021s |
| conv2d-2 | AC | 277.7ms |
| conv2d-3 | AC | 92.1ms |
| crc1 | AC | 51.4ms |
| crc2 | AC | 50.9ms |
| crc3 | AC | 50.7ms |
| crypto-1 | AC | 191.2ms |
| crypto-2 | AC | 134.1ms |
| crypto-3 | AC | 77.3ms |
| fft0 | AC | 5.694s |
| fft1 | AC | 11.975s |
| fft2 | AC | 31.2ms |
| h-1-01 | AC | 17.679s |
| h-1-02 | AC | 1.569s |
| h-1-03 | AC | 8.511s |
| h-10-01 | AC | 10.0ms |
| h-10-02 | AC | 22.0ms |
| h-10-03 | AC | 41.2ms |
| h-4-01 | AC | 1.401s |
| h-4-02 | AC | 4.421s |
| h-4-03 | AC | 8.643s |
| h-5-01 | AC | 2.053s |
| h-5-02 | AC | 2.052s |
| h-5-03 | AC | 2.062s |
| h-8-01 | WA | - |
| h-8-02 | WA | - |
| h-8-03 | WA | - |
| h-9-01 | AC | 1.7ms |
| h-9-02 | AC | 1.5ms |
| h-9-03 | AC | 1.6ms |
| huffman-01 | AC | 45.074s |
| huffman-02 | AC | 44.921s |
| huffman-03 | AC | 45.175s |
| knapsack_naive-1 | AC | 161.0ms |
| knapsack_naive-2 | AC | 160.6ms |
| knapsack_naive-3 | AC | 160.4ms |
| many_mat_cal-1 | AC | 59.137s |
| many_mat_cal-2 | AC | 59.137s |
| many_mat_cal-3 | AC | 59.056s |
| matmul1 | AC | 7.134s |
| matmul2 | AC | 7.171s |
| matmul3 | AC | 7.145s |
| optimization_scheduling1 | AC | 1.4ms |
| optimization_scheduling2 | AC | 1.4ms |
| optimization_scheduling3 | AC | 1.4ms |
| shuffle0 | AC | 1.906s |
| shuffle1 | AC | 1.004s |
| shuffle2 | AC | 38.7ms |
| sl1 | AC | 2.365s |
| sl2 | AC | 711.4ms |
| sl3 | AC | 305.4ms |
| transpose0 | AC | 1.572s |
| transpose1 | AC | 2.583s |
| transpose2 | AC | 2.495s |
