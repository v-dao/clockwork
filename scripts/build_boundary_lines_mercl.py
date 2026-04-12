#!/usr/bin/env python3
"""将国界/折线矢量转为 Web 墨卡托二进制 .mercl（CWl1），供 situation_view 绘制。

说明：Natural Earth 的「陆地国界」在 10m_cultural（ne_10m_admin_0_boundary_lines_land），
不在 10m_physical。若只有 physical 目录，可把 cultural 中的同名 .shp/.geojson 拷入后使用 -i。

无第三方依赖。支持输入：.geojson / .json / .shp（ESRI PolyLine）。"""
from __future__ import annotations

import argparse
import json
import math
import struct
import urllib.request
from pathlib import Path


def lonlat_to_web_mercator_m(lon_deg: float, lat_deg: float) -> tuple[float, float]:
    r = 6378137.0
    y_max = 20037508.34
    lon_rad = math.radians(lon_deg)
    lat_rad = math.radians(lat_deg)
    lat_rad = max(-math.pi / 2 * 0.999999, min(math.pi / 2 * 0.999999, lat_rad))
    x = r * lon_rad
    y = r * math.log(math.tan(math.pi / 4.0 + lat_rad / 2.0))
    y = max(-y_max, min(y_max, y))
    return (float(x), float(y))


def split_strip_merc(
    lonlat: list[tuple[float, float]],
) -> list[list[tuple[float, float]]]:
    """在墨卡托平面上避免跨日期变更线的大跳：相邻点 x 差过大则拆条。"""
    if len(lonlat) < 2:
        return []
    merc: list[tuple[float, float]] = [lonlat_to_web_mercator_m(lon, lat) for lon, lat in lonlat]
    out: list[list[tuple[float, float]]] = []
    cur: list[tuple[float, float]] = [merc[0]]
    for i in range(1, len(merc)):
        pmx, pmy = cur[-1]
        mx, my = merc[i]
        if abs(mx - pmx) > 25_000_000.0:
            if len(cur) >= 2:
                out.append(cur)
            cur = [(mx, my)]
        else:
            cur.append((mx, my))
    if len(cur) >= 2:
        out.append(cur)
    return out


def iter_linestrings_from_geom(geom: dict) -> list[list[tuple[float, float]]]:
    """返回若干 [lon,lat] 折线（未投影）。"""
    t = geom.get("type")
    coords = geom.get("coordinates")
    if not coords:
        return []
    lines: list[list[tuple[float, float]]] = []
    if t == "LineString":
        ring = [(float(p[0]), float(p[1])) for p in coords]
        lines.append(ring)
    elif t == "MultiLineString":
        for seg in coords:
            lines.append([(float(p[0]), float(p[1])) for p in seg])
    elif t == "GeometryCollection":
        for g in geom.get("geometries", []):
            lines.extend(iter_linestrings_from_geom(g))
    return lines


def geojson_to_strips(fc: dict) -> list[list[tuple[float, float]]]:
    strips: list[list[tuple[float, float]]] = []
    for feat in fc.get("features", []):
        g = feat.get("geometry")
        if not g:
            continue
        for line in iter_linestrings_from_geom(g):
            strips.extend(split_strip_merc(line))
    return strips


def read_shp_polylines(path: Path) -> list[list[tuple[float, float]]]:
    """读取 ESRI Shapefile PolyLine（type=3），返回经纬度折线列表。"""
    raw = path.read_bytes()
    if len(raw) < 100:
        raise ValueError("shp too small")
    file_len_words = struct.unpack_from("<I", raw, 24)[0]
    file_len = min(file_len_words * 2, len(raw))

    pos = 100
    lines: list[list[tuple[float, float]]] = []

    while pos + 8 <= file_len:
        _rec_num = struct.unpack_from(">I", raw, pos)[0]
        content_words = struct.unpack_from(">I", raw, pos + 4)[0]
        content_len = content_words * 2
        rec_start = pos + 8
        pos = rec_start + content_len
        if rec_start + content_len > len(raw) or content_len < 4:
            break
        st = struct.unpack_from("<i", raw, rec_start)[0]
        if st != 3 or content_len < 44:
            continue
        num_parts = struct.unpack_from("<i", raw, rec_start + 36)[0]
        num_points = struct.unpack_from("<i", raw, rec_start + 40)[0]
        if num_parts < 1 or num_points < 2 or num_parts > 1_000_000 or num_points > 50_000_000:
            continue
        off = rec_start + 44
        parts = list(struct.unpack_from("<" + "i" * num_parts, raw, off))
        off += 4 * num_parts
        pts: list[tuple[float, float]] = []
        for _ in range(num_points):
            if off + 16 > len(raw):
                break
            x, y = struct.unpack_from("<dd", raw, off)
            off += 16
            pts.append((float(x), float(y)))
        for pi in range(num_parts):
            start = parts[pi]
            end = parts[pi + 1] if pi + 1 < num_parts else num_points
            seg = pts[start:end]
            if len(seg) >= 2:
                lines.append(seg)

    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description="Build CWl1 boundary line strips for situation_view.")
    ap.add_argument(
        "-i",
        "--input",
        type=Path,
        help="GeoJSON or .shp (PolyLine). Default: download NE admin-0 boundary lines (land).",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output .mercl (default: assets/maps/world_boundary_lines.mercl)",
    )
    ap.add_argument(
        "--url",
        default="https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
        "master/geojson/ne_10m_admin_0_boundary_lines_land.geojson",
        help="GeoJSON URL when --input omitted",
    )
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    out = args.output
    if out is None:
        out = root / "assets" / "maps" / "world_boundary_lines.mercl"
    elif not out.is_absolute():
        out = root / out

    strips: list[list[tuple[float, float]]] = []

    if args.input is not None:
        src = args.input if args.input.is_absolute() else root / args.input
        if not src.is_file():
            raise SystemExit(f"missing input {src}")
        suf = src.suffix.lower()
        if suf == ".shp":
            for line in read_shp_polylines(src):
                strips.extend(split_strip_merc(line))
        else:
            data = json.loads(src.read_text(encoding="utf-8"))
            strips = geojson_to_strips(data)
    else:
        print(f"fetching {args.url}")
        with urllib.request.urlopen(args.url, timeout=180) as resp:  # noqa: S310
            raw = resp.read()
        data = json.loads(raw.decode("utf-8"))
        strips = geojson_to_strips(data)

    good = [s for s in strips if len(s) >= 2]
    if not good:
        raise SystemExit("no line strips produced")

    chunks: list[bytes] = [b"CWl1", struct.pack("<II", 1, len(good))]
    for strip in good:
        flat: list[float] = []
        for x, y in strip:
            flat.extend((x, y))
        n = len(flat) // 2
        chunks.append(struct.pack("<I", n))
        chunks.append(struct.pack("<" + "f" * len(flat), *flat))

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"".join(chunks))
    print(f"wrote {out} ({len(good)} strips)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
