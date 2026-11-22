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

TILE = 1024
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
    if a_f is not None:
        out[:, :, 3] = np.clip(a_f * 255.0, 0, 255).astype(np.uint8)
    else:
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
    n2 = tileable_fbm(TILE, octaves=5, base_freq=32, seed=102)
    fine = tileable_fbm(TILE, octaves=3, base_freq=128, seed=103)
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    crack_field = np.abs(tileable_fbm(TILE, octaves=4, base_freq=6, seed=104) - 0.5)
    crack = np.clip(1.0 - crack_field * 58.0, 0.0, 1.0)
    wear = 0.5 + 0.5 * np.sin(x * math.pi * 4.0)
    base = 0.165 + (n1 - 0.5) * 0.055 + (n2 - 0.5) * 0.045 + (fine - 0.5) * 0.065
    fleck = (fine > 0.78).astype(np.float32) * 0.06
    lum = np.clip(base + fleck - crack * 0.075 - wear * 0.015, 0.05, 0.34)
    rgb = np.stack([lum * 0.98, lum * 0.99, lum * 1.04], axis=-1)
    h = n2 * 0.45 + fine * 0.55 - crack * 0.35
    rough = np.clip(0.86 - wear * 0.14 - crack * 0.25 + (n1 - 0.5) * 0.10, 0.45, 0.96)
    ao = np.clip(1.0 - crack * 0.35 - (1.0 - fine) * 0.12, 0.4, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.5)


def gen_sidewalk_pavers():
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    gx = (x / (TILE / 8.0))
    gy = (y / (TILE / 8.0))
    iy = np.floor(gy).astype(np.int32)
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % 8
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)

    seam_x = np.minimum(fx, 1.0 - fx)
    seam_y = np.minimum(fy, 1.0 - fy)
    seam_d = np.minimum(seam_x, seam_y)
    grout = np.clip((0.050 - seam_d) / 0.030, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.035) / 0.055, 0.0, 1.0)

    rng = np.random.default_rng(201)
    paver_var = rng.uniform(-0.055, 0.055, size=(8, 8)).astype(np.float32)
    paver_warm = rng.uniform(-0.025, 0.035, size=(8, 8)).astype(np.float32)
    pv = paver_var[iy % 8, ix_s]
    pw = paver_warm[iy % 8, ix_s]
    grain = tileable_fbm(TILE, octaves=5, base_freq=24, seed=202)

    r = 0.57 + pv + pw + (grain - 0.5) * 0.09
    g = 0.55 + pv + pw * 0.4 + (grain - 0.5) * 0.08
    b = 0.53 + pv - pw * 0.5 + (grain - 0.5) * 0.08
    rgb = np.stack([r, g, b], axis=-1) * (1.0 - grout[:, :, None] * 0.48)
    h = bevel * 0.55 + grain * 0.35 - grout * 0.50
    rough = np.clip(0.74 + grout * 0.20 + (grain - 0.5) * 0.12, 0.52, 0.96)
    ao = np.clip(1.0 - grout * 0.50, 0.4, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.0)


def gen_korean_apartment():
    """Ultra-crisp 1024px Korean Luxury Apartment (래미안/자이/아크로):
    4 bays x 4 floors, precast slab seams, 3-pane sliding veranda windows, glass railings, AC louver niche!"""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = (x / (TILE / 4.0))
    by = (y / (TILE / 4.0))
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    ci = np.floor(bx).astype(np.int32) % 4
    ri = np.floor(by).astype(np.int32) % 4

    grain = tileable_fbm(TILE, octaves=5, base_freq=16, seed=301)
    slab_seam = ((fy < 0.042) | (fy > 0.972)).astype(np.float32)
    slab_ledge = ((fy >= 0.042) & (fy < 0.085)).astype(np.float32)
    col_seam = ((fx < 0.022) | (fx > 0.978)).astype(np.float32)

    # Wide Korean sliding veranda window (fx in [0.08..0.80]) + side AC louver vent bay ([0.84..0.94])!
    in_win = (fx > 0.08) & (fx < 0.80) & (fy > 0.15) & (fy < 0.78)
    frame_outer = (fx > 0.062) & (fx < 0.818) & (fy > 0.125) & (fy < 0.805) & (~in_win)
    # Recessed AC outdoor-unit louver vent bay (실외기 루버) typical of every modern Korean apartment!
    in_louver = (fx > 0.845) & (fx < 0.945) & (fy > 0.22) & (fy < 0.74)
    louver_slat = in_louver & ((np.sin(fy * 260.0) > 0.0))

    sash_v = in_win & ((np.abs(fx - 0.44) < 0.015) | (np.abs(fx - 0.26) < 0.010) | (np.abs(fx - 0.62) < 0.010))
    rail_bar = in_win & (np.abs(fy - 0.54) < 0.016)
    rail_glass = in_win & (fy > 0.556) & (~sash_v)
    rail_posts = in_win & (fy > 0.54) & ((np.abs(fx - 0.17) < 0.008) | (np.abs(fx - 0.44) < 0.008) | (np.abs(fx - 0.71) < 0.008))

    rng = np.random.default_rng(302)
    unit_lit = rng.uniform(0.0, 1.0, size=(4, 4)).astype(np.float32)
    uval = unit_lit[ri, ci]
    curtain = np.clip(0.5 + 0.5 * np.sin(fx * 68.0 + uval * 10.0), 0.0, 1.0)

    wall_r = 0.80 + (grain - 0.5) * 0.07 - slab_seam * 0.16 + slab_ledge * 0.05 - col_seam * 0.08
    wall_g = 0.78 + (grain - 0.5) * 0.07 - slab_seam * 0.16 + slab_ledge * 0.05 - col_seam * 0.08
    wall_b = 0.75 + (grain - 0.5) * 0.06 - slab_seam * 0.15 + slab_ledge * 0.04 - col_seam * 0.07
    rgb = np.stack([wall_r, wall_g, wall_b], axis=-1)

    rgb[frame_outer] = np.array([0.20, 0.22, 0.25])
    rgb[in_louver] = np.array([0.28, 0.30, 0.33])
    rgb[louver_slat] = np.array([0.46, 0.48, 0.52])

    win_r = 0.11 + uval * 0.07 + curtain * 0.03
    win_g = 0.16 + uval * 0.07 + curtain * 0.03
    win_b = 0.23 + uval * 0.06 + curtain * 0.02
    win_rgb = np.stack([win_r, win_g, win_b], axis=-1)
    room_grad = np.clip((fy - 0.15) / 0.63, 0.0, 1.0)[:, :, None]
    win_rgb *= (0.58 + 0.48 * room_grad)
    rgb[in_win] = win_rgb[in_win]
    # Subtle frosted-tint lower glass safety railing
    rgb[rail_glass] = rgb[rail_glass] * 0.78 + np.array([0.08, 0.11, 0.14])
    rgb[sash_v | rail_bar | rail_posts] = np.array([0.16, 0.18, 0.21])

    h = 0.72 + grain * 0.10 - slab_seam * 0.25 + slab_ledge * 0.12 - col_seam * 0.10
    h[frame_outer] = 0.54
    h[in_louver] = 0.32
    h[louver_slat] = 0.60
    h[in_win] = 0.20
    h[sash_v | rail_bar | rail_posts] = 0.62

    rough = np.full((TILE, TILE), 0.76, dtype=np.float32) + (grain - 0.5) * 0.10
    rough[in_win] = 0.06
    rough[frame_outer | sash_v | rail_bar | in_louver] = 0.34
    ao = np.ones((TILE, TILE), dtype=np.float32) - slab_seam * 0.32
    ao[in_win] = np.where((fy < 0.22)[in_win], 0.62, 0.88)
    ao[in_louver] = 0.60
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.8)


def gen_glass_curtainwall():
    """Ultra-crisp 1024px Gangnam/Yeouido Glass Curtain-Wall Skyscraper (6 bays x 4 floors):
    projecting anodized aluminum fins, structural spandrel glass, realistic office blinds & acoustic ceiling!"""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 6.0)
    by = y / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    ci = np.floor(bx).astype(np.int32) % 6
    ri = np.floor(by).astype(np.int32) % 4

    rng = np.random.default_rng(401)
    pane_tint = rng.uniform(-0.035, 0.045, size=(4, 6)).astype(np.float32)[ri, ci]
    blinds = rng.uniform(0.0, 1.0, size=(4, 6)).astype(np.float32)[ri, ci]
    blind_drop = rng.uniform(0.28, 0.72, size=(4, 6)).astype(np.float32)[ri, ci]
    n = tileable_fbm(TILE, octaves=4, base_freq=8, seed=402)

    mullion_fin = (fx < 0.048) | (fx > 0.952)
    mullion_sub = (np.abs(fx - 0.50) < 0.012)
    spandrel = (fy < 0.25)  # Solid dark structural spandrel band between office floors
    spandrel_groove = (np.abs(fy - 0.125) < 0.012)
    transom = ((fy >= 0.25) & (fy < 0.295)) | (fy > 0.965)
    is_glass = (~mullion_fin) & (~mullion_sub) & (~spandrel) & (~transom)

    # Venetian office blinds drawn to varying heights per bay + subtle ceiling fluorescent bay
    has_blind = (blinds > 0.42) & (fy > 0.30) & (fy < blind_drop)
    blind_slat = (0.5 + 0.5 * np.sin(fy * 190.0)) * has_blind.astype(np.float32)
    ceil_strip = ((fy > 0.31) & (fy < 0.38) & (fx > 0.16) & (fx < 0.84)).astype(np.float32) * 0.07

    gr = 0.10 + pane_tint + ceil_strip + blind_slat * 0.11 + (n - 0.5) * 0.03
    gg = 0.17 + pane_tint + ceil_strip + blind_slat * 0.11 + (n - 0.5) * 0.03
    gb = 0.25 + pane_tint + ceil_strip * 0.7 + blind_slat * 0.10 + (n - 0.5) * 0.04
    rgb = np.stack([gr, gg, gb], axis=-1)

    rgb[spandrel] = np.array([0.09, 0.12, 0.17])
    rgb[spandrel_groove] = np.array([0.05, 0.07, 0.10])
    rgb[mullion_fin | transom] = np.array([0.24, 0.27, 0.32])
    rgb[mullion_sub] = np.array([0.16, 0.19, 0.24])

    h = np.full((TILE, TILE), 0.30, dtype=np.float32) + (n - 0.5) * 0.025
    h[spandrel] = 0.50
    h[spandrel_groove] = 0.28
    h[mullion_sub] = 0.65
    h[mullion_fin | transom] = 0.94

    rough = np.full((TILE, TILE), 0.045, dtype=np.float32)
    rough[spandrel] = 0.20
    rough[mullion_fin | transom | mullion_sub] = 0.28
    ao = np.ones((TILE, TILE), dtype=np.float32)
    ao[mullion_fin | transom] = 0.86
    ao[is_glass & (fy < 0.34)] = 0.76
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.2)


def gen_granite_office():
    """Ultra-crisp 1024px Honed Korean Pocheon Granite Corporate Office (4 bays x 4 floors):
    beveled ashlar stone panels, deep recessed window reveals, projecting stone sills & bronze mullions!"""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 4.0)
    by = y / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = by - np.floor(by)
    ci = np.floor(bx).astype(np.int32) % 4
    ri = np.floor(by).astype(np.int32) % 4

    sx = (x / (TILE / 8.0))
    sy = (y / (TILE / 8.0))
    psx = sx - np.floor(sx)
    psy = sy - np.floor(sy)
    stone_joint = (psx < 0.018) | (psx > 0.982) | (psy < 0.018) | (psy > 0.982)

    rng = np.random.default_rng(501)
    slab_var = rng.uniform(-0.048, 0.048, size=(8, 8)).astype(np.float32)[np.floor(sy).astype(np.int32) % 8, np.floor(sx).astype(np.int32) % 8]
    blinds = rng.uniform(0.0, 1.0, size=(4, 4)).astype(np.float32)[ri, ci]
    grain = tileable_fbm(TILE, octaves=5, base_freq=28, seed=502)

    in_win = (fx > 0.15) & (fx < 0.85) & (fy > 0.21) & (fy < 0.77)
    sill = (fx > 0.12) & (fx < 0.88) & (fy >= 0.77) & (fy < 0.83)
    lintel = (fx > 0.13) & (fx < 0.87) & (fy > 0.17) & (fy <= 0.21)
    frame = (fx > 0.125) & (fx < 0.875) & (fy > 0.185) & (fy < 0.795) & (~in_win) & (~sill) & (~lintel)
    mullion = in_win & ((np.abs(fx - 0.50) < 0.018) | (np.abs(fy - 0.36) < 0.014))

    r = 0.66 + slab_var + (grain - 0.5) * 0.08
    g = 0.64 + slab_var + (grain - 0.5) * 0.08
    b = 0.61 + slab_var + (grain - 0.5) * 0.07
    rgb = np.stack([r, g, b], axis=-1)
    rgb[stone_joint] *= 0.64
    rgb[sill] = rgb[sill] * 1.10
    rgb[lintel] = rgb[lintel] * 0.82
    rgb[frame | mullion] = np.array([0.21, 0.19, 0.17])

    win_shade = np.clip((fy - 0.21) / 0.56, 0.0, 1.0)
    b_slat = (0.5 + 0.5 * np.sin(fy * 170.0)) * (blinds > 0.50) * (fy < 0.52)
    wr = 0.10 + 0.07 * win_shade + b_slat * 0.09
    wg = 0.14 + 0.08 * win_shade + b_slat * 0.09
    wb = 0.20 + 0.09 * win_shade + b_slat * 0.08
    wrgb = np.stack([wr, wg, wb], axis=-1)
    rgb[in_win & (~mullion)] = wrgb[in_win & (~mullion)]

    h = 0.78 + grain * 0.10 - stone_joint.astype(np.float32) * 0.26
    h[sill] = 0.94
    h[lintel] = 0.64
    h[frame] = 0.54
    h[in_win] = 0.16
    h[mullion] = 0.52

    rough = np.full((TILE, TILE), 0.52, dtype=np.float32) + (grain - 0.5) * 0.12
    rough[in_win] = 0.055
    rough[frame | mullion] = 0.32
    ao = np.ones((TILE, TILE), dtype=np.float32) - stone_joint.astype(np.float32) * 0.30
    ao[in_win & (fy < 0.29)] = 0.65
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.8)


def gen_brick_commercial():
    """Ultra-crisp 1024px Seongsu/Hongdae Red-Terracotta Brick Commercial & Loft (4 bays x 4 floors)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    row = np.floor(y / (TILE / 56.0)).astype(np.int32)
    bx_raw = x / (TILE / 18.0) + (row % 2) * 0.5
    col = np.floor(bx_raw).astype(np.int32) % 18
    bfx = bx_raw - np.floor(bx_raw)
    bfy = (y / (TILE / 56.0)) - row
    mortar = (bfx < 0.065) | (bfx > 0.935) | (bfy < 0.13) | (bfy > 0.87)

    rng = np.random.default_rng(601)
    bvar = rng.uniform(-0.085, 0.085, size=(56, 18)).astype(np.float32)[row % 56, col]
    grain = tileable_fbm(TILE, octaves=5, base_freq=32, seed=602)

    r = np.clip(0.56 + bvar + (grain - 0.5) * 0.11, 0.2, 0.86)
    g = np.clip(0.30 + bvar * 0.6 + (grain - 0.5) * 0.07, 0.1, 0.65)
    b = np.clip(0.22 + bvar * 0.4 + (grain - 0.5) * 0.06, 0.08, 0.55)
    rgb = np.stack([r, g, b], axis=-1)
    rgb[mortar] = np.array([0.48, 0.46, 0.43]) * (0.85 + 0.3 * grain[mortar, None])

    wx = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    wy = (y / (TILE / 4.0)) - np.floor(y / (TILE / 4.0))
    in_win = (wx > 0.18) & (wx < 0.82) & (wy > 0.18) & (wy < 0.78)
    sill = (wx > 0.14) & (wx < 0.86) & (wy >= 0.78) & (wy < 0.84)
    lintel = (wx > 0.14) & (wx < 0.86) & (wy > 0.12) & (wy <= 0.18)
    frame = (wx > 0.15) & (wx < 0.85) & (wy > 0.15) & (wy < 0.81) & (~in_win) & (~sill) & (~lintel)
    mull = in_win & ((np.abs(wx - 0.50) < 0.018) | (np.abs(wy - 0.46) < 0.018))

    rgb[sill | lintel] = np.array([0.58, 0.56, 0.53]) * (0.90 + 0.2 * grain[sill | lintel, None])
    rgb[frame | mull] = np.array([0.12, 0.13, 0.15])
    wshade = np.clip((wy - 0.18) / 0.60, 0.0, 1.0)
    wrgb = np.stack([0.11 + 0.09 * wshade, 0.14 + 0.09 * wshade, 0.19 + 0.08 * wshade], axis=-1)
    rgb[in_win & (~mull)] = wrgb[in_win & (~mull)]

    h = 0.78 + grain * 0.14 - mortar.astype(np.float32) * 0.38
    h[sill] = 0.94
    h[lintel] = 0.86
    h[frame] = 0.58
    h[in_win] = 0.15
    h[mull] = 0.54

    rough = np.full((TILE, TILE), 0.84, dtype=np.float32) + (grain - 0.5) * 0.12
    rough[in_win] = 0.065
    rough[frame | mull] = 0.34
    ao = np.ones((TILE, TILE), dtype=np.float32) - mortar.astype(np.float32) * 0.34
    ao[in_win & (wy < 0.26)] = 0.62
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.5)


def gen_stucco_commercial():
    """Ultra-crisp 1024px Korean 근린생활시설 Mid-Rise Limestone-Tile/Stucco Commercial (4 bays x 4 floors)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    wx = (x / (TILE / 4.0)) - np.floor(x / (TILE / 4.0))
    wy = (y / (TILE / 4.0)) - np.floor(y / (TILE / 4.0))
    grain = tileable_fbm(TILE, octaves=5, base_freq=20, seed=701)

    tx = (x / (TILE / 16.0)) - np.floor(x / (TILE / 16.0))
    ty = (y / (TILE / 16.0)) - np.floor(y / (TILE / 16.0))
    tjoint = (tx < 0.024) | (ty < 0.024)
    floor_band = (wy < 0.045) | (wy > 0.965)

    in_win = (wx > 0.09) & (wx < 0.91) & (wy > 0.19) & (wy < 0.75)
    sill = (wx > 0.07) & (wx < 0.93) & (wy >= 0.75) & (wy < 0.80)
    frame = (wx > 0.068) & (wx < 0.932) & (wy > 0.165) & (wy < 0.775) & (~in_win) & (~sill)
    mull = in_win & ((np.abs(wx - 0.50) < 0.017) | (np.abs(wx - 0.28) < 0.010) | (np.abs(wx - 0.72) < 0.010))

    r = 0.77 + (grain - 0.5) * 0.07
    g = 0.75 + (grain - 0.5) * 0.07
    b = 0.71 + (grain - 0.5) * 0.07
    rgb = np.stack([r, g, b], axis=-1)
    rgb[tjoint] *= 0.83
    rgb[floor_band] *= 0.86
    rgb[sill] = np.array([0.46, 0.48, 0.52])
    rgb[frame | mull] = np.array([0.19, 0.21, 0.25])

    wshade = np.clip((wy - 0.19) / 0.56, 0.0, 1.0)
    wrgb = np.stack([0.12 + 0.08 * wshade, 0.17 + 0.09 * wshade, 0.23 + 0.09 * wshade], axis=-1)
    rgb[in_win & (~mull)] = wrgb[in_win & (~mull)]

    h = 0.74 + grain * 0.08 - tjoint.astype(np.float32) * 0.16 - floor_band.astype(np.float32) * 0.14
    h[sill] = 0.88
    h[frame] = 0.52
    h[in_win] = 0.18
    h[mull] = 0.50

    rough = np.full((TILE, TILE), 0.70, dtype=np.float32)
    rough[in_win] = 0.06
    rough[sill | frame | mull] = 0.32
    ao = np.ones((TILE, TILE), dtype=np.float32) - tjoint.astype(np.float32) * 0.18
    ao[in_win & (wy < 0.26)] = 0.66
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.8)


def gen_storefront_1f():
    """Ultra-crisp 1024px Ground-Floor Korean Retail Glass Bays & Corporate Lobby:
    honed basalt pilasters, sleek upper sign fascia, tempered glass bays, recessed doors & warm interiors!"""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    bx = x / (TILE / 4.0)
    fx = bx - np.floor(bx)
    fy = y / float(TILE)
    grain = tileable_fbm(TILE, octaves=4, base_freq=20, seed=801)

    pilaster = (fx < 0.085) | (fx > 0.915)
    lintel = fy < 0.17
    fascia_trim = (fy >= 0.15) & (fy < 0.18)
    kickplate = fy > 0.89
    in_glass = (~pilaster) & (~lintel) & (~kickplate)
    door_mull = in_glass & ((np.abs(fx - 0.50) < 0.018) | (np.abs(fy - 0.30) < 0.012) | (np.abs(fx - 0.25) < 0.008) | (np.abs(fx - 0.75) < 0.008))
    door_handle = in_glass & ((np.abs(fx - 0.46) < 0.010) | (np.abs(fx - 0.54) < 0.010)) & (fy > 0.54) & (fy < 0.70)

    rgb = np.stack([0.21 + (grain - 0.5) * 0.05, 0.22 + (grain - 0.5) * 0.05, 0.24 + (grain - 0.5) * 0.05], axis=-1)
    rgb[fascia_trim] = np.array([0.36, 0.39, 0.44])
    warm_shelf = 0.5 + 0.5 * np.sin(fy * 32.0) * 0.35
    gr = 0.26 + 0.18 * (1.0 - fy) * warm_shelf
    gg = 0.24 + 0.15 * (1.0 - fy) * warm_shelf
    gb = 0.20 + 0.11 * (1.0 - fy)
    rgb[in_glass] = np.stack([gr, gg, gb], axis=-1)[in_glass]
    rgb[door_mull] = np.array([0.13, 0.14, 0.16])
    rgb[door_handle] = np.array([0.74, 0.76, 0.80])

    h = np.full((TILE, TILE), 0.82, dtype=np.float32) + grain * 0.07
    h[fascia_trim] = 0.94
    h[in_glass] = 0.18
    h[door_mull] = 0.56
    h[door_handle] = 0.76
    rough = np.full((TILE, TILE), 0.44, dtype=np.float32)
    rough[in_glass] = 0.05
    rough[door_handle | fascia_trim] = 0.22
    ao = np.ones((TILE, TILE), dtype=np.float32)
    ao[in_glass & (fy < 0.24)] = 0.65
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.0)


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
