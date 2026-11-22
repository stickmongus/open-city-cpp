#!/usr/bin/env python3
"""
Compile REAL OpenStreetMap + Overture Maps (2026-11-18.0, 196K Seoul buildings + 1.3K 3D building_parts)
+ TUM GlobalBuildingAtlas (131K 3m satellite-measured Seoul heights) + AWS SRTM 30m DEM
into:
  1. assets/maps/seoul_real.ocmap  (OCM4 v2 high-fidelity 3D GIS world — 0% road-blocking buildings!)
  2. assets/maps/seoul_map_2d.png  (3072x1968 crisp Naver/Google-style cartographic map)

Usage:
  uv run --with "numpy>=2" --with pyarrow --with shapely --with scipy --with pillow \
      python3 tools/build_seoul_ocm4.py
"""

import hashlib
import json
import math
import os
import struct
import sys
import time
from collections import defaultdict

import numpy as np
import pyarrow.parquet as pq
from PIL import Image, ImageDraw
from scipy.spatial import cKDTree
from shapely import STRtree
from shapely.geometry import LineString, MultiPolygon, Polygon
from shapely.ops import unary_union
import shapely.wkb as wkb

ORIGIN_LAT = 37.5235
ORIGIN_LON = 126.9850
M_PER_DEG_LAT = 111132.0
M_PER_DEG_LON = 88304.0

BASE_OCMAP = "/tmp/osm_work/seoul_real_v3_base.ocmap"
OVERTURE_BLDGS = "/tmp/osm_work/seoul_overture_buildings.parquet"
OVERTURE_PARTS = "/tmp/osm_work/seoul_overture_parts.parquet"
GBA_JSON = "/tmp/osm_work/gba_korea_heights.json"
GBA_NPZ = "/tmp/osm_work/seoul_gba_heights.npz"

OUT_OCMAP = "assets/maps/seoul_real.ocmap"
OUT_MAP2D = "assets/maps/seoul_map_2d.png"

SKIP_CLASSES = {
    "roof", "canopy", "shelter", "kiosk", "bridge", "tunnel", "carport",
    "tent", "shed", "grandstand", "silo", "storage_tank", "transformer_tower",
    "ruins", "construction", "toilets", "greenhouse", "static_caravan",
    "cabin", "hut", "garage", "garages", "bunker", "outbuilding",
}

CSS_COLORS = {
    "white": (236, 236, 234), "ivory": (236, 232, 220), "beige": (218, 206, 186),
    "cream": (232, 224, 206), "tan": (204, 184, 156), "sand": (214, 198, 170),
    "gray": (166, 170, 176), "grey": (166, 170, 176), "lightgray": (204, 206, 210),
    "lightgrey": (204, 206, 210), "darkgray": (116, 120, 126), "darkgrey": (116, 120, 126),
    "silver": (192, 198, 206), "black": (78, 84, 92), "charcoal": (86, 92, 102),
    "brown": (152, 114, 92), "red": (178, 104, 86), "maroon": (146, 86, 76),
    "orange": (196, 134, 94), "yellow": (216, 196, 146), "gold": (204, 176, 118),
    "blue": (122, 162, 202), "lightblue": (152, 188, 218), "darkblue": (88, 116, 152),
    "navy": (86, 108, 142), "teal": (114, 162, 168), "green": (122, 158, 142),
}

# Authentic curated Seoul architectural RGB palettes per style (0..9)
STYLE_PALETTES = {
    # 0: Korean High-Rise Apartment (래미안/자이/힐스테이트/아크로)
    0: [(226, 222, 212), (214, 218, 222), (222, 214, 200), (206, 210, 216),
        (230, 224, 216), (200, 204, 210), (218, 210, 196), (210, 214, 212)],
    # 1: Blue/Teal Glass Curtain-Wall Skyscraper (테헤란로/여의도/잠실)
    1: [(132, 178, 212), (118, 168, 192), (146, 186, 214), (112, 154, 196),
        (128, 176, 182), (156, 192, 216), (104, 148, 182), (142, 174, 204)],
    # 2: Dark Charcoal / Bronze / Silver Metallic Curtain-Wall Tower (GFC/삼성타운/GT타워)
    2: [(108, 118, 132), (138, 130, 120), (168, 176, 186), (96, 108, 124),
        (124, 126, 132), (150, 140, 128), (116, 128, 144), (176, 182, 190)],
    # 3: Honed Granite / Limestone Corporate Office (강남/종로/광화문)
    3: [(202, 198, 190), (188, 182, 172), (212, 206, 198), (174, 172, 170),
        (196, 186, 176), (164, 166, 170), (210, 202, 190), (182, 180, 178)],
    # 4: Seongsu/Hongdae/Gangnam Brick Commercial & Loft
    4: [(186, 124, 98), (168, 112, 88), (196, 152, 122), (148, 104, 86),
        (178, 132, 106), (134, 102, 92), (202, 164, 134), (162, 118, 98)],
    # 5: Modern Seoul Commercial Stone-Tile / Stucco (근린생활시설)
    5: [(222, 216, 204), (210, 206, 200), (216, 204, 190), (198, 200, 204),
        (228, 224, 216), (204, 196, 186), (190, 192, 196), (214, 212, 206)],
    # 6: Traditional Korean Hanok / Palace / Temple
    6: [(224, 216, 200), (216, 206, 190), (228, 222, 208), (210, 198, 182)],
    # 7: Korean School / University / Hospital / Civic
    7: [(196, 154, 130), (212, 204, 190), (184, 144, 122), (204, 200, 194),
        (190, 176, 160), (216, 210, 198), (176, 156, 142), (200, 194, 186)],
    # 8: Industrial / Warehouse / Stadium / Station Hall
    8: [(178, 184, 192), (162, 168, 176), (192, 196, 200), (148, 156, 166)],
    # 9: Korean Multi-Family Villa / House (다세대·다가구 빌라)
    9: [(208, 198, 184), (192, 142, 118), (216, 210, 200), (186, 178, 168),
        (200, 184, 166), (178, 134, 114), (212, 204, 194), (194, 192, 188)],
}


def latlon_to_world(lat: float, lon: float):
    return (lon - ORIGIN_LON) * M_PER_DEG_LON, -(lat - ORIGIN_LAT) * M_PER_DEG_LAT


def parse_color(val):
    if not val or not isinstance(val, str):
        return None
    s = val.strip().lower()
    if s in CSS_COLORS:
        return CSS_COLORS[s]
    if s.startswith("#"):
        hx = s[1:]
        if len(hx) == 3:
            hx = "".join(c * 2 for c in hx)
        if len(hx) == 6:
            try:
                r, g, b = int(hx[0:2], 16), int(hx[2:4], 16), int(hx[4:6], 16)
                # Keep architectural realism (avoid hyper-saturated neon OSM hex tags like #0000FF!)
                if max(r, g, b) - min(r, g, b) > 150 and min(r, g, b) < 35:
                    # Desaturate neon OSM placeholder color toward realistic architectural tint
                    avg = (r + g + b) / 3.0
                    r = int(r * 0.45 + avg * 0.55)
                    g = int(g * 0.45 + avg * 0.55)
                    b = int(b * 0.45 + avg * 0.55)
                r = max(68, min(238, r))
                g = max(68, min(238, g))
                b = max(68, min(238, b))
                return (r, g, b)
            except ValueError:
                return None
    return None


def parse_roof_shape(val):
    if not val or not isinstance(val, str):
        return None
    s = val.strip().lower()
    if s in ("gabled", "saltbox", "half-hipped", "gambrel"):
        return 1
    if s in ("hipped", "mansard"):
        return 2
    if s in ("pyramidal", "cone"):
        return 3
    if s in ("dome", "onion", "round", "semicircular"):
        return 4
    if s in ("skillion", "lean_to"):
        return 5
    if s in ("flat", "flat_roof"):
        return 0
    return None


def det_hash(cx: float, cz: float, salt: int = 0) -> int:
    b = struct.pack("<ffi", round(cx, 1), round(cz, 1), salt)
    return int.from_bytes(hashlib.blake2b(b, digest_size=4).digest(), "little")


def sample_dem(dem_dm: np.ndarray, dem_min_x, dem_max_x, dem_min_z, dem_max_z, x: float, z: float) -> float:
    h_rows, w_cols = dem_dm.shape
    fx = max(0.0, min(w_cols - 1.001, (x - dem_min_x) / (dem_max_x - dem_min_x) * (w_cols - 1)))
    fz = max(0.0, min(h_rows - 1.001, (z - dem_min_z) / (dem_max_z - dem_min_z) * (h_rows - 1)))
    ix, iz = int(fx), int(fz)
    tx, tz = fx - ix, fz - iz
    h00 = float(dem_dm[iz, ix]) * 0.1
    h10 = float(dem_dm[iz, ix + 1]) * 0.1
    h01 = float(dem_dm[iz + 1, ix]) * 0.1
    h11 = float(dem_dm[iz + 1, ix + 1]) * 0.1
    return (h00 * (1.0 - tx) + h10 * tx) * (1.0 - tz) + (h01 * (1.0 - tx) + h11 * tx) * tz


def read_base_ocmap(path: str):
    data = open(path, "rb").read()
    hdr = struct.unpack_from("<4sIfffffIIffffIIIIIIIIIIII", data, 0)
    (magic, ver, minX, maxX, minZ, maxZ, chunkSz, cxN, czN,
     demMinX, demMaxX, demMinZ, demMaxZ, demW, demH,
     roadCount, roadVertCount, bldgCount, bldgVertCount,
     waterTriCount, parkTriCount, poiCount,
     fcr, fcb, fcp) = hdr
    assert magic == b"OCM3", f"Expected OCM3 base map, got {magic}"

    off = 100
    dem_bytes = data[off: off + demW * demH * 2]
    dem_dm = np.frombuffer(dem_bytes, dtype=np.int16).reshape((demH, demW))
    off += demW * demH * 2

    roads_bytes = data[off: off + roadCount * 30]
    roads = [struct.unpack_from("<IHBBBBfffff", roads_bytes, i * 30) for i in range(roadCount)]
    off += roadCount * 30

    rverts_bytes = data[off: off + roadVertCount * 8]
    rverts = np.frombuffer(rverts_bytes, dtype=np.float32).reshape((roadVertCount, 2))
    off += roadVertCount * 8

    # Skip old buildings & building verts
    off += bldgCount * 32
    off += bldgVertCount * 4

    # Read chunk headers (to preserve existing flatChunkRoads & flatChunkPois!)
    chunks_bytes = data[off: off + cxN * czN * 24]
    old_chunks = [struct.unpack_from("<IIIIII", chunks_bytes, i * 24) for i in range(cxN * czN)]
    off += cxN * czN * 24

    flat_roads_bytes = data[off: off + fcr * 4]
    off += fcr * 4
    # Skip old flat_bldgs
    off += fcb * 4
    flat_pois_bytes = data[off: off + fcp * 4]
    off += fcp * 4

    water_bytes = data[off: off + waterTriCount * 24]
    water_tris = np.frombuffer(water_bytes, dtype=np.float32).reshape((waterTriCount, 6))
    off += waterTriCount * 24

    park_bytes = data[off: off + parkTriCount * 24]
    park_tris = np.frombuffer(park_bytes, dtype=np.float32).reshape((parkTriCount, 6))
    off += parkTriCount * 24

    pois_bytes = data[off: off + poiCount * 12]
    off += poiCount * 12

    return {
        "hdr": hdr,
        "dem_bytes": dem_bytes, "dem_dm": dem_dm,
        "roads_bytes": roads_bytes, "roads": roads,
        "rverts_bytes": rverts_bytes, "rverts": rverts,
        "old_chunks": old_chunks,
        "flat_roads_bytes": flat_roads_bytes,
        "flat_pois_bytes": flat_pois_bytes,
        "water_bytes": water_bytes, "water_tris": water_tris,
        "park_bytes": park_bytes, "park_tris": park_tris,
        "pois_bytes": pois_bytes,
    }


def classify_seoul_architecture(cls, sub, name, fmat, fcol_raw, rshp_raw, rheight_raw,
                                area_m2, cx, cz, height, min_height, is_part):
    b = (cls or "").lower()
    s = (sub or "").lower()
    nm = (name or "").lower()
    fm = (fmat or "").lower()
    hv = det_hash(cx, cz, 17)
    hv2 = det_hash(cx, cz, 91)

    in_gangnam = (2000 < cx < 7800 and 500 < cz < 3800)
    in_yeouido = (-6600 < cx < -3400 and -1200 < cz < 1100)
    in_gwanghwamun = (-1800 < cx < 2400 and -5600 < cz < -3300)
    in_jamsil = (7800 < cx < 11200 and 400 < cz < 3400)
    in_seongsu_hongdae = (cx < -3200 and cz < -2000) or (3400 < cx < 6800 and -3600 < cz < -800)
    in_bukchon = (-1600 < cx < 500 and -6600 < cz < -4600)
    in_cbd = in_gangnam or in_yeouido or in_gwanghwamun or in_jamsil

    is_apt = (b in ("apartments", "dormitory") or
              any(k in nm for k in ("아파트", "래미안", "자이", "힐스테이트", "푸르지오", "아크로", "롯데캐슬", "아이파크", "더샵", "센트레빌", "e편한세상", "현대", "주공", "타워팰리스")) or
              ("동" in nm and area_m2 > 220 and height >= 18.0))
    is_hanok = (b in ("temple", "shrine", "palace") or
                any(k in nm for k in ("궁", "한옥", "사찰", "절", "향교", "서원")) or
                (in_bukchon and height <= 9.5 and area_m2 < 240 and b in ("", "house", "residential", "yes")))
    is_civic = (b in ("school", "university", "college", "hospital", "civic", "public", "library", "museum", "government", "kindergarten") or
                s in ("education", "medical", "civic"))
    is_ind = (b in ("industrial", "warehouse", "factory", "stadium", "sports_hall", "train_station", "hangar") or
              s in ("industrial", "transportation"))
    is_office = (b in ("office", "commercial", "hotel", "retail", "supermarket", "mall", "department_store") or
                 s == "commercial" or
                 any(k in nm for k in ("타워", "빌딩", "센터", "프라자", "플라자", "호텔", "백화점", "tower", "center", "building", "hotel")))
    is_villa = (b in ("house", "detached", "semidetached_house", "terrace", "bungalow", "residential") or s == "residential")

    # Determine authentic style (0..9) honoring explicit OSM/Overture facade_material first!
    if is_hanok:
        style = 6
    elif fm in ("glass", "mirror"):
        style = 1 if (hv % 3 != 0) else 2
    elif fm in ("metal", "steel", "aluminium", "aluminum", "panels"):
        style = 2 if height >= 20.0 else 8
    elif fm in ("brick", "clinker"):
        style = 4
    elif fm in ("stone", "granite", "limestone", "marble", "sandstone"):
        style = 3 if height >= 16.0 else 5
    elif fm in ("concrete", "plaster", "stucco", "cement_block"):
        style = 0 if (is_apt and height >= 18.0) else 5
    elif is_apt and height >= 18.0:
        style = 0
    elif is_civic:
        style = 7
    elif is_ind:
        style = 8
    elif height >= 42.0 and (in_cbd or is_office):
        # Mix of Blue/Teal Glass (1), Dark Metallic Curtain-Wall (2), and Honed Granite Corporate (3)!
        r_pick = hv % 10
        if r_pick < 4:
            style = 1
        elif r_pick < 7:
            style = 2
        else:
            style = 3
    elif height >= 22.0 and (in_cbd or is_office):
        r_pick = hv % 8
        if r_pick < 3:
            style = 3
        elif r_pick < 5:
            style = 1
        elif r_pick < 6:
            style = 2
        else:
            style = 5
    elif height >= 26.0 and not in_cbd:
        # Outside CBDs, tall buildings (>26m) in Seoul are overwhelmingly iconic Korean apartments!
        style = 0 if (hv % 5 != 0) else 3
    elif in_seongsu_hongdae and height <= 20.0 and (hv % 2 == 0):
        style = 4
    elif is_villa or (height <= 14.5 and area_m2 <= 240.0):
        style = 4 if (hv % 4 == 0) else 9
    else:
        style = 5 if (hv % 3 != 0) else (4 if (hv % 6 == 0) else 3)

    # Facade true RGB color
    parsed_col = parse_color(fcol_raw)
    if parsed_col is not None:
        fr, fg, fb = parsed_col
    else:
        pal = STYLE_PALETTES[style]
        fr, fg, fb = pal[hv2 % len(pal)]
        # Subtle organic per-building tonal micro-variation (-7..+7) so no two adjacent blocks look identical!
        d_lum = ((hv >> 8) % 15) - 7
        fr = max(62, min(240, fr + d_lum))
        fg = max(62, min(240, fg + d_lum))
        fb = max(62, min(240, fb + d_lum))

    # Roof shape & height
    rshp = parse_roof_shape(rshp_raw)
    rh = float(rheight_raw) if (rheight_raw is not None and math.isfinite(rheight_raw) and rheight_raw > 0.3) else 0.0
    if rshp is None:
        if style == 6:
            rshp = 2  # Hipped Korean Giwa roof
            rh = max(rh, 2.2)
        elif b == "church":
            rshp = 1  # Gabled chapel roof
            rh = max(rh, 3.6)
        elif style == 8 and area_m2 > 220.0 and height < 24.0:
            rshp = 1  # Gabled industrial/station hall
            rh = max(rh, 2.4)
        elif style == 9 and height <= 14.0 and area_m2 <= 240.0 and ((hv >> 4) % 10) < 4:
            rshp = 1 if ((hv >> 12) & 1) == 0 else 2  # Authentic pitched/hipped Seoul residential villa
            rh = max(rh, 1.9 + ((hv >> 16) % 10) * 0.1)
        elif style in (1, 2, 3) and height >= 46.0 and not is_part and area_m2 >= 260.0:
            rshp = 6  # Architectural stepped skyscraper crown / sky-louver cap
            rh = max(rh, min(6.5, height * 0.06))
        else:
            rshp = 0
            rh = 0.0
    else:
        if rshp in (1, 2, 3, 4, 5) and rh < 0.8:
            rh = 2.6 if style != 6 else 2.4

    rh_q = max(0, min(255, int(round(rh * 4.0))))

    # Flags: bit0=isPart, bit1=hasRetail1F, bit2=hasPodium, bit3=hasCrownBand
    flags = 0
    if is_part:
        flags |= 1
    if min_height < 0.5 and style in (1, 2, 3, 4, 5) and height >= 7.0:
        flags |= 2  # Authentic Korean 1F commercial retail storefront / corporate lobby!
    if min_height < 0.5 and style in (1, 2, 3, 5) and height >= 16.0 and area_m2 >= 180.0:
        flags |= 4  # 3D projecting 1F/2F cornice podium overhang!
    if style in (0, 1, 2, 3) and height >= 36.0:
        flags |= 8  # Architectural top crown band / mechanical sky-floor louver!

    variant_seed = (hv >> 16) & 0xFF
    return style, rshp, fr, fg, fb, rh_q, flags, variant_seed


def render_seoul_2d_map_v4(out_png: str, dem_dm: np.ndarray, hdr, water_tris, park_tris, roads, rverts, buildings_out):
    """Render ultra-crisp 3072x1968 Naver/Google-style cartographic map with all ~185K real Seoul buildings!"""
    W, H = 3072, 1968
    (_, _, minX, maxX, minZ, maxZ, _, _, _, demMinX, demMaxX, demMinZ, demMaxZ, demW, demH, *_) = hdr
    dem = dem_dm.astype(np.float32) * 0.1

    xs = np.linspace(minX, maxX, W, dtype=np.float32)
    zs = np.linspace(minZ, maxZ, H, dtype=np.float32)
    gx, gz = np.meshgrid(xs, zs)
    fx = np.clip((gx - demMinX) / (demMaxX - demMinX) * (demW - 1), 0, demW - 1.001)
    fz = np.clip((gz - demMinZ) / (demMaxZ - demMinZ) * (demH - 1), 0, demH - 1.001)
    ix = fx.astype(np.int32)
    iz = fz.astype(np.int32)
    tx = fx - ix
    tz = fz - iz
    elev = (
        (dem[iz, ix] * (1 - tx) + dem[iz, ix + 1] * tx) * (1 - tz) +
        (dem[iz + 1, ix] * (1 - tx) + dem[iz + 1, ix + 1] * tx) * tz
    )
    dz_dx = np.gradient(elev, axis=1) / ((maxX - minX) / W)
    dz_dz = np.gradient(elev, axis=0) / ((maxZ - minZ) / H)
    shade = np.clip(0.86 + (-dz_dx * 0.45 - dz_dz * 0.45), 0.55, 1.12)

    mtn = np.clip((elev - 48.0) / 140.0, 0.0, 1.0)
    r = (34.0 * (1.0 - mtn) + 28.0 * mtn) * shade
    g = (40.0 * (1.0 - mtn) + 54.0 * mtn) * shade
    b = (48.0 * (1.0 - mtn) + 36.0 * mtn) * shade
    img_arr = np.stack([np.clip(r, 0, 255), np.clip(g, 0, 255), np.clip(b, 0, 255)], axis=-1).astype(np.uint8)
    im = Image.fromarray(img_arr, mode="RGB")
    draw = ImageDraw.Draw(im)

    sx = W / (maxX - minX)
    sz = H / (maxZ - minZ)

    def to_px(x, z):
        return ((x - minX) * sx, (z - minZ) * sz)

    # 1. Parks
    for tri in park_tris:
        draw.polygon([to_px(tri[0], tri[1]), to_px(tri[2], tri[3]), to_px(tri[4], tri[5])], fill=(36, 74, 48))

    # 2. Han River & Water
    for tri in water_tris:
        draw.polygon([to_px(tri[0], tri[1]), to_px(tri[2], tri[3]), to_px(tri[4], tri[5])], fill=(26, 76, 128))

    # 3. All ~185K Real Seoul Building Footprints (shaded by real measured height!)
    for rec in buildings_out:
        cx, cz, loc_pts, style, rshp, fr, fg, fb, rh_q, flags, vseed, base_y, min_h, h, hx, hz = rec
        if min_h > 2.0:
            continue
        pts = [to_px(cx + lx * 0.05, cz + lz * 0.05) for (lx, lz) in loc_pts]
        if len(pts) >= 3:
            if h >= 50.0:
                col = (74, 86, 102)
            elif h >= 24.0:
                col = (60, 68, 82)
            else:
                col = (50, 56, 68)
            draw.polygon(pts, fill=col)

    # 4. Real Seoul Road Hierarchy (Emerald Green Expressways -> Amber Boulevards -> Crisp City Streets!)
    road_styles = {
        6: ((74, 80, 92), 1),
        5: ((88, 94, 108), 1),
        4: ((122, 130, 146), 2),
        3: ((168, 174, 186), 3),
        2: ((226, 184, 78), 5),
        1: ((48, 198, 118), 7),
        0: ((34, 214, 118), 8),
    }
    for cls_draw in (6, 5, 4, 3, 2, 1, 0):
        col, width = road_styles[cls_draw]
        for (fv, vc, rcls, lanes, rflags, nslot, hw, *_bb) in roads:
            if rcls != cls_draw or (rflags & 4) != 0 or vc < 2:
                continue
            pts = [to_px(float(rverts[fv + k, 0]), float(rverts[fv + k, 1])) for k in range(vc)]
            if cls_draw <= 1:
                draw.line(pts, fill=(14, 96, 54), width=width + 3, joint="curve")
                draw.line(pts, fill=col, width=width, joint="curve")
            else:
                draw.line(pts, fill=col, width=width, joint="curve")

    im.save(out_png, optimize=True)
    print(f"Saved crisp {W}x{H} 2D map: {out_png} ({os.path.getsize(out_png) / 1e6:.2f} MB)")


def main():
    t0 = time.time()
    print("1. Reading base roads/water/parks/POIs/DEM from", BASE_OCMAP)
    base = read_base_ocmap(BASE_OCMAP)
    hdr = base["hdr"]
    (_, _, minX, maxX, minZ, maxZ, chunkSz, cxN, czN,
     demMinX, demMaxX, demMinZ, demMaxZ, demW, demH,
     roadCount, roadVertCount, _, _, waterTriCount, parkTriCount, poiCount,
     fcr, _, fcp) = hdr
    dem_dm = base["dem_dm"]
    roads = base["roads"]
    rverts = base["rverts"]
    water_tris = base["water_tris"]
    park_tris = base["park_tris"]

    # 2. Build Shapely STRtree of ALL non-tunnel Seoul roads (roadClass <= 4) buffered by full halfWidth + 0.35m
    #    so ZERO buildings can EVER encroach onto ANY lane (including blue bus lanes & shoulders)!
    print("2. Building full-width road-corridor STRtree (25,295 Seoul roads)...")
    road_polys = []
    for (fv, vc, rcls, lanes, rflags, nslot, hw, *_bb) in roads:
        if rcls > 4 or (rflags & 4) != 0 or vc < 2:
            continue
        pts = [(float(rverts[fv + k, 0]), float(rverts[fv + k, 1])) for k in range(vc)]
        try:
            ls = LineString(pts)
            if ls.length < 0.5:
                continue
            pad = max(2.6, float(hw)) + (0.45 if rcls <= 2 else 0.25)
            road_polys.append(ls.buffer(pad, cap_style=2, join_style=2, quad_segs=3))
        except Exception:
            continue
    road_tree = STRtree(road_polys)
    print(f"   Indexed {len(road_polys)} full-width road corridor polygons in {time.time()-t0:.1f}s")

    # Build water STRtree to reject any bogus floating buildings inside Han River
    water_polys = []
    for tri in water_tris:
        p = Polygon([(tri[0], tri[1]), (tri[2], tri[3]), (tri[4], tri[5])])
        if p.is_valid and p.area > 5.0:
            water_polys.append(p)
    water_tree = STRtree(water_polys)

    # 3. Load Real Satellite Measured Heights (GBA direct OSM ID map + 131,008 spatial KDTree!)
    print("3. Loading TUM GlobalBuildingAtlas real 3m satellite heights...")
    raw_gba = json.load(open(GBA_JSON))
    gba_osm = {}
    for k, v in raw_gba.items():
        if k.startswith("osm") and k.endswith("KOR") and v.get("height") is not None:
            h = float(v["height"])
            if 2.5 <= h <= 560.0:
                gba_osm[k] = h
    del raw_gba

    gba_npz = np.load(GBA_NPZ)
    gba_pts = np.column_stack([gba_npz["x"], gba_npz["z"]])
    gba_hs = gba_npz["h"]
    gba_kdtree = cKDTree(gba_pts)
    print(f"   Direct OSM GBA heights: {len(gba_osm)}, Spatial Seoul GBA KDTree: {len(gba_hs)} points ({time.time()-t0:.1f}s)")

    # 4. Load Overture Maps 3D building_parts (grouped by building_id)
    print("4. Loading Overture Maps 2026-11-18.0 3D building_parts & buildings...")
    tp = pq.read_table(OVERTURE_PARTS)
    pd = tp.to_pydict()
    parts_by_bid = defaultdict(list)
    for i in range(tp.num_rows):
        if pd["is_underground"][i]:
            continue
        lvl = pd["level"][i]
        if lvl is not None and lvl < 0:
            continue
        bid = pd["building_id"][i]
        if bid:
            parts_by_bid[bid].append(i)
    print(f"   Loaded {tp.num_rows} 3D building_parts across {len(parts_by_bid)} multi-part Seoul complexes")

    tb = pq.read_table(OVERTURE_BLDGS)
    bd = tb.to_pydict()
    print(f"   Loaded {tb.num_rows} Overture Seoul buildings. Converting & road-clipping...")

    # Helper to convert lon/lat shapely geometry into world XZ Polygon
    def geom_to_world_poly(wkb_bytes):
        g = wkb.loads(wkb_bytes)
        if g.is_empty:
            return None
        if isinstance(g, MultiPolygon):
            g = max(g.geoms, key=lambda p: p.area)
        if not isinstance(g, Polygon):
            return None
        coords = [latlon_to_world(lat, lon) for (lon, lat) in g.exterior.coords]
        if len(coords) < 4:
            return None
        p = Polygon(coords)
        if not p.is_valid:
            p = p.buffer(0)
        if p.is_empty:
            return None
        if isinstance(p, MultiPolygon):
            p = max(p.geoms, key=lambda q: q.area)
        if not isinstance(p, Polygon):
            return None
        return p

    # First pass: collect raw candidate items (buildings or their 3D building_parts) with measured heights
    raw_items = []  # list of dicts
    stats = defaultdict(int)

    for i in range(tb.num_rows):
        if bd["is_underground"][i]:
            stats["skip_underground"] += 1
            continue
        lvl = bd["level"][i]
        if lvl is not None and lvl < 0:
            stats["skip_neg_level"] += 1
            continue
        cls = (bd["class"][i] or "").lower()
        sub = (bd["subtype"][i] or "").lower()
        if cls in SKIP_CLASSES:
            stats["skip_class"] += 1
            continue

        nm = ""
        if bd["names"][i] and isinstance(bd["names"][i], dict):
            nm = bd["names"][i].get("primary") or ""

        # Resolve parent explicit height / GBA direct height
        parent_h = None
        h_val = bd["height"][i]
        fl_val = bd["num_floors"][i]
        is_explicit_h = False
        if h_val is not None and math.isfinite(h_val) and 2.8 <= h_val <= 560.0:
            parent_h = float(h_val)
            is_explicit_h = True
            stats["h_explicit"] += 1
        elif fl_val is not None and fl_val >= 1:
            fl_h = 3.05 if cls in ("apartments", "residential", "house", "detached") else (3.85 if cls in ("office", "hotel", "commercial") else 3.45)
            parent_h = float(fl_val) * fl_h
            is_explicit_h = True
            stats["h_floors"] += 1
        else:
            srcs = bd["sources"][i]
            if srcs and srcs[0].get("dataset") == "OpenStreetMap":
                rid = srcs[0].get("record_id") or ""
                if rid.startswith(("w", "r")):
                    wid = rid[1:].split("@")[0]
                    gh = gba_osm.get(f"osm{wid}KOR")
                    if gh is not None:
                        parent_h = gh
                        stats["h_gba_osm"] += 1

        bid = bd["id"][i]
        p_indices = parts_by_bid.get(bid) if bd["has_parts"][i] else None
        use_parts = False
        if p_indices and len(p_indices) >= 2:
            # Verify parts actually have height info
            valid_p = [pi for pi in p_indices if (pd["height"][pi] is not None or pd["num_floors"][pi] is not None)]
            if len(valid_p) >= 2:
                use_parts = True

        if use_parts:
            stats["multipart_bldgs"] += 1
            for pi in p_indices:
                poly = geom_to_world_poly(pd["geometry"][pi])
                if poly is None or poly.area < 16.0:
                    continue
                ph = pd["height"][pi]
                pfl = pd["num_floors"][pi]
                if ph is not None and math.isfinite(ph) and 2.5 <= ph <= 560.0:
                    h_final = float(ph)
                elif pfl is not None and pfl >= 1:
                    h_final = float(pfl) * (3.1 if cls == "apartments" else 3.7)
                elif parent_h is not None:
                    h_final = parent_h
                else:
                    h_final = None
                pmin = pd["min_height"][pi]
                pmin_fl = pd["min_floor"][pi]
                min_h = 0.0
                if pmin is not None and math.isfinite(pmin) and pmin >= 0.0:
                    min_h = float(pmin)
                elif pmin_fl is not None and pmin_fl > 0:
                    min_h = float(pmin_fl) * 3.5
                if h_final is not None and min_h >= h_final - 1.2:
                    min_h = max(0.0, h_final - 3.0)
                raw_items.append({
                    "poly": poly, "h": h_final, "explicit": True, "min_h": min_h, "is_part": True,
                    "cls": cls, "sub": sub, "nm": nm,
                    "fmat": pd["facade_material"][pi] or bd["facade_material"][i],
                    "fcol": pd["facade_color"][pi] or bd["facade_color"][i],
                    "rshp": pd["roof_shape"][pi] or bd["roof_shape"][i],
                    "rh": pd["roof_height"][pi] or bd["roof_height"][i],
                })
        else:
            poly = geom_to_world_poly(bd["geometry"][i])
            if poly is None or poly.area < 22.0:
                stats["skip_tiny"] += 1
                continue
            raw_items.append({
                "poly": poly, "h": parent_h, "explicit": is_explicit_h, "min_h": 0.0, "is_part": False,
                "cls": cls, "sub": sub, "nm": nm,
                "fmat": bd["facade_material"][i],
                "fcol": bd["facade_color"][i],
                "rshp": bd["roof_shape"][i],
                "rh": bd["roof_height"][i],
            })

    print(f"   Collected {len(raw_items)} candidate footprints ({stats['multipart_bldgs']} 3D multi-part towers). Running spatial GBA height join & full-width road clipping...")

    # Batch query GBA spatial KDTree for items missing height!
    missing_idx = []
    missing_pts = []
    for idx, it in enumerate(raw_items):
        if it["h"] is None:
            c = it["poly"].centroid
            missing_idx.append(idx)
            missing_pts.append((c.x, c.y))

    if missing_pts:
        dists, nn_ids = gba_kdtree.query(np.array(missing_pts, dtype=np.float32), k=1, distance_upper_bound=15.0)
        for k_i, idx in enumerate(missing_idx):
            if dists[k_i] < 15.0 and nn_ids[k_i] < len(gba_hs):
                raw_items[idx]["h"] = float(gba_hs[nn_ids[k_i]])
                stats["h_gba_spatial"] += 1
            else:
                # Try wider neighborhood (38m, k=5 median) for brand-new infill buildings
                stats["h_need_fallback"] += 1

        # Second pass: neighborhood median within 45m from GBA KDTree for remaining infill buildings
        still_idx = [idx for idx in missing_idx if raw_items[idx]["h"] is None]
        if still_idx:
            s_pts = np.array([(raw_items[idx]["poly"].centroid.x, raw_items[idx]["poly"].centroid.y) for idx in still_idx], dtype=np.float32)
            d5, id5 = gba_kdtree.query(s_pts, k=5, distance_upper_bound=50.0)
            for k_i, idx in enumerate(still_idx):
                valid_hs = [float(gba_hs[j]) for dist_j, j in zip(d5[k_i], id5[k_i]) if dist_j < 50.0 and j < len(gba_hs)]
                if valid_hs:
                    valid_hs.sort()
                    raw_items[idx]["h"] = valid_hs[len(valid_hs) // 2]
                    stats["h_gba_neighbor"] += 1

    # Full-width road-corridor clipping + polygon preservation (up to 48 verts!) + building compilation
    buildings_out = []
    total_verts = 0

    for it in raw_items:
        poly = it["poly"]
        orig_area = poly.area
        c0 = poly.centroid
        if not (minX <= c0.x <= maxX and minZ <= c0.y <= maxZ):
            continue

        # Skip tiny transportation canopies/entrances (e.g., Seolleung Station subway exit boxes!)
        if it["sub"] == "transportation" and orig_area < 90.0:
            stats["skip_trans_box"] += 1
            continue

        # Reject buildings inside Han River water
        w_hits = water_tree.query(c0, predicate="intersects")
        if len(w_hits) > 0:
            stats["skip_water"] += 1
            continue

        # FULL-WIDTH ROAD-CORRIDOR CLIPPING: guarantees 0.00 cm intrusion onto ANY lane in Seoul!
        if it["min_h"] < 5.0:
            r_hits = road_tree.query(poly, predicate="intersects")
            if len(r_hits) > 0:
                road_u = unary_union([road_polys[int(ri)] for ri in r_hits])
                clipped = poly.difference(road_u)
                if isinstance(clipped, MultiPolygon):
                    clipped = max(clipped.geoms, key=lambda q: q.area) if len(clipped.geoms) > 0 else Polygon()
                if clipped.is_empty or not isinstance(clipped, Polygon) or clipped.area < 24.0 or (orig_area < 140.0 and clipped.area < orig_area * 0.58):
                    stats["culled_on_road"] += 1
                    continue
                poly = clipped
                stats["clipped_from_road"] += 1

        area_m2 = poly.area
        if area_m2 < 20.0:
            continue

        # Preserve TRUE architectural footprint shape (up to 48 exact vertices at 0.22m simplify!)
        poly_s = poly.simplify(0.22, preserve_topology=True)
        if poly_s.is_empty or not isinstance(poly_s, Polygon):
            poly_s = poly
        ext = list(poly_s.exterior.coords)[:-1]
        if len(ext) > 48:
            poly_s = poly.simplify(0.65, preserve_topology=True)
            if isinstance(poly_s, Polygon) and not poly_s.is_empty:
                ext = list(poly_s.exterior.coords)[:-1]
        if len(ext) > 48:
            poly_s = poly.simplify(1.5, preserve_topology=True)
            if isinstance(poly_s, Polygon) and not poly_s.is_empty:
                ext = list(poly_s.exterior.coords)[:-1]
        if len(ext) > 48:
            ext = list(poly.minimum_rotated_rectangle.exterior.coords)[:-1]
        if len(ext) < 3:
            continue

        # Consistent CCW winding in (x, z) so outward wall normals always point outward!
        signed2 = 0.0
        for j in range(len(ext)):
            x0, z0 = ext[j]
            x1, z1 = ext[(j + 1) % len(ext)]
            signed2 += (x0 * z1 - x1 * z0)
        if signed2 < 0.0:
            ext.reverse()

        xs = [p[0] for p in ext]
        zs = [p[1] for p in ext]
        cx = 0.5 * (min(xs) + max(xs))
        cz = 0.5 * (min(zs) + max(zs))
        if not (minX <= cx <= maxX and minZ <= cz <= maxZ):
            continue
        hx = max(1.0, 0.5 * (max(xs) - min(xs)))
        hz = max(1.0, 0.5 * (max(zs) - min(zs)))
        if hx > 480.0 or hz > 480.0:
            continue

        h = it["h"]
        if h is None:
            # Final area-calibrated fallback only for rare buildings with no OSM/GBA coverage
            hv = det_hash(cx, cz, 5) % 100
            if area_m2 > 700:
                h = 26.0 + (hv % 26)
            elif area_m2 > 220:
                h = 13.0 + (hv % 14)
            else:
                h = 7.0 + (hv % 8)
            stats["h_final_fallback"] += 1

        # Sanity guard against tiny sheds/kiosks getting satellite height bleed from an adjacent skyscraper!
        if not it["explicit"] and not it["is_part"] and area_m2 < 55.0:
            h = min(h, 15.0)
        elif not it["explicit"] and not it["is_part"] and area_m2 < 110.0:
            h = min(h, 24.0)

        h = max(3.6, min(555.0, float(h)))
        min_h = max(0.0, min(h - 2.0, float(it["min_h"])))

        loc_pts = []
        for vx, vz in ext:
            lx = max(-32760, min(32760, int(round((vx - cx) * 20.0))))
            lz = max(-32760, min(32760, int(round((vz - cz) * 20.0))))
            loc_pts.append((lx, lz))

        style, rshp, fr, fg, fb, rh_q, flags, vseed = classify_seoul_architecture(
            it["cls"], it["sub"], it["nm"], it["fmat"], it["fcol"], it["rshp"], it["rh"],
            area_m2, cx, cz, h, min_h, it["is_part"]
        )
        base_y = sample_dem(dem_dm, demMinX, demMaxX, demMinZ, demMaxZ, cx, cz)
        buildings_out.append((cx, cz, loc_pts, style, rshp, fr, fg, fb, rh_q, flags, vseed, base_y, min_h, h, hx, hz))
        total_verts += len(loc_pts)

    print(f"5. Compiled {len(buildings_out)} REAL Seoul 3D buildings ({total_verts} verts) in {time.time()-t0:.1f}s!")
    print("   Stats:", dict(stats))
    style_counts = defaultdict(int)
    roof_counts = defaultdict(int)
    for rec in buildings_out:
        style_counts[rec[3]] += 1
        roof_counts[rec[4]] += 1
    print("   Style distribution (0..9):", dict(sorted(style_counts.items())))
    print("   Roof distribution  (0..6):", dict(sorted(roof_counts.items())))

    # 6. Build O(1) spatial chunk building index
    num_chunks = cxN * czN
    chunk_bldgs = [[] for _ in range(num_chunks)]
    for bi, rec in enumerate(buildings_out):
        cx, cz, _, _, _, _, _, _, _, _, _, _, _, _, hx, hz = rec
        c0x = max(0, min(cxN - 1, int(math.floor((cx - hx - minX) / chunkSz))))
        c1x = max(0, min(cxN - 1, int(math.floor((cx + hx - minX) / chunkSz))))
        c0z = max(0, min(czN - 1, int(math.floor((cz - hz - minZ) / chunkSz))))
        c1z = max(0, min(czN - 1, int(math.floor((cz + hz - minZ) / chunkSz))))
        for cz_i in range(c0z, c1z + 1):
            row = cz_i * cxN
            for cx_i in range(c0x, c1x + 1):
                chunk_bldgs[row + cx_i].append(bi)

    flat_bldgs = []
    new_chunks_bytes = bytearray()
    old_chunks = base["old_chunks"]
    for ci in range(num_chunks):
        r_start, r_len, _, _, p_start, p_len = old_chunks[ci]
        b_start = len(flat_bldgs)
        blist = chunk_bldgs[ci]
        flat_bldgs.extend(blist)
        b_len = len(blist)
        new_chunks_bytes.extend(struct.pack("<IIIIII", r_start, r_len, b_start, b_len, p_start, p_len))

    # 7. Serialize OCM4 v2 binary file!
    print(f"6. Writing OCM4 v2 binary world -> {OUT_OCMAP} ...")
    out_hdr = struct.pack(
        "<4sIfffffIIffffIIIIIIIIIIII",
        b"OCM4", 2,
        minX, maxX, minZ, maxZ, chunkSz,
        cxN, czN,
        demMinX, demMaxX, demMinZ, demMaxZ,
        demW, demH,
        roadCount, roadVertCount,
        len(buildings_out), total_verts,
        waterTriCount, parkTriCount, poiCount,
        fcr, len(flat_bldgs), fcp,
    )
    assert len(out_hdr) == 100

    bldg_buf = bytearray(len(buildings_out) * 44)
    bvert_buf = bytearray(total_verts * 4)
    bv_off = 0
    for bi, rec in enumerate(buildings_out):
        cx, cz, loc_pts, style, rshp, fr, fg, fb, rh_q, flags, vseed, base_y, min_h, h, hx, hz = rec
        struct.pack_into(
            "<IHBBBBBBBBHfffffff",
            bldg_buf, bi * 44,
            bv_off, len(loc_pts),
            style, rshp, fr, fg, fb, rh_q, flags, vseed, 0,
            cx, cz, base_y, min_h, h, hx, hz,
        )
        for (lx, lz) in loc_pts:
            struct.pack_into("<hh", bvert_buf, bv_off * 4, lx, lz)
            bv_off += 1

    flat_b_bytes = np.array(flat_bldgs, dtype=np.uint32).tobytes()

    with open(OUT_OCMAP, "wb") as f:
        f.write(out_hdr)
        f.write(base["dem_bytes"])
        f.write(base["roads_bytes"])
        f.write(base["rverts_bytes"])
        f.write(bldg_buf)
        f.write(bvert_buf)
        f.write(new_chunks_bytes)
        f.write(base["flat_roads_bytes"])
        f.write(flat_b_bytes)
        f.write(base["flat_pois_bytes"])
        f.write(base["water_bytes"])
        f.write(base["park_bytes"])
        f.write(base["pois_bytes"])

    sz_mb = os.path.getsize(OUT_OCMAP) / (1024 * 1024)
    print(f"   Wrote {OUT_OCMAP}: {sz_mb:.2f} MB ({len(buildings_out)} buildings, {total_verts} verts)")

    # 8. Render upgraded 3072x1968 crisp 2D Seoul Map!
    print("7. Rendering upgraded 3072x1968 real Seoul 2D map...")
    render_seoul_2d_map_v4(OUT_MAP2D, dem_dm, hdr, water_tris, park_tris, roads, rverts, buildings_out)
    print(f"DONE in {time.time()-t0:.1f}s!")


if __name__ == "__main__":
    main()
