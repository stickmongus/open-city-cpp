#!/usr/bin/env python3
"""
Pure-Python generator for Open City Android launcher icons (mdpi..xxxhdpi).
Draws a vibrant neon twilight skyline + golden sunset disc + cyber-cyan road icon
using only Python standard library (struct + zlib).
"""
import math
import pathlib
import struct
import zlib


def write_png(path: pathlib.Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack("!I", len(data))
            + tag
            + data
            + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride : (y + 1) * stride])

    ihdr = struct.pack("!IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return lo if v < lo else (hi if v > hi else v)


def mix(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def render_icon(size: int, round_mask: bool) -> bytes:
    buf = bytearray(size * size * 4)
    # Skyscraper silhouettes (x0, x1, top_y) in normalized [0..1]
    buildings = [
        (0.06, 0.21, 0.44),
        (0.18, 0.34, 0.34),
        (0.32, 0.49, 0.24),
        (0.47, 0.62, 0.30),
        (0.60, 0.78, 0.20),
        (0.75, 0.94, 0.40),
    ]

    for py in range(size):
        ny = (py + 0.5) / size
        for px in range(size):
            nx = (px + 0.5) / size

            # Rounded-square or circle silhouette alpha with smooth anti-aliasing
            dx = abs(nx - 0.5)
            dy = abs(ny - 0.5)
            if round_mask:
                dist = math.hypot(dx, dy) - 0.47
            else:
                rad = 0.19
                qx = max(dx - (0.47 - rad), 0.0)
                qy = max(dy - (0.47 - rad), 0.0)
                dist = math.hypot(qx, qy) - rad
            aa = clamp(0.5 - dist * size * 0.9)
            if aa <= 0.0:
                continue

            # Smooth twilight-to-California-sunset gradient sky (zero band seam)
            sky_t = clamp(ny / 0.74)
            r = mix(12.0, 246.0, sky_t ** 1.65)
            g = mix(20.0, 96.0, sky_t ** 2.15)
            b = mix(62.0, 128.0, math.sin(sky_t * math.pi * 0.85))
            if sky_t > 0.65:
                warm = (sky_t - 0.65) / 0.35
                r = mix(r, 255.0, warm * 0.45)
                g = mix(g, 138.0, warm * 0.55)
                b = mix(b, 92.0, warm * 0.40)

            # Glowing golden sunset sun disc rising cleanly behind skyline
            sun_d = math.hypot(nx - 0.50, ny - 0.37)
            if sun_d < 0.27:
                sun_a = clamp((0.27 - sun_d) * size * 0.7)
                st = clamp((ny - 0.10) / 0.54)
                sr = 255.0
                sg = mix(236.0, 118.0, st)
                sb = mix(104.0, 62.0, st)
                r = mix(r, sr, sun_a)
                g = mix(g, sg, sun_a)
                b = mix(b, sb, sun_a)
            elif sun_d < 0.45:
                glow = ((0.45 - sun_d) / 0.18) ** 2 * 0.38
                r = min(255.0, r + 255.0 * glow)
                g = min(255.0, g + 150.0 * glow)
                b = min(255.0, b + 88.0 * glow)

            # Downtown glass skyscrapers with warm gold & cyber-cyan lit windows
            for idx, (bx0, bx1, btop) in enumerate(buildings):
                if bx0 <= nx <= bx1 and ny >= btop and ny <= 0.74:
                    r, g, b = 14.0, 21.0, 36.0
                    # Edge rim cyan/gold highlight
                    if abs(nx - bx0) < 0.013 or abs(nx - bx1) < 0.013 or abs(ny - btop) < 0.013:
                        r, g, b = 72.0, 222.0, 255.0
                    else:
                        cols = 3 if (bx1 - bx0) < 0.16 else 4
                        rows = max(4, int((0.74 - btop) * 18.0))
                        wx = ((nx - bx0) / (bx1 - bx0)) * cols
                        wy = ((ny - btop) / max(0.05, 0.74 - btop)) * rows
                        fx = wx - math.floor(wx)
                        fy = wy - math.floor(wy)
                        if 0.22 < fx < 0.78 and 0.24 < fy < 0.76:
                            cell_hash = math.sin(math.floor(wx) * 127.1 + math.floor(wy) * 311.7 + idx * 74.7) * 43758.5453
                            h = cell_hash - math.floor(cell_hash)
                            if h > 0.36:
                                if h > 0.68:
                                    r, g, b = 255.0, 214.0, 102.0
                                else:
                                    r, g, b = 88.0, 234.0, 255.0
                    break

            # Wet reflective avenue foreground with perspective yellow lane line & neon glow
            if ny > 0.74:
                road_t = clamp((ny - 0.74) / 0.26)
                r = mix(18.0, 10.0, road_t)
                g = mix(24.0, 14.0, road_t)
                b = mix(38.0, 26.0, road_t)
                # Horizon neon strip
                if ny < 0.77:
                    r, g, b = 78.0, 228.0, 255.0
                # Center golden highway stripe expanding toward viewer
                stripe_w = 0.012 + road_t * 0.022
                if abs(nx - 0.50) < stripe_w and ((ny * 18.0) % 1.0) > 0.25:
                    r, g, b = 255.0, 206.0, 68.0
                # Twin red/cyan tail/headlight reflections on wet asphalt
                for lx, col in [(0.34, (255, 64, 96)), (0.66, (68, 226, 255))]:
                    ld = abs(nx - lx)
                    if ld < 0.06:
                        glow = (1.0 - ld / 0.06) ** 2 * 0.48
                        r = min(255.0, r + col[0] * glow)
                        g = min(255.0, g + col[1] * glow)
                        b = min(255.0, b + col[2] * glow)

            # Subtle metallic border ring
            if abs(dist) < (1.5 / size):
                rim_a = clamp(1.0 - abs(dist) / (1.5 / size)) * 0.72
                r = mix(r, 96.0, rim_a)
                g = mix(g, 224.0, rim_a)
                b = mix(b, 255.0, rim_a)

            off = (py * size + px) * 4
            buf[off] = int(clamp(r, 0.0, 255.0))
            buf[off + 1] = int(clamp(g, 0.0, 255.0))
            buf[off + 2] = int(clamp(b, 0.0, 255.0))
            buf[off + 3] = int(clamp(aa * 255.0, 0.0, 255.0))
    return bytes(buf)


def main() -> int:
    res_dir = pathlib.Path(__file__).resolve().parent.parent / "android" / "res"
    densities = {
        "mipmap-mdpi": 48,
        "mipmap-hdpi": 72,
        "mipmap-xhdpi": 96,
        "mipmap-xxhdpi": 144,
        "mipmap-xxxhdpi": 192,
    }
    for folder, px in densities.items():
        write_png(res_dir / folder / "ic_launcher.png", px, px, render_icon(px, round_mask=False))
        write_png(res_dir / folder / "ic_launcher_round.png", px, px, render_icon(px, round_mask=True))
    print(f"[gen_android_icons] Generated launcher icons in {res_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
