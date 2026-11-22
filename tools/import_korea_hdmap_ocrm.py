#!/usr/bin/env python3
"""
Korean Government Official Road GIS -> Open City OCRM v1 Compiler & Seamless Patcher.

Supports direct import (from a folder or .zip archive) of official South Korean road GIS datasets:
  1. 국토지리정보원 정밀도로지도 (NGII HD Map, https://map.ngii.go.kr):
       - A3_DRIVEWAYSECTION.shp  (exact cm-grade carriageway road surface polygons)
       - B2_SURFACELINEMARK.shp  (exact lane lines: yellow/white/blue solid/dashed/double)
       - B3_SURFACEMARK.shp      (exact crosswalk polygons, stop bars, directional arrows)
       - A2_LINK.shp             (optional road link centerlines when A3 is absent)
  2. 행정안전부 도로명주소 전자지도 실폭도로 (https://business.juso.go.kr):
       - TL_SPRD_RW.shp          (exact real-width surveyed Korean road & junction polygons)
  3. 국토지리정보원 1:1000 / 1:5000 연속수치지형도 (Digital Topographic Map):
       - *A001*.shp              (도로경계면 — exact road polygons)
       - *A003*.shp              (보도 / 인도 — exact sidewalk polygons)
       - *A004*.shp              (교량 — bridge polygons)

Auto-detects Korean CRS from .prj or coordinate bounds (EPSG:5179 UTM-K, EPSG:5186 Central Belt 2010,
EPSG:5174 Bessel, EPSG:32652 UTM 52N, EPSG:4326 WGS84), reprojects to Open City Seoul world space,
computes mathematically disjoint sidewalks (0.00% intersection intrusion), syncs 3D terrain elevations
with `assets/maps/seoul_real.ocmap`, and seamlessly patches touched chunks onto `assets/maps/seoul_roads_hd.ocrm`.

Usage examples:
  # Patch Gangnam NGII HD Map ZIP directly onto existing seoul_roads_hd.ocrm:
  uv run --with pyshp --with shapely --with mapbox-earcut --with pyproj --with scipy --with numpy --with lz4 \
    python3 tools/import_korea_hdmap_ocrm.py --input ~/Downloads/NGII_Gangnam_HDMap.zip \
    --patch assets/maps/seoul_roads_hd.ocrm --out assets/maps/seoul_roads_hd.ocrm

  # Import 행정안전부 실폭도로 TL_SPRD_RW.shp folder:
  uv run --with pyshp --with shapely --with mapbox-earcut --with pyproj --with scipy --with numpy --with lz4 \
    python3 tools/import_korea_hdmap_ocrm.py --input ~/Downloads/TL_SPRD_RW_Seoul/ \
    --patch assets/maps/seoul_roads_hd.ocrm --out assets/maps/seoul_roads_hd.ocrm
"""
from __future__ import annotations

import argparse
import glob
import math
import os
import shutil
import sys
import tempfile
import time
import zipfile
from typing import Dict, List, Optional, Tuple

import numpy as np
import pyproj
import shapefile
from scipy.spatial import cKDTree
from shapely import box as shp_box
from shapely import unary_union
from shapely.geometry import LineString, Point, Polygon

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ocrm_writer import (
    ChunkMeshBuilder,
    Ocm6ElevationField,
    iter_lines,
    iter_polygons,
    latlon_to_world,
    read_ocrm_blobs,
    write_ocrm_file,
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OCMAP = os.path.join(ROOT, "assets", "maps", "seoul_real.ocmap")
DEFAULT_OCRM = os.path.join(ROOT, "assets", "maps", "seoul_roads_hd.ocrm")


def detect_crs_for_shp(shp_path: str, sample_xy: Tuple[float, float], override_crs: Optional[str]) -> pyproj.CRS:
    if override_crs:
        return pyproj.CRS.from_user_input(override_crs)
    prj_path = os.path.splitext(shp_path)[0] + ".prj"
    if os.path.isfile(prj_path):
        try:
            wkt = open(prj_path, "r", encoding="utf-8", errors="ignore").read().strip()
            if wkt:
                return pyproj.CRS.from_wkt(wkt)
        except Exception:
            pass
    x, y = sample_xy
    if 123.0 <= x <= 133.0 and 32.0 <= y <= 40.0:
        return pyproj.CRS.from_epsg(4326)
    if 700000.0 <= x <= 1400000.0 and 1400000.0 <= y <= 2300000.0:
        return pyproj.CRS.from_epsg(5179)  # Korea 2000 / Unified CS (UTM-K)
    if 200000.0 <= x <= 550000.0 and 3800000.0 <= y <= 4400000.0:
        return pyproj.CRS.from_epsg(32652)  # WGS84 / UTM 52N (common NGII HD Map)
    if 80000.0 <= x <= 350000.0 and 480000.0 <= y <= 700000.0:
        return pyproj.CRS.from_epsg(5186)  # Korea 2000 / Central Belt 2010
    if 80000.0 <= x <= 350000.0 and 360000.0 <= y <= 480000.0:
        return pyproj.CRS.from_epsg(5174)  # Korean 1985 / Modified Central Belt
    raise ValueError(
        f"Cannot auto-detect CRS for {shp_path} (sample xy={sample_xy}). Pass --crs EPSG:5179 (or 5186/32652/4326)."
    )


def make_world_projector(shp_path: str, sf: shapefile.Reader, override_crs: Optional[str]):
    if len(sf) == 0:
        return None
    sample_xy = None
    for sr in sf.iterShapes():
        if sr.points:
            sample_xy = (float(sr.points[0][0]), float(sr.points[0][1]))
            break
    if sample_xy is None:
        return None
    src_crs = detect_crs_for_shp(shp_path, sample_xy, override_crs)
    tr = pyproj.Transformer.from_crs(src_crs, "EPSG:4326", always_xy=True)

    def to_world_pts(pts: List[Tuple[float, float]]) -> List[Tuple[float, float]]:
        xs = np.fromiter((p[0] for p in pts), dtype=np.float64)
        ys = np.fromiter((p[1] for p in pts), dtype=np.float64)
        lons, lats = tr.transform(xs, ys)
        out = []
        for lat, lon in zip(lats, lons):
            out.append(latlon_to_world(float(lat), float(lon)))
        return out

    return src_crs.to_string(), to_world_pts


def shape_to_polygons_world(shp, to_world_pts) -> List[Polygon]:
    pts = shp.points
    if not pts:
        return []
    parts = list(shp.parts) + [len(pts)]
    rings = []
    for i in range(len(parts) - 1):
        sub = pts[parts[i]:parts[i + 1]]
        if len(sub) >= 3:
            wpts = to_world_pts(sub)
            rings.append(wpts)
    if not rings:
        return []
    # Distinguish exterior rings vs holes using signed area / containment
    polys: List[Polygon] = []
    cur_ext = None
    cur_holes = []
    for r in rings:
        p = Polygon(r).buffer(0)
        if p.is_empty or p.area < 0.05:
            continue
        if cur_ext is not None and cur_ext.contains(p.representative_point()):
            cur_holes.append(r)
        else:
            if cur_ext is not None:
                polys.extend(iter_polygons(Polygon(cur_ext.exterior.coords, cur_holes).buffer(0)))
            cur_ext = p if isinstance(p, Polygon) else iter_polygons(p)[0]
            cur_holes = []
    if cur_ext is not None:
        polys.extend(iter_polygons(Polygon(cur_ext.exterior.coords, cur_holes).buffer(0)))
    return polys


def shape_to_lines_world(shp, to_world_pts) -> List[LineString]:
    pts = shp.points
    if not pts:
        return []
    parts = list(shp.parts) + [len(pts)]
    out: List[LineString] = []
    for i in range(len(parts) - 1):
        sub = pts[parts[i]:parts[i + 1]]
        if len(sub) >= 2:
            wpts = to_world_pts(sub)
            ls = LineString(wpts)
            if ls.length >= 0.25:
                out.append(ls)
    return out


def open_shapefile_robust(shp_path: str) -> shapefile.Reader:
    for enc in ("utf-8", "cp949", "euc-kr", "latin1"):
        try:
            sf = shapefile.Reader(shp_path, encoding=enc)
            _ = sf.fields
            return sf
        except Exception:
            continue
    return shapefile.Reader(shp_path, encodingErrors="ignore")


def get_attr_upper(rec_dict: dict, *keys: str) -> str:
    for k in keys:
        for rk, rv in rec_dict.items():
            if rk.upper() == k.upper() and rv is not None:
                return str(rv).strip()
    return ""


def classify_ngii_line(rec_dict: dict) -> Tuple[str, float, Optional[Tuple[float, float]]]:
    """Map official NGII B2_SURFACELINEMARK Type/Kind codes to (color_tag, width_m, dash_tuple)."""
    t_str = get_attr_upper(rec_dict, "TYPE", "LTYPE", "CODE")
    k_str = get_attr_upper(rec_dict, "KIND", "LKIND")
    t_num = int(t_str) if t_str.isdigit() else 0
    k_num = int(k_str) if k_str.isdigit() else 0

    is_yellow = (100 <= t_num < 200) or (k_num in (501, 502, 515, 525))
    is_blue = (300 <= t_num < 400) or (k_num == 504)
    is_dashed = ((t_num % 10) == 2) or (t_num in (112, 212, 312))
    is_double = (120 <= t_num <= 129) or (220 <= t_num <= 229) or (320 <= t_num <= 329)

    width = 0.22 if is_double else (0.45 if k_num == 530 else 0.15)
    dash = (3.0, 5.0) if is_dashed else None
    if is_blue:
        return "blue", width, dash
    if is_yellow:
        return "yellow", width, dash
    return "white", width, dash


def collect_shapefiles(input_path: str, tmp_dir: str) -> List[str]:
    if os.path.isfile(input_path) and input_path.lower().endswith(".zip"):
        with zipfile.ZipFile(input_path, "r") as zf:
            zf.extractall(tmp_dir)
        search_root = tmp_dir
    elif os.path.isfile(input_path) and input_path.lower().endswith(".shp"):
        return [input_path]
    elif os.path.isdir(input_path):
        search_root = input_path
    else:
        raise FileNotFoundError(f"Input path not found or unsupported: {input_path}")

    shps = []
    for root, _, files in os.walk(search_root):
        for fn in files:
            if fn.lower().endswith(".shp"):
                shps.append(os.path.join(root, fn))
    return sorted(shps)


def classify_layer_file(shp_path: str) -> str:
    bn = os.path.basename(shp_path).upper()
    if "A3_DRIVEWAY" in bn or "DRIVEWAY" in bn:
        return "road_poly"
    if "TL_SPRD_RW" in bn or "SPRD_RW" in bn or "A001" in bn or "ROAD_AREA" in bn:
        return "road_poly"
    if "A004" in bn or "BRIDGE" in bn:
        return "bridge_poly"
    if "A003" in bn or "SIDEWALK" in bn or "FOOTWAY" in bn:
        return "sidewalk_poly"
    if "B2_SURFACELINE" in bn or "SURFACELINE" in bn or "LINEMARK" in bn:
        return "line_mark"
    if "B3_SURFACEMARK" in bn or "SURFACEMARK" in bn or "CROSSWALK" in bn:
        return "poly_mark"
    if "A2_LINK" in bn or "ROAD_LINK" in bn:
        return "road_link"
    return "unknown"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Import Korean Official Road GIS (NGII HD Map / TL_SPRD_RW / 1:1000 수치지형도) into OCRM v1"
    )
    parser.add_argument("--input", required=True, help="Directory, .zip archive, or .shp file")
    parser.add_argument("--ocmap", default=DEFAULT_OCMAP, help="Path to assets/maps/seoul_real.ocmap")
    parser.add_argument("--patch", default=None, help="Existing .ocrm file to seamlessly patch touched chunks into")
    parser.add_argument("--out", default=DEFAULT_OCRM, help="Output OCRM v1 path")
    parser.add_argument("--crs", default=None, help="Optional CRS override (e.g., EPSG:5179, EPSG:5186, EPSG:32652)")
    args = parser.parse_args()

    t0 = time.time()
    tmp_dir = tempfile.mkdtemp(prefix="oc_korea_gis_")
    try:
        shps = collect_shapefiles(args.input, tmp_dir)
        if not shps:
            raise RuntimeError(f"No .shp files found inside {args.input}")

        print(f"Loading OCM6 v4 elevation & building field from {args.ocmap} ...")
        elev = Ocm6ElevationField(args.ocmap)

        road_urb_polys: List[Polygon] = []
        road_hw_polys: List[Polygon] = []
        elev_road_polys: List[Polygon] = []
        explicit_sw_polys: List[Polygon] = []
        line_marks: List[Tuple[str, LineString, float, Optional[Tuple[float, float]], bool]] = []
        poly_marks: List[Tuple[str, Polygon, bool]] = []
        link_fallback_polys: List[Polygon] = []

        for shp_path in shps:
            role = classify_layer_file(shp_path)
            if role == "unknown":
                continue
            sf = open_shapefile_robust(shp_path)
            proj_info = make_world_projector(shp_path, sf, args.crs)
            if proj_info is None:
                continue
            crs_name, to_world_pts = proj_info
            print(f"  Reading [{role}] {os.path.basename(shp_path)} ({len(sf):,} records, CRS={crs_name}) ...")

            for sr in sf.iterShapeRecords():
                rec_d = sr.record.as_dict()
                if role == "road_poly":
                    rt = get_attr_upper(rec_d, "ROADTYPE", "RTYPE", "ROAD_TYPE")
                    rk = get_attr_upper(rec_d, "KIND", "ROAD_KIND", "RW_SE", "ROAD_CLS")
                    # Skip underground tunnels (NGII RoadType 2=tunnel, 4=underpass)
                    if rt in ("2", "4"):
                        continue
                    is_bridge = rt in ("3", "5")
                    is_hw = rk in ("1", "2")
                    for poly in shape_to_polygons_world(sr.shape, to_world_pts):
                        if is_bridge:
                            elev_road_polys.append(poly)
                        elif is_hw:
                            road_hw_polys.append(poly)
                        else:
                            road_urb_polys.append(poly)
                elif role == "bridge_poly":
                    for poly in shape_to_polygons_world(sr.shape, to_world_pts):
                        elev_road_polys.append(poly)
                elif role == "sidewalk_poly":
                    for poly in shape_to_polygons_world(sr.shape, to_world_pts):
                        explicit_sw_polys.append(poly)
                elif role == "line_mark":
                    col_tag, width_m, dash = classify_ngii_line(rec_d)
                    is_br = get_attr_upper(rec_d, "ROADTYPE") in ("3", "5")
                    for ls in shape_to_lines_world(sr.shape, to_world_pts):
                        line_marks.append((col_tag, ls, width_m, dash, is_br))
                elif role == "poly_mark":
                    k_str = get_attr_upper(rec_d, "KIND", "TYPE")
                    col_tag = "yellow" if k_str in ("515", "525", "534") else "white"
                    is_br = get_attr_upper(rec_d, "ROADTYPE") in ("3", "5")
                    for poly in shape_to_polygons_world(sr.shape, to_world_pts):
                        poly_marks.append((col_tag, poly, is_br))
                elif role == "road_link":
                    w_str = get_attr_upper(rec_d, "WIDTH", "ROAD_WIDTH")
                    try:
                        hw = max(2.8, float(w_str) * 0.5)
                    except ValueError:
                        hw = 3.5
                    for ls in shape_to_lines_world(sr.shape, to_world_pts):
                        p = ls.buffer(hw, cap_style="flat", join_style="mitre", mitre_limit=2.0)
                        link_fallback_polys.extend(iter_polygons(p))

        if not road_urb_polys and not road_hw_polys and not elev_road_polys and link_fallback_polys:
            print(f"  Using {len(link_fallback_polys):,} A2_LINK buffered road polygons (no A3/TL_SPRD_RW polygon layer present).")
            road_urb_polys = link_fallback_polys

        total_in = len(road_urb_polys) + len(road_hw_polys) + len(elev_road_polys)
        if total_in == 0:
            raise RuntimeError("No supported road geometries found in input shapefiles.")

        print(
            f"Loaded Korean GIS geometry: {len(road_urb_polys):,} urban road polys, {len(road_hw_polys):,} highway polys, "
            f"{len(elev_road_polys):,} bridge polys, {len(explicit_sw_polys):,} explicit sidewalks, "
            f"{len(line_marks):,} line marks, {len(poly_marks):,} polygon marks."
        )

        # Bin imported GIS geometry into 75x48 Open City chunks
        chunks_data: Dict[Tuple[int, int], dict] = {}

        def get_cd(cx: int, cz: int) -> dict:
            d = chunks_data.get((cx, cz))
            if d is None:
                d = {"urb": [], "hw": [], "elev": [], "sw": [], "lmarks": [], "pmarks": []}
                chunks_data[(cx, cz)] = d
            return d

        def bin_bounds(bounds, pad: float = 16.0):
            x0, z0, x1, z1 = bounds
            c0x = max(0, min(elev.cX - 1, int(math.floor((x0 - pad - elev.minX) / elev.chunkSz))))
            c1x = max(0, min(elev.cX - 1, int(math.floor((x1 + pad - elev.minX) / elev.chunkSz))))
            c0z = max(0, min(elev.cZ - 1, int(math.floor((z0 - pad - elev.minZ) / elev.chunkSz))))
            c1z = max(0, min(elev.cZ - 1, int(math.floor((z1 + pad - elev.minZ) / elev.chunkSz))))
            for cz in range(c0z, c1z + 1):
                for cx in range(c0x, c1x + 1):
                    yield cx, cz

        for p in road_urb_polys:
            for cx, cz in bin_bounds(p.bounds):
                get_cd(cx, cz)["urb"].append(p)
        for p in road_hw_polys:
            for cx, cz in bin_bounds(p.bounds):
                get_cd(cx, cz)["hw"].append(p)
        for p in elev_road_polys:
            for cx, cz in bin_bounds(p.bounds):
                get_cd(cx, cz)["elev"].append(p)
        for p in explicit_sw_polys:
            for cx, cz in bin_bounds(p.bounds):
                get_cd(cx, cz)["sw"].append(p)
        for rec in line_marks:
            for cx, cz in bin_bounds(rec[1].bounds, pad=2.0):
                get_cd(cx, cz)["lmarks"].append(rec)
        for rec in poly_marks:
            for cx, cz in bin_bounds(rec[1].bounds, pad=2.0):
                get_cd(cx, cz)["pmarks"].append(rec)

        # Load base OCRM chunks if patching an existing file
        chunk_blobs: Dict[Tuple[int, int], Tuple[bytes, int, int, int]] = {}
        flags = 7  # bit0 = LZ4, bit1 = Korean Government HD Map / 실폭도로, bit2 = SoA+Delta16
        if args.patch and os.path.isfile(args.patch):
            print(f"Loading existing OCRM base for seamless chunk patching: {args.patch} ...")
            _, chunk_blobs = read_ocrm_blobs(args.patch)

        k_urb_col = (128, 128, 132, 255)
        k_hw_col = (48, 50, 54, 255)
        k_sw_col = (198, 200, 204, 255)
        k_curb_col = (182, 184, 188, 255)
        k_soffit = (132, 136, 144, 255)
        paint_cols = {
            "white": (238, 240, 244, 255),
            "yellow": (236, 184, 38, 255),
            "blue": (32, 134, 236, 255),
        }

        patched_cnt = 0
        for (cx, cz), cdata in sorted(chunks_data.items()):
            x0 = elev.minX + cx * elev.chunkSz
            z0 = elev.minZ + cz * elev.chunkSz
            chunk_box = shp_box(x0, z0, x0 + elev.chunkSz, z0 + elev.chunkSz)
            halo_box = shp_box(x0 - 16.0, z0 - 16.0, x0 + elev.chunkSz + 16.0, z0 + elev.chunkSz + 16.0)
            cmb = ChunkMeshBuilder(cx, cz, elev.minX, elev.minZ, elev.chunkSz)

            urb_list = [g.intersection(halo_box) for g in cdata["urb"]]
            hw_list = [g.intersection(halo_box) for g in cdata["hw"]]
            urb_list = [g for g in urb_list if not g.is_empty]
            hw_list = [g for g in hw_list if not g.is_empty]
            all_g = urb_list + hw_list

            if all_g:
                ground_all = unary_union(all_g).buffer(0.18).buffer(-0.18).simplify(0.06, preserve_topology=True)
                hw_u = unary_union(hw_list).intersection(ground_all) if hw_list else Polygon()
                urb_u = ground_all.difference(hw_u)

                for p in iter_polygons(urb_u.intersection(chunk_box)):
                    cmb.add_polygon_surface(p, cmb.idx_urban, elev.eval_ground_y, 0.015, k_urb_col, subdiv_step=24.0)
                for p in iter_polygons(hw_u.intersection(chunk_box)):
                    cmb.add_polygon_surface(p, cmb.idx_highway, elev.eval_ground_y, 0.018, k_hw_col, subdiv_step=24.0)

                # Sidewalks: use explicit A003 sidewalk polygons if provided, else mathematically disjoint buffer\road!
                if cdata["sw"]:
                    sw_raw = unary_union([g.intersection(halo_box) for g in cdata["sw"]]).difference(ground_all)
                else:
                    urb_for_sw = ground_all.difference(hw_u.buffer(0.75)) if not hw_u.is_empty else ground_all
                    sw_raw = urb_for_sw.buffer(3.15, join_style="round", quad_segs=2).difference(ground_all) if not urb_for_sw.is_empty else Polygon()
                    b_boxes = elev.bldg_boxes_by_chunk.get((cx, cz), [])
                    if b_boxes and not sw_raw.is_empty:
                        b_polys = [shp_box(bx0 + 0.12, bz0 + 0.12, bx1 - 0.12, bz1 - 0.12)
                                   for (bx0, bz0, bx1, bz1) in b_boxes if (bx1 - bx0 > 0.5 and bz1 - bz0 > 0.5)]
                        if b_polys:
                            sw_raw = sw_raw.difference(unary_union(b_polys))

                if not sw_raw.is_empty:
                    curb_pts = []
                    for bl in iter_lines(ground_all.boundary):
                        spts = np.asarray(bl.coords, dtype=np.float64)
                        for i in range(len(spts) - 1):
                            a, b = spts[i], spts[i + 1]
                            d = math.hypot(b[0] - a[0], b[1] - a[1])
                            steps = max(1, int(math.ceil(d / 1.8)))
                            for k in range(steps):
                                t = k / steps
                                curb_pts.append((a[0] * (1.0 - t) + b[0] * t, a[1] * (1.0 - t) + b[1] * t))
                    curb_tree = cKDTree(np.asarray(curb_pts, dtype=np.float64)) if curb_pts else None

                    for swp in iter_polygons(sw_raw.intersection(chunk_box)):
                        if swp.area < 1.5:
                            continue
                        cmb.add_polygon_surface(swp, cmb.idx_sidewalk, elev.eval_ground_y, 0.125, k_sw_col, curb_tree=curb_tree, subdiv_step=24.0)
                    for bl in iter_lines(sw_raw.boundary.intersection(chunk_box)):
                        if bl.length < 0.3:
                            continue
                        cmb.add_curb_wall_along_ring(
                            np.asarray(bl.coords, dtype=np.float64),
                            elev.eval_ground_y, -0.55, 0.125, cmb.idx_sidewalk, k_curb_col, outward_sign=1.0,
                        )

            # Elevated bridges / viaducts from NGII RoadType 3/5 or A004
            el_list = [g.intersection(halo_box) for g in cdata["elev"]]
            el_list = [g for g in el_list if not g.is_empty]
            if el_list:
                el_u = unary_union(el_list).simplify(0.06, preserve_topology=True)
                for p in iter_polygons(el_u.intersection(chunk_box)):
                    cmb.add_polygon_surface(p, cmb.idx_urban, elev.eval_elev_y, 0.02, k_urb_col, subdiv_step=24.0)
                    cmb.add_polygon_surface(p, cmb.idx_bridge, elev.eval_elev_y, -1.25, k_soffit, subdiv_step=24.0, flip_down=True)
                for bl in iter_lines(el_u.boundary.intersection(chunk_box)):
                    if bl.length < 0.8:
                        continue
                    cmb.add_jersey_barrier_along_line(bl, elev.eval_elev_y, 1.0, is_median=False, with_sound_wall=False)

            # Surveyed B2_SURFACELINEMARK line markings
            for col_tag, ls, width_m, dash, is_br in cdata["lmarks"]:
                eval_fn = elev.eval_elev_y if is_br else elev.eval_ground_y
                col = paint_cols.get(col_tag, paint_cols["white"])
                for seg in iter_lines(ls.intersection(chunk_box)):
                    cmb.add_paint_polyline(seg, width_m, col, eval_fn, 0.038, dashed=dash)

            # Surveyed B3_SURFACEMARK polygon markings (crosswalks, arrows, stop bars!)
            for col_tag, poly, is_br in cdata["pmarks"]:
                eval_fn = elev.eval_elev_y if is_br else elev.eval_ground_y
                col = paint_cols.get(col_tag, paint_cols["white"])
                for p in iter_polygons(poly.intersection(chunk_box)):
                    cmb.add_polygon_surface(p, cmb.idx_marking, eval_fn, 0.040, col, subdiv_step=16.0)

            if not cmb.is_empty():
                comp, raw_sz = cmb.serialize_compressed()
                r_tris = (len(cmb.idx_urban) + len(cmb.idx_highway) + len(cmb.idx_inter)) // 3
                sw_tris = len(cmb.idx_sidewalk) // 3
                chunk_blobs[(cx, cz)] = (comp, raw_sz, r_tris, sw_tris)
                patched_cnt += 1

        fsize, r_tris, sw_tris = write_ocrm_file(
            args.out,
            elev.minX, elev.maxX, elev.minZ, elev.maxZ, elev.chunkSz,
            elev.cX, elev.cZ,
            chunk_blobs,
            flags=flags,
        )
        print(
            f"SUCCESS! Compiled {patched_cnt:,} Korean GIS chunks into {args.out} "
            f"({fsize/1e6:.2f} MB total, {len(chunk_blobs):,} chunks, {r_tris:,} road tris, {sw_tris:,} sidewalk tris) in {time.time()-t0:.1f}s!"
        )
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
