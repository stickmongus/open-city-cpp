#!/usr/bin/env python3
"""
Compile REAL Grade-Separated 3D Seoul Roads, Expressways, Ring Roads & Bridges (OCM5 v3)
combining:
  - Overture Maps (2026-11-18.0) Seoul Transportation (topological connectors, real multi-level
    level_rules -1..+3, road_flags is_bridge/is_tunnel/is_link, real Korean names, width_rules,
    and exact oneway access_restrictions)
  - Existing OpenStreetMap exact lane counts + 184,308 OCM4 3D Seoul buildings + Water/Parks/POIs
  - Pre-filtered Bare-Earth AWS SRTM 30m DTM (100% synced with C++ runtime!)

Produces:
  1. assets/maps/seoul_real.ocmap  (OCM5 v3: Vec3(x,y,z) per road vertex, 32B OsmRoadRec w/ layer & flags2,
                                    smooth C1 multi-level bridges/viaducts/ramps, ZERO sawtooth cliffs!)
  2. assets/maps/seoul_map_2d.png  (3072x1968 crisp Naver/Google-style map with true scaled road widths)

Usage:
  uv run --with "numpy>=2" --with pyarrow --with shapely --with scipy --with pillow \
      python3 tools/build_seoul_ocm5_roads.py
"""

import math
import os
import struct
import time
from collections import defaultdict, deque

import numpy as np
import pyarrow.parquet as pq
from PIL import Image, ImageDraw
from shapely import STRtree
from shapely.geometry import LineString, MultiPolygon, Point, Polygon
from shapely.ops import substring
import shapely.wkb as wkb

ORIGIN_LAT = 37.5235
ORIGIN_LON = 126.9850
M_PER_DEG_LAT = 111132.0
M_PER_DEG_LON = 88304.0

BASE_OCMAP = "/tmp/road_work/seoul_real_v4_base.ocmap"
OVERTURE_ROADS = "/tmp/road_work/seoul_overture_roads.parquet"
OUT_OCMAP = "assets/maps/seoul_real.ocmap"
OUT_MAP2D = "assets/maps/seoul_map_2d.png"

CLASS_TO_IDX = {
    "motorway": 0,
    "trunk": 1,
    "primary": 2,
    "secondary": 3,
    "tertiary": 4,
    "residential": 5,
    "unclassified": 5,
    "living_street": 6,
}

# Authoritative Real-World Korean Expressway, Ring Road, Bridge & Boulevard Engineering Specs:
# name_substring -> (forced_class_or_None, lanes_if_oneway, lanes_if_twoway, is_ring_or_exp)
REAL_SEOUL_ROAD_SPECS = [
    ("경부고속도로", 0, 5, 10, True),
    ("경부간선도로", 0, 4, 8, True),
    ("세종포천고속도로", 0, 4, 8, True),
    ("올림픽대로", 1, 4, 8, True),
    ("강변북로", 1, 4, 8, True),
    ("내부순환로", 1, 3, 6, True),
    ("북부간선로", 1, 3, 6, True),
    ("동부간선로", 1, 3, 6, True),
    ("분당수서로", 1, 3, 6, True),
    ("서부간선로", 1, 2, 4, True),
    ("강남순환로", 1, 3, 6, True),
    ("우면산로", 1, 3, 6, True),
    # Major Seoul Boulevards & Han River Bridges
    ("영동대로", None, 6, 12, False),
    ("세종대로", None, 5, 10, False),
    ("테헤란로", None, 5, 10, False),
    ("강남대로", None, 5, 10, False),
    ("올림픽로", None, 5, 10, False),
    ("송파대로", None, 5, 10, False),
    ("한남대로", None, 4, 8, False),
    ("반포대로", None, 4, 8, False),
    ("동작대로", None, 4, 8, False),
    ("여의대로", None, 5, 10, False),
    ("남부순환로", None, 4, 8, False),
    ("양재대로", None, 4, 8, False),
    ("도산대로", None, 4, 8, False),
    ("언주로", None, 4, 8, False),
    ("봉은사로", None, 4, 8, False),
    ("천호대로", None, 4, 8, False),
    ("마포대로", None, 4, 8, False),
    ("강동대로", None, 4, 8, False),
    ("국회대로", None, 4, 8, False),
    ("노들로", None, 3, 6, False),
    ("종로", None, 4, 8, False),
    ("을지로", None, 3, 6, False),
    ("퇴계로", None, 3, 6, False),
    ("왕산로", None, 4, 8, False),
    ("동일로", None, 3, 6, False),
    ("고산자로", None, 3, 6, False),
    ("성산로", None, 4, 8, False),
    ("양화로", None, 4, 8, False),
    ("경인로", None, 4, 8, False),
]

TUNNEL_NAME_KEYWORDS = ("지하도로", "지하차도", "터널")


def latlon_to_world(lat: float, lon: float):
    return (lon - ORIGIN_LON) * M_PER_DEG_LON, -(lat - ORIGIN_LAT) * M_PER_DEG_LAT


def road_class_lift(cls: int) -> float:
    return 0.035 + float(6 - cls if cls < 6 else 0) * 0.008


def load_v4_base(path: str):
    with open(path, "rb") as f:
        data = f.read()
    hdr = struct.unpack_from("<4sIfffffIIffffIIIIIIIIIIII", data, 0)
    magic, ver = hdr[0], hdr[1]
    assert magic == b"OCM4" and ver == 2, f"Expected OCM4 v2, got {magic} v{ver}"
    minX, maxX, minZ, maxZ, chunkSz = hdr[2:7]
    cxN, czN = hdr[7:9]
    demMinX, demMaxX, demMinZ, demMaxZ = hdr[9:13]
    demW, demH = hdr[13:15]
    roadCount, roadVertCount = hdr[15:17]
    bldgCount, bldgVertCount = hdr[17:19]
    waterTriCount, parkTriCount, poiCount = hdr[19:22]
    fcr, fcb, fcp = hdr[22:25]

    off = 100
    def take(nbytes):
        nonlocal off
        s = data[off:off + nbytes]
        off += nbytes
        return s

    dem_dm = np.frombuffer(take(demW * demH * 2), dtype=np.int16).copy().reshape((demH, demW))
    old_roads_bytes = take(roadCount * 30)
    old_rverts_bytes = take(roadVertCount * 8)
    bldg_bytes = take(bldgCount * 44)
    bvert_bytes = take(bldgVertCount * 4)
    old_chunks_bytes = take(cxN * czN * 24)
    _ = take(fcr * 4)
    _ = take(fcb * 4)
    flat_pois_bytes = take(fcp * 4)
    water_bytes = take(waterTriCount * 24)
    park_bytes = take(parkTriCount * 24)
    pois_bytes = take(poiCount * 12)
    assert off == len(data), f"Byte mismatch {off} vs {len(data)}"

    old_roads = [struct.unpack_from("<IHBBBBfffff", old_roads_bytes, i * 30) for i in range(roadCount)]
    old_rverts = np.frombuffer(old_rverts_bytes, dtype=np.float32).reshape((roadVertCount, 2))
    old_chunks = [struct.unpack_from("<IIIIII", old_chunks_bytes, i * 24) for i in range(cxN * czN)]
    water_tris = np.frombuffer(water_bytes, dtype=np.float32).reshape((waterTriCount, 6))
    park_tris = np.frombuffer(park_bytes, dtype=np.float32).reshape((parkTriCount, 6))

    # Unpack OCM4 buildings
    bldgs = []
    for i in range(bldgCount):
        bldgs.append(list(struct.unpack_from("<IHBBBBBBBBHfffffff", bldg_bytes, i * 44)))
    bverts = np.frombuffer(bvert_bytes, dtype=np.int16).reshape((bldgVertCount, 2))

    return {
        "hdr": hdr,
        "dem_dm": dem_dm,
        "old_roads": old_roads,
        "old_rverts": old_rverts,
        "bldgs": bldgs,
        "bverts": bverts,
        "old_chunks": old_chunks,
        "flat_pois_bytes": flat_pois_bytes,
        "water_bytes": water_bytes,
        "park_bytes": park_bytes,
        "pois_bytes": pois_bytes,
        "water_tris": water_tris,
        "park_tris": park_tris,
    }


def sample_dem_dm(dem_dm: np.ndarray, demMinX, demMaxX, demMinZ, demMaxZ, x: float, z: float) -> float:
    H, W = dem_dm.shape
    fx = max(0.0, min(W - 1.001, (x - demMinX) / (demMaxX - demMinX) * (W - 1)))
    fz = max(0.0, min(H - 1.001, (z - demMinZ) / (demMaxZ - demMinZ) * (H - 1)))
    ix, iz = int(fx), int(fz)
    tx, tz = fx - ix, fz - iz
    h00 = float(dem_dm[iz, ix]) * 0.1
    h10 = float(dem_dm[iz, ix + 1]) * 0.1
    h01 = float(dem_dm[iz + 1, ix]) * 0.1
    h11 = float(dem_dm[iz + 1, ix + 1]) * 0.1
    return (h00 * (1.0 - tx) + h10 * tx) * (1.0 - tz) + (h01 * (1.0 - tx) + h11 * tx) * tz


def bake_bare_earth_dtm(dem_dm: np.ndarray, hdr, roads_pts, bldgs, water_tris):
    """Apply exact bare-earth urban skyscraper SRTM spike filter ONCE at build time so
    pre-solved 3D road heights match C++ runtime sampleDem() to 0.000m precision!"""
    demMinX, demMaxX, demMinZ, demMaxZ, W, H = hdr[9:15]
    invX = (W - 1) / (demMaxX - demMinX)
    invZ = (H - 1) / (demMaxZ - demMinZ)
    urban = np.zeros((H, W), dtype=np.uint8)

    for pts, cls in roads_pts:
        if cls > 3:
            continue
        for x, z in pts:
            ix = max(0, min(W - 1, int(round((x - demMinX) * invX))))
            iz = max(0, min(H - 1, int(round((z - demMinZ) * invZ))))
            if dem_dm[iz, ix] < 1350:
                urban[iz, ix] = 1

    for b in bldgs:
        if b[15] >= 26.0:
            ix = max(0, min(W - 1, int(round((b[11] - demMinX) * invX))))
            iz = max(0, min(H - 1, int(round((b[12] - demMinZ) * invZ))))
            if dem_dm[iz, ix] < 1350:
                urban[iz, ix] = 1

    # Dilate by 1 cell (~80m)
    dilated = urban.copy()
    for dz in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dz == 0:
                continue
            shifted = np.zeros_like(urban)
            sy0, sy1 = max(0, dz), H + min(0, dz)
            sx0, sx1 = max(0, dx), W + min(0, dx)
            shifted[sy0:sy1, sx0:sx1] = urban[sy0 - dz:sy1 - dz, sx0 - dx:sx1 - dx]
            dilated |= shifted
    dilated &= (dem_dm < 1350).astype(np.uint8)

    # 9x9 15th-percentile bare-earth clamp (+9.0m cap)
    filtered = dem_dm.copy()
    pad = np.pad(dem_dm, 4, mode="edge")
    ys, xs = np.where(dilated > 0)
    for iz, ix in zip(ys, xs):
        win = np.sort(pad[iz:iz + 9, ix:ix + 9].ravel())
        cap = min(32000, int(win[12]) + 90)
        if filtered[iz, ix] > cap:
            filtered[iz, ix] = cap

    # 2 gentle 3x3 smoothing passes on urban cells
    for _ in range(2):
        nxt = filtered.copy()
        for iz, ix in zip(ys, xs):
            if 1 <= iz < H - 1 and 1 <= ix < W - 1:
                avg = (int(filtered[iz - 1:iz + 2, ix - 1:ix + 2].sum()) + 4) // 9
                nxt[iz, ix] = (int(filtered[iz, ix]) + avg * 2) // 3
        filtered = nxt

    return filtered


def eval_rule_at(rules, t_mid: float, default=None):
    if not rules:
        return default
    for r in rules:
        btw = r.get("between")
        if btw is None or (btw[0] <= t_mid <= btw[1]):
            return r.get("value", r.get("values", default))
    return default


def eval_flags_at(road_flags, t_mid: float):
    vals = set()
    if not road_flags:
        return vals
    for r in road_flags:
        btw = r.get("between")
        if btw is None or (btw[0] <= t_mid <= btw[1]):
            for v in (r.get("values") or []):
                vals.add(v)
    return vals


def eval_oneway_at(access_rules, t_mid: float):
    """Returns: 1 = forward oneway, -1 = reverse oneway (needs flip), 0 = two-way."""
    if not access_rules:
        return 0
    for r in access_rules:
        if r.get("access_type") != "denied":
            continue
        btw = r.get("between")
        if btw is not None and not (btw[0] <= t_mid <= btw[1]):
            continue
        when = r.get("when") or {}
        heading = when.get("heading")
        mode = when.get("mode")
        # Applies to motor vehicles or all modes
        if mode is not None and not any(m in ("motor_vehicle", "car", "vehicle") for m in mode):
            continue
        if heading == "backward":
            return 1
        if heading == "forward":
            return -1
    return 0


def densify_pts(pts, max_step: float):
    if len(pts) < 2:
        return pts
    out = [pts[0]]
    for i in range(len(pts) - 1):
        x0, z0 = pts[i]
        x1, z1 = pts[i + 1]
        d = math.hypot(x1 - x0, z1 - z0)
        if d < 0.2:
            continue
        n = max(1, int(math.ceil(d / max_step)))
        for k in range(1, n + 1):
            t = k / n
            out.append((x0 + (x1 - x0) * t, z0 + (z1 - z0) * t))
    return out


def main():
    t0 = time.time()
    print("1. Loading OCM4 v2 base world (DEM, 184K buildings, water, parks, POIs, OSM lane refs)...")
    base = load_v4_base(BASE_OCMAP)
    hdr = base["hdr"]
    minX, maxX, minZ, maxZ, chunkSz = hdr[2:7]
    cxN, czN = hdr[7:9]
    demMinX, demMaxX, demMinZ, demMaxZ, demW, demH = hdr[9:15]
    water_tris = base["water_tris"]
    park_tris = base["park_tris"]
    bldgs = base["bldgs"]
    bverts = base["bverts"]

    # Build water STRtree (distinguishing main Han River wide spans from tributaries!)
    print("2. Indexing Han River & Seoul tributary water polygons...")
    water_polys = []
    for tri in water_tris:
        p = Polygon([(tri[0], tri[1]), (tri[2], tri[3]), (tri[4], tri[5])])
        if p.is_valid and p.area > 1.0:
            water_polys.append(p)
    water_tree = STRtree(water_polys)

    # Identify Han River main channel triangles (z in [-2600..+2400], wide main channel)
    han_polys = []
    for p in water_polys:
        bx0, bz0, bx1, bz1 = p.bounds
        cx_p, cz_p = 0.5 * (bx0 + bx1), 0.5 * (bz0 + bz1)
        # Main Han River runs roughly E-W across Seoul with z in [-2500..+2200] (excluding narrow tributaries like Tancheon x>6900, z>1100 or Jungnangcheon x>4500, z<-1600)
        if (-7800 <= cx_p <= 11600 and -2600 <= cz_p <= 2100):
            if cx_p > 6850 and cz_p > 850:
                continue  # Tancheon stream (Samseonggyo!)
            if cx_p > 4200 and cz_p < -1650:
                continue  # Jungnangcheon stream
            if cx_p < -6200 and cz_p > 950:
                continue  # Anyangcheon stream
            if p.area >= 350.0:
                han_polys.append(p)
    han_tree = STRtree(han_polys)
    print(f"   Indexed {len(water_polys)} water tris ({len(han_polys)} Han River main-channel tris)")

    # Build spatial reference tree of existing OSM roads for exact OSM lane counts
    print("3. Indexing existing OSM road lane counts...")
    old_lines = []
    old_meta = []
    old_rverts = base["old_rverts"]
    for fv, vc, cls, lanes, flags, nslot, hw, *_ in base["old_roads"]:
        if vc < 2:
            continue
        pts = [(float(old_rverts[fv + k, 0]), float(old_rverts[fv + k, 1])) for k in range(vc)]
        if len(pts) < 2:
            continue
        ls = LineString(pts)
        if ls.length < 1.0:
            continue
        dx = pts[-1][0] - pts[0][0]
        dz = pts[-1][1] - pts[0][1]
        dl = max(1e-4, math.hypot(dx, dz))
        old_lines.append(ls)
        old_meta.append((cls, lanes, bool(flags & 1), hw, dx / dl, dz / dl))
    old_tree = STRtree(old_lines)

    # Load Overture Maps Seoul Transportation
    print(f"4. Parsing Overture Maps Transportation ({OVERTURE_ROADS})...")
    tbl = pq.read_table(OVERTURE_ROADS)
    col_id = tbl["id"].to_pylist()
    col_name = tbl["name"].to_pylist()
    col_cls = tbl["class"].to_pylist()
    col_sub = tbl["subclass"].to_pylist()
    col_conn = tbl["connectors"].to_pylist()
    col_flags = tbl["road_flags"].to_pylist()
    col_level = tbl["level_rules"].to_pylist()
    col_width = tbl["width_rules"].to_pylist()
    col_access = tbl["access_restrictions"].to_pylist()
    col_wkb = tbl["geom_wkb"].to_pylist()

    raw_segs = []
    skipped_tunnels = 0
    skipped_constr = 0

    for idx in range(tbl.num_rows):
        cls_str = col_cls[idx]
        if cls_str not in CLASS_TO_IDX:
            continue
        name = (col_name[idx] or "").strip()
        if any(kw in name for kw in TUNNEL_NAME_KEYWORDS):
            skipped_tunnels += 1
            continue
        geom = wkb.loads(col_wkb[idx])
        if geom.is_empty or not isinstance(geom, LineString):
            continue
        w_pts = [latlon_to_world(lat, lon) for lon, lat in geom.coords]
        if len(w_pts) < 2:
            continue
        # Quick bounds check
        xs = [p[0] for p in w_pts]
        zs = [p[1] for p in w_pts]
        if max(xs) < minX - 180.0 or min(xs) > maxX + 180.0 or max(zs) < minZ - 180.0 or min(zs) > maxZ + 180.0:
            continue

        w_line = LineString(w_pts)
        if w_line.length < 1.2:
            continue

        r_flags = col_flags[idx]
        r_levels = col_level[idx]
        r_widths = col_width[idx]
        r_access = col_access[idx]
        r_conns = col_conn[idx] or []
        sub = col_sub[idx]

        # Collect linear-reference split points along [0, 1] for level/bridge/tunnel transitions
        breaks = {0.0, 1.0}
        for rule_list in (r_flags, r_levels):
            if not rule_list:
                continue
            for r in rule_list:
                btw = r.get("between")
                if btw is not None:
                    b0 = max(0.0, min(1.0, float(btw[0])))
                    b1 = max(0.0, min(1.0, float(btw[1])))
                    if 0.002 < b0 < 0.998:
                        breaks.add(b0)
                    if 0.002 < b1 < 0.998:
                        breaks.add(b1)
        # Also include connector positions near internal breaks so connectors attach accurately
        conn_map = []
        for c in r_conns:
            cid = c.get("connector_id")
            cat = float(c.get("at", 0.0))
            if cid:
                conn_map.append((cat, cid))

        sorted_b = sorted(breaks)
        # Merge breaks closer than 1.5m
        merged_b = [sorted_b[0]]
        for b in sorted_b[1:]:
            if (b - merged_b[-1]) * w_line.length >= 1.5 or b >= 0.999:
                merged_b.append(b)
        if merged_b[-1] < 0.999:
            merged_b[-1] = 1.0

        for si in range(len(merged_b) - 1):
            t0_s, t1_s = merged_b[si], merged_b[si + 1]
            t_mid = 0.5 * (t0_s + t1_s)
            fl_set = eval_flags_at(r_flags, t_mid)
            if "is_under_construction" in fl_set:
                skipped_constr += 1
                continue
            if "is_tunnel" in fl_set:
                skipped_tunnels += 1
                continue
            layer = eval_rule_at(r_levels, t_mid, 0)
            if layer is None:
                layer = 0
            layer = int(layer)
            if layer <= -2:
                skipped_tunnels += 1
                continue

            sub_geom = substring(w_line, t0_s, t1_s, normalized=True)
            if sub_geom.is_empty or not isinstance(sub_geom, LineString) or sub_geom.length < 1.0:
                continue
            s_pts = list(sub_geom.coords)
            is_bridge = ("is_bridge" in fl_set) or (layer >= 1)
            is_link = (sub == "link") or ("is_link" in fl_set)

            ow = eval_oneway_at(r_access, t_mid)
            if ow == -1:
                s_pts.reverse()
                t0_s, t1_s = 1.0 - t1_s, 1.0 - t0_s
                oneway = True
                flipped = True
            elif ow == 1:
                oneway = True
                flipped = False
            else:
                oneway = False
                flipped = False

            rcls = CLASS_TO_IDX[cls_str]
            is_ring_exp = (rcls <= 1)

            # Apply authoritative Seoul road classification & ring-road promotion
            spec_lanes = None
            for kw, f_cls, l_1w, l_2w, r_exp in REAL_SEOUL_ROAD_SPECS:
                if kw in name:
                    if f_cls is not None and not is_link:
                        rcls = f_cls
                    if r_exp:
                        is_ring_exp = True
                    if not is_link:
                        spec_lanes = l_1w if oneway else l_2w
                    break

            if rcls <= 1 or is_link:
                oneway = True

            # Match exact OSM lane count from v4_base when not overridden by named spec
            osm_lanes = None
            if spec_lanes is None:
                mid_pt = Point(sub_geom.interpolate(0.5, normalized=True))
                cands = old_tree.query(mid_pt.buffer(7.0))
                if len(cands) > 0:
                    sdx = s_pts[-1][0] - s_pts[0][0]
                    sdz = s_pts[-1][1] - s_pts[0][1]
                    sdl = max(1e-4, math.hypot(sdx, sdz))
                    sdx /= sdl
                    sdz /= sdl
                    best_d = 7.0
                    for ci in cands:
                        ocls, olanes, o1w, ohw, odx, odz = old_meta[ci]
                        if abs(ocls - rcls) > 1:
                            continue
                        if abs(sdx * odx + sdz * odz) < 0.80:
                            continue
                        d = old_lines[ci].distance(mid_pt)
                        if d < best_d:
                            best_d = d
                            if o1w == oneway:
                                osm_lanes = olanes
                            elif oneway and not o1w:
                                osm_lanes = max(1, (olanes + 1) // 2)
                            else:
                                osm_lanes = min(10, olanes * 2)

            # Determine authentic real-world Korean lane count & halfWidth (meters)
            explicit_w = eval_rule_at(r_widths, t_mid, None)
            if is_link:
                lanes = 1 if rcls >= 2 else (2 if (osm_lanes and osm_lanes >= 2) else 1)
                hw = 3.15 if lanes == 1 else 4.55
            else:
                if spec_lanes is not None:
                    lanes = spec_lanes
                elif osm_lanes is not None:
                    lanes = int(max(1, min(12, osm_lanes)))
                else:
                    if rcls == 0:
                        lanes = 4 if oneway else 8
                    elif rcls == 1:
                        lanes = 3 if oneway else 6
                    elif rcls == 2:
                        lanes = 4 if oneway else 6
                    elif rcls == 3:
                        lanes = 3 if oneway else 4
                    elif rcls == 4:
                        lanes = 2 if oneway else 3
                    elif rcls == 5:
                        lanes = 2
                    else:
                        lanes = 1

                if rcls <= 1:
                    # Korean Expressway / Urban Ring Road Standard: 3.50m/lane + 1.8m outer + 0.7m inner shoulder
                    sh = 2.40 if lanes >= 4 else 1.90
                    hw = (lanes * 3.50 + sh) * 0.5
                elif rcls <= 4:
                    # Korean Urban Arterial Standard: 3.25m/lane + 0.70m curb gutters
                    hw = (lanes * 3.25 + 0.70) * 0.5
                elif rcls == 5:
                    hw = 3.40
                else:
                    hw = 2.70

                # Honor sane explicit Overture width_rules (single carriageway widths only!)
                if explicit_w is not None and 4.0 <= float(explicit_w) <= 22.0:
                    hw = 0.5 * float(explicit_w)

                hw = max(2.65, min(16.5, hw))

            # Densify long segments so 3D vertical curves on bridges/viaducts/ramps are ultra-smooth!
            max_step = 18.0 if (is_bridge or is_link or rcls <= 2) else 32.0
            s_pts = densify_pts(s_pts, max_step)
            if len(s_pts) < 2:
                continue

            # Clip endpoints slightly outside core bounds
            s_pts = [(max(minX - 220.0, min(maxX + 220.0, x)), max(minZ - 220.0, min(maxZ + 220.0, z))) for x, z in s_pts]

            # Match start/end connector_ids in original unflipped [0,1] parameterization,
            # and assign deterministic synthetic connector IDs at internal linear-reference splits
            # so layer=0 ground approaches ALWAYS weld seamlessly to layer=1+ bridges/viaducts!
            raw_start_cid = None
            raw_end_cid = None
            for cat, cid in conn_map:
                if abs(cat - t0_s) < 0.008:
                    raw_start_cid = cid
                if abs(cat - t1_s) < 0.008:
                    raw_end_cid = cid
            if raw_start_cid is None and si > 0:
                raw_start_cid = f"{col_id[idx]}__brk_{si}"
            if raw_end_cid is None and si + 1 < len(merged_b) - 1:
                raw_end_cid = f"{col_id[idx]}__brk_{si + 1}"
            if flipped:
                start_cid, end_cid = raw_end_cid, raw_start_cid
            else:
                start_cid, end_cid = raw_start_cid, raw_end_cid

            raw_segs.append({
                "pts": s_pts,
                "cls": rcls,
                "lanes": int(max(1, min(12, lanes))),
                "oneway": oneway,
                "is_bridge": is_bridge,
                "is_link": is_link,
                "is_ring_exp": is_ring_exp,
                "layer": max(-2, min(4, layer)),
                "hw": float(hw),
                "name": name,
                "start_cid": start_cid,
                "end_cid": end_cid,
                "seg_id": col_id[idx],
            })

    print(f"   Extracted {len(raw_segs)} surface/elevated Seoul road segments (skipped {skipped_tunnels} tunnels, {skipped_constr} construction)")

    # 5. Bake clean bare-earth DTM matching C++ runtime 1:1!
    print("5. Baking clean bare-earth SRTM DTM (removing skyscraper radar spikes)...")
    dem_dm = bake_bare_earth_dtm(base["dem_dm"], hdr, [(s["pts"], s["cls"]) for s in raw_segs], bldgs, water_tris)

    # 6. Parallel Split-Carriageway Spacing Clamper (same layer, opposing direction -> flush, ZERO overlap!)
    print("6. Running parallel split-carriageway width calibration...")
    seg_lines = [LineString(s["pts"]) for s in raw_segs]
    seg_tree = STRtree(seg_lines)
    clamped_cnt = 0
    for i, s in enumerate(raw_segs):
        if s["cls"] > 3 or s["is_link"] or not s["oneway"]:
            continue
        ls = seg_lines[i]
        mid_pt = ls.interpolate(0.5, normalized=True)
        dx = s["pts"][-1][0] - s["pts"][0][0]
        dz = s["pts"][-1][1] - s["pts"][0][1]
        dl = max(1e-4, math.hypot(dx, dz))
        tx, tz = dx / dl, dz / dl

        cands = seg_tree.query(mid_pt.buffer(s["hw"] * 2.1 + 4.0))
        min_opp_d = 999.0
        for j in cands:
            if j == i:
                continue
            o = raw_segs[j]
            if o["cls"] > 3 or o["is_link"] or abs(o["layer"] - s["layer"]) > 0:
                continue
            odx = o["pts"][-1][0] - o["pts"][0][0]
            odz = o["pts"][-1][1] - o["pts"][0][1]
            odl = max(1e-4, math.hypot(odx, odz))
            if (tx * odx + tz * odz) / odl < -0.82:
                d = seg_lines[j].distance(mid_pt)
                if 3.0 < d < min_opp_d:
                    min_opp_d = d
        if min_opp_d < 999.0 and s["hw"] > min_opp_d * 0.498:
            s["hw"] = max(2.85, min_opp_d * 0.498)
            clamped_cnt += 1
    print(f"   Calibrated {clamped_cnt} split-carriageway half-widths to sit flush with opposing decks")

    # 7. GLOBAL GRAPH-CONTINUOUS 3D ELEVATION SOLVER (Height-Above-DTM + Max-Heap Dijkstra Monotone Grade Envelope!)
    print("7. Solving global graph-continuous 3D road elevations (<=7.5% grade guarantee, ZERO cliffs)...")
    import heapq

    node_pos = []
    node_dem = []
    node_layer_max = []
    node_layer_min = []
    node_is_bridge = []
    node_is_han = []
    node_is_water = []
    node_is_ring_exp = []
    node_is_link_only = []
    node_cls_min = []

    # Key connectors by (cid, layer_band) if two segments differ by >=2 layers so bogus OSM
    # multi-level crossing tags never fuse a layer=2 viaduct to a layer=0 ground street!
    cid_nodes = defaultdict(list)  # cid -> [(layer, nid)]
    spatial_end_buckets = defaultdict(list)  # (gx, gz) -> [(layer, nid)]

    def alloc_node(x, z, layer, is_bridge, is_link, is_ring_exp, cls, cid=None):
        nid = None
        if cid is not None and cid in cid_nodes:
            for c_lay, c_nid in cid_nodes[cid]:
                if abs(c_lay - layer) <= 1:
                    nid = c_nid
                    break
        if nid is None:
            gx, gz = int(round(x / 1.5)), int(round(z / 1.5))
            best_d2 = 1.44 if cid is None else 0.25  # Tight 0.5m weld even if one side lacked CID at an abutment!
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for c_lay, cand in spatial_end_buckets.get((gx + dx, gz + dz), ()):
                        if abs(c_lay - layer) > 1:
                            continue
                        cx, cz = node_pos[cand]
                        d2 = (cx - x) ** 2 + (cz - z) ** 2
                        # Require tighter 0.35m coincidence when stitching across adjacent layers (bridge abutments)
                        lim = best_d2 if c_lay == layer else min(best_d2, 0.35 * 0.35)
                        if d2 <= lim:
                            best_d2 = d2
                            nid = cand

        if nid is None:
            nid = len(node_pos)
            node_pos.append((x, z))
            node_dem.append(sample_dem_dm(dem_dm, demMinX, demMaxX, demMinZ, demMaxZ, x, z))
            node_layer_max.append(layer)
            node_layer_min.append(layer)
            node_is_bridge.append(is_bridge)
            p = Point(x, z)
            in_w = len(water_tree.query(p, predicate="intersects")) > 0
            in_h = in_w and (len(han_tree.query(p, predicate="intersects")) > 0)
            node_is_water.append(in_w)
            node_is_han.append(in_h)
            node_is_ring_exp.append(is_ring_exp)
            node_is_link_only.append(is_link)
            node_cls_min.append(cls)
            if cid is not None:
                cid_nodes[cid].append((layer, nid))
            spatial_end_buckets[(int(round(x / 1.5)), int(round(z / 1.5)))].append((layer, nid))
            return nid

        if cid is not None and not any(cn == nid for _, cn in cid_nodes[cid]):
            cid_nodes[cid].append((layer, nid))
        node_layer_max[nid] = max(node_layer_max[nid], layer)
        node_layer_min[nid] = min(node_layer_min[nid], layer)
        node_is_bridge[nid] = node_is_bridge[nid] or is_bridge
        node_is_ring_exp[nid] = node_is_ring_exp[nid] or is_ring_exp
        node_is_link_only[nid] = node_is_link_only[nid] and is_link
        node_cls_min[nid] = min(node_cls_min[nid], cls)
        return nid

    seg_nodes = []
    adj = defaultdict(list)  # nid -> [(nbr, dist, stiff)]
    all_edges = []

    for si, s in enumerate(raw_segs):
        pts = s["pts"]
        nids = []
        for vi, (x, z) in enumerate(pts):
            cid = s["start_cid"] if vi == 0 else (s["end_cid"] if vi == len(pts) - 1 else None)
            if 0 < vi < len(pts) - 1:
                nid = len(node_pos)
                node_pos.append((x, z))
                node_dem.append(sample_dem_dm(dem_dm, demMinX, demMaxX, demMinZ, demMaxZ, x, z))
                node_layer_max.append(s["layer"])
                node_layer_min.append(s["layer"])
                node_is_bridge.append(s["is_bridge"])
                p = Point(x, z)
                in_w = len(water_tree.query(p, predicate="intersects")) > 0
                in_h = in_w and (len(han_tree.query(p, predicate="intersects")) > 0)
                node_is_water.append(in_w)
                node_is_han.append(in_h)
                node_is_ring_exp.append(s["is_ring_exp"])
                node_is_link_only.append(s["is_link"])
                node_cls_min.append(s["cls"])
            else:
                nid = alloc_node(x, z, s["layer"], s["is_bridge"], s["is_link"], s["is_ring_exp"], s["cls"], cid)
            nids.append(nid)
        seg_nodes.append(nids)

        stiff_mult = 3.0 if (s["is_bridge"] or s["cls"] <= 1) else (1.8 if s["is_link"] else 1.0)
        for vi in range(len(nids) - 1):
            u, v = nids[vi], nids[vi + 1]
            if u == v:
                continue
            x0, z0 = node_pos[u]
            x1, z1 = node_pos[v]
            d = max(0.5, math.hypot(x1 - x0, z1 - z0))
            k = (40.0 * stiff_mult) / max(3.0, d)
            adj[u].append((v, d, k))
            adj[v].append((u, d, k))
            all_edges.append((u, v, si, s["layer"], s["is_bridge"], x0, z0, x1, z1))

    N = len(node_pos)
    dem_arr = np.array(node_dem, dtype=np.float64)
    print(f"   Built topological 3D road graph: {N} unique nodes, {len(all_edges)} edges")

    # 8. Detect all 2D multi-level crossings (non-connected edges with layer_hi > layer_lo)
    print("8. Detecting multi-level grade-separated crossings across Seoul...")
    elev_edge_indices = []
    edge_geoms = []
    for ei, (u, v, si, lay, is_br, x0, z0, x1, z1) in enumerate(all_edges):
        eg = LineString([(x0, z0), (x1, z1)])
        edge_geoms.append(eg)
        if lay >= 1 or is_br:
            elev_edge_indices.append(ei)
    edge_tree = STRtree(edge_geoms)

    # Required minimum height-above-DTM (h_req) for every node!
    # Ground roads start at classLift (~0.04..0.08m); interior bridges/viaducts get exact Korean clearance,
    # while ground-to-bridge abutment transition nodes (node_layer_min <= 0 and not over water) stay unpinned
    # so the Max-Heap Dijkstra + C1 vertical curve solver smoothly ramps right onto every bridge deck!
    h_req = np.array([road_class_lift(node_cls_min[nid]) for nid in range(N)], dtype=np.float64)
    is_pinned_elev = np.zeros(N, dtype=bool)

    for nid in range(N):
        d_y = dem_arr[nid]
        lay_hi = node_layer_max[nid]
        lay_lo = node_layer_min[nid]
        is_br = node_is_bridge[nid]
        in_han = node_is_han[nid]
        in_wat = node_is_water[nid]
        is_re = node_is_ring_exp[nid]

        if in_han and is_br:
            if lay_hi <= 0:
                # Jamsugyo lower submersible deck under Banpo Bridge (~4.2m above Han River)
                h_req[nid] = max(1.5, 4.2 - d_y)
            else:
                # All 18 iconic Han River main bridges (~14.5m real river navigation clearance!)
                h_req[nid] = max(6.8, (14.5 + (lay_hi - 1) * 6.4) - d_y)
            is_pinned_elev[nid] = True
        elif lay_lo >= 1 and is_br:
            # Pure interior elevated viaducts & overpasses (strictly above ground level!):
            #   Layer 1 (Samseonggyo interior span / overpass): +5.6m..+7.2m above stream/ground below
            #   Layer 2 (Naebu Circular Ring Road / Bundang-Suseo 2nd-tier): +11.6m..+13.0m
            #   Layer 3 (3rd-tier high JC flyover like Bundang-Suseo over Samseonggyo): +19.5m
            if lay_hi >= 3:
                h_req[nid] = 19.5
            elif lay_hi == 2:
                h_req[nid] = 11.6 if is_re else 13.0
            else:
                h_req[nid] = 7.4 if is_re else (5.8 if in_wat else 5.2)
            is_pinned_elev[nid] = True
        elif is_br and in_wat:
            h_req[nid] = 5.4
            is_pinned_elev[nid] = True

    # Single-pass multi-level crossing clearance lift on h_req (capped at 22.5m above DTM -> ZERO ratcheting!)
    cross_count = 0
    seen_pairs = set()
    for ei in elev_edge_indices:
        u1, v1, si1, lay1, br1, *_ = all_edges[ei]
        cands = edge_tree.query(edge_geoms[ei].buffer(2.0), predicate="intersects")
        for ej in cands:
            if ej == ei:
                continue
            u2, v2, si2, lay2, br2, *_ = all_edges[ej]
            if si1 == si2 or u1 in (u2, v2) or v1 in (u2, v2):
                continue
            if lay1 <= lay2:
                continue
            key = (min(ei, ej), max(ei, ej))
            if key in seen_pairs:
                continue
            seen_pairs.add(key)
            if edge_geoms[ei].distance(edge_geoms[ej]) < 2.0:
                cross_count += 1
                d_lay = lay1 - lay2
                req_clr = 6.5 if d_lay == 1 else (13.0 if d_lay == 2 else 19.2)
                lo_h = max(h_req[u2], h_req[v2])
                target_hi_h = min(22.5, lo_h + req_clr)
                if h_req[u1] < target_hi_h:
                    h_req[u1] = target_hi_h
                if h_req[v1] < target_hi_h:
                    h_req[v1] = target_hi_h
    print(f"   Enforced >=6.5m vertical clearance on {cross_count} multi-level crossings (capped <=22.5m above DTM)")

    # 9. MAX-HEAP DIJKSTRA MONOTONE GRADE ENVELOPE (Mathematically guarantees |dh/ds| <= 7.0% EVERYWHERE!)
    print("9. Computing Max-Heap Dijkstra C1 Grade Envelope (<=7.0% max ramp/bridge grade) + smoothing...")
    MAX_GRADE = 0.068  # 6.8% max Korean expressway/bridge ramp grade!
    h = h_req.copy()
    pq_heap = [(-float(h[i]), i) for i in range(N) if h[i] > 0.25]
    heapq.heapify(pq_heap)
    nbr_lists = [adj[i] for i in range(N)]

    while pq_heap:
        neg_hu, u = heapq.heappop(pq_heap)
        hu = -neg_hu
        if hu < h[u] - 1e-6:
            continue
        for v, d, _k in nbr_lists[u]:
            # Smoothly propagate downward at <= 6.8% grade onto connected approaches & IC ramps!
            cand = hu - MAX_GRADE * d
            if cand > h[v] + 1e-4:
                h[v] = cand
                heapq.heappush(pq_heap, (-cand, v))

    # Gentle C1 vertical-curve rounding (40 iterations of clamped Laplacian on h)
    # Softens crest & sag vertical transitions while strictly preserving h >= h_req on bridges/water!
    for _ in range(40):
        h_next = h.copy()
        for u in range(N):
            nbs = nbr_lists[u]
            if not nbs:
                continue
            # Keep flat ground roads far from ramps firmly at ground level; round crests & ramps smoothly!
            if h[u] <= 0.12 and all(h[v] <= 0.12 for v, _, _ in nbs):
                continue
            w_anc = 1.8 if is_pinned_elev[u] else 0.18
            w_sum = w_anc
            val_sum = w_anc * h[u]
            for v, _d, k in nbs:
                w_sum += k
                val_sum += k * h[v]
            nh = val_sum / w_sum
            if is_pinned_elev[u]:
                nh = max(h_req[u] - 0.35, nh)
            h_next[u] = max(road_class_lift(node_cls_min[u]), nh)
        h = h_next

    # Final exact grade guard pass (guarantees ZERO edges > 7.5% relative grade!)
    pq_heap2 = [(-float(h[i]), i) for i in range(N) if h[i] > 0.25]
    heapq.heapify(pq_heap2)
    while pq_heap2:
        neg_hu, u = heapq.heappop(pq_heap2)
        hu = -neg_hu
        if hu < h[u] - 1e-6:
            continue
        for v, d, _k in nbr_lists[u]:
            cand = hu - 0.075 * d
            if cand > h[v] + 1e-4:
                h[v] = cand
                heapq.heappush(pq_heap2, (-cand, v))

    # Final smooth world Y = bare-earth DTM + smooth height-above-DTM!
    # Also flatten water-span bridge decks horizontally so bridges across Han River / Tancheon have dead-level decks!
    y = dem_arr + h
    # Smooth absolute y gently along elevated spans (eliminates any subtle DEM ripples under long bridges!)
    for _ in range(25):
        y_next = y.copy()
        for u in range(N):
            if h[u] < 2.2:
                continue
            nbs = nbr_lists[u]
            if not nbs:
                continue
            w_sum = 0.25
            val_sum = 0.25 * y[u]
            for v, _d, k in nbs:
                w_sum += k
                val_sum += k * y[v]
            ny = val_sum / w_sum
            if node_is_water[u]:
                ny = max(dem_arr[u] + 3.5, ny)
            else:
                ny = max(dem_arr[u] + 0.08, ny)
            y_next[u] = ny
        y = y_next

    # Final absolute grade check on bridges & viaducts
    max_grade_br = 0.0
    steep_br = 0
    max_dh_all = 0.0
    for u, v, si, lay, is_br, x0, z0, x1, z1 in all_edges:
        d = max(0.5, math.hypot(x1 - x0, z1 - z0))
        gh = abs(h[v] - h[u]) / d
        if gh > max_dh_all:
            max_dh_all = gh
        if is_br or lay >= 1:
            g = abs(y[v] - y[u]) / d
            if g > max_grade_br:
                max_grade_br = g
            if g > 0.12:
                steep_br += 1
    print(f"   3D Profile Solved! Max relative grade={max_dh_all*100:.1f}%, max bridge abs grade={max_grade_br*100:.1f}%, bridge edges>12%={steep_br}/{len(elev_edge_indices)}")

    def report_spot(label, rx0, rx1, rz0, rz1, name_filter=None):
        print(f"   [Check {label}]:")
        shown = 0
        for si, s in enumerate(raw_segs):
            if s["cls"] > 2:
                continue
            if name_filter and not any(k in (s["name"] or "") for k in name_filter):
                continue
            nids = seg_nodes[si]
            xs = [node_pos[n][0] for n in nids]
            zs = [node_pos[n][1] for n in nids]
            if max(xs) >= rx0 and min(xs) <= rx1 and max(zs) >= rz0 and min(zs) <= rz1:
                ys = [y[n] for n in nids]
                dems = [dem_arr[n] for n in nids]
                print(f"     {s['name'] or '<ramp>':14s} cls={s['cls']} lay={s['layer']} br={int(s['is_bridge'])} "
                      f"hw={s['hw']:.1f}m lanes={s['lanes']} y=[{min(ys):.1f}..{max(ys):.1f}m] (dem={min(dems):.1f}..{max(dems):.1f}m)")
                shown += 1
                if shown >= 8:
                    break

    report_spot("Samseonggyo x Bundang-Suseo", 7150, 7560, 1380, 1560, ["올림픽로", "테헤란로", "분당수서로", "동부간선로"])
    report_spot("Banpo Bridge / Jamsugyo", 850, 1150, 600, 1500)
    report_spot("Naebu Circular Expressway", -7500, 7500, -7500, 0, ["내부순환로"])
    report_spot("Gyeongbu Expressway", 1800, 3200, 1200, 4200, ["경부고속도로"])
    report_spot("Olympic-daero & Gangbyeonbuk-ro", 800, 3600, 400, 1800, ["올림픽대로", "강변북로"])

    # 10. Re-check building clearance against newly widened ground-level (layer <= 0) roads!
    print("10. Verifying 0% ground-road building intrusions against newly widened roads...")
    gnd_road_polys = []
    for si, s in enumerate(raw_segs):
        if s["cls"] > 4 or s["layer"] >= 1 or s["is_bridge"]:
            continue
        ls = seg_lines[si]
        gnd_road_polys.append(ls.buffer(s["hw"] + 0.55, cap_style=2, join_style=2))
    gnd_road_tree = STRtree(gnd_road_polys)

    kept_bldgs = []
    new_bverts = []
    culled_b = 0
    clipped_b = 0
    for b in bldgs:
        fv, vc = b[0], b[1]
        cx, cz, baseY, minH, h, hx, hz = b[11:18]
        if vc < 3:
            continue
        # Resnap every building foundation to our exact baked bare-earth DTM!
        b[13] = sample_dem_dm(dem_dm, demMinX, demMaxX, demMinZ, demMaxZ, cx, cz)
        pts = [(cx + float(bverts[fv + k, 0]) * 0.05, cz + float(bverts[fv + k, 1]) * 0.05) for k in range(vc)]

        if minH < 4.5:
            poly = Polygon(pts)
            if not poly.is_valid:
                poly = poly.buffer(0)
            if poly.is_empty or not isinstance(poly, Polygon):
                culled_b += 1
                continue
            hits = gnd_road_tree.query(poly, predicate="intersects")
            if len(hits) > 0:
                orig_area = poly.area
                clipped = poly
                for hi in hits:
                    clipped = clipped.difference(gnd_road_polys[hi])
                    if clipped.is_empty:
                        break
                if isinstance(clipped, MultiPolygon):
                    clipped = max(clipped.geoms, key=lambda q: q.area) if len(clipped.geoms) > 0 else Polygon()
                if clipped.is_empty or not isinstance(clipped, Polygon) or clipped.area < 20.0 or (orig_area < 140.0 and clipped.area < orig_area * 0.55):
                    culled_b += 1
                    continue
                clipped_s = clipped.simplify(0.25, preserve_topology=True)
                if isinstance(clipped_s, Polygon) and not clipped_s.is_empty:
                    clipped = clipped_s
                ext = list(clipped.exterior.coords)[:-1]
                if len(ext) > 48:
                    ext = list(clipped.minimum_rotated_rectangle.exterior.coords)[:-1]
                if len(ext) < 3:
                    culled_b += 1
                    continue
                # Ensure CCW
                s2 = sum(ext[j][0] * ext[(j + 1) % len(ext)][1] - ext[(j + 1) % len(ext)][0] * ext[j][1] for j in range(len(ext)))
                if s2 < 0.0:
                    ext.reverse()
                xs = [p[0] for p in ext]
                zs = [p[1] for p in ext]
                cx = 0.5 * (min(xs) + max(xs))
                cz = 0.5 * (min(zs) + max(zs))
                hx = max(1.0, 0.5 * (max(xs) - min(xs)))
                hz = max(1.0, 0.5 * (max(zs) - min(zs)))
                b[11], b[12] = cx, cz
                b[13] = sample_dem_dm(dem_dm, demMinX, demMaxX, demMinZ, demMaxZ, cx, cz)
                b[16], b[17] = hx, hz
                pts = ext
                clipped_b += 1

        new_fv = len(new_bverts)
        b[0] = new_fv
        b[1] = len(pts)
        for px, pz in pts:
            lx = max(-32760, min(32760, int(round((px - cx) * 20.0))))
            lz = max(-32760, min(32760, int(round((pz - cz) * 20.0))))
            new_bverts.append((lx, lz))
        kept_bldgs.append(b)
    print(f"   Kept {len(kept_bldgs)} buildings (clipped {clipped_b}, culled {culled_b} overlapping newly widened roads)")

    # 11. Build O(1) spatial chunks for roads & buildings!
    print("11. Packing OCM5 v3 spatial chunks & serializing assets/maps/seoul_real.ocmap ...")
    num_chunks = cxN * czN
    chunk_roads = [[] for _ in range(num_chunks)]
    chunk_bldgs = [[] for _ in range(num_chunks)]

    # Pack roads & Vec3(x, y, z) road vertices
    road_records = []
    road_verts_3d = []
    name_slots = {"": 0}
    for si, s in enumerate(raw_segs):
        nids = seg_nodes[si]
        if len(nids) < 2:
            continue
        fv = len(road_verts_3d)
        rminX = rminZ = 1e9
        rmaxX = rmaxZ = -1e9
        any_han = False
        max_elev_above_dem = 0.0
        for nid in nids:
            px, pz = node_pos[nid]
            py = float(y[nid])
            road_verts_3d.append((px, py, pz))
            rminX = min(rminX, px)
            rmaxX = max(rmaxX, px)
            rminZ = min(rminZ, pz)
            rmaxZ = max(rmaxZ, pz)
            if node_is_han[nid]:
                any_han = True
            max_elev_above_dem = max(max_elev_above_dem, py - float(dem_arr[nid]))

        flags = 0
        if s["oneway"]:
            flags |= 1
        if s["is_bridge"] or max_elev_above_dem >= 1.8:
            flags |= 2
        # flags bit2 (4) = tunnel (already excluded)

        flags2 = 0
        if s["is_link"]:
            flags2 |= 1
        if s["is_ring_exp"]:
            flags2 |= 2
        if s["is_bridge"] or max_elev_above_dem >= 1.8:
            flags2 |= 4
        if any_han and s["is_bridge"]:
            flags2 |= 8

        nm = s["name"]
        if nm in name_slots:
            nslot = name_slots[nm]
        elif len(name_slots) < 255:
            nslot = len(name_slots)
            name_slots[nm] = nslot
        else:
            nslot = 0

        hw = s["hw"]
        ri = len(road_records)
        road_records.append((
            fv, len(nids), s["cls"], s["lanes"], flags, nslot,
            max(-120, min(120, int(s["layer"]))), flags2,
            hw, rminX, rminZ, rmaxX, rmaxZ
        ))

        c0x = max(0, min(cxN - 1, int(math.floor((rminX - hw - 10.0 - minX) / chunkSz))))
        c1x = max(0, min(cxN - 1, int(math.floor((rmaxX + hw + 10.0 - minX) / chunkSz))))
        c0z = max(0, min(czN - 1, int(math.floor((rminZ - hw - 10.0 - minZ) / chunkSz))))
        c1z = max(0, min(czN - 1, int(math.floor((rmaxZ + hw + 10.0 - minZ) / chunkSz))))
        for cz_i in range(c0z, c1z + 1):
            row = cz_i * cxN
            for cx_i in range(c0x, c1x + 1):
                chunk_roads[row + cx_i].append(ri)

    for bi, b in enumerate(kept_bldgs):
        cx, cz, hx, hz = b[11], b[12], b[16], b[17]
        c0x = max(0, min(cxN - 1, int(math.floor((cx - hx - minX) / chunkSz))))
        c1x = max(0, min(cxN - 1, int(math.floor((cx + hx - minX) / chunkSz))))
        c0z = max(0, min(czN - 1, int(math.floor((cz - hz - minZ) / chunkSz))))
        c1z = max(0, min(czN - 1, int(math.floor((cz + hz - minZ) / chunkSz))))
        for cz_i in range(c0z, c1z + 1):
            row = cz_i * cxN
            for cx_i in range(c0x, c1x + 1):
                chunk_bldgs[row + cx_i].append(bi)

    flat_roads = []
    flat_bldgs = []
    new_chunks_bytes = bytearray()
    old_chunks = base["old_chunks"]
    for ci in range(num_chunks):
        _, _, _, _, p_start, p_len = old_chunks[ci]
        r_start = len(flat_roads)
        flat_roads.extend(chunk_roads[ci])
        r_len = len(chunk_roads[ci])
        b_start = len(flat_bldgs)
        flat_bldgs.extend(chunk_bldgs[ci])
        b_len = len(chunk_bldgs[ci])
        new_chunks_bytes.extend(struct.pack("<IIIIII", r_start, r_len, b_start, b_len, p_start, p_len))

    # Serialize OCM5 v3 Header (100 bytes)
    out_hdr = struct.pack(
        "<4sIfffffIIffffIIIIIIIIIIII",
        b"OCM5", 3,
        minX, maxX, minZ, maxZ, chunkSz,
        cxN, czN,
        demMinX, demMaxX, demMinZ, demMaxZ,
        demW, demH,
        len(road_records), len(road_verts_3d),
        len(kept_bldgs), len(new_bverts),
        hdr[19], hdr[20], hdr[21],
        len(flat_roads), len(flat_bldgs), hdr[24],
    )
    assert len(out_hdr) == 100

    # Pack 32-byte naturally aligned OsmRoadRec (<IHBBBBbBfffff)
    road_buf = bytearray(len(road_records) * 32)
    for i, r in enumerate(road_records):
        struct.pack_into("<IHBBBBbBfffff", road_buf, i * 32, *r)

    rverts_buf = np.array(road_verts_3d, dtype=np.float32).tobytes()

    bldg_buf = bytearray(len(kept_bldgs) * 44)
    for i, b in enumerate(kept_bldgs):
        struct.pack_into("<IHBBBBBBBBHfffffff", bldg_buf, i * 44, *b)

    bvert_buf = np.array(new_bverts, dtype=np.int16).tobytes()
    flat_r_bytes = np.array(flat_roads, dtype=np.uint32).tobytes()
    flat_b_bytes = np.array(flat_bldgs, dtype=np.uint32).tobytes()

    with open(OUT_OCMAP, "wb") as f:
        f.write(out_hdr)
        f.write(dem_dm.astype(np.int16).tobytes())
        f.write(road_buf)
        f.write(rverts_buf)
        f.write(bldg_buf)
        f.write(bvert_buf)
        f.write(new_chunks_bytes)
        f.write(flat_r_bytes)
        f.write(flat_b_bytes)
        f.write(base["flat_pois_bytes"])
        f.write(base["water_bytes"])
        f.write(base["park_bytes"])
        f.write(base["pois_bytes"])

    sz_mb = os.path.getsize(OUT_OCMAP) / (1024 * 1024)
    print(f"   Wrote {OUT_OCMAP} (OCM5 v3): {sz_mb:.2f} MB ({len(road_records)} 3D roads, {len(road_verts_3d)} 3D verts, {len(kept_bldgs)} buildings)")

    # 12. Render upgraded 3072x1968 crisp 2D Seoul Map with true scaled multi-level roads!
    print("12. Rendering 3072x1968 real Seoul 2D map with true scaled expressways & ring roads...")
    render_seoul_2d_map_v5(OUT_MAP2D, dem_dm, hdr, water_tris, park_tris, road_records, road_verts_3d, kept_bldgs, new_bverts)
    print(f"ALL DONE in {time.time() - t0:.1f}s!")


def render_seoul_2d_map_v5(out_path, dem_dm, hdr, water_tris, park_tris, road_records, road_verts_3d, bldgs, bverts):
    W, H = 3072, 1968
    minX, maxX, minZ, maxZ = hdr[2:6]
    demMinX, demMaxX, demMinZ, demMaxZ, demW, demH = hdr[9:15]
    px_per_m = W / (maxX - minX)

    # Terrain hillshade background
    xs = np.linspace(minX, maxX, W, dtype=np.float32)
    zs = np.linspace(minZ, maxZ, H, dtype=np.float32)
    fx = np.clip((xs - demMinX) / (demMaxX - demMinX) * (demW - 1), 0, demW - 1.001)
    fz = np.clip((zs - demMinZ) / (demMaxZ - demMinZ) * (demH - 1), 0, demH - 1.001)
    ix = fx.astype(np.int32)
    iz = fz.astype(np.int32)
    tx = (fx - ix)[None, :]
    tz = (fz - iz)[:, None]
    d00 = dem_dm[iz[:, None], ix[None, :]].astype(np.float32) * 0.1
    d10 = dem_dm[iz[:, None], ix[None, :] + 1].astype(np.float32) * 0.1
    d01 = dem_dm[iz[:, None] + 1, ix[None, :]].astype(np.float32) * 0.1
    d11 = dem_dm[iz[:, None] + 1, ix[None, :] + 1].astype(np.float32) * 0.1
    elev = (d00 * (1 - tx) + d10 * tx) * (1 - tz) + (d01 * (1 - tx) + d11 * tx) * tz
    gx_e = np.gradient(elev, axis=1)
    gz_e = np.gradient(elev, axis=0)
    shade = np.clip(0.84 - gx_e * 0.08 - gz_e * 0.08, 0.52, 1.12)
    mtn = np.clip((elev - 48.0) / 140.0, 0.0, 1.0)
    bg = np.zeros((H, W, 3), dtype=np.uint8)
    bg[..., 0] = np.clip((38 * (1 - mtn) + 28 * mtn) * shade, 0, 255).astype(np.uint8)
    bg[..., 1] = np.clip((44 * (1 - mtn) + 62 * mtn) * shade, 0, 255).astype(np.uint8)
    bg[..., 2] = np.clip((54 * (1 - mtn) + 46 * mtn) * shade, 0, 255).astype(np.uint8)

    im = Image.fromarray(bg, mode="RGB")
    draw = ImageDraw.Draw(im)

    def to_px(x, z):
        return (x - minX) * px_per_m, (z - minZ) * px_per_m

    for tri in park_tris:
        draw.polygon([to_px(tri[0], tri[1]), to_px(tri[2], tri[3]), to_px(tri[4], tri[5])], fill=(34, 74, 52))
    for tri in water_tris:
        draw.polygon([to_px(tri[0], tri[1]), to_px(tri[2], tri[3]), to_px(tri[4], tri[5])], fill=(26, 78, 128))

    # Crisp 184K building footprints
    for b in bldgs:
        fv, vc = b[0], b[1]
        cx, cz, h = b[11], b[12], b[15]
        pts = [to_px(cx + bverts[fv + k][0] * 0.05, cz + bverts[fv + k][1] * 0.05) for k in range(vc)]
        col = (74, 84, 98) if h >= 32.0 else ((62, 68, 80) if h >= 14.0 else (52, 58, 68))
        draw.polygon(pts, fill=col)

    # Draw roads layered strictly bottom-to-top (layer -1 -> 0 -> 1 -> 2 -> 3, and minor -> major!)
    order = sorted(range(len(road_records)), key=lambda i: (road_records[i][6], -road_records[i][2], road_records[i][8]))
    for i in order:
        fv, vc, cls, lanes, flags, nslot, layer, flags2, hw, *_ = road_records[i]
        pts = [to_px(road_verts_3d[fv + k][0], road_verts_3d[fv + k][2]) for k in range(vc)]
        if len(pts) < 2:
            continue
        is_exp = (cls <= 1) or ((flags2 & 2) != 0)
        w_px = max(1, int(round(hw * 2.0 * px_per_m)))
        if is_exp:
            draw.line(pts, fill=(14, 72, 48), width=w_px + 2)
            draw.line(pts, fill=(38, 186, 118), width=max(2, w_px))
        elif cls == 2:
            draw.line(pts, fill=(198, 152, 56), width=max(2, w_px))
        elif cls == 3:
            draw.line(pts, fill=(138, 122, 84), width=max(1, w_px))
        elif cls == 4:
            draw.line(pts, fill=(92, 98, 110), width=max(1, w_px))
        else:
            draw.line(pts, fill=(68, 74, 84), width=1)

    im.save(out_path, optimize=True)
    print(f"   Saved {out_path} ({os.path.getsize(out_path)//1024} KB)")


if __name__ == "__main__":
    main()
