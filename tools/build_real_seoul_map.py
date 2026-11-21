#!/usr/bin/env python3
"""
Compile REAL OpenStreetMap Seoul vector data + REAL AWS Terrarium SRTM DEM elevation
into:
  1. assets/maps/seoul_real.ocmap  (compact O(1)-chunked binary GIS world)
  2. assets/maps/seoul_map_2d.png  (2048x1312 crisp Naver/Google-style cartographic map)

Usage:
  uv run --with osmium --with shapely --with pillow --with numpy python3 tools/build_real_seoul_map.py \
      /tmp/osm_work/south-korea-latest.osm.pbf /tmp/osm_work/dem_tiles
"""

import math
import os
import struct
import sys
import time
import numpy as np
import osmium
from PIL import Image, ImageDraw
from shapely.geometry import Polygon, MultiPolygon
from shapely.ops import triangulate

# Real GPS Origin: Yongsan / Banpo Bridge center (37.5235 N, 126.9850 E)
ORIGIN_LAT = 37.5235
ORIGIN_LON = 126.9850
M_PER_DEG_LAT = 111132.0
M_PER_DEG_LON = 88304.0  # 111320 * cos(37.5235 deg)

# Core Real-OSM Urban Extract Box: 19.2 km (E-W) x 12.288 km (N-S)
# Covers Yeouido, Hongdae, Gwanghwamun, Myeongdong, Jongno, Dongdaemun, Namsan,
# Yongsan, Banpo, Seocho, Gangnam, Teheran-ro, COEX, Seongsu, Jamsil, Olympic Park!
CHUNK_SIZE = 256.0
CHUNKS_X = 75
CHUNKS_Z = 48
MIN_X = -7680.0
MAX_X = MIN_X + CHUNKS_X * CHUNK_SIZE   # +11520.0
MIN_Z = -7424.0
MAX_Z = MIN_Z + CHUNKS_Z * CHUNK_SIZE   # +4864.0

MIN_LAT = ORIGIN_LAT - MAX_Z / M_PER_DEG_LAT  # ~37.4797
MAX_LAT = ORIGIN_LAT - MIN_Z / M_PER_DEG_LAT  # ~37.5903
MIN_LON = ORIGIN_LON + MIN_X / M_PER_DEG_LON  # ~126.8980
MAX_LON = ORIGIN_LON + MAX_X / M_PER_DEG_LON  # ~127.1155

# Greater Seoul SRTM DEM Box: 30.72 km x 24.576 km (includes real Bukhansan, Gwanaksan, Namsan, etc.)
DEM_MIN_X = -15360.0
DEM_MAX_X =  15360.0
DEM_MIN_Z = -13312.0
DEM_MAX_Z =  11264.0
DEM_W = 384
DEM_H = 307

ROAD_CLASSES = {
    "motorway": 0, "motorway_link": 0,
    "trunk": 1, "trunk_link": 1,
    "primary": 2, "primary_link": 2,
    "secondary": 3, "secondary_link": 3,
    "tertiary": 4, "tertiary_link": 4,
    "residential": 5, "unclassified": 5,
    "living_street": 6,
}

DEFAULT_LANES = {0: 6, 1: 6, 2: 6, 3: 4, 4: 3, 5: 2, 6: 1}
DEFAULT_HALF_W = {0: 11.5, 1: 10.5, 2: 9.2, 3: 6.8, 4: 5.0, 5: 3.6, 6: 2.8}


def latlon_to_world(lat: float, lon: float):
    x = (lon - ORIGIN_LON) * M_PER_DEG_LON
    z = -(lat - ORIGIN_LAT) * M_PER_DEG_LAT
    return x, z


def world_to_latlon(x: float, z: float):
    lat = ORIGIN_LAT - z / M_PER_DEG_LAT
    lon = ORIGIN_LON + x / M_PER_DEG_LON
    return lat, lon


def build_dem(dem_dir: str):
    """Build real 384x307 SRTM elevation grid from downloaded AWS Terrarium zoom-11 tiles."""
    z = 11
    tile_cache = {}
    for fname in os.listdir(dem_dir):
        if fname.endswith(".png"):
            parts = fname[:-4].split("_")
            if len(parts) == 3 and int(parts[0]) == z:
                im = Image.open(os.path.join(dem_dir, fname)).convert("RGB")
                tile_cache[(int(parts[1]), int(parts[2]))] = np.array(im, dtype=np.float32)

    n = 2 ** z
    grid = np.zeros((DEM_H, DEM_W), dtype=np.float32)
    for iz in range(DEM_H):
        wz = DEM_MIN_Z + (iz / (DEM_H - 1)) * (DEM_MAX_Z - DEM_MIN_Z)
        lat = ORIGIN_LAT - wz / M_PER_DEG_LAT
        lat_r = math.radians(lat)
        fy = (1.0 - math.asinh(math.tan(lat_r)) / math.pi) / 2.0 * n
        ty = int(fy)
        py = min(255, max(0, int((fy - ty) * 256)))
        for ix in range(DEM_W):
            wx = DEM_MIN_X + (ix / (DEM_W - 1)) * (DEM_MAX_X - DEM_MIN_X)
            lon = ORIGIN_LON + wx / M_PER_DEG_LON
            fx = (lon + 180.0) / 360.0 * n
            tx = int(fx)
            px = min(255, max(0, int((fx - tx) * 256)))
            arr = tile_cache.get((tx, ty))
            if arr is not None:
                r, g, b = arr[py, px]
                h = (r * 256.0 + g + b / 256.0) - 32768.0
                grid[iz, ix] = max(0.0, h - 7.0)  # River water datum ~0m

    # Gentle 3x3 smoothing pass in low urban valleys (< 35m) so streets flow smoothly without SRTM staircase noise
    smooth = grid.copy()
    for _ in range(2):
        padded = np.pad(smooth, 1, mode="edge")
        avg = (
            padded[:-2, :-2] + padded[:-2, 1:-1] + padded[:-2, 2:] +
            padded[1:-1, :-2] + 2.0 * padded[1:-1, 1:-1] + padded[1:-1, 2:] +
            padded[2:, :-2] + padded[2:, 1:-1] + padded[2:, 2:]
        ) / 10.0
        # Preserve sharp peaks (> 50m), smooth gentle urban flats
        w_smooth = np.clip(1.0 - (smooth - 20.0) / 55.0, 0.0, 1.0)
        smooth = smooth * (1.0 - w_smooth * 0.75) + avg * (w_smooth * 0.75)
    return smooth


def sample_dem_grid(dem: np.ndarray, x: float, z: float) -> float:
    fx = (x - DEM_MIN_X) / (DEM_MAX_X - DEM_MIN_X) * (DEM_W - 1)
    fz = (z - DEM_MIN_Z) / (DEM_MAX_Z - DEM_MIN_Z) * (DEM_H - 1)
    fx = max(0.0, min(DEM_W - 1.001, fx))
    fz = max(0.0, min(DEM_H - 1.001, fz))
    ix, iz = int(fx), int(fz)
    tx, tz = fx - ix, fz - iz
    h00 = dem[iz, ix]
    h10 = dem[iz, ix + 1]
    h01 = dem[iz + 1, ix]
    h11 = dem[iz + 1, ix + 1]
    return float((h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz)


def simplify_polyline(pts, tol=0.8):
    if len(pts) <= 2:
        return pts
    # Iterative Douglas-Peucker
    keep = [False] * len(pts)
    keep[0] = True
    keep[-1] = True
    stack = [(0, len(pts) - 1)]
    tol2 = tol * tol
    while stack:
        a, b = stack.pop()
        ax, az = pts[a]
        bx, bz = pts[b]
        dx, dz = bx - ax, bz - az
        l2 = dx * dx + dz * dz
        best_i = -1
        best_d2 = -1.0
        for i in range(a + 1, b):
            px, pz = pts[i]
            if l2 < 1e-6:
                d2 = (px - ax) ** 2 + (pz - az) ** 2
            else:
                t = max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / l2))
                qx, qz = ax + t * dx, az + t * dz
                d2 = (px - qx) ** 2 + (pz - qz) ** 2
            if d2 > best_d2:
                best_d2 = d2
                best_i = i
        if best_d2 > tol2 and best_i > 0:
            keep[best_i] = True
            stack.append((a, best_i))
            stack.append((best_i, b))
    return [pts[i] for i in range(len(pts)) if keep[i]]


def parse_float(val, default=None):
    if not val:
        return default
    s = val.strip().lower().replace("m", "").replace(",", ".").split(";")[0].strip()
    try:
        v = float(s)
        if math.isfinite(v) and v > 0:
            return v
    except ValueError:
        pass
    return default


def classify_building_and_height(tags, area_m2, cx, cz):
    b = (tags.get("building") or "yes").lower()
    name = (tags.get("name") or tags.get("name:ko") or "").lower()

    # Determine authentic Seoul architectural style:
    # 0=KoreanApt, 1=GlassTower, 2=GraniteOffice, 3=BrickCommercial, 4=StuccoCommercial, 5=Hanok, 6=CivicLandmark
    is_apt = (b in ("apartments", "residential", "dormitory")) or ("아파트" in name) or ("래미안" in name) or ("자이" in name) or ("힐스테이트" in name) or ("푸르지오" in name) or ("아크로" in name) or ("롯데캐슬" in name) or ("동" in name and area_m2 > 260)
    is_hanok = (b in ("temple", "shrine", "palace")) or ("궁" in name) or ("한옥" in name) or ("전" in name and cz < -4500 and -1600 < cx < 600)
    is_office = (b in ("office", "commercial", "hotel", "retail", "supermarket", "mall"))

    h = parse_float(tags.get("height"))
    levels = parse_float(tags.get("building:levels"))
    if h is None and levels is not None:
        floor_h = 3.15 if is_apt else 3.75
        h = levels * floor_h

    # District-aware realistic Seoul height estimation when OSM lacks explicit height tag
    in_gangnam_cbd = (2000 < cx < 7800 and 500 < cz < 3800)
    in_yeouido = (-6600 < cx < -3400 and -1200 < cz < 1100)
    in_gwanghwamun_cbd = (-1800 < cx < 2400 and -5600 < cz < -3300)
    in_jamsil = (7800 < cx < 11200 and 400 < cz < 3400)
    in_hongdae_seongsu = (cx < -3200 and cz < -2000) or (3400 < cx < 6800 and -3600 < cz < -800)

    if h is None:
        hv = abs(hash((round(cx, 1), round(cz, 1)))) % 100
        if is_hanok:
            h = 7.5 + (hv % 4)
        elif is_apt:
            if area_m2 > 450:
                h = 46.0 + (hv % 34)  # High-rise 15~26F Korean apartment slab
            else:
                h = 15.0 + (hv % 14)  # Mid-rise villa / officetel
        elif in_gangnam_cbd or in_yeouido or in_gwanghwamun_cbd:
            if area_m2 > 900:
                h = 48.0 + (hv % 72)
            elif area_m2 > 320:
                h = 24.0 + (hv % 36)
            else:
                h = 12.0 + (hv % 16)
        elif in_jamsil and area_m2 > 500:
            h = 38.0 + (hv % 42)
        else:
            if area_m2 > 800:
                h = 22.0 + (hv % 28)
            elif area_m2 > 220:
                h = 13.0 + (hv % 15)
            else:
                h = 7.5 + (hv % 10)

    h = max(4.2, min(555.0, h))

    if is_hanok:
        style = 5
    elif is_apt and h >= 24.0:
        style = 0
    elif h >= 36.0 and (in_gangnam_cbd or in_yeouido or in_gwanghwamun_cbd or in_jamsil or is_office):
        style = 1 if (abs(int(cx + cz)) % 3 != 0) else 2
    elif h >= 20.0 and is_office:
        style = 2 if (abs(int(cx * 3 + cz)) % 2 == 0) else 1
    elif in_hongdae_seongsu or (b in ("house", "detached", "terrace")) or (h < 18.0 and (abs(int(cx * 7 + cz * 3)) % 3 == 0)):
        style = 3
    else:
        style = 4

    color_idx = abs(int(cx * 13.7 + cz * 29.3)) & 7
    return style, color_idx, h


def triangulate_polygon_safe(poly: Polygon, min_area=18.0):
    """Triangulate a 2D shapely Polygon cleanly, returning list of ((x0,z0),(x1,z1),(x2,z2))."""
    if poly.is_empty or poly.area < min_area:
        return []
    if not poly.is_valid:
        poly = poly.buffer(0)
        if poly.is_empty:
            return []
    polys = list(poly.geoms) if isinstance(poly, MultiPolygon) else [poly]
    out_tris = []
    for p in polys:
        if p.area < min_area:
            continue
        p_simp = p.simplify(2.2, preserve_topology=True)
        if p_simp.is_empty or not p_simp.is_valid:
            p_simp = p
        try:
            raw_tris = triangulate(p_simp)
        except Exception:
            continue
        for tri in raw_tris:
            if tri.area < 1.0:
                continue
            rp = tri.representative_point()
            if p_simp.contains(rp):
                coords = list(tri.exterior.coords)[:3]
                if len(coords) == 3:
                    # Ensure consistent upward (+Y) CCW winding in (x, z) plane:
                    # cross((b-a), (c-a)).y = (bz-az)*(cx-ax) - (bx-ax)*(cz-az) > 0
                    (x0, z0), (x1, z1), (x2, z2) = coords[0], coords[1], coords[2]
                    cross_y = (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0)
                    if cross_y < 0:
                        out_tris.append(((x0, z0), (x2, z2), (x1, z1)))
                    else:
                        out_tris.append(((x0, z0), (x1, z1), (x2, z2)))
    return out_tris


class SeoulExtractor(osmium.SimpleHandler):
    def __init__(self, dem):
        super().__init__()
        self.dem = dem
        self.roads = []      # (pts, road_class, lanes, flags, half_w, name_slot)
        self.buildings = []  # (cx, cz, local_pts_i16, style, color_idx, base_y, height, half_x, half_z)
        self.water_polys = []
        self.park_polys = []
        self.pois = []       # (x, z, kind, name_id)  kind: 0=subway, 1=bus_stop
        self.road_name_map = {"": 0}
        self.road_names = [""]

    def _name_slot(self, name: str) -> int:
        if not name:
            return 0
        s = name.strip()
        if s in self.road_name_map:
            return self.road_name_map[s]
        if len(self.road_names) >= 255:
            return 0
        idx = len(self.road_names)
        self.road_name_map[s] = idx
        self.road_names.append(s)
        return idx

    def node(self, n):
        if not n.location.valid():
            return
        lat, lon = n.location.lat, n.location.lon
        if not (MIN_LAT <= lat <= MAX_LAT and MIN_LON <= lon <= MAX_LON):
            return
        t = n.tags
        if t.get("railway") == "subway_entrance" or t.get("station") == "subway":
            x, z = latlon_to_world(lat, lon)
            if MIN_X <= x <= MAX_X and MIN_Z <= z <= MAX_Z:
                self.pois.append((x, z, 0, self._name_slot(t.get("name") or "")))
        elif t.get("highway") == "bus_stop":
            x, z = latlon_to_world(lat, lon)
            if MIN_X <= x <= MAX_X and MIN_Z <= z <= MAX_Z:
                self.pois.append((x, z, 1, self._name_slot(t.get("name") or "")))

    def way(self, w):
        if len(w.nodes) < 2:
            return
        t = w.tags
        hw = t.get("highway")
        if hw not in ROAD_CLASSES:
            return
        pts = []
        any_in = False
        for nd in w.nodes:
            if not nd.location.valid():
                continue
            lat, lon = nd.location.lat, nd.location.lon
            x, z = latlon_to_world(lat, lon)
            if MIN_X - 200.0 <= x <= MAX_X + 200.0 and MIN_Z - 200.0 <= z <= MAX_Z + 200.0:
                any_in = True
            pts.append((x, z))
        if not any_in or len(pts) < 2:
            return

        rcls = ROAD_CLASSES[hw]
        tol = 0.65 if rcls <= 3 else 1.0
        pts = simplify_polyline(pts, tol=tol)
        if len(pts) < 2:
            return

        # Clip away huge outlying segments far outside Seoul box
        pts = [(max(MIN_X - 250.0, min(MAX_X + 250.0, x)), max(MIN_Z - 250.0, min(MAX_Z + 250.0, z))) for x, z in pts]
        lanes_val = parse_float(t.get("lanes"))
        lanes = int(max(1, min(12, round(lanes_val)))) if lanes_val else DEFAULT_LANES[rcls]
        half_w = DEFAULT_HALF_W[rcls]
        if lanes_val:
            half_w = max(2.6, min(18.0, lanes * 1.72 + (1.2 if rcls <= 2 else 0.6)))
        if hw.endswith("_link"):
            half_w = min(half_w, 5.2)

        flags = 0
        if t.get("oneway") in ("yes", "1", "true"):
            flags |= 1
        if t.get("bridge") not in (None, "no", "false"):
            flags |= 2
        if t.get("tunnel") not in (None, "no", "false"):
            flags |= 4
        if flags & 4:
            # Skip deep underground subway/car tunnels from surface road rendering so they don't cut hills
            return

        nslot = self._name_slot(t.get("name") or "")
        self.roads.append((pts, rcls, lanes, flags, half_w, nslot))

    def area(self, a):
        t = a.tags
        bld = t.get("building")
        nat = t.get("natural")
        ww = t.get("waterway")
        lu = t.get("landuse")
        lei = t.get("leisure")

        is_bldg = bld is not None and bld != "no"
        is_water = (nat == "water") or (ww == "riverbank") or (t.get("water") in ("river", "lake", "pond", "reservoir"))
        is_park = (lei in ("park", "garden")) or (lu in ("grass", "forest", "recreation_ground", "meadow"))

        if not (is_bldg or is_water or is_park):
            return

        for outer in a.outer_rings():
            coords = []
            any_in = False
            for nd in outer:
                if not nd.location.valid():
                    continue
                x, z = latlon_to_world(nd.location.lat, nd.location.lon)
                if MIN_X - 80.0 <= x <= MAX_X + 80.0 and MIN_Z - 80.0 <= z <= MAX_Z + 80.0:
                    any_in = True
                coords.append((x, z))
            if not any_in or len(coords) < 4:
                continue

            if is_bldg:
                # Compute polygon area & centroid fast
                if coords[0] != coords[-1]:
                    coords.append(coords[0])
                try:
                    poly = Polygon(coords)
                    if not poly.is_valid:
                        poly = poly.buffer(0)
                    if poly.is_empty or isinstance(poly, MultiPolygon):
                        if isinstance(poly, MultiPolygon):
                            poly = max(poly.geoms, key=lambda g: g.area)
                        else:
                            continue
                    area_m2 = poly.area
                    if area_m2 < 20.0:
                        continue
                    poly_s = poly.simplify(0.55, preserve_topology=True)
                    if poly_s.is_empty or not isinstance(poly_s, Polygon):
                        poly_s = poly
                    ext = list(poly_s.exterior.coords)[:-1]
                    if len(ext) > 16:
                        poly_s = poly.simplify(1.6, preserve_topology=True)
                        if isinstance(poly_s, Polygon) and not poly_s.is_empty:
                            ext = list(poly_s.exterior.coords)[:-1]
                    if len(ext) > 20:
                        # Fallback to oriented minimum rotated rectangle
                        ext = list(poly.minimum_rotated_rectangle.exterior.coords)[:-1]
                    if len(ext) < 3:
                        continue
                    # Ensure consistent CCW winding in (x, z) so outward wall normals always point OUTWARD!
                    signed2 = 0.0
                    for i in range(len(ext)):
                        x0, z0 = ext[i]
                        x1, z1 = ext[(i + 1) % len(ext)]
                        signed2 += (x0 * z1 - x1 * z0)
                    if signed2 < 0:
                        ext.reverse()

                    xs = [p[0] for p in ext]
                    zs = [p[1] for p in ext]
                    cx = 0.5 * (min(xs) + max(xs))
                    cz = 0.5 * (min(zs) + max(zs))
                    if not (MIN_X <= cx <= MAX_X and MIN_Z <= cz <= MAX_Z):
                        continue
                    hx = max(1.0, 0.5 * (max(xs) - min(xs)))
                    hz = max(1.0, 0.5 * (max(zs) - min(zs)))
                    if hx > 450.0 or hz > 450.0:
                        continue  # Reject broken mega-polygons

                    # Quantize local vertices to int16 (0.05m resolution -> range +/- 1638m!)
                    loc_pts = []
                    for vx, vz in ext:
                        lx = max(-32760, min(32760, int(round((vx - cx) * 20.0))))
                        lz = max(-32760, min(32760, int(round((vz - cz) * 20.0))))
                        loc_pts.append((lx, lz))

                    style, color_idx, height = classify_building_and_height(t, area_m2, cx, cz)
                    base_y = sample_dem_grid(self.dem, cx, cz)
                    self.buildings.append((cx, cz, loc_pts, style, color_idx, base_y, height, hx, hz))
                except Exception:
                    continue
            elif is_water or is_park:
                holes = []
                for inner in a.inner_rings(outer):
                    hc = []
                    for nd in inner:
                        if nd.location.valid():
                            hc.append(latlon_to_world(nd.location.lat, nd.location.lon))
                    if len(hc) >= 4:
                        holes.append(hc)
                try:
                    poly = Polygon(coords, holes)
                    if not poly.is_valid:
                        poly = poly.buffer(0)
                    if poly.is_empty:
                        continue
                    # Clip strictly to Seoul map bounds (+200m margin)
                    clip_box = Polygon([
                        (MIN_X - 200, MIN_Z - 200), (MAX_X + 200, MIN_Z - 200),
                        (MAX_X + 200, MAX_Z + 200), (MIN_X - 200, MAX_Z + 200)
                    ])
                    clipped = poly.intersection(clip_box)
                    if clipped.is_empty or clipped.area < (80.0 if is_water else 160.0):
                        continue
                    if is_water:
                        self.water_polys.append(clipped)
                    else:
                        self.park_polys.append(clipped)
                except Exception:
                    continue


def render_seoul_2d_map(out_png: str, dem: np.ndarray, water_polys, park_polys, roads, buildings):
    """Render crisp 2048x1312 Naver/Google-style cartographic map directly from real OSM Seoul data!"""
    W, H = 2048, 1312
    # 1. Start with subtle warm cartographic urban background + SRTM mountain hillshade!
    xs = np.linspace(MIN_X, MAX_X, W, dtype=np.float32)
    zs = np.linspace(MIN_Z, MAX_Z, H, dtype=np.float32)
    gx, gz = np.meshgrid(xs, zs)
    fx = np.clip((gx - DEM_MIN_X) / (DEM_MAX_X - DEM_MIN_X) * (DEM_W - 1), 0, DEM_W - 1.001)
    fz = np.clip((gz - DEM_MIN_Z) / (DEM_MAX_Z - DEM_MIN_Z) * (DEM_H - 1), 0, DEM_H - 1.001)
    ix = fx.astype(np.int32)
    iz = fz.astype(np.int32)
    tx = fx - ix
    tz = fz - iz
    elev = (
        (dem[iz, ix] * (1 - tx) + dem[iz, ix + 1] * tx) * (1 - tz) +
        (dem[iz + 1, ix] * (1 - tx) + dem[iz + 1, ix + 1] * tx) * tz
    )
    # Hillshade from NW sun
    dz_dx = np.gradient(elev, axis=1) / ((MAX_X - MIN_X) / W)
    dz_dz = np.gradient(elev, axis=0) / ((MAX_Z - MIN_Z) / H)
    shade = np.clip(0.86 + (-dz_dx * 0.45 - dz_dz * 0.45), 0.55, 1.12)

    # Dark sleek GPS cartographic theme (matches modern car navigation & high contrast in HUD!)
    mtn = np.clip((elev - 48.0) / 140.0, 0.0, 1.0)
    r = (34.0 * (1.0 - mtn) + 28.0 * mtn) * shade
    g = (40.0 * (1.0 - mtn) + 54.0 * mtn) * shade
    b = (48.0 * (1.0 - mtn) + 36.0 * mtn) * shade
    img_arr = np.stack([np.clip(r, 0, 255), np.clip(g, 0, 255), np.clip(b, 0, 255)], axis=-1).astype(np.uint8)
    im = Image.fromarray(img_arr, mode="RGB")
    draw = ImageDraw.Draw(im)

    def to_px(x, z):
        px = (x - MIN_X) / (MAX_X - MIN_X) * W
        py = (z - MIN_Z) / (MAX_Z - MIN_Z) * H
        return (px, py)

    def draw_geom(geom, fill):
        geoms = list(geom.geoms) if isinstance(geom, MultiPolygon) else ([geom] if isinstance(geom, Polygon) else [])
        for p in geoms:
            pts = [to_px(x, z) for x, z in p.exterior.coords]
            if len(pts) >= 3:
                draw.polygon(pts, fill=fill)

    # 2. Real Seoul Parks
    for pk in park_polys:
        draw_geom(pk, (36, 74, 48))

    # 3. Real Han River, Tributaries & Seokchon Lake
    for wp in water_polys:
        draw_geom(wp, (26, 76, 128))
        # Paint islands (inner holes) back to urban/park land!
        geoms = list(wp.geoms) if isinstance(wp, MultiPolygon) else ([wp] if isinstance(wp, Polygon) else [])
        for p in geoms:
            for hole in p.interiors:
                pts = [to_px(x, z) for x, z in hole.coords]
                if len(pts) >= 3:
                    draw.polygon(pts, fill=(40, 48, 56))

    # 4. Real Seoul Building Footprints (subtle crisp architectural massing!)
    for cx, cz, loc_pts, style, color_idx, base_y, height, hx, hz in buildings:
        if hx < 3.5 and hz < 3.5:
            continue
        pts = [to_px(cx + lx * 0.05, cz + lz * 0.05) for lx, lz in loc_pts]
        col = (66, 78, 92) if height > 35.0 else (52, 60, 70)
        draw.polygon(pts, fill=col)

    # 5. Real Seoul Road Network sorted from minor alleys -> major boulevards -> emerald-green expressways!
    road_order = sorted(roads, key=lambda r: -r[1])
    for pts, rcls, lanes, flags, half_w, nslot in road_order:
        ppx = [to_px(x, z) for x, z in pts]
        if len(ppx) < 2:
            continue
        if rcls <= 1:      # Korean Expressway / Motorway (경부고속도로 · 올림픽대로 · 강변북로 · 내부순환로) — Vivid Emerald Green!
            draw.line(ppx, fill=(14, 78, 52), width=7)
            draw.line(ppx, fill=(46, 212, 134), width=4)
        elif rcls == 2:    # Primary City Avenue (반포대로 · 강남대로 · 테헤란로 · 세종대로 · 여의대로) — Warm Gold!
            draw.line(ppx, fill=(236, 198, 92), width=3)
        elif rcls == 3:    # Secondary Boulevard — Cool Silver-Blue
            draw.line(ppx, fill=(156, 174, 198), width=2)
        elif rcls == 4:    # Tertiary Street — Slate
            draw.line(ppx, fill=(104, 116, 132), width=1)
        else:              # Residential alleys — Subtle Dark Slate
            draw.line(ppx, fill=(66, 74, 86), width=1)

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    im.save(out_png, optimize=True)
    print(f"Saved crisp real Seoul cartographic map: {out_png} ({os.path.getsize(out_png) / 1024:.0f} KB)")


def write_ocmap(out_path: str, dem: np.ndarray, ext: SeoulExtractor, water_tris, park_tris):
    # Flatten road vertices
    road_verts = []
    road_recs = []
    for pts, rcls, lanes, flags, half_w, nslot in ext.roads:
        first_v = len(road_verts)
        xs = [p[0] for p in pts]
        zs = [p[1] for p in pts]
        road_verts.extend(pts)
        road_recs.append((first_v, len(pts), rcls, lanes, flags, nslot, half_w, min(xs), min(zs), max(xs), max(zs)))

    # Flatten building local int16 vertices
    bldg_verts = []
    bldg_recs = []
    for cx, cz, loc_pts, style, color_idx, base_y, height, hx, hz in ext.buildings:
        first_v = len(bldg_verts)
        bldg_verts.extend(loc_pts)
        bldg_recs.append((first_v, len(loc_pts), style, color_idx, cx, cz, base_y, height, hx, hz))

    # Build 75x48 spatial chunk index tables for O(1) runtime streaming & collision!
    num_chunks = CHUNKS_X * CHUNKS_Z
    chunk_roads = [[] for _ in range(num_chunks)]
    chunk_bldgs = [[] for _ in range(num_chunks)]
    chunk_pois = [[] for _ in range(num_chunks)]

    def chunk_range(x0, z0, x1, z1):
        cx0 = max(0, min(CHUNKS_X - 1, int((x0 - MIN_X) // CHUNK_SIZE)))
        cx1 = max(0, min(CHUNKS_X - 1, int((x1 - MIN_X) // CHUNK_SIZE)))
        cz0 = max(0, min(CHUNKS_Z - 1, int((z0 - MIN_Z) // CHUNK_SIZE)))
        cz1 = max(0, min(CHUNKS_Z - 1, int((z1 - MIN_Z) // CHUNK_SIZE)))
        return cx0, cx1, cz0, cz1

    for ri, rec in enumerate(road_recs):
        _, _, _, _, _, _, hw, x0, z0, x1, z1 = rec
        cx0, cx1, cz0, cz1 = chunk_range(x0 - hw, z0 - hw, x1 + hw, z1 + hw)
        for cz in range(cz0, cz1 + 1):
            base = cz * CHUNKS_X
            for cx in range(cx0, cx1 + 1):
                chunk_roads[base + cx].append(ri)

    for bi, rec in enumerate(bldg_recs):
        _, _, _, _, cx_w, cz_w, _, _, hx, hz = rec
        cx0, cx1, cz0, cz1 = chunk_range(cx_w - hx, cz_w - hz, cx_w + hx, cz_w + hz)
        for cz in range(cz0, cz1 + 1):
            base = cz * CHUNKS_X
            for cx in range(cx0, cx1 + 1):
                chunk_bldgs[base + cx].append(bi)

    for pi, (px, pz, pkind, pname) in enumerate(ext.pois):
        cx0, _, cz0, _ = chunk_range(px, pz, px, pz)
        chunk_pois[cz0 * CHUNKS_X + cx0].append(pi)

    flat_chunk_roads = []
    flat_chunk_bldgs = []
    flat_chunk_pois = []
    chunk_headers = []
    for ci in range(num_chunks):
        rs, rl = len(flat_chunk_roads), len(chunk_roads[ci])
        flat_chunk_roads.extend(chunk_roads[ci])
        bs, bl = len(flat_chunk_bldgs), len(chunk_bldgs[ci])
        flat_chunk_bldgs.extend(chunk_bldgs[ci])
        ps, pl = len(flat_chunk_pois), len(chunk_pois[ci])
        flat_chunk_pois.extend(chunk_pois[ci])
        chunk_headers.append((rs, rl, bs, bl, ps, pl))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        # 1. Header (100 bytes: magic+ver + 5f + 2I + 4f + 12I)
        f.write(struct.pack(
            "<4sIfffffIIffffIIIIIIIIIIII",
            b"OCM3", 1,
            MIN_X, MAX_X, MIN_Z, MAX_Z, CHUNK_SIZE,
            CHUNKS_X, CHUNKS_Z,
            DEM_MIN_X, DEM_MAX_X, DEM_MIN_Z, DEM_MAX_Z,
            DEM_W, DEM_H,
            len(road_recs), len(road_verts),
            len(bldg_recs), len(bldg_verts),
            len(water_tris), len(park_tris),
            len(ext.pois),
            len(flat_chunk_roads), len(flat_chunk_bldgs), len(flat_chunk_pois)
        ))
        # 2. DEM heightmap (int16 decimeters)
        dem_i16 = np.clip(np.round(dem * 10.0), -32000, 32000).astype("<i2")
        f.write(dem_i16.tobytes())

        # 3. Roads (32 bytes per OsmRoadRec)
        for rec in road_recs:
            f.write(struct.pack("<IHBBBBfffff", *rec))
        # Road verts (8 bytes each: float x, z)
        rv_arr = np.array(road_verts, dtype="<f4")
        f.write(rv_arr.tobytes())

        # 4. Buildings (32 bytes per OsmBldgRec)
        for rec in bldg_recs:
            f.write(struct.pack("<IHBBffffff", *rec))
        # Building local verts (4 bytes each: int16 lx, lz in 0.05m units)
        bv_arr = np.array(bldg_verts, dtype="<i2")
        f.write(bv_arr.tobytes())

        # 5. Chunk headers (24 bytes each: rs, rl, bs, bl, ps, pl) + flat index arrays
        for ch in chunk_headers:
            f.write(struct.pack("<IIIIII", *ch))
        f.write(np.array(flat_chunk_roads, dtype="<u4").tobytes())
        f.write(np.array(flat_chunk_bldgs, dtype="<u4").tobytes())
        f.write(np.array(flat_chunk_pois, dtype="<u4").tobytes())

        # 6. Water & Park triangles (24 bytes per triangle: 6 floats x0,z0,x1,z1,x2,z2)
        if water_tris:
            wt_arr = np.array([c for tri in water_tris for pt in tri for c in pt], dtype="<f4")
            f.write(wt_arr.tobytes())
        if park_tris:
            pt_arr = np.array([c for tri in park_tris for pt in tri for c in pt], dtype="<f4")
            f.write(pt_arr.tobytes())

        # 7. POIs (12 bytes each: float x, float z, uint16 kind, uint16 name_slot)
        for px, pz, pkind, pname in ext.pois:
            f.write(struct.pack("<ffHH", px, pz, pkind, pname))

    sz_mb = os.path.getsize(out_path) / (1024.0 * 1024.0)
    print(f"Wrote {out_path}: {sz_mb:.2f} MB")
    print(f"  Roads={len(road_recs)} ({len(road_verts)} verts), Buildings={len(bldg_recs)} ({len(bldg_verts)} verts)")
    print(f"  WaterTris={len(water_tris)}, ParkTris={len(park_tris)}, POIs={len(ext.pois)}")


def main():
    pbf_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/osm_work/south-korea-latest.osm.pbf"
    dem_dir = sys.argv[2] if len(sys.argv) > 2 else "/tmp/osm_work/dem_tiles"
    t0 = time.time()

    print("1. Building real Seoul SRTM DEM elevation grid...")
    dem = build_dem(dem_dir)
    print(f"   DEM min={dem.min():.1f}m, max={dem.max():.1f}m (Namsan={sample_dem_grid(dem, 283, -3078):.1f}m, Bukhansan={sample_dem_grid(dem, -618, -15024):.1f}m)")

    print("2. Extracting real Seoul roads, building polygons, Han River water, parks & POIs from OSM PBF...")
    ext = SeoulExtractor(dem)
    ext.apply_file(pbf_path, locations=True, idx="flex_mem")
    print(f"   Extracted in {time.time() - t0:.1f}s: {len(ext.roads)} roads, {len(ext.buildings)} buildings, "
          f"{len(ext.water_polys)} water polys, {len(ext.park_polys)} park polys, {len(ext.pois)} POIs")

    print("3. Triangulating real Han River / Seokchon Lake water & Seoul parks...")
    water_tris = []
    for wp in ext.water_polys:
        water_tris.extend(triangulate_polygon_safe(wp, min_area=60.0))
    park_tris = []
    for pk in ext.park_polys:
        park_tris.extend(triangulate_polygon_safe(pk, min_area=180.0))
    print(f"   Triangulated {len(water_tris)} water tris & {len(park_tris)} park tris")

    print("4. Writing binary assets/maps/seoul_real.ocmap...")
    write_ocmap("assets/maps/seoul_real.ocmap", dem, ext, water_tris, park_tris)

    print("5. Rendering 2048x1312 real cartographic Seoul map assets/maps/seoul_map_2d.png...")
    render_seoul_2d_map("assets/maps/seoul_map_2d.png", dem, ext.water_polys, ext.park_polys, ext.roads, ext.buildings)
    print(f"Done in {time.time() - t0:.1f}s!")


if __name__ == "__main__":
    main()
