#!/usr/bin/env python3
"""
Generate high-resolution 2048x2048 (4x4 grid of 512x512 seamless tiles) PBR texture atlases
for Open City Seoul v3.0.0 Native Vulkan renderer:
  - assets/textures/pbr_albedo.png        (RGBA8 sRGB Albedo + Alpha/WindowMask)
  - assets/textures/pbr_normal_rough.png  (RGBA8 Linear: R=NormalX, G=NormalY, B=Roughness, A=AO/SpecMask)

Tile layout (col = id % 4, row = id // 4, 512x512 each):
  0: Weathered Seoul Asphalt Road (mineral aggregate, tire wear tracks, subtle tar cracks)
  1: Korean Interlocking Sidewalk Granite Pavers (beveled sandstone/grey tiles + mortar grooves)
  2: Korean Luxury Apartment Facade (warm travertine concrete, balcony slabs, tinted veranda glass & rails)
  3: Gangnam/Yeouido Glass Curtain-Wall Skyscraper (reflective tinted panes, dark mullions, office interiors)
  4: Honed Granite/Limestone Office Facade (ashlar stone joints, bronze-framed recessed office windows)
  5: Seongsu/Hongdae Red-Terracotta Brick Commercial (running-bond bricks, mortar relief, dark loft windows)
  6: Seoul Mid-Rise Stone-Tile/Stucco Commercial (ivory-grey cladding, tidy 2F-5F clinic/retail windows)
  7: Ground-Floor Commercial Storefront Glass & Stone Pilasters (warm interior glow, dark mullions)
  8: Lush Seoul Park Grass & Clover (organic multi-scale grass blades, soil/moss tonal richness)
  9: Seoul Mountain Forest & Granite Cliff Slope (dense Korean pine canopy + granite rock strata)
 10: Concrete Rooftop Waterproof Membrane & Gravel Parapet
 11: Korean Traditional Hanok Giwa Roof Tiles (curved dark ceramic ribs + overlapping tile rows)
 12: Poured Bridge & Retaining Wall Formwork Concrete (subtle tie holes, weathering streaks)
 13: Realistic Fissured Korean Tree Bark (deep vertical organic ridges)
 14: Layered Organic Foliage Cluster (multi-leaf canopy texture for Ginkgo / Cherry / Pine)
 15: Korean Yellow Tactile Braille Paving & Granite Curbstone (raised tactile bump dots!)
"""

import math
import os
import numpy as np
from PIL import Image

TILE = 512
GRID = 4
SIZE = TILE * GRID


def tileable_fbm(n: int, octaves=5, base_freq=4, seed=1):
    """Fast vectorized seamless periodic fBM noise in [0, 1]."""
    rng = np.random.default_rng(seed)
    acc = np.zeros((n, n), dtype=np.float32)
    amp = 0.5
    tot = 0.0
    for oct_i in range(octaves):
        freq = base_freq * (2 ** oct_i)
        if freq > n:
            break
        grid = rng.random((freq, freq), dtype=np.float32)
        # Bilinear smoothstep periodic upsample
        coords = np.linspace(0, freq, n, endpoint=False, dtype=np.float32)
        i0 = np.floor(coords).astype(np.int32) % freq
        i1 = (i0 + 1) % freq
        f = coords - np.floor(coords)
        s = f * f * (3.0 - 2.0 * f)
        sy = s[:, None]
        sx = s[None, :]
        v00 = grid[np.ix_(i0, i0)]
        v10 = grid[np.ix_(i0, i1)]
        v01 = grid[np.ix_(i1, i0)]
        v11 = grid[np.ix_(i1, i1)]
        layer = (v00 * (1 - sx) + v10 * sx) * (1 - sy) + (v01 * (1 - sx) + v11 * sx) * sy
        acc += layer * amp
        tot += amp
        amp *= 0.5
    return acc / max(tot, 1e-5)


def height_to_normal_xy(h: np.ndarray, strength: float = 2.8):
    """Compute seamless Sobel tangent-space normal (nx, ny) packed into [0, 255]."""
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    nx = -dx
    ny = -dy
    nz = np.ones_like(h)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    nx *= inv_len
    ny *= inv_len
    r = np.clip((nx * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    g = np.clip((ny * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    return r, g


def make_rgba(rgb_f, a_f=None):
    out = np.empty((TILE, TILE, 4), dtype=np.uint8)
    out[:, :, 0:3] = np.clip(rgb_f * 255.0, 0, 255).astype(np.uint8)
    out[:, :, 3] = 255
    return out


def make_nr(h, rough_f, ao_f, strength=3.0):
    r, g = height_to_normal_xy(h, strength=strength)
    out = np.empty((TILE, TILE, 4), dtype=np.uint8)
    out[:, :, 0] = r
    out[:, :, 1] = g
    out[:, :, 2] = np.clip(rough_f * 255.0, 0, 255).astype(np.uint8)
    out[:, :, 3] = np.clip(ao_f * 255.0, 0, 255).astype(np.uint8)
    return out


def gen_asphalt():
    n1 = tileable_fbm(TILE, octaves=6, base_freq=8, seed=101)
    n2 = tileable_fbm(TILE, octaves=4, base_freq=32, seed=102)
    fine = tileable_fbm(TILE, octaves=3, base_freq=64, seed=103)
    # Dark mineral speckles + subtle longitudinal tire wear + tar seam cracks
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    crack_field = np.abs(tileable_fbm(TILE, octaves=4, base_freq=6, seed=104) - 0.5)
    crack = np.clip(1.0 - crack_field * 55.0, 0.0, 1.0)
    wear = 0.5 + 0.5 * np.sin(x * math.pi * 4.0)
    base = 0.165 + (n1 - 0.5) * 0.055 + (n2 - 0.5) * 0.045 + (fine - 0.5) * 0.065
    # Bright quartz aggregate flecks
    fleck = (fine > 0.78).astype(np.float32) * 0.06
    lum = np.clip(base + fleck - crack * 0.075 - wear * 0.015, 0.05, 0.34)
    rgb = np.stack([lum * 0.98, lum * 0.99, lum * 1.04], axis=-1)
    h = n2 * 0.45 + fine * 0.55 - crack * 0.35
    rough = np.clip(0.86 - wear * 0.14 - crack * 0.25 + (n1 - 0.5) * 0.10, 0.45, 0.96)
    ao = np.clip(1.0 - crack * 0.35 - (1.0 - fine) * 0.12, 0.4, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.5)


def gen_sidewalk_pavers():
    # Authentic Seoul interlocking square/herringbone granite sidewalk tiles (8x8 per tile)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    gx = (x / (TILE / 8.0))
    gy = (y / (TILE / 8.0))
    ix = np.floor(gx).astype(np.int32)
    iy = np.floor(gy).astype(np.int32)
    # Stagger every other row by half brick
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % 8
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)

    seam_x = np.minimum(fx, 1.0 - fx)
    seam_y = np.minimum(fy, 1.0 - fy)
    seam_d = np.minimum(seam_x, seam_y)
    grout = np.clip((0.055 - seam_d) / 0.035, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.04) / 0.06, 0.0, 1.0)

    # Per-paver warm sandstone / Seoul granite variation
    rng = np.random.default_rng(201)
    paver_var = rng.uniform(-0.055, 0.055, size=(8, 8)).astype(np.float32)
    paver_warm = rng.uniform(-0.025, 0.035, size=(8, 8)).astype(np.float32)
    pv = paver_var[iy % 8, ix_s]
    pw = paver_warm[iy % 8, ix_s]
    grain = tileable_fbm(TILE, octaves=5, base_freq=16, seed=202)

    r = 0.57 + pv + pw + (grain - 0.5) * 0.09
    g = 0.55 + pv + pw * 0.4 + (grain - 0.5) * 0.08
    b = 0.53 + pv - pw * 0.5 + (grain - 0.5) * 0.08
    rgb = np.stack([r, g, b], axis=-1) * (1.0 - grout[:, :, None] * 0.48)
    h = bevel * 0.55 + grain * 0.35 - grout * 0.50
    rough = np.clip(0.74 + grout * 0.20 + (grain - 0.5) * 0.12, 0.52, 0.96)
    ao = np.clip(1.0 - grout * 0.50, 0.4, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.0)


def gen_korean_apartment():
    # Authentic Seoul high-rise apartment (래미안/자이/아크로): 4 bays x 4 floors per 512px tile!
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = (x / (TILE / 4.0))
    by = (y / (TILE / 4.0))
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    ci = np.floor(bx).astype(np.int32) % 4
    ri = np.floor(by).astype(np.int32) % 4

    grain = tileable_fbm(TILE, octaves=5, base_freq=12, seed=301)
    # Warm travertine/concrete exterior panel wall + horizontal floor slab line
    slab_seam = ((fy < 0.045) | (fy > 0.97)).astype(np.float32)
    col_seam = ((fx < 0.025) | (fx > 0.975)).astype(np.float32)

    # Wide Korean veranda sliding window: fx in [0.09..0.91], fy in [0.16..0.78]
    in_win = (fx > 0.09) & (fx < 0.91) & (fy > 0.16) & (fy < 0.78)
    frame_outer = (fx > 0.07) & (fx < 0.93) & (fy > 0.13) & (fy < 0.81) & (~in_win)
    # Center sliding sash mullion + balcony glass safety railing at bottom 32% of window
    sash_v = in_win & ((np.abs(fx - 0.50) < 0.018) | (np.abs(fx - 0.28) < 0.012) | (np.abs(fx - 0.72) < 0.012))
    rail_bar = in_win & (np.abs(fy - 0.54) < 0.018)
    rail_posts = in_win & (fy > 0.54) & ((np.abs(fx - 0.20) < 0.010) | (np.abs(fx - 0.50) < 0.010) | (np.abs(fx - 0.80) < 0.010))
    # Subtle curtain/blind depth per apartment unit
    rng = np.random.default_rng(302)
    unit_lit = rng.uniform(0.0, 1.0, size=(4, 4)).astype(np.float32)
    uval = unit_lit[ri, ci]
    curtain = np.clip(0.5 + 0.5 * np.sin(fx * 55.0 + uval * 10.0), 0.0, 1.0)

    # Concrete wall color
    wall_r = 0.78 + (grain - 0.5) * 0.08 - slab_seam * 0.14 - col_seam * 0.07
    wall_g = 0.76 + (grain - 0.5) * 0.08 - slab_seam * 0.14 - col_seam * 0.07
    wall_b = 0.73 + (grain - 0.5) * 0.07 - slab_seam * 0.13 - col_seam * 0.07
    rgb = np.stack([wall_r, wall_g, wall_b], axis=-1)

    # Dark anodized window frame
    rgb[frame_outer] = np.array([0.22, 0.24, 0.27])

    # Recessed tinted sliding veranda glass with soft interior curtain tones
    win_r = 0.15 + uval * 0.12 + curtain * 0.04
    win_g = 0.21 + uval * 0.11 + curtain * 0.04
    win_b = 0.28 + uval * 0.08 + curtain * 0.03
    win_rgb = np.stack([win_r, win_g, win_b], axis=-1)
    # Soft gradient darker near ceiling of room
    room_grad = np.clip((fy - 0.16) / 0.62, 0.0, 1.0)[:, :, None]
    win_rgb *= (0.62 + 0.48 * room_grad)
    rgb[in_win] = win_rgb[in_win]
    rgb[sash_v | rail_bar | rail_posts] = np.array([0.18, 0.20, 0.23])

    h = 0.70 + grain * 0.12 - slab_seam * 0.22 - col_seam * 0.10
    h[frame_outer] = 0.55
    h[in_win] = 0.25
    h[sash_v | rail_bar | rail_posts] = 0.58

    rough = np.full((TILE, TILE), 0.78, dtype=np.float32) + (grain - 0.5) * 0.10
    rough[in_win] = 0.08
    rough[frame_outer | sash_v | rail_bar] = 0.36
    ao = np.ones((TILE, TILE), dtype=np.float32) - slab_seam * 0.28
    ao[in_win] = 0.85
    # Pack window mask in albedo alpha channel (alpha < 0.5 => emissive/reflective window pane!)
    win_mask = np.where(in_win & (~sash_v) & (~rail_bar), 0.25, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=5.0)


def gen_glass_curtainwall():
    # Gangnam / Yeouido modern glass skyscraper (6 bays x 4 floors)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 6.0)
    by = y / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    ci = np.floor(bx).astype(np.int32) % 6
    ri = np.floor(by).astype(np.int32) % 4

    rng = np.random.default_rng(401)
    pane_tint = rng.uniform(-0.04, 0.05, size=(4, 6)).astype(np.float32)[ri, ci]
    blinds = rng.uniform(0.0, 1.0, size=(4, 6)).astype(np.float32)[ri, ci]
    n = tileable_fbm(TILE, octaves=4, base_freq=8, seed=402)

    mullion_v = (fx < 0.055) | (fx > 0.945)
    spandrel = (fy < 0.24)  # Solid dark structural spandrel panel between office floors
    transom = (fy >= 0.24) & (fy < 0.29) | (fy > 0.96)
    is_glass = (~mullion_v) & (~spandrel) & (~transom)

    # Deep reflective slate-blue architectural glass + subtle office blinds/ceiling strip
    blind_lines = (0.5 + 0.5 * np.sin(fy * 110.0)) * (blinds > 0.45) * (fy < 0.62)
    ceil_glow = ((fy > 0.31) & (fy < 0.41)).astype(np.float32) * 0.10
    gr = 0.14 + pane_tint + ceil_glow + blind_lines * 0.08 + (n - 0.5) * 0.04
    gg = 0.22 + pane_tint + ceil_glow + blind_lines * 0.08 + (n - 0.5) * 0.04
    gb = 0.31 + pane_tint + ceil_glow * 0.7 + blind_lines * 0.07 + (n - 0.5) * 0.05
    rgb = np.stack([gr, gg, gb], axis=-1)

    # Spandrel dark metallic blue-grey glass & anodized aluminum mullions
    rgb[spandrel] = np.array([0.11, 0.14, 0.19])
    rgb[mullion_v | transom] = np.array([0.22, 0.25, 0.30])

    h = np.full((TILE, TILE), 0.35, dtype=np.float32) + (n - 0.5) * 0.03
    h[spandrel] = 0.52
    h[mullion_v | transom] = 0.85

    rough = np.full((TILE, TILE), 0.05, dtype=np.float32)
    rough[spandrel] = 0.22
    rough[mullion_v | transom] = 0.32
    ao = np.ones((TILE, TILE), dtype=np.float32)
    ao[mullion_v | transom] = 0.88
    win_mask = np.where(is_glass, 0.20, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=4.5)


def gen_granite_office():
    # Honed Korean granite corporate office tower (4 bays x 4 floors)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 4.0)
    by = y / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    # Stone panel ashlar joints (8x8 panels)
    sx = (x / (TILE / 8.0))
    sy = (y / (TILE / 8.0))
    psx = sx - np.floor(sx)
    psy = sy - np.floor(sy)
    stone_joint = (psx < 0.022) | (psx > 0.978) | (psy < 0.022) | (psy > 0.978)

    rng = np.random.default_rng(501)
    slab_var = rng.uniform(-0.045, 0.045, size=(8, 8)).astype(np.float32)[np.floor(sy).astype(np.int32) % 8, np.floor(sx).astype(np.int32) % 8]
    grain = tileable_fbm(TILE, octaves=5, base_freq=20, seed=502)

    in_win = (fx > 0.16) & (fx < 0.84) & (fy > 0.22) & (fy < 0.78)
    frame = (fx > 0.13) & (fx < 0.87) & (fy > 0.19) & (fy < 0.81) & (~in_win)
    mullion = in_win & (np.abs(fx - 0.50) < 0.020)

    r = 0.64 + slab_var + (grain - 0.5) * 0.09
    g = 0.62 + slab_var + (grain - 0.5) * 0.09
    b = 0.59 + slab_var + (grain - 0.5) * 0.08
    rgb = np.stack([r, g, b], axis=-1)
    rgb[stone_joint] *= 0.68
    rgb[frame | mullion] = np.array([0.24, 0.21, 0.19])

    # Deep bronze-grey reflective corporate office glass
    win_shade = np.clip((fy - 0.22) / 0.56, 0.0, 1.0)
    wr = 0.12 + 0.08 * win_shade
    wg = 0.17 + 0.09 * win_shade
    wb = 0.22 + 0.10 * win_shade
    wrgb = np.stack([wr, wg, wb], axis=-1)
    rgb[in_win & (~mullion)] = wrgb[in_win & (~mullion)]

    h = 0.78 + grain * 0.10 - stone_joint.astype(np.float32) * 0.22
    h[frame] = 0.56
    h[in_win] = 0.20
    h[mullion] = 0.52

    rough = np.full((TILE, TILE), 0.54, dtype=np.float32) + (grain - 0.5) * 0.12
    rough[in_win] = 0.07
    rough[frame | mullion] = 0.34
    ao = np.ones((TILE, TILE), dtype=np.float32) - stone_joint.astype(np.float32) * 0.28
    win_mask = np.where(in_win & (~mullion), 0.22, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=5.0)


def gen_brick_commercial():
    # Authentic Seongsu / Hongdae / Gangnam red-terracotta brick commercial facade (4 bays x 4 floors)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    # Brick grid: 16 bricks wide x 48 courses tall
    row = np.floor(y / (TILE / 48.0)).astype(np.int32)
    bx_raw = x / (TILE / 16.0) + (row % 2) * 0.5
    col = np.floor(bx_raw).astype(np.int32) % 16
    bfx = bx_raw - np.floor(bx_raw)
    bfy = (y / (TILE / 48.0)) - row
    mortar = (bfx < 0.07) | (bfx > 0.93) | (bfy < 0.14) | (bfy > 0.86)

    rng = np.random.default_rng(601)
    bvar = rng.uniform(-0.08, 0.08, size=(48, 16)).astype(np.float32)[row % 48, col]
    grain = tileable_fbm(TILE, octaves=5, base_freq=24, seed=602)

    r = np.clip(0.54 + bvar + (grain - 0.5) * 0.11, 0.2, 0.85)
    g = np.clip(0.28 + bvar * 0.6 + (grain - 0.5) * 0.07, 0.1, 0.65)
    b = np.clip(0.21 + bvar * 0.4 + (grain - 0.5) * 0.06, 0.08, 0.55)
    rgb = np.stack([r, g, b], axis=-1)
    rgb[mortar] = np.array([0.48, 0.46, 0.43]) * (0.85 + 0.3 * grain[mortar, None])

    # Recessed black-steel loft windows (4 bays x 4 floors)
    wx = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    wy = (y / (TILE / 4.0)) - np.floor(y / (TILE / 4.0))
    in_win = (wx > 0.18) & (wx < 0.82) & (wy > 0.18) & (wy < 0.80)
    frame = (wx > 0.14) & (wx < 0.86) & (wy > 0.14) & (wy < 0.84) & (~in_win)
    mull = in_win & ((np.abs(wx - 0.50) < 0.022) | (np.abs(wy - 0.48) < 0.022))

    rgb[frame | mull] = np.array([0.13, 0.14, 0.16])
    wshade = np.clip((wy - 0.18) / 0.62, 0.0, 1.0)
    wrgb = np.stack([0.14 + 0.12 * wshade, 0.17 + 0.11 * wshade, 0.22 + 0.09 * wshade], axis=-1)
    rgb[in_win & (~mull)] = wrgb[in_win & (~mull)]

    h = 0.78 + grain * 0.14 - mortar.astype(np.float32) * 0.36
    h[frame] = 0.58
    h[in_win] = 0.18
    h[mull] = 0.55

    rough = np.full((TILE, TILE), 0.84, dtype=np.float32) + (grain - 0.5) * 0.12
    rough[in_win] = 0.08
    rough[frame | mull] = 0.35
    ao = np.ones((TILE, TILE), dtype=np.float32) - mortar.astype(np.float32) * 0.32
    win_mask = np.where(in_win & (~mull), 0.24, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=6.0)


def gen_stucco_commercial():
    # Authentic Korean 근린생활시설 mid-rise ivory/limestone commercial building (4 bays x 4 floors)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    wx = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    wy = (y / (TILE / 4.0)) - np.floor(y / (TILE / 4.0))
    grain = tileable_fbm(TILE, octaves=5, base_freq=16, seed=701)

    # Subtle exterior porcelain/granite tile grid (16x16)
    tx = (x / (TILE / 16.0)) - np.floor(x / (TILE / 16.0))
    ty = (y / (TILE / 16.0)) - np.floor(y / (TILE / 16.0))
    tjoint = (tx < 0.03) | (ty < 0.03)

    in_win = (wx > 0.11) & (wx < 0.89) & (wy > 0.20) & (wy < 0.76)
    frame = (wx > 0.08) & (wx < 0.92) & (wy > 0.17) & (wy < 0.79) & (~in_win)
    mull = in_win & ((np.abs(wx - 0.50) < 0.020) | (np.abs(wx - 0.30) < 0.012) | (np.abs(wx - 0.70) < 0.012))

    r = 0.76 + (grain - 0.5) * 0.08
    g = 0.74 + (grain - 0.5) * 0.08
    b = 0.70 + (grain - 0.5) * 0.08
    rgb = np.stack([r, g, b], axis=-1)
    rgb[tjoint] *= 0.84
    rgb[frame | mull] = np.array([0.20, 0.22, 0.26])

    wshade = np.clip((wy - 0.20) / 0.56, 0.0, 1.0)
    wrgb = np.stack([0.16 + 0.11 * wshade, 0.21 + 0.11 * wshade, 0.26 + 0.10 * wshade], axis=-1)
    rgb[in_win & (~mull)] = wrgb[in_win & (~mull)]

    h = 0.74 + grain * 0.08 - tjoint.astype(np.float32) * 0.15
    h[frame] = 0.52
    h[in_win] = 0.22
    h[mull] = 0.50

    rough = np.full((TILE, TILE), 0.72, dtype=np.float32)
    rough[in_win] = 0.08
    ao = np.ones((TILE, TILE), dtype=np.float32) - tjoint.astype(np.float32) * 0.18
    win_mask = np.where(in_win & (~mull), 0.25, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=4.2)


def gen_storefront_1f():
    # Ground-floor Korean retail glass bays + dark stone pilasters + warm interior cafe/shop lighting
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = y / float(TILE)
    grain = tileable_fbm(TILE, octaves=4, base_freq=16, seed=801)

    pilaster = (fx < 0.09) | (fx > 0.91)
    lintel = fy < 0.16
    kickplate = fy > 0.88
    in_glass = (~pilaster) & (~lintel) & (~kickplate)
    door_mull = in_glass & ((np.abs(fx - 0.50) < 0.022) | (np.abs(fy - 0.28) < 0.014))

    # Warm dark honed basalt pilasters & transom
    rgb = np.stack([0.24 + (grain - 0.5) * 0.06, 0.25 + (grain - 0.5) * 0.06, 0.27 + (grain - 0.5) * 0.06], axis=-1)
    # Warm sunlit / lit Korean shop interior behind glass
    warm_shelf = 0.5 + 0.5 * np.sin(fy * 28.0) * 0.35
    gr = 0.34 + 0.22 * (1.0 - fy) * warm_shelf
    gg = 0.30 + 0.18 * (1.0 - fy) * warm_shelf
    gb = 0.24 + 0.12 * (1.0 - fy)
    rgb[in_glass] = np.stack([gr, gg, gb], axis=-1)[in_glass]
    rgb[door_mull] = np.array([0.14, 0.15, 0.17])

    h = np.full((TILE, TILE), 0.80, dtype=np.float32) + grain * 0.08
    h[in_glass] = 0.22
    h[door_mull] = 0.56
    rough = np.full((TILE, TILE), 0.48, dtype=np.float32)
    rough[in_glass] = 0.06
    ao = np.ones((TILE, TILE), dtype=np.float32)
    win_mask = np.where(in_glass & (~door_mull), 0.28, 1.0).astype(np.float32)
    return make_rgba(rgb, win_mask), make_nr(h, rough, ao, strength=4.5)


def gen_park_grass():
    # Rich multi-scale organic Seoul park grass (NO MORE flat solid green!)
    n_macro = tileable_fbm(TILE, octaves=5, base_freq=6, seed=901)
    n_mid = tileable_fbm(TILE, octaves=5, base_freq=24, seed=902)
    n_blades = tileable_fbm(TILE, octaves=4, base_freq=96, seed=903)
    # Directional fine grass-blade streaks
    rng = np.random.default_rng(904)
    blade = rng.normal(0.5, 0.18, size=(TILE, TILE)).astype(np.float32)
    blade = (blade + np.roll(blade, 1, axis=0) + np.roll(blade, -1, axis=0)) / 3.0

    r = np.clip(0.16 + n_macro * 0.12 + n_mid * 0.06 + (blade - 0.5) * 0.10, 0.08, 0.42)
    g = np.clip(0.32 + n_macro * 0.16 + n_blades * 0.10 + (blade - 0.5) * 0.14, 0.16, 0.62)
    b = np.clip(0.11 + n_macro * 0.06 + n_mid * 0.04 + (blade - 0.5) * 0.06, 0.06, 0.28)
    rgb = np.stack([r, g, b], axis=-1)
    h = n_mid * 0.40 + n_blades * 0.35 + blade * 0.25
    rough = np.clip(0.88 + (n_macro - 0.5) * 0.12, 0.72, 0.98)
    ao = np.clip(0.75 + n_blades * 0.25, 0.55, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.5)


def gen_mountain_forest():
    n1 = tileable_fbm(TILE, octaves=6, base_freq=6, seed=1001)
    n2 = tileable_fbm(TILE, octaves=5, base_freq=24, seed=1002)
    n3 = tileable_fbm(TILE, octaves=3, base_freq=64, seed=1003)
    # Dense Korean mountain pine canopy + subtle exposed granite patches
    rock_mask = np.clip((n1 - 0.66) * 5.0, 0.0, 1.0)
    fr = 0.13 + n2 * 0.11 + n3 * 0.05
    fg = 0.26 + n2 * 0.15 + n3 * 0.07
    fb = 0.12 + n2 * 0.07 + n3 * 0.03
    rr = 0.48 + n2 * 0.14
    rg = 0.47 + n2 * 0.13
    rb = 0.45 + n2 * 0.12
    rgb = np.stack([
        fr * (1 - rock_mask) + rr * rock_mask,
        fg * (1 - rock_mask) + rg * rock_mask,
        fb * (1 - rock_mask) + rb * rock_mask,
    ], axis=-1)
    h = n1 * 0.35 + n2 * 0.40 + n3 * 0.25
    rough = np.full((TILE, TILE), 0.90, dtype=np.float32)
    ao = np.clip(0.68 + n2 * 0.32, 0.5, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=7.0)


def gen_rooftop_concrete():
    n1 = tileable_fbm(TILE, octaves=5, base_freq=8, seed=1101)
    n2 = tileable_fbm(TILE, octaves=4, base_freq=32, seed=1102)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    gx = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    gy = (y / (TILE / 4.0)) - np.floor(y / (TILE / 4.0))
    exp_joint = (gx < 0.015) | (gy < 0.015)
    lum = np.clip(0.47 + (n1 - 0.5) * 0.12 + (n2 - 0.5) * 0.08 - exp_joint * 0.14, 0.2, 0.72)
    rgb = np.stack([lum * 0.98, lum * 1.00, lum * 1.02], axis=-1)
    h = n1 * 0.4 + n2 * 0.6 - exp_joint * 0.25
    rough = np.clip(0.84 + (n1 - 0.5) * 0.14, 0.62, 0.96)
    ao = np.ones((TILE, TILE), dtype=np.float32) - exp_joint * 0.30
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.0)


def gen_hanok_giwa():
    # Traditional Korean dark ceramic curved Giwa roof tiles (16 ribs x 12 overlapping courses)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    rib_phase = (x / (TILE / 16.0)) * math.tau
    rib = 0.5 + 0.5 * np.cos(rib_phase)
    course = (y / (TILE / 12.0)) - np.floor(y / (TILE / 12.0))
    lip = (course < 0.14).astype(np.float32)
    grain = tileable_fbm(TILE, octaves=4, base_freq=16, seed=1201)
    lum = np.clip(0.17 + rib * 0.11 - lip * 0.06 + (grain - 0.5) * 0.04, 0.06, 0.36)
    rgb = np.stack([lum * 0.96, lum * 1.00, lum * 1.08], axis=-1)
    h = rib * 0.72 + (1.0 - course) * 0.22 - lip * 0.18
    rough = np.clip(0.56 - rib * 0.10 + (grain - 0.5) * 0.08, 0.35, 0.78)
    ao = np.clip(0.65 + rib * 0.35 - lip * 0.25, 0.35, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=7.5)


def gen_bridge_concrete():
    n1 = tileable_fbm(TILE, octaves=5, base_freq=8, seed=1301)
    n2 = tileable_fbm(TILE, octaves=4, base_freq=32, seed=1302)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    panel_x = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    panel_y = (y / (TILE / 8.0)) - np.floor(y / (TILE / 8.0))
    seam = (panel_x < 0.014) | (panel_y < 0.018)
    lum = np.clip(0.58 + (n1 - 0.5) * 0.12 + (n2 - 0.5) * 0.06 - seam * 0.14, 0.25, 0.80)
    rgb = np.stack([lum * 0.99, lum * 1.00, lum * 0.99], axis=-1)
    h = n1 * 0.35 + n2 * 0.45 - seam * 0.28
    rough = np.clip(0.78 + (n1 - 0.5) * 0.12, 0.55, 0.95)
    ao = np.ones((TILE, TILE), dtype=np.float32) - seam * 0.32
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.5)


def gen_tree_bark():
    # Deep vertically fissured Korean Ginkgo / Pine bark
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    n1 = tileable_fbm(TILE, octaves=5, base_freq=8, seed=1401)
    # Stretch horizontally to create vertical bark ridges
    ridge = 0.5 + 0.5 * np.sin((x * 22.0 + n1 * 2.6) * math.tau)
    fine = tileable_fbm(TILE, octaves=4, base_freq=32, seed=1402)
    r = np.clip(0.24 + ridge * 0.14 + (fine - 0.5) * 0.08, 0.10, 0.50)
    g = np.clip(0.18 + ridge * 0.11 + (fine - 0.5) * 0.06, 0.08, 0.40)
    b = np.clip(0.13 + ridge * 0.08 + (fine - 0.5) * 0.05, 0.05, 0.30)
    rgb = np.stack([r, g, b], axis=-1)
    h = ridge * 0.70 + fine * 0.30
    rough = np.full((TILE, TILE), 0.92, dtype=np.float32)
    ao = np.clip(0.55 + ridge * 0.45, 0.40, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=8.0)


def gen_foliage_leaves():
    # Layered organic multi-leaf foliage canopy texture (neutral crisp tint so vertex color drives Ginkgo/Cherry/Pine!)
    n1 = tileable_fbm(TILE, octaves=5, base_freq=12, seed=1501)
    n2 = tileable_fbm(TILE, octaves=4, base_freq=32, seed=1502)
    n3 = tileable_fbm(TILE, octaves=3, base_freq=64, seed=1503)
    # Distinct leaf clusters with dappled shadow pockets between leaves!
    leaf = np.clip(0.45 + n2 * 0.45 + (n3 - 0.5) * 0.35, 0.20, 1.0)
    dapple = np.clip(0.55 + (n1 - 0.5) * 0.60, 0.35, 1.0)
    lum = np.clip(leaf * dapple * 1.15, 0.25, 1.0)
    rgb = np.stack([lum * 0.94, lum * 1.00, lum * 0.88], axis=-1)
    h = n2 * 0.55 + n3 * 0.45
    rough = np.clip(0.76 - n3 * 0.18, 0.48, 0.92)
    ao = np.clip(0.45 + leaf * 0.55, 0.35, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=7.5)


def gen_tactile_yellow():
    # Korean yellow braille tactile paving (점자블록) — crisp raised dots/bars!
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    gx = (x / (TILE / 12.0)) - np.floor(x / (TILE / 12.0)) - 0.5
    gy = (y / (TILE / 12.0)) - np.floor(y / (TILE / 12.0)) - 0.5
    dist = np.sqrt(gx * gx + gy * gy)
    dome = np.clip(1.0 - dist / 0.28, 0.0, 1.0)
    dome = dome * dome * (3.0 - 2.0 * dome)
    grain = tileable_fbm(TILE, octaves=4, base_freq=24, seed=1601)

    r = np.clip(0.88 + dome * 0.08 + (grain - 0.5) * 0.05, 0.5, 1.0)
    g = np.clip(0.70 + dome * 0.08 + (grain - 0.5) * 0.05, 0.4, 0.95)
    b = np.clip(0.11 + dome * 0.04 + (grain - 0.5) * 0.03, 0.05, 0.30)
    rgb = np.stack([r, g, b], axis=-1)
    h = 0.25 + dome * 0.70 + grain * 0.05
    rough = np.clip(0.62 - dome * 0.12, 0.42, 0.78)
    ao = np.clip(0.82 + dome * 0.18, 0.65, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=7.0)


def main():
    os.makedirs("assets/textures", exist_ok=True)
    gens = [
        gen_asphalt, gen_sidewalk_pavers, gen_korean_apartment, gen_glass_curtainwall,
        gen_granite_office, gen_brick_commercial, gen_stucco_commercial, gen_storefront_1f,
        gen_park_grass, gen_mountain_forest, gen_rooftop_concrete, gen_hanok_giwa,
        gen_bridge_concrete, gen_tree_bark, gen_foliage_leaves, gen_tactile_yellow,
    ]
    albedo = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    norm_r = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    for idx, fn in enumerate(gens):
        col = idx % GRID
        row = idx // GRID
        a_tile, nr_tile = fn()
        albedo[row * TILE:(row + 1) * TILE, col * TILE:(col + 1) * TILE] = a_tile
        norm_r[row * TILE:(row + 1) * TILE, col * TILE:(col + 1) * TILE] = nr_tile
        print(f"  Baked PBR tile {idx:2d}: {fn.__name__}")

    a_path = "assets/textures/pbr_albedo.png"
    n_path = "assets/textures/pbr_normal_rough.png"
    Image.fromarray(albedo, mode="RGBA").save(a_path, optimize=True)
    Image.fromarray(norm_r, mode="RGBA").save(n_path, optimize=True)
    print(f"Saved {a_path} ({os.path.getsize(a_path) / 1024:.0f} KB)")
    print(f"Saved {n_path} ({os.path.getsize(n_path) / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
