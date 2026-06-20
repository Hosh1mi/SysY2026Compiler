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
    <td>实现更彻底的死代码消除（DCE）</td>
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
    <td><b>特性支持</b></td>
    <td>完整支持 Vec 向量类型</td>
  </tr>
</table>

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
| `-O0` / `-O1` / `-O2` | 优化等级（`-O2`仅用于调试） |
| `--dump-ir` | 每个 pass 前后 dump IR |
| `--verify-ir` | 每个 pass 后校验 IR 完整性（TODO:目前无作用） |
| `--fno-peephole` | 在 `-O1` 下禁用 peephole 汇编后优化 |
| `--fno-pre-schedule` | 在 `-O1` 下禁用 preRA 虚拟机器指令调度 |
| `--fno-schedule` | 在 `-O1` 下禁用 MachineInstr 调度 |
| `--dump-pre-machine-instr` | 输出 preRA 虚拟 MachineInstr（vreg defs/uses、latency），dump 到 stderr |
| `--dump-machine-instr` | 输出每个函数的 MachineInstr 详细信息（opcode类型、defs/uses、latency、标志位），dump 到 stderr |

注:`--dump-ir`为stderr实现，输出极长，使用`2>`重定向到文件里查看

`--dump-machine-instr` 同样输出到 stderr，可单独使用或配合 `-S`：
```bash
./compiler test.sy --dump-machine-instr 2> dump.txt   # 只 dump MachineInstr
./compiler -S test.sy --dump-machine-instr 2> dump.txt # 同时输出汇编
```

批量测试脚本在 `test/` 下，命名规则：
- `arm_` 前缀：ARM 原生环境（容器内 `gcc` 直接编译运行）
- `amd_` 前缀：交叉编译环境（`aarch64-linux-gnu-gcc` + `qemu-aarch64`）

## 