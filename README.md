# Compiler2026-NO_COMPILE_NO_LIFE

## 进度

### TODO

<table>
  <tr style="background-color:#f6f8fa;">
    <th style="width:25%">Module</th>
    <th>TODO</th>
  </tr>
  
  <tr>
    <td style="vertical-align:top;"><b>Mid-opt</b></td>
    <td>LoopRotate pass</td>
  </tr>
  <tr>
    <td></td>
    <td>LoopTiling pass</td>
  </tr>
  <tr>
    <td></td>
    <td>矩阵乘法内层循环展开</td>
  </tr>
  <tr>
    <td></td>
    <td>实现更彻底的死代码消除（DCE）</td>
  </tr>
  <tr>
    <td></td>
    <td>修复CFGSimplify里的各种错误</td>
  </tr>
  
  <tr>
    <td><b>Pass 管理</b></td>
    <td>建立合理的 pass 顺序管理机制（可能需要使某些pass存在返回值）</td>
  </tr>
  
  <tr>
    <td style="vertical-align:top;"><b>Backend</b></td>
    <td>完善 NEON 指令支持</td>
  </tr>
  <tr>
    <td></td>
    <td>规范头部全局量分布</td>
  </tr>
  
  <tr>
    <td style="vertical-align:top;"><b>已知bug</b></td>
    <td>排查部分测试集上函数内联后寄存器溢出的异常情况</td>
  </tr>
  <tr>
    <td></td>
    <td>CFGSimplify不应生成select（？）</td>
  </tr>
  <tr>
    <td></td>
    <td>inlineExpand不支持Unary</td>
  </tr>
  <tr>
    <td></td>
    <td>压栈保存过多被调用者保存寄存器</td>
  </tr>
  <tr>
    <td></td>
    <td>大量无效寄存器到寄存器拷贝(冗余mov)</td>
  </tr>
  <tr>
    <td></td>
    <td>栈帧设计大量槽位闲置</td>
  </tr>
  <tr>
    <td></td>
    <td>无条件跳转链 + 可被 cbz/cbnz 吸收的模式未利用</td>
  </tr>
  <tr>
    <td></td>
    <td>全局变量重复加载</td>
  </tr>
  <tr>
    <td></td>
    <td>movz + mov 双指令中转模式普遍(w9中转)</td>
  </tr>
  <tr>
    <td></td>
    <td>叶函数式尾循环仍设置完整 FP 帧</td>
  </tr>

  <tr>
    <td><b>特性支持</b></td>
    <td>完整支持 Vec 向量类型</td>
  </tr>
</table>

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

调试选项:

TODO: 用参数选择pass

| Flag | 作用 |
|------|------|
| `-O0` / `-O1` / `-O2` | 优化等级（`-O2`仅用于调试） |
| `--dump-ir` | 每个 pass 前后 dump IR |
| `--verify-ir` | 每个 pass 后校验 IR 完整性（TODO:目前无作用） |
| `--fno-peephole` | 禁用 peephole 汇编后优化 |

Pass 基类 (`pass.hpp`) 提供 `name()` 纯虚函数，每个 pass 需返回类名（如 `"Mem2Reg"`），供 dump/verify 使用。

注:`--dump-ir`为stderr实现，输出极长，使用`2>`重定向到文件里查看

对`performance/`采用批量测试，在`test/`下运行：
```bash
./arm_test.sh
```

注：如果使用`qemu`交叉编译，在`test/`下运行：
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
