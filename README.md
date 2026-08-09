# Compiler2026-NO_COMPILE_NO_LIFE
## 项目运行

```bash
# 项目代码会放在 /workspace 下
docker build --platform linux/arm64 -t sysy-dev .; docker run -it --platform linux/arm64 -v $(pwd):/workspace sysy-dev

# 在根目录下构建：
mkdir build && cd build && cmake .. && make -j8

# 生成IR:
./compiler -c -o out.ll <testpath>

# 生成汇编:
./compiler -S -o out.s <.sy path>

# 编译运行:
gcc out.s ../lib/libsysy.a -o out
./out < <testinput>
```

## 调试选项

| Flag | 作用 |
|------|------|
| `-O0` / `-O1` | 优化等级 |
| `--dump-ir` | 每个 pass 前后 dump IR |
| `--verify-ir` | 每个 pass 后校验 IR 完整性 |
| `--dump-pre-machine-instr` | 输出 preRA 虚拟 MachineInstr（vreg defs/uses、latency），dump 到 stderr |
| `--dump-machine-instr` | 输出每个函数的 MachineInstr 详细信息（opcode类型、defs/uses、latency、标志位），dump 到 stderr |

注:`--dump-ir`为stderr实现，输出极长，使用`2>`重定向到文件里查看

`--dump-machine-instr` 同样输出到 stderr，可单独使用或配合 `-S`：
```bash
./compiler test.sy --dump-machine-instr 2> dump.txt   # 只 dump MachineInstr
./compiler -S test.sy --dump-machine-instr 2> dump.txt # 同时输出汇编
```

## 项目说明

见`docs/`
