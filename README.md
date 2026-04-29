# Compiler2026-NO_COMPILE_NO_LIFE

### 进度
已初步完成：
- 词法分析
- 语法分析
- AST构建
- 静态语义检查
- LLVM IR生成

TODO:
- IR -> ARM
- IR opt

### 项目运行

在`src/`下运行`make compiler`以编译，运行`make run`以测试。可修改`run`下的`.sy`文件以更改测试目标。通过修改`compiler`下的`-D`参数启用不同调试宏。

### `git commit`规范

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