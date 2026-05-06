# Compiler2026-NO_COMPILE_NO_LIFE

### 进度

已初步完成：

正确性：
- 词法分析
- 语法分析
- AST构建
- 静态语义检查
- LLVM IR生成
- IR -> ARM

IR Pass:
- 简单的Pass 管理器
- Dead Code Eliminate pass（局部死代码消除）
- Array Simplify pass ~~（疑似没什么用）~~

TODO:

IR Pass:
- Constant Fold
- Loop Invariant
- IR 分层 : HIR, MIR, LIR

Backend:
- 寄存器分配：图着色算法
- 多线程

Misc:
- `autotest.py`测试改为使用clang
- `autotest.py`打印错误信息
- `main.cpp`里合理处理参数


### 项目结构

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

### 项目运行

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
python3 autotest.py
```
过程中不会在控制台打印，不会实时打印，测试结束后会打印到`test/`下的result.txt，不打印具体错误信息，出现任何错误均为RE。

### `git commit`规范

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