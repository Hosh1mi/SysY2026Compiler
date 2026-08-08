#!/usr/bin/env python3
"""Local HTTP bridge for the SysY pipeline visualizer.

The bridge keeps compiler execution local, writes source snippets only to a
temporary directory inside the repository, and exposes pass snapshots as JSON.
"""

from __future__ import annotations

import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


VISUALIZER_DIR = Path(__file__).resolve().parent
ROOT = VISUALIZER_DIR.parent
HTML = VISUALIZER_DIR / "index.html"
RUNS = VISUALIZER_DIR / ".runs"
RUNS.mkdir(exist_ok=True)

MID_MARKER = re.compile(r"^; === IR (Before|After) (.+?) ===\s*$")
MACHINE_MARKER = re.compile(r"^; \*\*\* MIR (before|after) (.+?) \*\*\*\s*$")

_CACHE: dict[str, dict[str, Any]] = {}
_CACHE_LOCK = threading.Lock()
_CACHE_LIMIT = 4


def _docker_container() -> str | None:
    docker = shutil.which("docker")
    if not docker:
        return None
    try:
        result = subprocess.run(
            [docker, "ps", "--format", "{{.Names}}"],
            check=True,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    names = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return names[0] if names else None


def _compiler_candidates() -> list[Path]:
    configured = os.environ.get("SYSY_COMPILER_PATH")
    if configured:
        candidate = Path(configured)
        return [candidate if candidate.is_absolute() else ROOT / candidate]
    return [ROOT / "build" / "compiler"]


def _container_path(path: Path) -> str | None:
    try:
        relative = path.resolve().relative_to(ROOT)
    except ValueError:
        return None
    return f"/workspace/{relative}"


def _compiler_command(env: dict[str, str], args: list[str]) -> list[str]:
    candidates = _compiler_candidates()
    candidate = candidates[0]
    for possible in candidates:
        if not possible.exists() or not os.access(possible, os.X_OK):
            continue
        candidate = possible
        try:
            subprocess.run(
                [str(possible), "--help"],
                capture_output=True,
                timeout=1,
            )
            return [str(possible), *args]
        except OSError:
            continue
    for possible in reversed(candidates):
        if possible.exists() and os.access(possible, os.X_OK):
            candidate = possible
            break

    container = _docker_container()
    if not container:
        raise RuntimeError(
            "No runnable compiler found. Build the configured compiler or start the project Docker container."
        )
    container_compiler = _container_path(candidate)
    if container_compiler is None:
        raise RuntimeError(
            "Compiler path must be inside the project when Docker fallback is used."
        )
    mapped_args = []
    for arg in args:
        path = Path(arg)
        mapped = _container_path(path) if path.is_absolute() else None
        mapped_args.append(mapped or arg)
    return [
        "docker",
        "exec",
        container,
        "env",
        *[f"{key}={value}" for key, value in env.items()],
        container_compiler,
        *mapped_args,
    ]


def run_compiler(source: str, opt: int, args: list[str], env_updates: dict[str, str]) -> tuple[str, str]:
    fd, filename = tempfile.mkstemp(prefix="snippet-", suffix=".sy", dir=RUNS)
    os.close(fd)
    path = Path(filename)
    path.write_text(source, encoding="utf-8")
    env = os.environ.copy()
    env.update(env_updates)
    compiler_args = [f"-O{opt}", *args, str(path)]
    try:
        command = _compiler_command(env_updates, compiler_args)
        result = subprocess.run(
            command,
            cwd=ROOT,
            env=env if command[0] != "docker" else None,
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "compiler failed"
            raise RuntimeError(detail)
        return result.stdout, result.stderr
    finally:
        path.unlink(missing_ok=True)


def _blocks(stderr: str, marker: re.Pattern[str]) -> list[tuple[str, str, str]]:
    blocks: list[tuple[str, str, str]] = []
    current: tuple[str, str, list[str]] | None = None
    for line in stderr.splitlines(keepends=True):
        match = marker.match(line.rstrip("\n"))
        if match:
            if current:
                blocks.append((current[0], current[1], "".join(current[2]).strip()))
            current = (match.group(1), match.group(2), [])
        elif current and (
            line.startswith("; === ")
            or line.startswith("; *** ")
            or line.startswith("[PipelinePass] ")
            or line.startswith("[LoopPipeline]")
            or line.startswith("[aarch64-rewrite]")
        ):
            blocks.append((current[0], current[1], "".join(current[2]).strip()))
            current = None
        elif current:
            current[2].append(line)
    if current:
        blocks.append((current[0], current[1], "".join(current[2]).strip()))
    return blocks


def parse_mid(stderr: str) -> list[dict[str, Any]]:
    pending: dict[str, list[str]] = {}
    result: list[dict[str, Any]] = []
    occurrences: dict[str, int] = {}
    for when, name, text in _blocks(stderr, MID_MARKER):
        if when == "Before":
            pending.setdefault(name, []).append(text)
            continue
        before_queue = pending.setdefault(name, [])
        before = before_queue.pop(0) if before_queue else ""
        occurrence = occurrences.get(name, 0)
        occurrences[name] = occurrence + 1
        result.append({
            "kind": "mid",
            "name": name,
            "occurrence": occurrence,
            "before": before,
            "after": text,
            "changed": before != text,
        })
    return result


def parse_machine(stderr: str) -> dict[str, dict[str, Any]]:
    """Collect every function's snapshot under the pass that produced it."""
    stages: dict[str, dict[str, list[str]]] = {}
    for when, name, text in _blocks(stderr, MACHINE_MARKER):
        key = "Before" if when == "before" else "After"
        stages.setdefault(name, {"Before": [], "After": []})[key].append(text)
    return {
        name: {
            "before": "\n\n".join(values["Before"]),
            "after": "\n\n".join(values["After"]),
            "changed": "\n\n".join(values["Before"]) != "\n\n".join(values["After"]),
        }
        for name, values in stages.items()
    }


def parse_selection_dags(stderr: str) -> str:
    dags: list[str] = []
    current: list[str] | None = None
    for line in stderr.splitlines():
        if line.startswith("selection-dag "):
            if current:
                dags.append("\n".join(current))
            current = [line]
            continue
        if current is None:
            continue
        current.append(line)
        if line == "}":
            dags.append("\n".join(current))
            current = None
    if current:
        dags.append("\n".join(current))
    return "\n\n".join(dags)


def _cache_key(source: str, opt: int) -> str:
    digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
    compiler = _compiler_candidates()[0]
    try:
        stat = compiler.stat()
        compiler_identity = f"{compiler}:{stat.st_size}:{stat.st_mtime_ns}"
    except OSError:
        compiler_identity = str(compiler)
    return f"{opt}:{digest}:{compiler_identity}"


def _stage_display_name(name: str) -> str:
    return name.replace("::", " / ")


def compile_once(source: str, opt: int) -> dict[str, Any]:
    """Compile once and retain every mid/backend snapshot for later clicks."""
    key = _cache_key(source, opt)
    with _CACHE_LOCK:
        cached = _CACHE.get(key)
        if cached is not None:
            return cached

        stdout, stderr = run_compiler(
            source,
            opt,
            ["-S", "--dump-ir", "--dump-pre-machine-instr"],
            {
                "DUMP_IR_PASS": "*",
                "TRACE_PASS_PIPELINE": "1",
                "DUMP_MACHINE_PIPELINE": "1",
            },
        )
        mid = parse_mid(stderr)
        occurrences: dict[str, int] = {}
        stages: list[dict[str, Any]] = []
        mid_snapshots: dict[tuple[str, int], dict[str, Any]] = {}
        for item in mid:
            name = item["name"]
            if name == "CanonicalCleanup":
                continue
            occurrence = occurrences.get(name, 0)
            occurrences[name] = occurrence + 1
            item["occurrence"] = occurrence
            item["displayName"] = _stage_display_name(name)
            item["id"] = f"mid:{name}:{occurrence}"
            mid_snapshots[(name, occurrence)] = item
            stages.append({
                "id": item["id"],
                "kind": "mid",
                "name": name,
                "displayName": item["displayName"],
                "parent": name.split("::", 1)[0] if "::" in name else None,
                "occurrence": occurrence,
                "changed": item["changed"],
            })
        machine = parse_machine(stderr)
        backend: dict[str, dict[str, Any]] = {}
        selection_dags = parse_selection_dags(stderr)
        if selection_dags:
            backend["SelectionDAG"] = {
                "before": "",
                "after": selection_dags,
                "changed": True,
            }
        if selection_dags and machine:
            first_machine = next(iter(machine.values()))["before"]
            backend["AArch64InstructionSelection"] = {
                "before": selection_dags,
                "after": first_machine,
                "changed": selection_dags != first_machine,
            }
        backend.update(machine)
        for pass_name, backend_item in backend.items():
            stages.append({
                "id": f"backend:{pass_name}",
                "kind": "backend",
                "name": pass_name,
                "phase": pass_name,
                "changed": backend_item["changed"],
            })
        stages.append({
            "id": "backend:assembly",
            "kind": "backend",
            "name": "Assembly emission",
            "phase": "assembly",
            "changed": True,
        })
        snapshot = {
            "stages": stages,
            "mid": mid_snapshots,
            "backend": backend,
            "assembly": stdout,
            "opt": opt,
        }
        _CACHE[key] = snapshot
        while len(_CACHE) > _CACHE_LIMIT:
            _CACHE.pop(next(iter(_CACHE)))
        return snapshot


def side_by_side(before: str, after: str) -> list[dict[str, Any]]:
    left = before.splitlines()
    right = after.splitlines()
    rows: list[dict[str, Any]] = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, left, right, autojunk=False).get_opcodes():
        if tag == "equal":
            rows.extend({"left": left[i], "right": right[j], "status": "equal"} for i, j in zip(range(i1, i2), range(j1, j2)))
        elif tag == "delete":
            rows.extend({"left": left[i], "right": "", "status": "removed"} for i in range(i1, i2))
        elif tag == "insert":
            rows.extend({"left": "", "right": right[j], "status": "added"} for j in range(j1, j2))
        else:
            count = max(i2 - i1, j2 - j1)
            for offset in range(count):
                rows.append({
                    "left": left[i1 + offset] if i1 + offset < i2 else "",
                    "right": right[j1 + offset] if j1 + offset < j2 else "",
                    "status": "changed",
                })
    return rows


def compile_summary(source: str, opt: int) -> dict[str, Any]:
    snapshot = compile_once(source, opt)
    return {"stages": snapshot["stages"], "opt": opt}


class Handler(BaseHTTPRequestHandler):
    def _json(self, status: int, value: Any) -> None:
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        if self.path == "/api/health":
            self._json(200, {"ok": True})
            return
        if self.path != "/":
            self.send_error(404)
            return
        payload = HTML.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self) -> None:
        if self.path not in ("/api/compile", "/api/stage"):
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length))
            source = str(body.get("source", ""))
            opt = max(0, min(3, int(body.get("opt", 1))))
            if not source.strip():
                raise ValueError("请输入 SysY 源码")
            if self.path == "/api/compile":
                self._json(200, compile_summary(source, opt))
                return

            snapshot = compile_once(source, opt)
            kind = body.get("kind")
            if kind == "mid":
                name = str(body["name"])
                occurrence = int(body.get("occurrence", 0))
                item = snapshot["mid"].get((name, occurrence))
                if item is None:
                    raise RuntimeError(f"未找到 Pass {name} 的第 {occurrence + 1} 次快照")
            else:
                phase = str(body["phase"])
                if phase == "assembly":
                    snapshots = next(
                        reversed(snapshot["backend"].values()),
                        {"before": "", "after": ""},
                    )
                    item = {
                        "before": snapshots["after"],
                        "after": snapshot["assembly"],
                        "name": "Assembly emission",
                        "changed": snapshots["after"] != snapshot["assembly"],
                    }
                else:
                    item = snapshot["backend"].get(phase)
                    if not item:
                        raise RuntimeError(f"未找到后端阶段 {phase} 的快照")
                    item["name"] = phase
            item["rows"] = side_by_side(item.get("before", ""), item.get("after", ""))
            self._json(200, item)
        except (ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as exc:
            self._json(400, {"error": str(exc)})


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Run the local SysY pipeline visualizer")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"SysY visualizer: http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
