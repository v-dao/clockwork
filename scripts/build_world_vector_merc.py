#!/usr/bin/env python3
"""将 Natural Earth 陆地 GeoJSON 转为 Web 墨卡托米制多边形二进制，供 situation_view 矢量填充。

无第三方依赖（仅 urllib + json）。默认下载 ne_110m_land.geojson（公有领域数据）。

输出格式（小端）：
魔数 b\"CWv2\" + uint32 version=2
  uint32 num_polygons
  每个 polygon: uint32 num_rings
    每个 ring: uint32 n_vertices, 后跟 n_vertices 个 (float32 x, float32 y)
 每个 polygon 的首环为外环，其余为洞（与 GeoJSON / GLU 奇偶规则一致）。
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import urllib.request
from pathlib import Path


def lonlat_to_web_mercator_m(lon_deg: float, lat_deg: float) -> tuple[float, float]:
    """与 situation_view.cpp 中 mercator_meters_to_lonlat 互逆（球面 Web 墨卡托）。"""
    r = 6378137.0
    y_max = 20037508.34
    lon_rad = math.radians(lon_deg)
    lat_rad = math.radians(lat_deg)
    lat_rad = max(-math.pi / 2 * 0.999999, min(math.pi / 2 * 0.999999, lat_rad))
    x = r * lon_rad
    y = r * math.log(math.tan(math.pi / 4.0 + lat_rad / 2.0))
    y = max(-y_max, min(y_max, y))
    return (float(x), float(y))


def iter_polygon_rings(geom: dict) -> list[list[list[float]]]:
    """返回若干 polygon，每个为 ring 列表；每环为 [lon, lat] 列表。"""
    t = geom.get("type")
    coords = geom.get("coordinates")
    if not coords:
        return []
    out: list[list[list[float]]] = []
    if t == "Polygon":
        poly = coords
        rings = [[[float(p[0]), float(p[1])] for p in ring] for ring in poly]
        out.append(rings)
    elif t == "MultiPolygon":
        for poly in coords:
            rings = [[[float(p[0]), float(p[1])] for p in ring] for ring in poly]
            out.append(rings)
    return out


def load_geojson(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def collect_all_polygons(fc: dict) -> list[list[list[list[float]]]]:
    """list[polygon][ring][vertex][lon,lat]"""
    polys: list[list[list[list[float]]]] = []
    for feat in fc.get("features", []):
        g = feat.get("geometry")
        if not g:
            continue
        for pr in iter_polygon_rings(g):
            polys.append(pr)
    return polys


def write_merc2(path: Path, polygons: list[list[list[list[float]]]]) -> None:
    cleaned: list[list[list[tuple[float, float]]]] = []
    for rings in polygons:
        cr: list[list[tuple[float, float]]] = []
        for ring in rings:
            if len(ring) < 3:
                continue
            cr.append([(float(lon), float(lat)) for lon, lat in ring])
        if cr:
            cleaned.append(cr)

    chunks: list[bytes] = [b"CWv2", struct.pack("<I", 2)]
    chunks.append(struct.pack("<I", len(cleaned)))
    for rings in cleaned:
        chunks.append(struct.pack("<I", len(rings)))
        for ring in rings:
            flat: list[float] = []
            for lon, lat in ring:
                mx, my = lonlat_to_web_mercator_m(lon, lat)
                flat.extend((mx, my))
            n = len(flat) // 2
            chunks.append(struct.pack("<I", n))
            chunks.append(struct.pack("<" + "f" * len(flat), *flat))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(chunks))
    print(f"wrote {path} ({len(cleaned)} land polygons)")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Web Mercator vector land file for situation_view.")
    ap.add_argument(
        "-i",
        "--input",
        type=Path,
        help="GeoJSON path (default: download Natural Earth 110m land)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output .merc2 (default: assets/maps/world_land.merc2)",
    )
    ap.add_argument(
        "--url",
        default="https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
 "master/geojson/ne_110m_land.geojson",
        help="Download URL when --input omitted",
    )
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    out = args.output
    if out is None:
        out = root / "assets" / "maps" / "world_land.merc2"
    elif not out.is_absolute():
        out = root / out

    if args.input is not None:
        src = args.input if args.input.is_absolute() else root / args.input
        if not src.is_file():
            raise SystemExit(f"missing input {src}")
        data = load_geojson(src)
    else:
        print(f"fetching {args.url}")
        with urllib.request.urlopen(args.url, timeout=120) as resp:  # noqa: S310
            raw = resp.read()
        data = json.loads(raw.decode("utf-8"))

    polys = collect_all_polygons(data)
    if not polys:
        raise SystemExit("no polygons found in GeoJSON")
    write_merc2(out, polys)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
