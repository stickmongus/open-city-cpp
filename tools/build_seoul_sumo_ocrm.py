#!/usr/bin/env python3
"""
Eclipse SUMO netconvert + Shapely Single-Topology 3D Seoul Road Compiler -> OCRM v1 (`assets/maps/seoul_roads_hd.ocrm`).

Replaces overlapping runtime road ribbons with a mathematically watertight, pre-baked 3D road network:
  1. Parses real Seoul OpenStreetMap roads (from Geofabrik south-korea-latest.osm.pbf or cached XML),
     excluding underground tunnels and classifying true elevated bridges/viaducts vs ground roads using
     the 15m bare-earth DTM + OCM6 v4 3D elevations.
  2. Runs DLR Eclipse SUMO `netconvert` separately on Ground (join-dist 28m, curved corners) and Elevated
     layers to compute real autonomous-driving-grade unified intersection polygons & junction-trimmed lanes.
  3. Builds per-chunk watertight single-layer asphalt unions, mathematically disjoint sidewalks/curbs/medians
     (ZERO intersection intrusion!), crisp clipped Korean lane markings & zebra crosswalks, and elevated
     box-girder viaducts/Jersey barriers/piers -> writes compact LZ4-compressed `assets/maps/seoul_roads_hd.ocrm`.
"""
from __future__ import annotations

import argparse
import math
import multiprocessing as mp
import os
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from typing import Dict, List, Optional, Tuple
from xml.sax.saxutils import escape

import numpy as np
import pyproj
from scipy.spatial import cKDTree
from shapely import box as shp_box
from shapely import unary_union
from shapely.geometry import LineString, MultiPolygon, Point, Polygon
from shapely.ops import substring

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ocrm_writer import (
    ChunkMeshBuilder,
    Ocm6ElevationField,
    iter_lines,
    iter_polygons,
    latlon_to_world,
    write_ocrm_file,
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OCMAP_PATH = os.path.join(ROOT, "assets", "maps", "seoul_real.ocmap")
OUT_OCRM = os.path.join(ROOT, "assets", "maps", "seoul_roads_hd.ocrm")
WORK_DIR = "/tmp/seoul_osm"

KEEP_HW = {
    "motorway", "motorway_link", "trunk", "trunk_link",
    "primary", "primary_link", "secondary", "secondary_link",
    "tertiary", "tertiary_link", "unclassified", "residential", "living_street",
}
HIGHWAY_TYPES = {"highway.motorway", "highway.motorway_link", "highway.trunk", "highway.trunk_link"}
MAJOR_URBAN_TYPES = {
    "highway.primary", "highway.primary_link",
    "highway.secondary", "highway.secondary_link",
    "highway.tertiary", "highway.tertiary_link",
}

KOREA_TYPES_XML = """<types>
    <type id="highway.motorway"       priority="14" numLanes="4" speed="27.78" width="3.50" oneway="1"/>
    <type id="highway.motorway_link"  priority="13" numLanes="1" speed="16.67" width="3.50" oneway="1"/>
    <type id="highway.trunk"          priority="13" numLanes="3" speed="22.22" width="3.45" oneway="1"/>
    <type id="highway.trunk_link"     priority="12" numLanes="1" speed="13.89" width="3.40" oneway="1"/>
    <type id="highway.primary"        priority="12" numLanes="3" speed="16.67" width="3.25"/>
    <type id="highway.primary_link"   priority="11" numLanes="1" speed="13.89" width="3.25" oneway="1"/>
    <type id="highway.secondary"      priority="11" numLanes="2" speed="13.89" width="3.20"/>
    <type id="highway.secondary_link" priority="10" numLanes="1" speed="11.11" width="3.20" oneway="1"/>
    <type id="highway.tertiary"       priority="10" numLanes="2" speed="13.89" width="3.10"/>
    <type id="highway.tertiary_link"  priority="9"  numLanes="1" speed="11.11" width="3.10" oneway="1"/>
    <type id="highway.unclassified"   priority="4"  numLanes="1" speed="11.11" width="3.00"/>
    <type id="highway.residential"    priority="3"  numLanes="1" speed="8.33"  width="2.90"/>
    <type id="highway.living_street"  priority="2"  numLanes="1" speed="5.56"  width="2.60"/>
</types>"""

# Major named Seoul boulevards & expressways: fill realistic Korean lane counts ONLY when OSM way omits `lanes` tag!
SEOUL_DEFAULT_LANES = (
    ("경부고속도로", 4),
    ("올림픽대로", 4),
    ("강변북로", 4),
    ("내부순환로", 3),
    ("동부간선도로", 3),
    ("서부간선도로", 3),
    ("테헤란로", 4),
    ("강남대로", 4),
    ("세종대로", 4),
    ("영동대로", 4),
    ("반포대로", 4),
    ("한강대로", 4),
)


def split_osm_ground_and_elevated(osm_in: str, elev: Ocm6ElevationField, out_gnd: str, out_elev: str) -> None:
    """Split Seoul OSM roads into clean Ground vs True-Elevated (Han River bridges & viaducts >=3.2m clear), dropping tunnels."""
    t0 = time.time()
    nodes: Dict[str, Tuple[float, float, bool]] = {}
    gnd_ways = []
    elev_ways = []
    gnd_nodes = set()
    elev_nodes = set()

    for _, el in ET.iterparse(osm_in, events=("end",)):
        if el.tag == "node":
            nid = el.attrib["id"]
            lat = float(el.attrib["lat"])
            lon = float(el.attrib["lon"])
            is_tls = any(c.attrib.get("k") == "highway" and c.attrib.get("v") == "traffic_signals" for c in el)
            nodes[nid] = (lat, lon, is_tls)
            el.clear()
        elif el.tag == "way":
            tags = {c.attrib["k"]: c.attrib["v"] for c in el if c.tag == "tag"}
            hw = tags.get("highway", "")
            tun = tags.get("tunnel", "")
            if hw in KEEP_HW and tun not in ("yes", "building_passage", "covered"):
                nids = [c.attrib["ref"] for c in el if c.tag == "nd" and c.attrib["ref"] in nodes]
                if len(nids) >= 2:
                    # Sanitize & fill realistic Korean lane counts on Seoul boulevards & expressways:
                    # 1. Many Korean OSM mappers tag total bidirectional lanes (6, 8, 10) on oneway=yes divided boulevards
                    #    (e.g. 강남대로 lanes=8, 세종대로 lanes=8, 천호대로 lanes=10) or ramps (*_link lanes=8).
                    # 2. Fill missing `lanes` on iconic named Seoul boulevards & expressways.
                    road_name = tags.get("name", "")
                    is_ow_way = (tags.get("oneway", "") == "yes") or (hw in ("motorway", "motorway_link", "trunk_link"))
                    if "lanes" in tags and is_ow_way:
                        try:
                            raw_ln = int(tags["lanes"].split(";")[0].strip())
                            if hw.endswith("_link"):
                                if raw_ln > 2:
                                    tags["lanes"] = "2"
                            elif raw_ln >= 6:
                                fixed_ln = min(5, max(3, raw_ln // 2))
                                tags["lanes"] = str(fixed_ln)
                        except ValueError:
                            pass
                    if "경부고속도로" in road_name and hw in ("trunk", "motorway"):
                        tags["highway"] = "motorway"
                        hw = "motorway"
                        tags["oneway"] = "yes"
                        try:
                            cur_ln = int(tags.get("lanes", "5").split(";")[0].strip())
                        except ValueError:
                            cur_ln = 5
                        if cur_ln < 5:
                            tags["lanes"] = "5"
                    if "lanes" not in tags and hw in ("motorway", "trunk", "primary") and is_ow_way:
                        for kw, def_ln in SEOUL_DEFAULT_LANES:
                            if kw in road_name:
                                tags["lanes"] = str(def_ln)
                                break

                    is_br_tag = (tags.get("bridge", "") not in ("", "no"))
                    try:
                        layer_val = int(tags.get("layer", "0").split(";")[0])
                    except ValueError:
                        layer_val = 0
                    # Check true vertical clearance against 15m bare-earth DTM + Han River water!
                    # Keep ground-hugging expressways (경부고속도로/올림픽대로 culvert bridges < 3.2m clear)
                    # in ONE continuous ground highway network so ZERO transverse parapet seams ever split expressways!
                    is_true_elev = False
                    if is_br_tag or layer_val >= 1:
                        wpts = [latlon_to_world(nodes[n][0], nodes[n][1]) for n in nids]
                        xs = np.array([p[0] for p in wpts], dtype=np.float64)
                        zs = np.array([p[1] for p in wpts], dtype=np.float64)
                        dtm = elev.sample_dtm(xs, zs)
                        if np.any(dtm <= 0.08):
                            is_true_elev = True
                        else:
                            ey = elev.eval_elev_y(xs, zs)
                            clear_thresh = 3.25 if hw in ("motorway", "trunk", "motorway_link", "trunk_link") else 2.40
                            if float(np.percentile(ey - dtm, 75)) >= clear_thresh:
                                is_true_elev = True
                    rec = (el.attrib["id"], nids, tags)
                    if is_true_elev:
                        elev_ways.append(rec)
                        elev_nodes.update(nids)
                    else:
                        gnd_ways.append(rec)
                        gnd_nodes.update(nids)
            el.clear()

    def write_osm(path: str, ways_list, node_set):
        with open(path, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6">\n')
            for nid in node_set:
                lat, lon, is_tls = nodes[nid]
                if is_tls:
                    f.write(f'  <node id="{nid}" lat="{lat:.7f}" lon="{lon:.7f}"><tag k="highway" v="traffic_signals"/></node>\n')
                else:
                    f.write(f'  <node id="{nid}" lat="{lat:.7f}" lon="{lon:.7f}"/>\n')
            for wid, nids, tags in ways_list:
                f.write(f'  <way id="{wid}">\n')
                for nid in nids:
                    f.write(f'    <nd ref="{nid}"/>\n')
                for k, v in tags.items():
                    f.write(f'    <tag k="{escape(k)}" v="{escape(v)}"/>\n')
                f.write("  </way>\n")
            f.write("</osm>\n")

    write_osm(out_gnd, gnd_ways, gnd_nodes)
    write_osm(out_elev, elev_ways, elev_nodes)
    print(f"Split OSM: {len(gnd_ways):,} ground ways, {len(elev_ways):,} true-elevated bridge/viaduct ways in {time.time()-t0:.1f}s")


def run_sumo_netconvert(osm_path: str, out_net: str, typ_path: str, join_dist: int, corner_detail: int) -> None:
    nc = shutil.which("netconvert")
    assert nc is not None, "Eclipse SUMO netconvert not found on PATH!"
    cmd = [
        nc,
        "--osm-files", osm_path,
        "--type-files", typ_path,
        "--output-file", out_net,
        "--default.spreadtype", "center",
        "--osm.oneway-spread-right", "false",
        "--geometry.remove", "true",
        "--junctions.join", "true",
        "--junctions.join-dist", str(join_dist),
        "--junctions.corner-detail", str(corner_detail),
        "--junctions.internal-link-detail", "4",
        "--ramps.guess", "true",
        "--tls.discard-simple", "true",
        "--tls.join", "true",
        "--tls.join-dist", str(join_dist),
        "--output.street-names", "true",
        "--no-warnings", "true",
    ]
    t0 = time.time()
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"netconvert failed on {osm_path}: {res.stderr[:800]}")
    print(f"  SUMO netconvert (centered) -> {os.path.basename(out_net)} ({os.path.getsize(out_net)/1e6:.1f} MB) in {time.time()-t0:.1f}s")


def parse_sumo_net(net_path: str) -> Tuple[List[dict], List[dict]]:
    """Parse SUMO .net.xml into world-space edge records (with individual lane geometries) and junction polygons."""
    t0 = time.time()
    proj = None
    ox, oy = 0.0, 0.0
    edges: List[dict] = []
    juncs: List[dict] = []
    junc_types: Dict[str, str] = {}

    def convert_shape(sh_str: str) -> List[Tuple[float, float]]:
        pts = []
        for tok in sh_str.split():
            parts = tok.split(",")
            sx, sy = float(parts[0]), float(parts[1])
            lon, lat = proj.transform(sx - ox, sy - oy)
            pts.append(latlon_to_world(lat, lon))
        return pts

    # Pass 1: fast iterparse
    for _, el in ET.iterparse(net_path, events=("end",)):
        tag = el.tag
        if tag == "location":
            ox, oy = map(float, el.attrib["netOffset"].split(","))
            proj = pyproj.Transformer.from_crs(el.attrib["projParameter"], "EPSG:4326", always_xy=True)
        elif tag == "edge":
            fn = el.attrib.get("function", "")
            if fn == "":
                etype = el.attrib.get("type", "highway.residential")
                lanes_out = []
                for ln in el.findall("lane"):
                    sh = ln.attrib.get("shape", "")
                    if not sh:
                        continue
                    pts = convert_shape(sh)
                    if len(pts) >= 2:
                        w = float(ln.attrib.get("width", "3.20"))
                        lanes_out.append((pts, w))
                if lanes_out:
                    edges.append({
                        "id": el.attrib["id"],
                        "type": etype,
                        "from": el.attrib.get("from", ""),
                        "to": el.attrib.get("to", ""),
                        "lanes": lanes_out,
                    })
            el.clear()
        elif tag == "junction":
            jt = el.attrib.get("type", "")
            jid = el.attrib.get("id", "")
            if jt != "internal":
                junc_types[jid] = jt
                sh = el.attrib.get("shape", "")
                if sh:
                    pts = convert_shape(sh)
                    if len(pts) >= 3:
                        inc_lanes = el.attrib.get("incLanes", "").split()
                        juncs.append({
                            "id": jid,
                            "type": jt,
                            "pts": pts,
                            "degree": len(inc_lanes),
                        })
            el.clear()

    # Attach destination junction info to edges so we know where to place crosswalks/stop bars
    for e in edges:
        e["to_type"] = junc_types.get(e["to"], "")
        e["from_type"] = junc_types.get(e["from"], "")

    print(f"  Parsed {os.path.basename(net_path)}: {len(edges):,} edges, {len(juncs):,} junctions in {time.time()-t0:.1f}s")
    return edges, juncs


def build_edge_polygon_and_lines(edge: dict) -> Tuple[Optional[Polygon], List[Tuple[str, LineString, float]]]:
    """
    Build exact carriageway polygon from SUMO's outermost lanes plus clean Korean lane-marking polylines:
      - lanes[0] is rightmost lane (curb side in right-hand traffic)
      - lanes[-1] is leftmost lane (center/median side)
    Returns (carriageway_poly, [(kind, line, width), ...]).
    """
    lanes = edge["lanes"]
    n_l = len(lanes)
    etype = edge["type"]
    is_hw = etype in HIGHWAY_TYPES
    is_major = etype in MAJOR_URBAN_TYPES

    lane_lines = [LineString(pts) for pts, _ in lanes]
    lane_widths = [w for _, w in lanes]

    # Exact carriageway polygon = union of per-lane buffered ribbons with flat caps so ends stay razor-flush at junctions!
    polys = []
    for ls, lw in zip(lane_lines, lane_widths):
        if ls.length < 0.2:
            continue
        p = ls.buffer(lw * 0.5 + 0.08, cap_style="flat", join_style="mitre", mitre_limit=2.0)
        if not p.is_empty:
            polys.append(p)
    if not polys:
        return None, []
    poly = unary_union(polys)
    if not isinstance(poly, Polygon):
        ps = iter_polygons(poly)
        if not ps:
            return None, []
        poly = max(ps, key=lambda g: g.area)

    markings: List[Tuple[str, LineString, float]] = []
    # 1. Dashed white lane dividers between adjacent same-direction lanes
    for k in range(n_l - 1):
        # Divider sits on left side of lane k (+half width in SUMO right-hand-drive -> left normal in world (x,z)?)
        # Check coordinate handedness: world +X=East, +Z=South!
        # Moving East (+X, 0): RIGHT is South (+Z), LEFT is North (-Z).
        # In Shapely 2D (x, z), offset_curve(+d) shifts to the LEFT of (dx, dz) in standard math (+Y up),
        # which in (+Z down) screen space is SOUTH (RIGHT)! So offset_curve(-d) is LEFT (toward center/median)!
        ls = lane_lines[k]
        if ls.length < 1.5:
            continue
        try:
            div = ls.offset_curve(-lane_widths[k] * 0.5, join_style="mitre", mitre_limit=2.0)
            for dl in iter_lines(div):
                if dl.length >= 1.5:
                    # On wide Seoul arterials (primary/secondary >=4 lanes), innermost lane divider or outer curb bus lane = Blue Bus Lane!
                    if is_major and n_l >= 4 and (k == n_l - 2):
                        markings.append(("bus_blue", dl, 0.16))
                    else:
                        markings.append(("hw_dash" if is_hw else "dash", dl, 0.14))
        except Exception:
            pass

    # 2. Inner/center boundary of leftmost lane -> Yellow centerline / median edge line
    if is_hw or is_major or n_l >= 2:
        ls_in = lane_lines[-1]
        if ls_in.length >= 2.0:
            try:
                inner = ls_in.offset_curve(-(lane_widths[-1] * 0.5 - 0.14), join_style="mitre", mitre_limit=2.0)
                for il in iter_lines(inner):
                    if il.length >= 2.0:
                        markings.append(("yellow_center", il, 0.14))
            except Exception:
                pass

    # 3. Outer curb boundary of rightmost lane -> Solid white highway shoulder OR Korean yellow curb edge line
    if is_hw or is_major:
        ls_out = lane_lines[0]
        if ls_out.length >= 2.0:
            try:
                outer = ls_out.offset_curve(+(lane_widths[0] * 0.5 - 0.18), join_style="mitre", mitre_limit=2.0)
                for ol in iter_lines(outer):
                    if ol.length >= 2.0:
                        markings.append(("white_edge" if is_hw else "yellow_edge", ol, 0.16 if is_hw else 0.13))
            except Exception:
                pass

    # 4. Crisp Korean Zebra Crosswalk + Stop Bar at major urban junction approaches & exits!
    if is_major and n_l >= 2:
        mid_ls = lane_lines[n_l // 2]
        total_w = sum(lane_widths)
        # Incoming approach -> zebra crosswalk + solid white stop bar
        if mid_ls.length >= 11.0 and edge.get("to_type") in ("traffic_light", "priority", "right_before_left"):
            L = mid_ls.length
            p_end = mid_ls.interpolate(L - 0.4)
            p_pre = mid_ls.interpolate(L - 3.6)
            dx, dz = p_end.x - p_pre.x, p_end.y - p_pre.y
            dl = math.hypot(dx, dz)
            if dl > 0.5:
                tx, tz = dx / dl, dz / dl
                rx, rz = -tz, tx
                p_r = lane_lines[0].interpolate(max(0.0, lane_lines[0].length - 2.4))
                p_l = lane_lines[-1].interpolate(max(0.0, lane_lines[-1].length - 2.4))
                cx_m = 0.5 * (p_r.x + p_l.x)
                cz_m = 0.5 * (p_r.y + p_l.y)
                span = min(
                    math.hypot(p_r.x - p_l.x, p_r.y - p_l.y) + 0.5 * (lane_widths[0] + lane_widths[-1]) - 0.5,
                    total_w - 0.4,
                )
                if span >= 3.2:
                    n_bars = max(3, int(span / 0.92))
                    for bi in range(n_bars):
                        lat = -0.5 * span + (bi + 0.5) * (span / n_bars)
                        bx = cx_m + rx * lat
                        bz = cz_m + rz * lat
                        bar = LineString([(bx - tx * 1.7, bz - tz * 1.7), (bx + tx * 1.7, bz + tz * 1.7)])
                        markings.append(("zebra", bar, 0.46))
                    sx_c = cx_m - tx * 3.1
                    sz_c = cz_m - tz * 3.1
                    stop_bar = LineString([
                        (sx_c - rx * (span * 0.5), sz_c - rz * (span * 0.5)),
                        (sx_c + rx * (span * 0.5), sz_c + rz * (span * 0.5)),
                    ])
                    markings.append(("stop_bar", stop_bar, 0.45))

        # Outgoing exit from signalized junction -> matching pedestrian zebra crosswalk (no stop bar)
        if mid_ls.length >= 6.5 and edge.get("from_type") in ("traffic_light", "priority", "right_before_left"):
            p_start = mid_ls.interpolate(0.4)
            p_nxt = mid_ls.interpolate(min(mid_ls.length, 3.6))
            dx, dz = p_nxt.x - p_start.x, p_nxt.y - p_start.y
            dl = math.hypot(dx, dz)
            if dl > 0.5:
                tx, tz = dx / dl, dz / dl
                rx, rz = -tz, tx
                p_r = lane_lines[0].interpolate(min(lane_lines[0].length, 2.4))
                p_l = lane_lines[-1].interpolate(min(lane_lines[-1].length, 2.4))
                cx_m = 0.5 * (p_r.x + p_l.x)
                cz_m = 0.5 * (p_r.y + p_l.y)
                span = min(
                    math.hypot(p_r.x - p_l.x, p_r.y - p_l.y) + 0.5 * (lane_widths[0] + lane_widths[-1]) - 0.5,
                    total_w - 0.4,
                )
                if span >= 3.2:
                    n_bars = max(3, int(span / 0.92))
                    for bi in range(n_bars):
                        lat = -0.5 * span + (bi + 0.5) * (span / n_bars)
                        bx = cx_m + rx * lat
                        bz = cz_m + rz * lat
                        bar = LineString([(bx - tx * 1.7, bz - tz * 1.7), (bx + tx * 1.7, bz + tz * 1.7)])
                        markings.append(("zebra", bar, 0.46))

    return poly, markings


# Global worker state initialized per fork
_G_ELEV: Optional[Ocm6ElevationField] = None
_G_CHUNKS_DATA: Dict[Tuple[int, int], dict] = {}


def _build_one_chunk(args: Tuple[int, int, float, float, float]) -> Tuple[Tuple[int, int], Optional[Tuple[bytes, int, int, int]]]:
    cx, cz, min_x, min_z, chunk_sz = args
    elev = _G_ELEV
    cdata = _G_CHUNKS_DATA.get((cx, cz))
    if not cdata:
        return (cx, cz), None

    x0 = min_x + cx * chunk_sz
    z0 = min_z + cz * chunk_sz
    x1 = x0 + chunk_sz
    z1 = z0 + chunk_sz
    chunk_box = shp_box(x0, z0, x1, z1)
    halo_box = shp_box(x0 - 20.0, z0 - 20.0, x1 + 20.0, z1 + 20.0)

    cmb = ChunkMeshBuilder(cx, cz, min_x, min_z, chunk_sz)

    # ------------------------------------------------------------------
    # 1. GROUND LAYER: Single-Topology Road Union + Disjoint Sidewalks!
    # ------------------------------------------------------------------
    g_hw_polys = [p.intersection(halo_box) for p in cdata["g_hw"]]
    g_urb_polys = [p.intersection(halo_box) for p in cdata["g_urb"]]
    g_sw_src = [p.intersection(halo_box) for p in cdata["g_sw_src"]]
    g_junc_hw = [p.intersection(halo_box) for p in cdata["g_jhw"]]
    g_junc_urb = [p.intersection(halo_box) for p in cdata["g_jurb"]]

    g_hw_polys = [g for g in g_hw_polys if not g.is_empty]
    g_urb_polys = [g for g in g_urb_polys if not g.is_empty]
    g_sw_src = [g for g in g_sw_src if not g.is_empty]
    g_junc_hw = [g for g in g_junc_hw if not g.is_empty]
    g_junc_urb = [g for g in g_junc_urb if not g.is_empty]

    e_portals_pre = [p.intersection(halo_box) for p in cdata["e_portals"]]
    e_portals_pre = [g for g in e_portals_pre if not g.is_empty]
    portal_mask = unary_union(e_portals_pre) if e_portals_pre else Polygon()

    ground_all = Polygon()
    all_g = g_hw_polys + g_urb_polys + g_junc_hw + g_junc_urb
    if all_g:
        # Morphological closing (+0.42m / -0.42m) seals micro-slivers between trimmed lanes & curved junctions!
        ground_all = unary_union(all_g).buffer(0.42, join_style="mitre", mitre_limit=2.0).buffer(-0.42, join_style="mitre", mitre_limit=2.0)

        # Fill tiny enclosed interior holes (< 58 m^2) trapped inside multi-carriageway crossroads so
        # NO residual median/sidewalk sliver ever appears inside an intersection!
        filled_parts = []
        for poly in iter_polygons(ground_all):
            kept_holes = [h for h in poly.interiors if Polygon(h).area >= 58.0]
            filled_parts.append(Polygon(poly.exterior, kept_holes))
        ground_all = unary_union(filled_parts).simplify(0.14, preserve_topology=True) if filled_parts else Polygon()

        hw_union = unary_union(g_hw_polys + g_junc_hw).intersection(ground_all).simplify(0.14, preserve_topology=True) if (g_hw_polys or g_junc_hw) else Polygon()
        inter_union = unary_union(g_junc_urb).intersection(ground_all).difference(hw_union).simplify(0.14, preserve_topology=True) if g_junc_urb else Polygon()
        urb_union = ground_all.difference(hw_union).difference(inter_union).simplify(0.14, preserve_topology=True)

        # Clip each disjoint asphalt region strictly to this chunk box & emit 3D triangulated meshes!
        k_urb_col = (128, 128, 132, 255)   # r ~ 0.50 -> Tile 0 urban crushed-granite asphalt
        k_hw_col  = ( 48,  50,  54, 255)   # r < 0.35 -> Tile 1 dark Korean expressway SMA
        k_int_col = (212, 212, 216, 255)   # r > 0.65 -> Tile 2 worn intersection asphalt

        for p in iter_polygons(urb_union.intersection(chunk_box)):
            cmb.add_polygon_surface(p, cmb.idx_urban, elev.eval_ground_y, 0.015, k_urb_col, subdiv_step=36.0)
        for p in iter_polygons(hw_union.intersection(chunk_box)):
            cmb.add_polygon_surface(p, cmb.idx_highway, elev.eval_ground_y, 0.018, k_hw_col, subdiv_step=36.0)
        for p in iter_polygons(inter_union.intersection(chunk_box)):
            cmb.add_polygon_surface(p, cmb.idx_inter, elev.eval_ground_y, 0.016, k_int_col, subdiv_step=32.0)

        # MATHEMATICALLY DISJOINT SIDEWALKS, BUILDING-FRONT GRANITE PLAZAS & PURE GRANITE MEDIAN ISLANDS!
        # Extending buffer to 6.50m (0..0.3m curb, 0.3..3.15m pavers+Braille, 3.15..6.50m Tile 4 building setback plaza)
        # simultaneously eliminates sidewalk-to-building trenches AND merges wide boulevard medians into pure granite!
        sw_seeds = g_sw_src
        if sw_seeds:
            major_u = unary_union(sw_seeds).intersection(ground_all)
            if not hw_union.is_empty:
                major_u = major_u.difference(hw_union.buffer(0.75))
            raw_sw = major_u.buffer(6.50, join_style="round", quad_segs=2).difference(ground_all) if not major_u.is_empty else Polygon()
            # Clip sidewalks/plazas cleanly against EXACT synced building polygons (shrunk 8cm so plaza kisses building walls!)
            b_polys_exact = elev.bldg_polys_by_chunk.get((cx, cz), [])
            if b_polys_exact and not raw_sw.is_empty:
                clipped_bp = [bp.buffer(-0.08) for bp in b_polys_exact]
                clipped_bp = [bp for bp in clipped_bp if not bp.is_empty and bp.area > 1.0]
                if clipped_bp:
                    raw_sw = raw_sw.difference(unary_union(clipped_bp))
            raw_sw = raw_sw.simplify(0.22, preserve_topology=True) if not raw_sw.is_empty else Polygon()

            # Identify ALL boulevard median islands (both enclosed holes AND continuous medians touching halo_box!)
            road_touch_zone = ground_all.buffer(0.45)
            halo_edge_zone = halo_box.boundary.buffer(0.65)
            median_polys = []
            for poly in iter_polygons(ground_all):
                for hole in poly.interiors:
                    hp = Polygon(hole)
                    if 58.0 <= hp.area <= 6500.0:
                        median_polys.append(hp)
            for sw_full in iter_polygons(raw_sw):
                if sw_full.area > 4200.0:
                    continue
                non_halo_bnd = sw_full.boundary.difference(halo_edge_zone)
                if non_halo_bnd.length > 2.0:
                    touch_len = non_halo_bnd.intersection(road_touch_zone).length
                    if (touch_len / non_halo_bnd.length) >= 0.72:
                        median_polys.append(sw_full)
            median_hole_union = unary_union(median_polys) if median_polys else Polygon()

            # Build curb-distance KDTree along ground_all boundary for crisp outer sidewalk yellow tactile strips!
            curb_pts = []
            for bl in iter_lines(ground_all.boundary):
                spts = np.asarray(bl.coords, dtype=np.float64)
                for i in range(len(spts) - 1):
                    a, b = spts[i], spts[i + 1]
                    d = math.hypot(b[0] - a[0], b[1] - a[1])
                    steps = max(1, int(math.ceil(d / 2.2)))
                    for k in range(steps):
                        t = k / steps
                        curb_pts.append((a[0] * (1.0 - t) + b[0] * t, a[1] * (1.0 - t) + b[1] * t))
            curb_tree = cKDTree(np.asarray(curb_pts, dtype=np.float64)) if curb_pts else None

            k_sw_col = (198, 200, 204, 255)
            k_curb_col = (182, 184, 188, 255)
            for swp in iter_polygons(raw_sw.intersection(chunk_box)):
                if swp.area < 2.5:
                    continue
                rep = swp.representative_point()
                if elev.is_water_xy(rep.x, rep.y):
                    continue
                is_med = (not median_hole_union.is_empty) and median_hole_union.contains(rep)
                cmb.add_polygon_surface(
                    swp, cmb.idx_sidewalk, elev.eval_ground_y, 0.125, k_sw_col,
                    curb_tree=curb_tree, is_median_island=is_med, subdiv_step=36.0,
                )

            # Watertight 11cm vertical dressed granite curbs ONLY along road-facing curb edges
            if not raw_sw.is_empty:
                road_curb_zone = ground_all.boundary.buffer(0.45)
                sw_bnd = raw_sw.boundary.intersection(road_curb_zone).intersection(chunk_box)
                for bl in iter_lines(sw_bnd):
                    if bl.length < 0.6:
                        continue
                    mp_pt = bl.interpolate(0.5, normalized=True)
                    if elev.is_water_xy(mp_pt.x, mp_pt.y):
                        continue
                    p0_pt = bl.interpolate(max(0.0, bl.length * 0.5 - 0.5))
                    p1_pt = bl.interpolate(min(bl.length, bl.length * 0.5 + 0.5))
                    dx, dz = p1_pt.x - p0_pt.x, p1_pt.y - p0_pt.y
                    dl = math.hypot(dx, dz)
                    out_sign = 1.0
                    if dl > 0.05:
                        nx, nz = -dz / dl, dx / dl
                        if raw_sw.contains(Point(mp_pt.x + nx * 0.25, mp_pt.y + nz * 0.25)):
                            out_sign = -1.0
                    cmb.add_curb_wall_along_ring(
                        np.asarray(bl.coords, dtype=np.float64),
                        elev.eval_ground_y, -0.35, 0.125, cmb.idx_sidewalk, k_curb_col, outward_sign=out_sign,
                    )

        # Ground Expressway Concrete New Jersey Barriers, Anti-Glare Medians & Acoustic Walls
        if not hw_union.is_empty:
            hw_bnd = hw_union.boundary.intersection(chunk_box)
            # Exclude seams where highway merges into ground intersections/ramps OR dead-end portals onto elevated bridges!
            non_hw_touch = ground_all.difference(hw_union).buffer(1.10)
            if not non_hw_touch.is_empty:
                hw_bnd = hw_bnd.difference(non_hw_touch)
            if not portal_mask.is_empty:
                hw_bnd = hw_bnd.difference(portal_mask)
            for bl in iter_lines(hw_bnd):
                if bl.length < 2.5:
                    continue
                mp_pt = bl.interpolate(0.5, normalized=True)
                p0_pt = bl.interpolate(max(0.0, bl.length * 0.5 - 1.0))
                p1_pt = bl.interpolate(min(bl.length, bl.length * 0.5 + 1.0))
                dx, dz = p1_pt.x - p0_pt.x, p1_pt.y - p0_pt.y
                dl = math.hypot(dx, dz)
                if dl < 0.1:
                    continue
                nx, nz = -dz / dl, dx / dl
                out_sign = 1.0 if not hw_union.contains(Point(mp_pt.x + nx * 1.2, mp_pt.y + nz * 1.2)) else -1.0
                ox_n, oz_n = nx * out_sign, nz * out_sign
                is_med = any(
                    hw_union.contains(Point(mp_pt.x + ox_n * dist, mp_pt.y + oz_n * dist))
                    for dist in (2.2, 4.0, 6.5, 9.5, 13.0, 17.0)
                )
                faces_water = (
                    elev.is_water_xy(mp_pt.x + ox_n * 30.0, mp_pt.y + oz_n * 30.0)
                    or elev.is_water_xy(mp_pt.x + ox_n * 75.0, mp_pt.y + oz_n * 75.0)
                )
                cmb.add_jersey_barrier_along_line(
                    bl, elev.eval_ground_y, out_sign, is_median=is_med,
                    with_sound_wall=(not is_med and not faces_water),
                )

        # Crisp Ground Road Markings (strictly clipped outside junction polygons!)
        junc_clip = unary_union(g_junc_urb + g_junc_hw).buffer(0.25) if (g_junc_urb or g_junc_hw) else Polygon()
        k_white = (238, 240, 244, 255)
        k_yellow = (236, 184, 38, 255)
        k_blue = (32, 134, 236, 255)
        for kind, line in cdata["g_marks"]:
            clipped = line.intersection(chunk_box)
            if clipped.is_empty:
                continue
            if kind not in ("zebra", "stop_bar") and not junc_clip.is_empty:
                clipped = clipped.difference(junc_clip)
            for seg in iter_lines(clipped):
                if kind == "dash":
                    cmb.add_paint_polyline(seg, 0.14, k_white, elev.eval_ground_y, 0.036, dashed=(3.0, 5.0))
                elif kind == "hw_dash":
                    cmb.add_paint_polyline(seg, 0.16, k_white, elev.eval_ground_y, 0.038, dashed=(8.0, 12.0))
                elif kind == "yellow_center":
                    cmb.add_paint_polyline(seg, 0.15, k_yellow, elev.eval_ground_y, 0.037)
                elif kind == "yellow_edge":
                    cmb.add_paint_polyline(seg, 0.13, k_yellow, elev.eval_ground_y, 0.035)
                elif kind == "white_edge":
                    cmb.add_paint_polyline(seg, 0.17, k_white, elev.eval_ground_y, 0.037)
                elif kind == "bus_blue":
                    cmb.add_paint_polyline(seg, 0.18, k_blue, elev.eval_ground_y, 0.038)
                elif kind == "zebra":
                    cmb.add_paint_polyline(seg, 0.46, k_white, elev.eval_ground_y, 0.040)
                elif kind == "stop_bar":
                    cmb.add_paint_polyline(seg, 0.45, k_white, elev.eval_ground_y, 0.040)

    # ------------------------------------------------------------------
    # 2. ELEVATED LAYERS: Separate Expressway Flyovers vs Urban Bridges
    #    (keeps Bundang-Suseo flying 13m cleanly above Samseonggyo!)
    # ------------------------------------------------------------------
    k_hw_col = (48, 50, 54, 255)
    k_urb_col = (128, 128, 132, 255)
    k_soffit = (132, 136, 144, 255)
    k_white = (238, 240, 244, 255)
    k_yellow = (236, 184, 38, 255)
    k_pier = (164, 168, 174, 255)

    def emit_elev_layer(poly_list, junc_list, marks_list, pier_list, is_hw_layer: bool):
        ps = [p.intersection(halo_box) for p in (poly_list + junc_list)]
        ps = [g for g in ps if not g.is_empty]
        if not ps:
            return
        eval_fn = elev.eval_elev_hw_y if is_hw_layer else elev.eval_elev_urb_y
        deck_u = unary_union(ps).buffer(0.30, join_style="mitre", mitre_limit=2.0).buffer(-0.30, join_style="mitre", mitre_limit=2.0).simplify(0.14, preserve_topology=True)
        surf_idx = cmb.idx_highway if is_hw_layer else cmb.idx_urban
        surf_col = k_hw_col if is_hw_layer else k_urb_col
        soffit_dz = -1.35 if is_hw_layer else -1.18

        for p in iter_polygons(deck_u.intersection(chunk_box)):
            cmb.add_polygon_surface(p, surf_idx, eval_fn, 0.02, surf_col, subdiv_step=36.0)
            cmb.add_polygon_surface(p, cmb.idx_bridge, eval_fn, soffit_dz, k_soffit, subdiv_step=36.0, flip_down=True)

        # Side box-girder fascia & concrete bridge parapets along ALL true outer edges
        # (ONLY suppress transverse end-caps inside exact dead-end portal_mask — NEVER over lower roads!)
        elev_bnd = deck_u.boundary.intersection(chunk_box)
        if not portal_mask.is_empty:
            elev_bnd = elev_bnd.difference(portal_mask)
        for bl in iter_lines(elev_bnd):
            if bl.length < 1.2:
                continue
            mp_pt = bl.interpolate(0.5, normalized=True)
            p0_pt = bl.interpolate(max(0.0, bl.length * 0.5 - 0.5))
            p1_pt = bl.interpolate(min(bl.length, bl.length * 0.5 + 0.5))
            dx, dz = p1_pt.x - p0_pt.x, p1_pt.y - p0_pt.y
            dl = math.hypot(dx, dz)
            out_sign = 1.0
            if dl > 0.05:
                nx, nz = -dz / dl, dx / dl
                if deck_u.contains(Point(mp_pt.x + nx * 0.35, mp_pt.y + nz * 0.35)):
                    out_sign = -1.0
            cmb.add_curb_wall_along_ring(
                np.asarray(bl.coords, dtype=np.float64),
                eval_fn, soffit_dz, 0.04, cmb.idx_bridge, k_soffit, outward_sign=out_sign, u_cm=0,
            )
            cmb.add_jersey_barrier_along_line(bl, eval_fn, out_sign, is_median=False, with_sound_wall=False)

        for kind, line in marks_list:
            clipped = line.intersection(chunk_box)
            for seg in iter_lines(clipped):
                if kind in ("dash", "hw_dash"):
                    cmb.add_paint_polyline(seg, 0.15, k_white, eval_fn, 0.040, dashed=(8.0, 12.0) if kind == "hw_dash" else (3.0, 5.0))
                elif kind in ("yellow_center", "yellow_edge"):
                    cmb.add_paint_polyline(seg, 0.15, k_yellow, eval_fn, 0.040)
                elif kind == "white_edge":
                    cmb.add_paint_polyline(seg, 0.17, k_white, eval_fn, 0.040)

        # Vertical Korean Concrete Bridge Piers every ~32m (strictly skipping lower road lanes!)
        for p_line in pier_list:
            cl = p_line.intersection(chunk_box)
            for seg in iter_lines(cl):
                s = 14.0
                while s < seg.length - 6.0:
                    pt = seg.interpolate(s)
                    px, pz = float(pt.x), float(pt.y)
                    if not ground_all.is_empty and ground_all.contains(Point(px, pz)):
                        s += 32.0
                        continue
                    top_y = float(eval_fn(np.array([px]), np.array([pz]))[0]) - 1.25
                    bot_y = float(elev.sample_dtm(np.array([px]), np.array([pz]))[0]) - 1.0
                    if top_y - bot_y > 2.2:
                        r = 0.85
                        cmb.add_quad(cmb.idx_bridge, (px - r, bot_y, pz - r), (px + r, bot_y, pz - r), (px + r, top_y, pz - r), (px - r, top_y, pz - r), (0, 0, -1), 0, 0, k_pier)
                        cmb.add_quad(cmb.idx_bridge, (px + r, bot_y, pz - r), (px + r, bot_y, pz + r), (px + r, top_y, pz + r), (px + r, top_y, pz - r), (1, 0, 0), 0, 0, k_pier)
                        cmb.add_quad(cmb.idx_bridge, (px + r, bot_y, pz + r), (px - r, bot_y, pz + r), (px - r, top_y, pz + r), (px + r, top_y, pz + r), (0, 0, 1), 0, 0, k_pier)
                        cmb.add_quad(cmb.idx_bridge, (px - r, bot_y, pz + r), (px - r, bot_y, pz - r), (px - r, top_y, pz - r), (px - r, top_y, pz + r), (-1, 0, 0), 0, 0, k_pier)
                    s += 32.0

    emit_elev_layer(cdata["e_hw"], cdata["e_jhw"], cdata["e_marks_hw"], cdata["e_piers_hw"], is_hw_layer=True)
    emit_elev_layer(cdata["e_urb"], cdata["e_jurb"], cdata["e_marks_urb"], cdata["e_piers_urb"], is_hw_layer=False)

    if cmb.is_empty():
        return (cx, cz), None
    comp, raw_sz = cmb.serialize_compressed()
    r_tris = (len(cmb.idx_urban) + len(cmb.idx_highway) + len(cmb.idx_inter)) // 3
    sw_tris = len(cmb.idx_sidewalk) // 3
    return (cx, cz), (comp, raw_sz, r_tris, sw_tris)


def main() -> None:
    parser = argparse.ArgumentParser(description="Compile Seoul roads via Eclipse SUMO netconvert into OCRM v1")
    parser.add_argument("--work-dir", default=WORK_DIR)
    parser.add_argument("--osm", default=None)
    parser.add_argument("--ocmap", default=OCMAP_PATH)
    parser.add_argument("--out", default=OUT_OCRM)
    parser.add_argument("--workers", type=int, default=max(1, os.cpu_count() or 2))
    parser.add_argument("--reuse-sumo", action="store_true", help="Reuse cached SUMO .net.xml files in work-dir")
    parser.add_argument("--sumo-only", action="store_true", help="Only run SUMO netconvert and exit (before OCM6 sync)")
    args = parser.parse_args()

    work_dir = args.work_dir
    osm_in = args.osm or os.path.join(work_dir, "seoul_roads.osm")

    t_start = time.time()
    print(f"Loading OCM6 v4 elevation & building field from {args.ocmap} ...")
    elev = Ocm6ElevationField(args.ocmap)

    os.makedirs(work_dir, exist_ok=True)
    typ_path = os.path.join(work_dir, "korea_types.typ.xml")
    with open(typ_path, "w", encoding="utf-8") as f:
        f.write(KOREA_TYPES_XML)

    gnd_osm = os.path.join(work_dir, "seoul_ground.osm")
    elev_osm = os.path.join(work_dir, "seoul_elevated.osm")
    gnd_net = os.path.join(work_dir, "seoul_ground.net.xml")
    elev_net = os.path.join(work_dir, "seoul_elevated.net.xml")

    if not (args.reuse_sumo and os.path.isfile(gnd_net) and os.path.isfile(elev_net)):
        split_osm_ground_and_elevated(osm_in, elev, gnd_osm, elev_osm)
        run_sumo_netconvert(gnd_osm, gnd_net, typ_path, join_dist=28, corner_detail=6)
        run_sumo_netconvert(elev_osm, elev_net, typ_path, join_dist=14, corner_detail=4)

    if args.sumo_only:
        print(f"SUMO-only pass complete in {time.time()-t_start:.1f}s: {gnd_net}, {elev_net}")
        return

    g_edges, g_juncs = parse_sumo_net(gnd_net)
    e_edges, e_juncs = parse_sumo_net(elev_net)

    print("Building exact SUMO carriageway & junction polygons and binning into 75x48 chunks ...")
    t_bin = time.time()
    chunks_data: Dict[Tuple[int, int], dict] = {}

    def get_cd(cx: int, cz: int) -> dict:
        d = chunks_data.get((cx, cz))
        if d is None:
            d = {
                "g_hw": [], "g_urb": [], "g_sw_src": [], "g_jhw": [], "g_jurb": [], "g_marks": [],
                "e_hw": [], "e_urb": [], "e_jhw": [], "e_jurb": [],
                "e_marks_hw": [], "e_marks_urb": [], "e_piers_hw": [], "e_piers_urb": [],
                "e_portals": [],
            }
            chunks_data[(cx, cz)] = d
        return d

    def bin_geom(bounds, pad: float = 20.0):
        x0, z0, x1, z1 = bounds
        c0x = max(0, min(elev.cX - 1, int(math.floor((x0 - pad - elev.minX) / elev.chunkSz))))
        c1x = max(0, min(elev.cX - 1, int(math.floor((x1 + pad - elev.minX) / elev.chunkSz))))
        c0z = max(0, min(elev.cZ - 1, int(math.floor((z0 - pad - elev.minZ) / elev.chunkSz))))
        c1z = max(0, min(elev.cZ - 1, int(math.floor((z1 + pad - elev.minZ) / elev.chunkSz))))
        for cz in range(c0z, c1z + 1):
            for cx in range(c0x, c1x + 1):
                yield cx, cz

    # Bin ground edges
    hw_junc_ids = set()
    major_junc_ids = set()
    for e in g_edges:
        is_hw = e["type"] in HIGHWAY_TYPES
        is_sw_road = (e["type"] in MAJOR_URBAN_TYPES) or (not is_hw and len(e["lanes"]) >= 2)
        if is_hw:
            hw_junc_ids.add(e["from"])
            hw_junc_ids.add(e["to"])
        if is_sw_road:
            major_junc_ids.add(e["from"])
            major_junc_ids.add(e["to"])
        poly, marks = build_edge_polygon_and_lines(e)
        if poly is None or poly.is_empty:
            continue
        key = "g_hw" if is_hw else "g_urb"
        for cx, cz in bin_geom(poly.bounds):
            cd = get_cd(cx, cz)
            cd[key].append(poly)
            if is_sw_road:
                cd["g_sw_src"].append(poly)
        for kind, ln, _ in marks:
            for cx, cz in bin_geom(ln.bounds, pad=2.0):
                get_cd(cx, cz)["g_marks"].append((kind, ln))

    # Bin ground junctions
    for j in g_juncs:
        jp = Polygon(j["pts"]).buffer(0)
        if jp.is_empty or jp.area < 1.0:
            continue
        key = "g_jhw" if j["id"] in hw_junc_ids else "g_jurb"
        is_maj_j = j["id"] in major_junc_ids
        for cx, cz in bin_geom(jp.bounds):
            cd = get_cd(cx, cz)
            cd[key].append(jp)
            if is_maj_j and key == "g_jurb":
                cd["g_sw_src"].append(jp)

    # Bin elevated edges & junctions separately into expressway flyovers vs urban bridges,
    # and mark exact dead-end bridge entry/exit portals so ZERO transverse parapet caps ever form!
    e_node_deg: Dict[str, int] = {}
    e_hw_junc_ids = set()
    for e in e_edges:
        e_node_deg[e["from"]] = e_node_deg.get(e["from"], 0) + 1
        e_node_deg[e["to"]] = e_node_deg.get(e["to"], 0) + 1
        if e["type"] in HIGHWAY_TYPES:
            e_hw_junc_ids.add(e["from"])
            e_hw_junc_ids.add(e["to"])

    for e in e_edges:
        is_hw = e["type"] in HIGHWAY_TYPES
        poly, marks = build_edge_polygon_and_lines(e)
        if poly is None or poly.is_empty:
            continue
        key = "e_hw" if is_hw else "e_urb"
        pier_key = "e_piers_hw" if is_hw else "e_piers_urb"
        mark_key = "e_marks_hw" if is_hw else "e_marks_urb"
        mid_pts = e["lanes"][len(e["lanes"]) // 2][0]
        mid_ln = LineString(mid_pts)
        tot_w = sum(lw for _, lw in e["lanes"])
        for cx, cz in bin_geom(poly.bounds):
            cd = get_cd(cx, cz)
            cd[key].append(poly)
            cd[pier_key].append(mid_ln)
        for kind, ln, _ in marks:
            if kind in ("dash", "hw_dash", "yellow_center", "yellow_edge", "white_edge"):
                for cx, cz in bin_geom(ln.bounds, pad=2.0):
                    get_cd(cx, cz)[mark_key].append((kind, ln))
        # Exact dead-end portal circles where elevated bridge/viaduct meets ground approaches
        if e_node_deg.get(e["from"], 0) <= 1:
            pcap = Point(mid_pts[0]).buffer(tot_w * 0.5 + 2.8)
            for cx, cz in bin_geom(pcap.bounds, pad=4.0):
                get_cd(cx, cz)["e_portals"].append(pcap)
        if e_node_deg.get(e["to"], 0) <= 1:
            pcap = Point(mid_pts[-1]).buffer(tot_w * 0.5 + 2.8)
            for cx, cz in bin_geom(pcap.bounds, pad=4.0):
                get_cd(cx, cz)["e_portals"].append(pcap)

    for j in e_juncs:
        jp = Polygon(j["pts"]).buffer(0)
        if jp.is_empty or jp.area < 1.0:
            continue
        jkey = "e_jhw" if j["id"] in e_hw_junc_ids else "e_jurb"
        for cx, cz in bin_geom(jp.bounds):
            get_cd(cx, cz)[jkey].append(jp)

    print(f"Binned geometry across {len(chunks_data):,} active Seoul chunks in {time.time()-t_bin:.1f}s. Baking 3D meshes ...")

    global _G_ELEV, _G_CHUNKS_DATA
    _G_ELEV = elev
    _G_CHUNKS_DATA = chunks_data

    tasks = [(cx, cz, elev.minX, elev.minZ, elev.chunkSz) for (cx, cz) in sorted(chunks_data.keys())]
    chunk_blobs: Dict[Tuple[int, int], Tuple[bytes, int, int, int]] = {}
    t_bake = time.time()

    if args.workers > 1:
        with mp.Pool(processes=args.workers) as pool:
            done = 0
            for key, blob in pool.imap_unordered(_build_one_chunk, tasks, chunksize=16):
                if blob is not None:
                    chunk_blobs[key] = blob
                done += 1
                if done % 500 == 0 or done == len(tasks):
                    print(f"  Baked {done:,}/{len(tasks):,} chunks ({time.time()-t_bake:.1f}s) ...", flush=True)
    else:
        for i, t in enumerate(tasks, 1):
            key, blob = _build_one_chunk(t)
            if blob is not None:
                chunk_blobs[key] = blob
            if i % 500 == 0 or i == len(tasks):
                print(f"  Baked {i:,}/{len(tasks):,} chunks ({time.time()-t_bake:.1f}s) ...", flush=True)

    fsize, r_tris, sw_tris = write_ocrm_file(
        args.out,
        elev.minX, elev.maxX, elev.minZ, elev.maxZ, elev.chunkSz,
        elev.cX, elev.cZ,
        chunk_blobs,
        flags=5,  # bit0 = LZ4 compressed, bit1 = 0 (SUMO netconvert), bit2 = SoA+Delta16
    )
    print(
        f"SUCCESS! Wrote {args.out}: {fsize/1e6:.2f} MB ({len(chunk_blobs):,} chunks, "
        f"{r_tris:,} road tris, {sw_tris:,} sidewalk/curb tris) in {time.time()-t_start:.1f}s!"
    )


if __name__ == "__main__":
    main()
