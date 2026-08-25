# Compiler2026-NO_COMPILE_NO_LIFE

The repository for the National First Prize winner in the ARM Backend track of the Compiler System Implementation Competition, part of the 2026 Computer System Development Capability Competition.

This repo has removed all mattern matches, whose files, along with deprecated experimental polyhedral subsystem, can be recovered from `archive/`.

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

## Hardware Standards

* CG-FPGA15EG is based on the Xilinx XCZU15EG SoC platform and integrates an ARM Cortex-A53 MPCore processor, providing a multicore high-performance computing and benchmarking environment.
* CPU: ARM Cortex-A53 MPCore
* Number of cores: 4 (2 or 3 cores are isolated via CPU isolation for target program benchmarking)
* Architecture: ARMv8-A, 64-bit
* Supports NEON SIMD
* Supports floating-point and double-precision floating-point instructions
* L1 data cache: 32 KB, 4-way set associative, private to each core
* L1 instruction cache: 32 KB, 2-way set associative, private to each core
* Shared L2 cache: 1 MB, 16-way set associative, shared by all 4 cores
* Floating-point instructions (FP): The ARM Cortex-A53 MPCore integrates an ARMv8-A Floating-Point Unit (FPU), supporting single-precision (32-bit) and double-precision (64-bit) floating-point arithmetic in compliance with the IEEE 754 standard. The floating-point unit fully supports the following operations. These floating-point instructions are implemented under the ARMv8-A architecture through the VFPv4 (Vector Floating-Point v4) functional block and can work in conjunction with the vector SIMD unit:

  * Single-precision floating-point arithmetic (float)
  * Double-precision floating-point arithmetic (double)
  * Floating-point addition, subtraction, multiplication, division, square root, fused multiply-add (FMA), and comparison operations
  * Floating-point-to-integer and integer-to-floating-point conversions
  * Floating-point conditional execution instructions
* Vector instructions (SIMD/NEON): The Cortex-A53 supports the Advanced SIMD (NEON) extension for data-parallel vector operations. NEON is a 128-bit-wide SIMD unit supporting the following operations:

  * Integer and floating-point vector addition, subtraction, multiplication, and division
  * Vector multiply-accumulate operations (VMLA, VMLS, VFMA, VFMS)
  * Single-precision floating-point vector operations
  * Vector logical AND, OR, NOT, and XOR operations
  * Shifts, saturation, absolute value, maximum/minimum, and conditional selection
  * Vector table lookup and rearrangement
  * Single-precision floating-point matrix operations (via software libraries)
* Note: The Cortex-A53 NEON unit does not support double-precision floating-point vector operations (i.e., NEON SIMD supports only float32 vectors, not float64 vectors). Double-precision floating-point computation can only be performed in scalar form through the VFP unit.

## To Viewers

Experiments have shown that even without any pattern matches, it is enough to make it to the final. And please, if pattern matches are made, make sure to have grasped the idea, for it might be of help sometime in the unknown future.