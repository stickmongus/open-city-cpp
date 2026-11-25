#!/usr/bin/env python3
"""
Generate dedicated 4096x4096 (4x4 grid of 1024x1024 seamless tiles) Seoul Street & Road PBR Atlases
for Open City Seoul v5.0.0 Native Vulkan renderer:
  - assets/textures/street_albedo.png        (RGBA8 sRGB Albedo)
  - assets/textures/street_normal_rough.png  (RGBA8 Linear: R=NormalX, G=NormalY, B=Roughness, A=AO)

Tile layout (col = id % 4, row = id // 4, 1024x1024 each):
   0: Urban Seoul Fine-Aggregate Asphalt (crushed granite/basalt aggregate, pores, realistic micro-wear)
   1: Korean Expressway Dark SMA Asphalt (smooth high-speed stone-mastic highway pavement)
   2: Intersection Worn Asphalt (polished multi-directional tire wear & subtle aggregate patina)
   3: Korean Interlocking Sidewalk Granite Pavers (beveled warm-grey & ochre Seoul sidewalk bricks)
   4: Seoul Boulevard Large Granite Flagstones (honed Pocheon speckled granite plaza slabs)
   5: Korean Yellow Tactile Braille Paving (high-relief guide ribs + warning blister dots)
   6: Korean Dressed Granite Curbstone (1m mortar-jointed Pocheon granite curb blocks)
   7: Seoul Cast-Iron Manhole Cover & Stormwater Grate (concentric tread + slotted curb drain)
   8: Concrete Bridge Deck & Steel Finger Expansion Joint (broom-grooved deck + steel joint)
   9: Poured Formwork Bridge Pier & Retaining Wall Concrete (tie holes, formwork seams, rain streaks)
  10: Expressway New Jersey Concrete Barrier (precast concrete + base tire scuff + chevron band)
  11: Acoustic Expressway Sound Barrier (anodized aluminum frame + translucent sound panels)
  12: Seoul Cast-Iron Street Tree Grate & Granite Planter Curb
  13: Korean Terracotta Non-Slip Bike Lane / School-Zone Aggregate Pavement
  14: Seoul Residential Brick / Cobble Alleyway Pavement (Seongsu / Itaewon / Bukchon)
  15: Han River Dressed Granite Embankment & Waterfront Promenade Concrete
"""

import math
import os
import time
import numpy as np
from PIL import Image

TILE = 1024
GRID = 4
SIZE = TILE * GRID


def tileable_fbm(n: int, octaves=5, base_freq=4, seed=1):
    rng = np.random.default_rng(seed)
    acc = np.zeros((n, n), dtype=np.float32)
    amp = 0.5
    tot = 0.0
    for _ in range(octaves):
        freq = base_freq
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
        acc += ((v00 * (1 - sx) + v10 * sx) * (1 - sy) + (v01 * (1 - sx) + v11 * sx) * sy) * amp
        tot += amp
        amp *= 0.5
        base_freq *= 2
    return acc / max(tot, 1e-5)


def high_freq_grain(n: int, seed: int):
    rng = np.random.default_rng(seed)
    g = rng.random((n, n), dtype=np.float32)
    # 3x3 gentle blur blend to mimic natural crushed mineral grain without harsh single-pixel aliasing
    g_sm = (g * 4.0 + np.roll(g, 1, 0) + np.roll(g, -1, 0) + np.roll(g, 1, 1) + np.roll(g, -1, 1)) / 8.0
    return g_sm


def height_to_normal_xy(h: np.ndarray, strength: float = 3.5):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    nx = -dx
    ny = -dy
    nz = np.ones_like(h)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    r = np.clip((nx * inv_len * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    g = np.clip((ny * inv_len * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    return r, g


def make_rgba(rgb_f, a_f=None):
    out = np.empty((TILE, TILE, 4), dtype=np.uint8)
    out[:, :, 0:3] = np.clip(rgb_f * 255.0, 0, 255).astype(np.uint8)
    out[:, :, 3] = 255 if a_f is None else np.clip(a_f * 255.0, 0, 255).astype(np.uint8)
    return out


def make_nr(h, rough_f, ao_f, strength=4.0):
    r, g = height_to_normal_xy(h, strength=strength)
    out = np.empty((TILE, TILE, 4), dtype=np.uint8)
    out[:, :, 0] = r
    out[:, :, 1] = g
    out[:, :, 2] = np.clip(rough_f * 255.0, 0, 255).astype(np.uint8)
    out[:, :, 3] = np.clip(ao_f * 255.0, 0, 255).astype(np.uint8)
    return out


def tile_0_urban_asphalt():
    """Photorealistic Urban Seoul Fine-Aggregate Asphalt (crushed Pocheon granite/basalt flecks, subtle micro-wear, ZERO cartoon squiggle loops!)."""
    macro = tileable_fbm(TILE, octaves=5, base_freq=6, seed=1001)
    mid = tileable_fbm(TILE, octaves=5, base_freq=28, seed=1002)
    agg = tileable_fbm(TILE, octaves=4, base_freq=128, seed=1003)
    fine_agg = tileable_fbm(TILE, octaves=3, base_freq=256, seed=1006)
    micro = high_freq_grain(TILE, seed=1004)
    # Ultra-sparse, hair-thin natural weathered micro-fracture (masked by low-freq gate so it NEVER forms closed rings!)
    crack_gate = np.clip((tileable_fbm(TILE, octaves=3, base_freq=4, seed=1007) - 0.62) * 4.5, 0.0, 1.0)
    crack_f = np.abs(tileable_fbm(TILE, octaves=4, base_freq=10, seed=1005) - 0.5)
    hairline = np.clip(1.0 - crack_f * 145.0, 0.0, 1.0) * crack_gate

    # Authentic crushed Pocheon granite quartz specks + dark basalt binder at realistic sunlit Seoul charcoal albedo (sRGB ~64-78)
    quartz = np.clip((micro - 0.68) * 3.2, 0.0, 1.0) * 0.088
    basalt = np.clip((0.28 - micro) * 3.2, 0.0, 1.0) * 0.056
    lum = (
        0.258
        + (macro - 0.5) * 0.038
        + (mid - 0.5) * 0.032
        + (agg - 0.5) * 0.048
        + (fine_agg - 0.5) * 0.034
        + quartz
        - basalt
        - hairline * 0.022
    )
    lum = np.clip(lum, 0.13, 0.42)
    rgb = np.stack([lum * 0.985, lum * 0.995, lum * 1.032], axis=-1)
    h = mid * 0.22 + agg * 0.40 + fine_agg * 0.20 + micro * 0.18 - hairline * 0.08
    rough = np.clip(0.84 - hairline * 0.18 + (macro - 0.5) * 0.09 - quartz * 0.95, 0.44, 0.96)
    ao = np.clip(1.0 - hairline * 0.14 - (1.0 - agg) * 0.14 - basalt * 1.15, 0.55, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.6)


def tile_1_expressway_sma():
    """Korean Expressway Dark Stone-Mastic Asphalt (SMA): sleek, rich dark-slate high-speed highway pavement."""
    macro = tileable_fbm(TILE, octaves=5, base_freq=6, seed=1101)
    mid = tileable_fbm(TILE, octaves=4, base_freq=32, seed=1102)
    agg = tileable_fbm(TILE, octaves=3, base_freq=160, seed=1103)
    micro = high_freq_grain(TILE, seed=1104)
    # Subtle longitudinal asphalt paver roller banding
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    paver_sheen = 0.5 + 0.5 * np.sin(x * math.pi * 6.0 + macro * 1.2)

    quartz = np.clip((micro - 0.74) * 3.8, 0.0, 1.0) * 0.056
    lum = 0.186 + (macro - 0.5) * 0.024 + (mid - 0.5) * 0.022 + (agg - 0.5) * 0.034 + quartz - paver_sheen * 0.010
    lum = np.clip(lum, 0.09, 0.32)
    rgb = np.stack([lum * 0.96, lum * 0.99, lum * 1.055], axis=-1)
    h = mid * 0.20 + agg * 0.45 + micro * 0.35
    rough = np.clip(0.76 - paver_sheen * 0.10 + (macro - 0.5) * 0.08, 0.44, 0.92)
    ao = np.clip(1.0 - (1.0 - agg) * 0.13, 0.68, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.8)


def tile_2_intersection_asphalt():
    """Intersection Worn Asphalt: multi-directional polished mineral aggregate matching urban sunlit charcoal."""
    macro = tileable_fbm(TILE, octaves=5, base_freq=8, seed=1201)
    mid = tileable_fbm(TILE, octaves=5, base_freq=24, seed=1202)
    agg = tileable_fbm(TILE, octaves=4, base_freq=128, seed=1203)
    micro = high_freq_grain(TILE, seed=1204)
    polish = tileable_fbm(TILE, octaves=3, base_freq=4, seed=1205)

    quartz = np.clip((micro - 0.70) * 3.2, 0.0, 1.0) * 0.072
    lum = 0.246 + (macro - 0.5) * 0.036 + (mid - 0.5) * 0.028 + (agg - 0.5) * 0.042 + (polish - 0.5) * 0.022 + quartz
    lum = np.clip(lum, 0.12, 0.38)
    rgb = np.stack([lum * 0.99, lum * 0.995, lum * 1.03], axis=-1)
    h = mid * 0.22 + agg * 0.48 + micro * 0.30
    rough = np.clip(0.74 - polish * 0.18 + (macro - 0.5) * 0.10, 0.38, 0.92)
    ao = np.clip(1.0 - (1.0 - agg) * 0.14, 0.62, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.0)


def tile_3_seoul_sidewalk_pavers():
    """Authentic Seoul Standard 30x30cm Shot-Blasted Granite-Concrete Sidewalk Pavers (서울형 보도블록 300x300mm).
    8x8 blocks per 2.40m tile -> exact 30.0cm x 30.0cm KS standard, tight 3mm sand-filled joints,
    warm-neutral Korean Pocheon granite-concrete speckle, subtle per-tile tonal variation (ZERO black cartoon grid!).
    """
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    cols = 8.0
    rows = 8.0
    gx = x / (TILE / cols)
    gy = y / (TILE / rows)
    iy = np.floor(gy).astype(np.int32)
    # Clean running-bond half-tile stagger typical of Seoul Design sidewalks
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % int(cols)
    iy_m = iy % int(rows)
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)

    # Crisp, readable 4mm sand-filled mortar seams (stays sharp across mip levels without black cartoon lines!)
    seam_d = np.minimum(np.minimum(fx, 1.0 - fx), np.minimum(fy, 1.0 - fy))
    grout = np.clip((0.025 - seam_d) / 0.011, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.020) / 0.028, 0.0, 1.0)

    rng = np.random.default_rng(1304)
    p_val = rng.uniform(-0.036, 0.036, size=(int(rows), int(cols))).astype(np.float32)
    p_warm = rng.uniform(-0.012, 0.016, size=(int(rows), int(cols))).astype(np.float32)
    # Subtle Seoul architectural granite-band rhythm (every 8th row slightly lighter warm-stone accent band)
    band = (iy_m == 0).astype(np.float32) * 0.022
    pv = p_val[iy_m, ix_s] + band
    pw = p_warm[iy_m, ix_s]

    macro = tileable_fbm(TILE, octaves=4, base_freq=8, seed=1305)
    grain = tileable_fbm(TILE, octaves=5, base_freq=32, seed=1302)
    micro = high_freq_grain(TILE, seed=1303)
    # Authentic shot-blasted Korean granite-aggregate mineral flecks (quartz + fine biotite)
    quartz = np.clip((micro - 0.72) * 3.4, 0.0, 1.0) * 0.052
    biotite = np.clip((0.24 - micro) * 3.2, 0.0, 1.0) * 0.050

    base = 0.645 + pv + (macro - 0.5) * 0.026 + (grain - 0.5) * 0.042 + quartz - biotite
    r = base * 1.032 + pw
    g = base * 1.006 + pw * 0.35
    b = base * 0.958 - pw * 0.35
    # Warm medium-gray sand-filled mortar joint (crisp & natural, NEVER pitch black!)
    joint_rgb = np.stack([np.full_like(r, 0.415), np.full_like(g, 0.404), np.full_like(b, 0.386)], axis=-1)
    rgb = np.stack([r, g, b], axis=-1) * (1.0 - grout[:, :, None]) + joint_rgb * grout[:, :, None]
    h = bevel * 0.48 + grain * 0.24 + micro * 0.22 - grout * 0.38
    rough = np.clip(0.76 + grout * 0.16 + (grain - 0.5) * 0.09 - quartz * 0.75, 0.55, 0.96)
    ao = np.clip(1.0 - grout * 0.28 - biotite * 0.60, 0.62, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=3.8)


def tile_4_granite_flagstones():
    """Seoul Boulevard & Public-Plaza Flamed Pocheon Granite Flagstones (서울 포천석 화강암 판석 600x300mm).
    4 cols x 8 rows per 2.40m tile -> exact 60.0cm x 30.0cm architectural slabs in half-running bond,
    matching Tile 3's warm Korean granite family with crisp tight 3.5mm joints (ZERO black cartoon boxes!).
    """
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    cols = 4.0
    rows = 8.0
    gx = x / (TILE / cols)
    gy = y / (TILE / rows)
    iy = np.floor(gy).astype(np.int32)
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % int(cols)
    iy_m = iy % int(rows)
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)

    # Isotropic 3.5mm tight architectural granite mortar seams (scale X by 2.0 because slab is 2:1 aspect!)
    seam_x = np.minimum(fx, 1.0 - fx) * 2.0
    seam_y = np.minimum(fy, 1.0 - fy)
    seam_d = np.minimum(seam_x, seam_y)
    grout = np.clip((0.022 - seam_d) / 0.010, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.018) / 0.024, 0.0, 1.0)

    rng = np.random.default_rng(1404)
    sv = rng.uniform(-0.036, 0.036, size=(int(rows), int(cols))).astype(np.float32)
    st = rng.uniform(-0.010, 0.014, size=(int(rows), int(cols))).astype(np.float32)
    macro = tileable_fbm(TILE, octaves=4, base_freq=8, seed=1405)
    grain = tileable_fbm(TILE, octaves=5, base_freq=28, seed=1402)
    speck = high_freq_grain(TILE, seed=1403)
    biotite = np.clip((0.23 - speck) * 3.4, 0.0, 1.0) * 0.068
    feldspar = np.clip((speck - 0.74) * 3.4, 0.0, 1.0) * 0.056

    base = 0.665 + sv[iy_m, ix_s] + (macro - 0.5) * 0.026 + (grain - 0.5) * 0.040 - biotite + feldspar
    tint = st[iy_m, ix_s]
    stone_rgb = np.stack([base * 1.022 + tint, base * 1.004 + tint * 0.35, base * 0.968 - tint * 0.35], axis=-1)
    joint_rgb = np.stack([np.full_like(base, 0.435), np.full_like(base, 0.426), np.full_like(base, 0.412)], axis=-1)
    rgb = stone_rgb * (1.0 - grout[:, :, None]) + joint_rgb * grout[:, :, None]
    h = bevel * 0.44 + grain * 0.24 + speck * 0.22 - grout * 0.34
    rough = np.clip(0.63 + grout * 0.24 + (grain - 0.5) * 0.10 - feldspar * 0.80, 0.44, 0.94)
    ao = np.clip(1.0 - grout * 0.26 - biotite * 0.55, 0.64, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=3.4)


def tile_5_tactile_yellow():
    """Korean KS F 4561 Standard Yellow Tactile Guide Paving Block (노란 선형 점자블록 300x300mm).
    1 full 30x30cm tile per [0..1] UV with 4 crisp raised flat-top trapezoidal guide bars along V,
    non-slip micro-stipple texture, subtle foot-wear patina, and clean 2.5mm perimeter tile seam!
    """
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    # Crisp 2.5mm perimeter tile seam between consecutive 300x300mm tactile blocks
    edge_d = np.minimum(np.minimum(x, 1.0 - x), np.minimum(y, 1.0 - y))
    seam = np.clip((0.015 - edge_d) / 0.008, 0.0, 1.0)

    # 4 KS F 4561 linear guide bars spaced 75mm apart (centers at x = 0.125, 0.375, 0.625, 0.875)
    rib_u = (x * 4.0) % 1.0
    dx_rib = np.abs(rib_u - 0.5)
    dy_rib = np.abs(y - 0.5)
    in_rib_x = np.clip((0.235 - dx_rib) / 0.075, 0.0, 1.0)
    in_rib_y = np.clip((0.455 - dy_rib) / 0.032, 0.0, 1.0)
    rib_h = in_rib_x * in_rib_y

    grain = tileable_fbm(TILE, octaves=4, base_freq=28, seed=1501)
    micro = high_freq_grain(TILE, seed=1502)
    valley_dust = (1.0 - rib_h) * 0.075

    r = 0.885 + rib_h * 0.055 - valley_dust + (grain - 0.5) * 0.036 + (micro - 0.5) * 0.018
    g = 0.715 + rib_h * 0.050 - valley_dust * 0.90 + (grain - 0.5) * 0.032 + (micro - 0.5) * 0.016
    b = 0.115 + rib_h * 0.025 + valley_dust * 0.20 + (grain - 0.5) * 0.018
    joint_rgb = np.stack([np.full_like(r, 0.44), np.full_like(g, 0.39), np.full_like(b, 0.22)], axis=-1)
    rgb = np.stack([r, g, b], axis=-1) * (1.0 - seam[:, :, None]) + joint_rgb * seam[:, :, None]
    h = rib_h * 0.78 + grain * 0.08 + micro * 0.08 - seam * 0.36
    rough = np.clip(0.58 - rib_h * 0.14 + seam * 0.28 + (grain - 0.5) * 0.06, 0.36, 0.94)
    ao = np.clip(0.84 + rib_h * 0.16 - seam * 0.32, 0.50, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.8)


def tile_6_granite_curbstone():
    """Korean Dressed Pocheon Granite Curbstone (화강석 경계석 200x200x1000mm: 2 blocks per 2m with fine chisel grain)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    blk_y = (y * 2.0) % 1.0
    iy = np.floor(y * 2.0).astype(np.int32) % 2
    joint = np.clip((0.016 - np.minimum(blk_y, 1.0 - blk_y)) / 0.008, 0.0, 1.0)
    edge_bevel = np.clip((np.minimum(x, 1.0 - x) - 0.03) / 0.075, 0.0, 1.0)
    gutter_grime = np.clip(1.0 - x * 4.2, 0.0, 1.0) * 0.12

    rng = np.random.default_rng(1604)
    bv = rng.uniform(-0.028, 0.028, size=2).astype(np.float32)[iy]
    grain = tileable_fbm(TILE, octaves=5, base_freq=28, seed=1602)
    speck = high_freq_grain(TILE, seed=1603)
    biotite = np.clip((0.23 - speck) * 3.2, 0.0, 1.0) * 0.058
    quartz = np.clip((speck - 0.74) * 3.2, 0.0, 1.0) * 0.050

    base = 0.675 + bv + (grain - 0.5) * 0.050 + quartz - biotite - gutter_grime
    stone_rgb = np.stack([base * 1.022, base * 1.006, base * 0.974], axis=-1)
    joint_rgb = np.stack([np.full_like(base, 0.42), np.full_like(base, 0.412), np.full_like(base, 0.402)], axis=-1)
    rgb = stone_rgb * (1.0 - joint[:, :, None]) + joint_rgb * joint[:, :, None]
    h = edge_bevel * 0.46 + grain * 0.28 + speck * 0.22 - joint * 0.42
    rough = np.clip(0.70 + joint * 0.18 + (grain - 0.5) * 0.10, 0.50, 0.96)
    ao = np.clip(1.0 - joint * 0.30 - gutter_grime * 0.45, 0.55, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.2)


def tile_7_manhole_drain():
    """Cast-Iron Seoul Manhole Medallion (left half) + Slotted Stormwater Gutter Grate (right half)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    grain = tileable_fbm(TILE, octaves=5, base_freq=32, seed=1701)
    # Dark asphalt surround
    lum = 0.17 + (grain - 0.5) * 0.05
    rgb = np.stack([lum * 0.98, lum * 0.99, lum * 1.04], axis=-1)
    h = grain * 0.25
    rough = np.full((TILE, TILE), 0.78, dtype=np.float32)
    ao = np.full((TILE, TILE), 0.92, dtype=np.float32)

    # Concentric cast-iron manhole medallion centered at (0.5, 0.5)
    dx = x - 0.5
    dy = y - 0.5
    rad = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    in_ring = (rad < 0.44)
    outer_lip = (rad >= 0.40) & (rad < 0.44)
    tread = ((np.sin(rad * 110.0) > 0.1) ^ (np.sin(ang * 24.0) > 0.0)).astype(np.float32)
    rib_rings = (np.abs(np.sin(rad * 42.0)) > 0.86).astype(np.float32)
    iron_lum = 0.24 + tread * 0.055 + rib_rings * 0.045 + (grain - 0.5) * 0.04
    iron_rgb = np.stack([iron_lum * 1.05, iron_lum * 0.98, iron_lum * 0.93], axis=-1)
    rgb = np.where(in_ring[:, :, None], iron_rgb, rgb)
    h = np.where(in_ring, 0.45 + tread * 0.28 + rib_rings * 0.22, h)
    h = np.where(outer_lip, 0.30, h)
    rough = np.where(in_ring, 0.48 - tread * 0.08, rough)
    ao = np.where(outer_lip, 0.45, np.where(in_ring, 0.72 + tread * 0.25, ao))
    return make_rgba(rgb), make_nr(h, rough, ao, strength=7.0)


def tile_8_bridge_deck_joint():
    """Concrete Bridge Deck with transverse micro-tining & steel finger expansion seam."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    macro = tileable_fbm(TILE, octaves=5, base_freq=8, seed=1801)
    fine = high_freq_grain(TILE, seed=1802)
    tine = np.sin(y * math.pi * 160.0) * 0.5 + 0.5

    lum = 0.22 + (macro - 0.5) * 0.045 + (fine - 0.5) * 0.045 - tine * 0.014
    rgb = np.stack([lum * 0.99, lum * 1.00, lum * 1.03], axis=-1)
    h = macro * 0.25 + fine * 0.35 + tine * 0.18
    rough = np.clip(0.80 + (macro - 0.5) * 0.10, 0.55, 0.95)
    ao = np.clip(0.95 - tine * 0.10, 0.70, 1.0)

    # Steel finger expansion joint strip near y in [0.46..0.54]
    in_joint = (np.abs(y - 0.50) < 0.040)
    finger = (np.sin(x * math.pi * 64.0) > 0.0)
    seam_gap = (np.abs(y - 0.50 + np.where(finger, 0.008, -0.008)) < 0.009)
    steel_rgb = np.stack([np.full_like(lum, 0.36), np.full_like(lum, 0.38), np.full_like(lum, 0.41)], axis=-1)
    gap_rgb = np.zeros_like(steel_rgb) + 0.04
    rgb = np.where(in_joint[:, :, None], np.where(seam_gap[:, :, None], gap_rgb, steel_rgb), rgb)
    h = np.where(in_joint, np.where(seam_gap, 0.05, 0.62), h)
    rough = np.where(in_joint, np.where(seam_gap, 0.95, 0.34), rough)
    ao = np.where(seam_gap, 0.25, ao)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.5)


def tile_9_formwork_concrete():
    """Korean Viaduct & Retaining Wall Poured Formwork Concrete (seams, tie-rod dimples, weathering)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    macro = tileable_fbm(TILE, octaves=5, base_freq=6, seed=1901)
    fine = tileable_fbm(TILE, octaves=4, base_freq=48, seed=1902)
    streak = tileable_fbm(TILE, octaves=4, base_freq=16, seed=1903)
    # Vertical rain streak bias
    rain_s = np.mean(np.stack([np.roll(streak, k * 8, axis=0) for k in range(6)], axis=0), axis=0)

    px = (x * 4.0) % 1.0
    py = (y * 4.0) % 1.0
    seam = np.clip((0.015 - np.minimum(np.minimum(px, 1.0 - px), np.minimum(py, 1.0 - py))) / 0.008, 0.0, 1.0)
    # Recessed tie-rod holes at (0.25,0.25), (0.75,0.25), (0.25,0.75), (0.75,0.75)
    tx = np.minimum(np.abs(px - 0.25), np.abs(px - 0.75))
    ty = np.minimum(np.abs(py - 0.25), np.abs(py - 0.75))
    hole = np.clip((0.022 - np.sqrt(tx * tx + ty * ty)) / 0.010, 0.0, 1.0)

    base = 0.60 + (macro - 0.5) * 0.11 + (fine - 0.5) * 0.07 - (rain_s - 0.45) * 0.10
    rgb = np.stack([base * 0.99, base * 1.00, base * 1.02], axis=-1) * (1.0 - seam[:, :, None] * 0.30 - hole[:, :, None] * 0.48)
    h = 0.55 + (fine - 0.5) * 0.25 - seam * 0.35 - hole * 0.55
    rough = np.clip(0.84 + (macro - 0.5) * 0.12 + seam * 0.10, 0.62, 0.98)
    ao = np.clip(1.0 - seam * 0.38 - hole * 0.60, 0.30, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.0)


def tile_10_jersey_barrier():
    """Expressway Precast Concrete New Jersey Barrier with subtle base road-splash & chevron band."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    grain = tileable_fbm(TILE, octaves=5, base_freq=20, seed=2001)
    fine = high_freq_grain(TILE, seed=2002)
    joint = np.clip((0.018 - np.minimum((x * 2.0) % 1.0, 1.0 - (x * 2.0) % 1.0)) / 0.010, 0.0, 1.0)
    splash = np.clip(1.0 - y * 3.0, 0.0, 1.0) * 0.16

    base = 0.68 + (grain - 0.5) * 0.08 + (fine - 0.5) * 0.04 - splash
    rgb = np.stack([base * 0.99, base * 1.00, base * 1.02], axis=-1) * (1.0 - joint[:, :, None] * 0.45)
    # Subtle yellow/black reflective safety band near top of barrier
    in_band = (y > 0.80) & (y < 0.88)
    chev = (((x * 8.0 + y * 4.0) % 1.0) < 0.5)
    band_col = np.where(chev[:, :, None], np.array([0.92, 0.72, 0.12], dtype=np.float32), np.array([0.14, 0.15, 0.17], dtype=np.float32))
    rgb = np.where(in_band[:, :, None], band_col, rgb)
    h = 0.52 + (grain - 0.5) * 0.22 - joint * 0.50
    rough = np.where(in_band, 0.34, np.clip(0.80 + (grain - 0.5) * 0.10, 0.60, 0.95))
    ao = np.clip(1.0 - joint * 0.50 - splash * 0.5, 0.35, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=4.5)


def tile_11_sound_barrier():
    """Korean Acoustic Expressway Sound Barrier (anodized aluminum H-posts + translucent blue-grey glass)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    px = (x * 4.0) % 1.0
    py = (y * 4.0) % 1.0
    post_x = (px < 0.07) | (px > 0.93)
    rail_y = (py < 0.05) | (py > 0.95)
    frame = post_x | rail_y
    grain = tileable_fbm(TILE, octaves=4, base_freq=24, seed=2101)
    # Horizontal subtle acoustic ribs inside polycarbonate panel
    rib = 0.5 + 0.5 * np.sin(y * math.pi * 64.0)

    glass_rgb = np.stack([
        0.48 + (grain - 0.5) * 0.04 + rib * 0.02,
        0.58 + (grain - 0.5) * 0.04 + rib * 0.02,
        0.68 + (grain - 0.5) * 0.04 + rib * 0.02,
    ], axis=-1)
    frame_rgb = np.stack([
        0.24 + (grain - 0.5) * 0.03,
        0.27 + (grain - 0.5) * 0.03,
        0.31 + (grain - 0.5) * 0.03,
    ], axis=-1)
    rgb = np.where(frame[:, :, None], frame_rgb, glass_rgb)
    h = np.where(frame, 0.72, 0.38 + rib * 0.06)
    rough = np.where(frame, 0.36, 0.12)
    ao = np.where(frame, 0.82, 0.96)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=5.5)


def tile_12_tree_grate():
    """Seoul Cast-Iron Street Tree Grate framed by Granite Planter Curb."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32) / TILE
    dx = x - 0.5
    dy = y - 0.5
    rad = np.sqrt(dx * dx + dy * dy)
    edge = np.maximum(np.abs(dx), np.abs(dy))
    grain = tileable_fbm(TILE, octaves=4, base_freq=28, seed=2201)

    in_curb = edge > 0.40
    in_trunk = rad < 0.14
    ang = np.arctan2(dy, dx)
    slot_rad = (np.sin(rad * 85.0) > 0.15)
    slot_ang = (np.sin(ang * 16.0) > -0.20)
    metal_bar = (~slot_rad) | (~slot_ang) | (rad < 0.17) | (edge > 0.37)

    soil_rgb = np.stack([0.20 + grain * 0.08, 0.16 + grain * 0.06, 0.12 + grain * 0.05], axis=-1)
    iron_rgb = np.stack([0.20 + grain * 0.04, 0.21 + grain * 0.04, 0.23 + grain * 0.04], axis=-1)
    curb_rgb = np.stack([0.60 + grain * 0.08, 0.59 + grain * 0.08, 0.58 + grain * 0.08], axis=-1)

    rgb = np.where(in_curb[:, :, None], curb_rgb,
                   np.where(in_trunk[:, :, None], soil_rgb,
                            np.where(metal_bar[:, :, None], iron_rgb, soil_rgb * 0.55)))
    h = np.where(in_curb, 0.78, np.where(in_trunk, 0.25, np.where(metal_bar, 0.62, 0.15)))
    rough = np.where(in_curb, 0.68, np.where(metal_bar, 0.45, 0.92))
    ao = np.where(in_curb, 0.92, np.where(metal_bar, 0.85, 0.32))
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.5)


def tile_13_terracotta_nonslip():
    """Korean Red-Clay Bike Lane & School Zone Non-Slip Resin Aggregate Pavement."""
    macro = tileable_fbm(TILE, octaves=5, base_freq=10, seed=2301)
    agg = tileable_fbm(TILE, octaves=4, base_freq=128, seed=2302)
    micro = high_freq_grain(TILE, seed=2303)
    r = np.clip(0.54 + (macro - 0.5) * 0.08 + (agg - 0.5) * 0.08 + (micro - 0.5) * 0.06, 0.32, 0.72)
    g = np.clip(0.22 + (macro - 0.5) * 0.05 + (agg - 0.5) * 0.05 + (micro - 0.5) * 0.04, 0.12, 0.38)
    b = np.clip(0.18 + (macro - 0.5) * 0.04 + (agg - 0.5) * 0.04 + (micro - 0.5) * 0.04, 0.10, 0.32)
    rgb = np.stack([r, g, b], axis=-1)
    h = agg * 0.55 + micro * 0.45
    rough = np.clip(0.86 + (macro - 0.5) * 0.08, 0.68, 0.98)
    ao = np.clip(0.88 + agg * 0.12, 0.62, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.0)


def tile_14_alley_cobble():
    """Seoul Residential Brick / Cobble Alleyway Pavement (Seongsu / Bukchon / Itaewon)."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    cols, rows = 8.0, 16.0
    gx = x / (TILE / cols)
    gy = y / (TILE / rows)
    iy = np.floor(gy).astype(np.int32)
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % int(cols)
    iy_m = iy % int(rows)
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)
    seam_d = np.minimum(np.minimum(fx, 1.0 - fx) * 2.0, np.minimum(fy, 1.0 - fy))
    grout = np.clip((0.068 - seam_d) / 0.036, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.05) / 0.075, 0.0, 1.0)

    rng = np.random.default_rng(2401)
    cv = rng.uniform(-0.07, 0.07, size=(int(rows), int(cols))).astype(np.float32)
    cw = rng.uniform(-0.03, 0.04, size=(int(rows), int(cols))).astype(np.float32)
    grain = tileable_fbm(TILE, octaves=5, base_freq=28, seed=2402)
    base = 0.34 + cv[iy_m, ix_s] + (grain - 0.5) * 0.08
    warm = cw[iy_m, ix_s]
    rgb = np.stack([base + warm * 1.1, base + warm * 0.3, base - warm * 0.4], axis=-1) * (1.0 - grout[:, :, None] * 0.55)
    h = bevel * 0.58 + grain * 0.30 - grout * 0.58
    rough = np.clip(0.76 + grout * 0.20 + (grain - 0.5) * 0.12, 0.52, 0.96)
    ao = np.clip(1.0 - grout * 0.58, 0.32, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.2)


def tile_15_river_embankment():
    """Han River Granite Seawall Embankment & Waterfront Promenade Blocks."""
    y, x = np.mgrid[0:TILE, 0:TILE].astype(np.float32)
    cols, rows = 6.0, 12.0
    gx = x / (TILE / cols)
    gy = y / (TILE / rows)
    iy = np.floor(gy).astype(np.int32)
    gx_s = gx + (iy % 2) * 0.5
    ix_s = np.floor(gx_s).astype(np.int32) % int(cols)
    iy_m = iy % int(rows)
    fx = gx_s - np.floor(gx_s)
    fy = gy - np.floor(gy)
    seam_d = np.minimum(np.minimum(fx, 1.0 - fx) * 2.0, np.minimum(fy, 1.0 - fy))
    grout = np.clip((0.055 - seam_d) / 0.028, 0.0, 1.0)
    bevel = np.clip((seam_d - 0.04) / 0.06, 0.0, 1.0)
    rng = np.random.default_rng(2501)
    sv = rng.uniform(-0.06, 0.06, size=(int(rows), int(cols))).astype(np.float32)
    grain = tileable_fbm(TILE, octaves=5, base_freq=24, seed=2502)
    base = 0.52 + sv[iy_m, ix_s] + (grain - 0.5) * 0.10
    rgb = np.stack([base * 0.98, base * 0.99, base * 1.01], axis=-1) * (1.0 - grout[:, :, None] * 0.55)
    h = bevel * 0.55 + grain * 0.35 - grout * 0.60
    rough = np.clip(0.78 + grout * 0.16, 0.56, 0.96)
    ao = np.clip(1.0 - grout * 0.56, 0.35, 1.0)
    return make_rgba(rgb), make_nr(h, rough, ao, strength=6.0)


def main():
    t0 = time.time()
    os.makedirs("assets/textures", exist_ok=True)
    albedo = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    nr = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)

    gens = [
        tile_0_urban_asphalt,
        tile_1_expressway_sma,
        tile_2_intersection_asphalt,
        tile_3_seoul_sidewalk_pavers,
        tile_4_granite_flagstones,
        tile_5_tactile_yellow,
        tile_6_granite_curbstone,
        tile_7_manhole_drain,
        tile_8_bridge_deck_joint,
        tile_9_formwork_concrete,
        tile_10_jersey_barrier,
        tile_11_sound_barrier,
        tile_12_tree_grate,
        tile_13_terracotta_nonslip,
        tile_14_alley_cobble,
        tile_15_river_embankment,
    ]
    for tid, fn in enumerate(gens):
        col, row = tid % GRID, tid // GRID
        a_tile, nr_tile = fn()
        albedo[row * TILE:(row + 1) * TILE, col * TILE:(col + 1) * TILE] = a_tile
        nr[row * TILE:(row + 1) * TILE, col * TILE:(col + 1) * TILE] = nr_tile
        print(f"  Generated street PBR tile {tid:2d}/15 ({fn.__name__})")

    alb_path = "assets/textures/street_albedo.png"
    nr_path = "assets/textures/street_normal_rough.png"
    Image.fromarray(albedo, mode="RGBA").save(alb_path, compress_level=6)
    Image.fromarray(nr, mode="RGBA").save(nr_path, compress_level=6)
    print(f"Saved {alb_path} ({os.path.getsize(alb_path)/1e6:.2f} MB) & {nr_path} ({os.path.getsize(nr_path)/1e6:.2f} MB) in {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
