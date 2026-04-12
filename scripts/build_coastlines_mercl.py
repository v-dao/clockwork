#!/usr/bin/env python3
"""从 Natural Earth 10m_physical/ne_10m_coastline.shp 生成 assets/maps/world_coastlines.mercl。

复用 build_boundary_lines_mercl.py（CWl1 折线格式）。无第三方依赖。"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    script = root / "scripts" / "build_boundary_lines_mercl.py"
    shp = root / "assets" / "maps" / "10m_physical" / "ne_10m_coastline.shp"
    out = root / "assets" / "maps" / "world_coastlines.mercl"
    if not shp.is_file():
        print(f"error: missing {shp}", file=sys.stderr)
        return 1
    return subprocess.call(
        [sys.executable, str(script), "-i", str(shp), "-o", str(out)],
        cwd=str(root),
    )


if __name__ == "__main__":
    raise SystemExit(main())
