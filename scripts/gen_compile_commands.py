#!/usr/bin/env python3
"""Emit build/compile_commands.json via `ninja -t compdb cxx` (stdlib only).

环境变量 NINJA_FILE：传给 ninja 的构建文件，默认 build.ninja；使用 build-headless.ninja 时
应设为 build-headless.ninja，以便 compdb 与当前目标集一致。
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    ninja = os.environ.get("NINJA", "ninja")
    ninja_file = os.environ.get("NINJA_FILE", "build.ninja")
    proc = subprocess.run(
        [ninja, "-f", ninja_file, "-t", "compdb", "cxx"],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout or "ninja -t compdb failed\n")
        return proc.returncode

    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        sys.stderr.write(f"invalid compdb JSON: {e}\n")
        return 1

    root_abs = str(root)
    for entry in data:
        entry["directory"] = root_abs

    out = root / "build" / "compile_commands.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
