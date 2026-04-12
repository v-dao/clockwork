#!/usr/bin/env python3
"""生成等距圆柱投影世界底图 BMP（24 bpp），供 situation_view 使用。无第三方依赖。

默认4096×2048；可用 --width/--height 或 --preset small|medium|large|huge 调整。
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path


def smootherstep01(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def write_bmp_rgb24(path: Path, width: int, height: int, rgb_pixels_top_first: bytes) -> None:
    """rgb_pixels_top_first: 每行 width*3 字节，逻辑为 R,G,B，首行为图像最北一行。

    写入磁盘时按 BMP 规范输出每像素 B,G,R（行仍自下而上排列）。
    """
    if len(rgb_pixels_top_first) != width * height * 3:
        raise ValueError("pixel size mismatch")
    row_stride = (width * 3 + 3) // 4 * 4
    pad = row_stride - width * 3
    bottom_up = bytearray()
    for y in range(height - 1, -1, -1):
        row_start = y * width * 3
        row = rgb_pixels_top_first[row_start : row_start + width * 3]
        for x in range(width):
            i = x * 3
            r, g, b = row[i], row[i + 1], row[i + 2]
            bottom_up.extend((b, g, r))
        bottom_up.extend(b"\x00" * pad)

    off_bits = 14 + 40
    file_size = off_bits + len(bottom_up)
    hdr = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, off_bits)
    info = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height,
        1,
        24,
        0,
        len(bottom_up),
        0,
        0,
        0,
        0,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(hdr + info + bottom_up)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate equirectangular world basemap BMP.")
    parser.add_argument("--width", type=int, help="image width (default: from --preset or 4096)")
    parser.add_argument("--height", type=int, help="image height (default: width//2)")
    parser.add_argument(
        "--preset",
        choices=("small", "medium", "large", "huge"),
        default="large",
        help="small=1024×512, medium=2048×1024, large=4096×2048, huge=8192×4096",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output path (default: assets/maps/world_equirect_WxH.bmp under repo root)",
    )
    args = parser.parse_args()

    presets = {
        "small": (1024, 512),
        "medium": (2048, 1024),
        "large": (4096, 2048),
        "huge": (8192, 4096),
    }
    if args.width is not None:
        w = args.width
        h = args.height if args.height is not None else max(1, w // 2)
    else:
        w, h = presets[args.preset]

    root = Path(__file__).resolve().parent.parent
    if args.output is not None:
        out = args.output if args.output.is_absolute() else root / args.output
    else:
        out = root / "assets" / "maps" / f"world_equirect_{w}x{h}.bmp"

    # 椭圆近似陆地（经纬度，单位度）；海洋深蓝，陆地绿/褐。
    # 注意：Web 墨卡托原点 (0m,0m) 对应 (lon=0, lat=0) 在几内亚湾海上；
    # 旧版非洲椭圆过大，会把该点标成陆地，导致局部场景「整屏深绿」。
    ellipses = [
        (-100.0, 45.0, 42.0, 30.0),
        (-60.0, -12.0, 20.0, 38.0),
        (20.0, 5.0, 24.0, 30.0),  # 非洲（西缘约 -4°，赤道附近可见岸；与下方小海盆组合后 (0,0) 仍为海）
        (18.0, 58.0, 22.0, 14.0),
        (95.0, 48.0, 55.0, 28.0),
        (128.0, -28.0, 28.0, 14.0),
        (-42.0, 68.0, 16.0, 14.0),
        (0.0, -78.0, 55.0, 12.0),
        (-135.0, 62.0, 22.0, 12.0),
        (50.0, 28.0, 18.0, 12.0),
    ]

    def land_strength(lon: float, lat: float) -> float:
        s = 0.0
        for cx, cy, rx, ry in ellipses:
            if rx <= 0 or ry <= 0:
                continue
            t = ((lon - cx) / rx) ** 2 + ((lat - cy) / ry) ** 2
            if t <= 1.0:
                s = max(s, 1.0 - t)
        # 仅挖掉几内亚湾极小块洋面（约 ±3.5°×±4.5°），避免旧版大椭圆把 0°～15°E 整段海岸全涂成海。
        gulf = ((lon + 1.0) / 3.5) ** 2 + (lat / 4.5) ** 2
        if gulf < 1.0:
            s = 0.0
        return s

    o_r, o_g, o_b = 20, 58, 118

    pix = bytearray(w * h * 3)
    for y in range(h):
        lat = 90.0 - (y + 0.5) / h * 180.0
        for x in range(w):
            lon = (x + 0.5) / w * 360.0 - 180.0
            ls = land_strength(lon, lat)
            # 软过渡岸线，高分辨率下锯齿更少
            edge = smootherstep01((ls - 0.02) / 0.18)
            lr = 35.0 + 80.0 * ls
            lg = 90.0 + 90.0 * ls
            lb = 40.0 + 40.0 * ls
            r = int(o_r + (lr - o_r) * edge)
            g = int(o_g + (lg - o_g) * edge)
            b = int(o_b + (lb - o_b) * edge)
            i = (y * w + x) * 3
            pix[i] = max(0, min(255, r))
            pix[i + 1] = max(0, min(255, g))
            pix[i + 2] = max(0, min(255, b))

    write_bmp_rgb24(out, w, h, bytes(pix))
    print(f"wrote {out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
