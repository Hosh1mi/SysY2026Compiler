#!/usr/bin/env python3
"""SysY 循环程序 fuzz 生成器（plan 5.1 验证项）。

生成随机循环嵌套（while 形、±1 步长 IV、全局/局部数组读写、归约、
break/continue、不变条件分支），用 -O0 与 -O1 编译运行做差分 oracle：
输出（stdout + 退出码）不一致即为错译，复现样本落盘。

用法（容器内）：
  python3 test/loop_fuzz.py [--n 200] [--seed 1] [--keep-dir /tmp/fuzz_fail]
依赖：/workspace/build/compiler、gcc、/workspace/lib/libsysy.a
"""
import argparse
import os
import random
import subprocess
import sys

TMP = "/tmp/loop_fuzz"


class Gen:
    def __init__(self, rng):
        self.r = rng
        self.gid = 0

    def fresh(self, p):
        self.gid += 1
        return f"{p}{self.gid}"

    # 数组维度刻意小，保证 -O0 也秒级跑完
    def program(self):
        n1 = self.r.randint(8, 40)
        n2 = self.r.randint(4, 16)
        lines = [
            f"int A[{n1}][{n2}];",
            f"int B[{n1}][{n2}];",
            f"int C[{n1}];",
            "int g;",
            "",
            "int main() {",
            "    int i = 0; int j = 0; int k = 0;",
            "    int s = 0; int t = 0;",
            # 确定性初始化
            "    i = 0;",
            f"    while (i < {n1}) {{",
            "        j = 0;",
            f"        while (j < {n2}) {{",
            "            A[i][j] = i * 7 + j * 3 - 11;",
            "            B[i][j] = i - j * 5 + 2;",
            "            j = j + 1;",
            "        }",
            "        C[i] = i * i - 4 * i;",
            "        i = i + 1;",
            "    }",
        ]
        for _ in range(self.r.randint(2, 5)):
            lines += self.loop_nest(n1, n2)
        lines += [
            "    s = 0; i = 0;",
            f"    while (i < {n1}) {{",
            "        j = 0;",
            f"        while (j < {n2}) {{",
            "            s = s + A[i][j] + B[i][j];",
            "            j = j + 1;",
            "        }",
            "        s = s + C[i] + g;",
            "        i = i + 1;",
            "    }",
            "    putint(s);",
            "    putch(10);",
            "    return s;",  # 退出码也参与差分（mod 256）
            "}",
        ]
        return "\n".join(lines)

    def loop_nest(self, n1, n2):
        kind = self.r.choice(
            ["doall2d", "reduce", "triangular", "breaky", "invcond", "selfdep"])
        L = []
        if kind == "doall2d":
            c = self.r.randint(1, 9)
            L += [
                "    i = 0;",
                f"    while (i < {n1}) {{",
                "        j = 0;",
                f"        while (j < {n2}) {{",
                f"            A[i][j] = A[i][j] * {c} + B[i][j] - i;",
                "            j = j + 1;",
                "        }",
                "        i = i + 1;",
                "    }",
            ]
        elif kind == "reduce":
            L += [
                "    t = 0; i = 0;",
                f"    while (i < {n1}) {{",
                "        j = 0;",
                f"        while (j < {n2}) {{",
                "            t = t + A[i][j] * B[i][j];",
                "            j = j + 1;",
                "        }",
                "        i = i + 1;",
                "    }",
                "    g = g + t;",
            ]
        elif kind == "triangular":
            L += [
                "    i = 0;",
                f"    while (i < {n1}) {{",
                "        j = i;",  # 非常量初值（IVSR/vectorize lane 回归点）
                f"        while (j < {n2}) {{",
                "            B[i][j] = B[i][j] + A[i][j] * 2;",
                "            j = j + 1;",
                "        }",
                "        i = i + 1;",
                "    }",
            ]
        elif kind == "breaky":
            lim = self.r.randint(2, n1)
            L += [
                "    i = 0;",
                f"    while (i < {n1}) {{",
                f"        if (C[i] > {lim * 3}) break;",  # 多 exiting（并行化禁区）
                "        C[i] = C[i] + i;",
                "        i = i + 1;",
                "    }",
            ]
        elif kind == "invcond":
            L += [
                f"    k = {self.r.randint(0, 1)};",
                "    i = 0;",
                f"    while (i < {n1}) {{",
                "        if (k) { C[i] = C[i] * 2; } else { C[i] = C[i] - 3; }",
                "        i = i + 1;",
                "    }",
            ]
        else:  # selfdep：跨迭代依赖（DOALL 禁区）
            L += [
                "    i = 1;",
                f"    while (i < {n1}) {{",
                "        C[i] = C[i - 1] + A[i][0];",
                "        i = i + 1;",
                "    }",
            ]
        return L


def run(cmd, timeout, **kw):
    return subprocess.run(cmd, capture_output=True, timeout=timeout, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep-dir", default="/tmp/loop_fuzz_fail")
    ap.add_argument("--compiler", default="/workspace/build/compiler")
    ap.add_argument("--lib", default="/workspace/lib")
    args = ap.parse_args()

    os.makedirs(TMP, exist_ok=True)
    os.makedirs(args.keep_dir, exist_ok=True)
    rng = random.Random(args.seed)
    fails = 0

    for case in range(args.n):
        src = Gen(rng).program()
        sy = f"{TMP}/f{case}.sy"
        open(sy, "w").write(src)
        outs = {}
        ok = True
        for opt in ("-O0", "-O1"):
            s_file = f"{TMP}/f{case}{opt}.s"
            elf = f"{TMP}/f{case}{opt}.elf"
            r = run([args.compiler, "-S", sy, "-o", s_file, opt], 120)
            if r.returncode != 0:
                print(f"[{case}] CE {opt}", flush=True)
                ok = False
                break
            r = run(["gcc", s_file, "-o", elf, "-L", args.lib, "-lsysy",
                     "-static"], 60)
            if r.returncode != 0:
                print(f"[{case}] LE {opt}", flush=True)
                ok = False
                break
            r = run([elf], 30, stdin=subprocess.DEVNULL)
            outs[opt] = (r.stdout, r.returncode)
        if not ok or outs["-O0"] != outs["-O1"]:
            fails += 1
            keep = f"{args.keep_dir}/f{case}.sy"
            open(keep, "w").write(src)
            if ok:
                print(f"[{case}] DIFF O0={outs['-O0']} O1={outs['-O1']} "
                      f"-> {keep}", flush=True)
            else:
                print(f"[{case}] kept -> {keep}", flush=True)
        elif case % 20 == 0:
            print(f"[{case}] ok", flush=True)

    print(f"done: {args.n} cases, {fails} failures", flush=True)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
