# Compiler2026-NO_COMPILE_NO_LIFE

## 进度

### TODO

<table>
  <tr style="background-color:#f6f8fa;">
    <th style="width:25%">模块</th>
    <th>待办事项</th>
  </tr>
  
  <tr>
    <td rowspan="8" style="vertical-align:top;"><b>中端优化</b></td>
    <td>新增 LoopRotate 变换 pass</td>
  </tr>
  <tr><td>新增 LoopTiling 分块 pass</td></tr>
  <tr><td>新增 LoopInterchange 交换 pass</td></tr>
  <tr><td>矩阵乘法内层循环展开</td></tr>
  <tr><td>实现更彻底的死代码消除（DCE）</td></tr>
  <tr><td>实现稀疏条件常量传播（SCCP）</td></tr>
  <tr><td>用正规 InstCombine 替换现有 algebraSimplify</td></tr>
  <tr><td>将 CSE 拆分为 EarlyCSE 与 GVN 两个独立 pass</td></tr>
  
  <tr>
    <td><b>Pass 管理</b></td>
    <td>建立合理的 pass 顺序管理机制</td>
  </tr>
  
  <tr>
    <td rowspan="5" style="vertical-align:top;"><b>后端改进</b></td>
    <td>拆分 backend 巨型文件，按功能模块化</td></tr>
  <tr><td>实现叶子函数栈帧省略优化</td></tr>
  <tr><td>完善 NEON 指令支持，使所有预期指令工作</td></tr>
  <tr><td>规范化汇编头部全局量的分布方式</td></tr>
  <tr><td>将 label 改为具体有意义的命名</td></tr>
  
  <tr>
    <td rowspan="2" style="vertical-align:top;"><b>问题修复</b></td>
    <td>排查部分测试集上寄存器溢出到栈的异常情况</td>
  </tr>
  <tr><td>修复魔数相关算法的实现错误</td></tr>
  
  <tr>
    <td><b>特性支持</b></td>
    <td>完整支持 Vec 向量类型</td>
  </tr>
</table>

## 项目结构

```text
root/
├── archive/
├── lib/
├── test/
└── src/
    ├── include/
    ├── frontend/
    ├── mid/
    ├── backend/
    └── main.cpp
```
- `archive/`下存放了可进行正确性测试的脚本（-O0），运行后会在根目录生成`checklist.md`

## 项目运行

使用根目录下使用docker搭建环境，推荐使用以下命令以防止flex/bison生成文件里包含特定用户路径：
```bash
docker build --platform linux/arm64 -t sysy-dev .; docker run -it --platform linux/arm64 -v $(pwd):/workspace sysy-dev
# 项目代码会放在 /workspace 下
```

在根目录下构建：
```bash
mkdir build && cd build && cmake .. && make -j4
```

在`build/`下运行:
```bash
./compiler <.sy path> > out.s
gcc out.s ../lib/libsysy.a -o out
```

对`performance/`采用批量测试，在`test/`下运行：
```bash
./run_tests.sh
```

## `git commit`规范

**为了diff可读性，commit前建议检查是否开启了ide的auto formatting**

```
<类型>(<范围>): <主题>

<正文>

<页脚>
```
| 类型 | 含义 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat(auth): add login with Google` |
| `fix` | 修复bug | `fix(cart): resolve total price miscalculation` |
| `docs` | 文档变更 | `docs(readme): update installation guide` |
| `style` | 代码格式（不影响逻辑） | `style: format with prettier` |
| `perf` | 性能优化 | `perf(list): add virtual scrolling` |
| `test` | 增加或修改测试 | `test: add unit tests for validator` |
| `chore` | 构建/工具/依赖变更 | `chore: upgrade vite to v5` |
| `revert` | 回滚某次提交 | `revert: revert "feat: add login"` |
| `refactor` | 重构（上述类型都不匹配时） | `refactor(api): extract common http client` |

- 主题使用祈使句、现在时，末尾不用加句号
- `commit`前检查无关文件，使用`.gitignore`排除
- ~~或者用简洁中文表达也可以~~