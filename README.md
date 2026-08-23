# Compiler2026-NO_COMPILE_NO_LIFE

2026年全国大学生计算机系统能力大赛-编译系统设计赛-编译系统实现赛中ARM后端赛道的国家一等奖仓库

## Quick Setup

```bash
# Redirects to '/workspace', mounting your current host path
# Or on ARM machine, ignore this one
docker build --platform linux/arm64 -t sysy-dev .; docker run -it --platform linux/arm64 -v $(pwd):/workspace sysy-dev

# Builds from scratch
mkdir build && cd build && cmake .. && make -j4

# Compilation
./compiler -S -o out.s <.sy path> [-O1]

# Generates binary
gcc out.s ../lib/libsysy.a -o out
```

## Debugging

```bash
# Generates IR
./compiler -c -o out.ll <src_path> [-O1] [--dump-ir 2> out.txt]
```

| Flag | Effect |
|------|--------|
| `-S` | Generate assembly output |
| `-c` | Generate intermediate representation |
| `-o` | Select output file |
| `-O0` | Disable optimization pipeline |
| `-O1` | Enable optimization pipeline |
| `--dump-ir` | Dump intermediate representation |
| `--dump-scev` | Dump scalar evolution analysis |
| `--dump-ast` | Dump abstract syntax tree |
| `--verify-ir` | Verify intermediate representation |
| `--dump-machine-instr` | Dump machine instructions |
| `--dump-pre-machine-instr` | Dump pre instruction selection machine code |

## Visualizer

`visualizer/` is a small static website that works like `godbolt.org`.