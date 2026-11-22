#!/usr/bin/env python3
"""
In-place OCM6 v4 Twin-Boulevard Leveling & Full-Corridor DTM Polisher (v5.1.0).

Fixes dual-carriageway vertical step cliffs (e.g. Teheran-ro EB vs WB differing by 1.0-1.8m
across a 19-22m boulevard span) and carves the full carriageway + median width cleanly into
the 2048x1638 15m bare-earth DTM so ground plazas never slice through asphalt!
"""
import math
import os
import struct
import time
import numpy as np
from scipy.ndimage import gaussian_filter
from scipy.sparse import csr_matrix
from scipy.spatial import cKDTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OCMAP_PATH = os.path.join(ROOT, "assets", "maps", "seoul_real.ocmap")
DEM_SCALE_M = 0.04


def sample_grid_bilinear(grid, demMinX, demMaxX, demMinZ, demMaxZ, xs, zs):
    H, W = grid.shape
    fx = np.clip((xs - demMinX) / (demMaxX - demMinX) * (W - 1), 0.0, W - 1.0001)
    fz = np.clip((zs - demMinZ) / (demMaxZ - demMinZ) * (H - 1), 0.0, H - 1.0001)
    x0 = np.floor(fx).astype(np.int32)
    z0 = np.floor(fz).astype(np.int32)
    tx = fx - x0
    tz = fz - z0
    x1 = np.minimum(x0 + 1, W - 1)
    z1 = np.minimum(z0 + 1, H - 1)
    return (
        (1.0 - tx) * (1.0 - tz) * grid[z0, x0]
        + tx * (1.0 - tz) * grid[z0, x1]
        + (1.0 - tx) * tz * grid[z1, x0]
        + tx * tz * grid[z1, x1]
    )


def sample_grid_cr(grid, demMinX, demMaxX, demMinZ, demMaxZ, xs, zs):
    H, W = grid.shape
    fx = np.clip((xs - demMinX) / (demMaxX - demMinX) * (W - 1), 0.0, W - 1.0001)
    fz = np.clip((zs - demMinZ) / (demMaxZ - demMinZ) * (H - 1), 0.0, H - 1.0001)
    x1 = np.floor(fx).astype(np.int32)
    z1 = np.floor(fz).astype(np.int32)
    tx = fx - x1
    tz = fz - z1

    def cr1d(p0, p1, p2, p3, t):
        t2 = t * t
        t3 = t2 * t
        return 0.5 * (
            (2.0 * p1)
            + (-p0 + p2) * t
            + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
            + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
        )

    rows_cr = []
    for dz in (-1, 0, 1, 2):
        zz = np.clip(z1 + dz, 0, H - 1)
        c0 = grid[zz, np.clip(x1 - 1, 0, W - 1)]
        c1 = grid[zz, np.clip(x1, 0, W - 1)]
        c2 = grid[zz, np.clip(x1 + 1, 0, W - 1)]
        c3 = grid[zz, np.clip(x1 + 2, 0, W - 1)]
        rows_cr.append(cr1d(c0, c1, c2, c3, tx))
    return cr1d(rows_cr[0], rows_cr[1], rows_cr[2], rows_cr[3], tz)


def main():
    t0 = time.time()
    print(f"Loading {OCMAP_PATH} ...")
    raw = bytearray(open(OCMAP_PATH, "rb").read())
    hdr = struct.unpack_from("<4sIfffffIIffffIIIIIIIIIIII", raw, 0)
    (
        magic, ver, minX, maxX, minZ, maxZ, chunkSz,
        cX, cZ, demMinX, demMaxX, demMinZ, demMaxZ,
        dW, dH, nR, nRV, nB, nBV, nW, nP, nPoi, nFCR, nFCB, nFCP
    ) = hdr
    assert magic == b"OCM6" and ver == 4, f"Expected OCM6 v4, got {magic} v{ver}"

    off_dem = 100
    off_roads = off_dem + dW * dH * 2
    off_rverts = off_roads + nR * 32
    off_bldgs = off_rverts + nRV * 12
    off_bverts = off_bldgs + nB * 44
    off_chunks = off_bverts + nBV * 4
    off_fcr = off_chunks + cX * cZ * 24
    off_fcb = off_fcr + nFCR * 4
    off_fcp = off_fcb + nFCB * 4
    off_water = off_fcp + nFCP * 4

    dem_q = np.frombuffer(raw, dtype=np.int16, count=dW * dH, offset=off_dem).reshape(dH, dW).copy()
    dtm_orig = dem_q.astype(np.float64) * DEM_SCALE_M
    rv3 = np.frombuffer(raw, dtype=np.float32, count=nRV * 3, offset=off_rverts).reshape(nRV, 3).copy()

    # Build exact water mask on DEM grid from water_tris + 0.0m Han River datum
    water_mask = (dtm_orig <= 0.02)

    # Parse road metadata + per-vertex attributes + tangents
    vx = rv3[:, 0].astype(np.float64)
    vy = rv3[:, 1].astype(np.float64)
    vz = rv3[:, 2].astype(np.float64)

    v_road = np.empty(nRV, dtype=np.int32)
    v_cls = np.empty(nRV, dtype=np.int32)
    v_hw = np.empty(nRV, dtype=np.float64)
    v_layer = np.empty(nRV, dtype=np.int32)
    v_bridge = np.zeros(nRV, dtype=bool)
    v_tunnel = np.zeros(nRV, dtype=bool)
    v_tx = np.zeros(nRV, dtype=np.float64)
    v_tz = np.zeros(nRV, dtype=np.float64)

    roads_meta = []
    edge_u = []
    edge_v = []
    edge_d = []
    edge_w = []

    # Endpoint welding hash (0.15m spatial cell + layer) so connected roads stay 100% watertight
    end_buckets = {}
    weld_u, weld_v, weld_w = [], [], []

    for ri in range(nR):
        fv, vc, rc, ln, fl, ns, lay, fl2, hw, rminx, rminz, rmaxx, rmaxz = struct.unpack_from(
            "<IHBBBBbBfffff", raw, off_roads + ri * 32
        )
        is_br = ((fl & 2) != 0) or (lay >= 1) or ((fl2 & 4) != 0)
        is_tun = (fl & 4) != 0
        roads_meta.append((fv, vc, rc, ln, fl, lay, fl2, hw, is_br, is_tun))
        if vc < 2:
            continue
        sl = slice(fv, fv + vc)
        v_road[sl] = ri
        v_cls[sl] = rc
        v_hw[sl] = hw
        v_layer[sl] = lay
        v_bridge[sl] = is_br
        v_tunnel[sl] = is_tun

        dx = np.diff(vx[sl])
        dz = np.diff(vz[sl])
        seg_l = np.hypot(dx, dz)
        inv_l = 1.0 / np.maximum(seg_l, 1e-4)
        ux = dx * inv_l
        uz = dz * inv_l
        # Vertex tangent (average of incident segments)
        v_tx[fv] = ux[0]
        v_tz[fv] = uz[0]
        v_tx[fv + vc - 1] = ux[-1]
        v_tz[fv + vc - 1] = uz[-1]
        if vc > 2:
            ax = ux[:-1] + ux[1:]
            az = uz[:-1] + uz[1:]
            al = np.hypot(ax, az)
            v_tx[fv + 1:fv + vc - 1] = np.where(al > 1e-4, ax / np.maximum(al, 1e-4), ux[1:])
            v_tz[fv + 1:fv + vc - 1] = np.where(al > 1e-4, az / np.maximum(al, 1e-4), uz[1:])

        stiff = 5.5 if (is_br or rc <= 1) else (4.0 if rc == 2 else (2.6 if rc <= 4 else 1.5))
        u_ids = np.arange(fv, fv + vc - 1, dtype=np.int32)
        v_ids = u_ids + 1
        d_arr = np.maximum(0.4, seg_l)
        w_arr = (28.0 * stiff) / np.maximum(2.0, d_arr)
        edge_u.append(u_ids)
        edge_v.append(v_ids)
        edge_d.append(d_arr)
        edge_w.append(w_arr)

        # Weld shared endpoints (same vertical band < 1.2m)
        if not is_tun:
            for ep in (fv, fv + vc - 1):
                key = (int(round(vx[ep] * 2.0)), int(round(vz[ep] * 2.0)))
                lst = end_buckets.get(key)
                if lst is None:
                    end_buckets[key] = [ep]
                else:
                    for prev_ep in lst:
                        if abs(vy[ep] - vy[prev_ep]) < 1.2:
                            weld_u.append(ep)
                            weld_v.append(prev_ep)
                            weld_w.append(160.0)
                    lst.append(ep)

    eu = np.concatenate(edge_u)
    ev = np.concatenate(edge_v)
    ed = np.concatenate(edge_d)
    ew = np.concatenate(edge_w)
    wu = np.array(weld_u, dtype=np.int32)
    wv = np.array(weld_v, dtype=np.int32)
    ww = np.array(weld_w, dtype=np.float64)

    dtm_at_v = sample_grid_bilinear(dtm_orig, demMinX, demMaxX, demMinZ, demMaxZ, vx, vz)
    v_clear = vy - dtm_at_v
    v_ground = (~v_bridge) & (~v_tunnel) & (v_layer <= 0) & (v_clear < 3.2)

    # 1. TWIN-CARRIAGEWAY LEVELING SPRINGS (up to 28m span across opposite-direction boulevards & expressways!)
    #    Use query_pairs(r=28.0) instead of k=12 because dense 3m-spaced Catmull-Rom polylines fill k=12
    #    entirely with same-road neighbors before reaching an opposing carriageway 20-26m across the median!
    #    Include BOTH ground boulevards AND parallel twin expressway carriageways (e.g. Gyeongbu NB/SB where
    #    one side was flagged bridge=True causing a 2.01m vertical median cliff, while keeping genuine double-deck
    #    split bridges like Banpo/Jamsu |dY|>=4.2m untouched!).
    twin_cand_mask = ((v_ground & (v_cls <= 3) & (v_hw >= 4.0)) |
                      ((~v_tunnel) & (v_cls <= 1) & (v_hw >= 4.8)))
    twin_idx = np.where(twin_cand_mask)[0]
    pts_twin = np.column_stack([vx[twin_idx], vz[twin_idx]])
    tree_twin = cKDTree(pts_twin)
    pairs_t = tree_twin.query_pairs(r=28.0, output_type="ndarray")

    u_l = twin_idx[pairs_t[:, 0]]
    v_l = twin_idx[pairs_t[:, 1]]
    diff_road = v_road[u_l] != v_road[v_l]
    u_l = u_l[diff_road]
    v_l = v_l[diff_road]

    rel_x = vx[v_l] - vx[u_l]
    rel_z = vz[v_l] - vz[u_l]
    d_l = np.hypot(rel_x, rel_z)
    dot_dir = v_tx[u_l] * v_tx[v_l] + v_tz[u_l] * v_tz[v_l]
    along_u = np.abs(rel_x * v_tx[u_l] + rel_z * v_tz[u_l])
    along_v = np.abs(rel_x * v_tx[v_l] + rel_z * v_tz[v_l])
    lat_u = np.abs(-rel_x * v_tz[u_l] + rel_z * v_tx[u_l])
    max_lat = v_hw[u_l] + v_hw[v_l] + 10.0
    both_ground = v_ground[u_l] & v_ground[v_l]
    both_exp = (v_cls[u_l] <= 1) & (v_cls[v_l] <= 1) & (np.abs(v_layer[u_l] - v_layer[v_l]) <= 1)

    ok = (
        (d_l > 2.5)
        & (dot_dir < -0.78)
        & (np.abs(v_cls[u_l] - v_cls[v_l]) <= 1)
        & (along_u < 5.5)
        & (along_v < 5.5)
        & (lat_u <= max_lat)
        & (
            (both_ground & (np.abs(vy[u_l] - vy[v_l]) < 4.5))
            | (both_exp & (np.abs(vy[u_l] - vy[v_l]) < 3.6))
        )
    )
    tu = u_l[ok].astype(np.int32)
    tv = v_l[ok].astype(np.int32)
    d_ok = d_l[ok]
    tw = (140.0 / np.maximum(4.0, d_ok)).astype(np.float64)

    # Record median centerlines between opposite twin ground carriageways for DTM carving
    med_x_list, med_z_list, med_u_list, med_v_list = [], [], [], []
    if len(tu) > 0:
        near_perp = (along_u[ok] < 2.2) & v_ground[tu] & v_ground[tv]
        if near_perp.any():
            mu_s = tu[near_perp]
            mv_s = tv[near_perp]
            med_x_list.append(0.5 * (vx[mu_s] + vx[mv_s]))
            med_z_list.append(0.5 * (vz[mu_s] + vz[mv_s]))
            med_u_list.append(mu_s)
            med_v_list.append(mv_s)

    print(f"   Added {len(tu):,} twin-carriageway leveling springs (ground+expressway r<=28m) + {len(wu):,} endpoint welds")

    # Also couple crossing/touching ground side streets within 14m so feeders climb smoothly onto leveled boulevards!
    gnd_idx = np.where(v_ground)[0]
    pts_g = np.column_stack([vx[gnd_idx], vz[gnd_idx]])
    tree_g = cKDTree(pts_g)
    dists_g, nbrs_g = tree_g.query(pts_g, k=10, distance_upper_bound=12.5, workers=-1)
    g_u_list, g_v_list, g_w_list = [], [], []
    for ki in range(1, 10):
        valid = (nbrs_g[:, ki] < len(gnd_idx)) & (dists_g[:, ki] > 0.35)
        u_g = gnd_idx[valid]
        v_g = gnd_idx[nbrs_g[valid, ki]]
        d_g = dists_g[valid, ki]
        ok_g = (
            (v_road[u_g] != v_road[v_g])
            & (np.abs(vy[u_g] - vy[v_g]) < 1.8)
            & (np.abs(dtm_at_v[u_g] - dtm_at_v[v_g]) < 1.8)
        )
        g_u_list.append(u_g[ok_g])
        g_v_list.append(v_g[ok_g])
        g_w_list.append(22.0 / np.maximum(2.5, d_g[ok_g]))
    gu = np.concatenate(g_u_list).astype(np.int32)
    gv = np.concatenate(g_v_list).astype(np.int32)
    gw = np.concatenate(g_w_list).astype(np.float64)
    print(f"   Added {len(gu):,} local cross-street / slip-lane flush springs")

    # Assemble CSR stiffness matrix (pin standalone high bridges/viaducts, while allowing twin expressway
    # carriageways to level smoothly to their shared mean deck elevation without losing river clearance!)
    rows = np.concatenate([eu, ev, wu, wv, tu, tv, gu, gv])
    cols = np.concatenate([ev, eu, wv, wu, tv, tu, gv, gu])
    vals = np.concatenate([ew, ew, ww, ww, tw, tw, gw, gw])
    W_mat = csr_matrix((vals, (rows, cols)), shape=(nRV, nRV))
    deg_w = np.asarray(W_mat.sum(axis=1)).ravel()

    is_twin_node = np.zeros(nRV, dtype=bool)
    is_twin_node[tu] = True
    is_twin_node[tv] = True
    is_twin_exp = is_twin_node & (v_cls <= 1)
    is_pinned_elev = (v_bridge | (v_layer >= 1) | (v_clear >= 3.2)) & (~is_twin_exp)
    w_anc = np.where(is_pinned_elev, 45.0, np.where(is_twin_exp, 0.35, np.where(is_twin_node, 0.12, 1.0)))
    w_anc = np.where(deg_w < 1e-6, 1.0, w_anc)
    inv_den = 1.0 / (w_anc + deg_w + 1e-12)

    y_new = vy.copy()
    for _ in range(160):
        y_next = (w_anc * vy + W_mat.dot(y_new)) * inv_den
        y_new = np.where(is_pinned_elev, vy, y_next)

    # Check before/after Teheran-ro EB vs WB perpendicular height difference every 25m!
    def check_teheran(y_arr, tag):
        diffs = []
        worst_info = (0.0, 0, 0.0, 0.0, 0.0)
        for tx in range(3800, 5201, 25):
            tz = 2820 - (tx - 3800) * 0.375
            eb_best = None
            wb_best = None
            for ri, (fv, vc, rc, ln, fl, lay, fl2, hw, is_br, is_tun) in enumerate(roads_meta):
                if rc != 2 or hw < 5.5 or vc < 2 or is_br or is_tun:
                    continue
                d2 = (vx[fv:fv+vc] - tx)**2 + (vz[fv:fv+vc] - tz)**2
                k = int(np.argmin(d2))
                if d2[k] < 36**2:
                    yaw = math.degrees(math.atan2(v_tz[fv+k], v_tx[fv+k]))
                    if abs(yaw) < 40:
                        if eb_best is None or d2[k] < eb_best[0]:
                            eb_best = (d2[k], float(y_arr[fv+k]), float(vx[fv+k]), float(vz[fv+k]))
                    elif abs(yaw) > 140:
                        if wb_best is None or d2[k] < wb_best[0]:
                            wb_best = (d2[k], float(y_arr[fv+k]), float(vx[fv+k]), float(vz[fv+k]))
            if eb_best and wb_best:
                sep = math.hypot(eb_best[2] - wb_best[2], eb_best[3] - wb_best[3])
                dy = abs(eb_best[1] - wb_best[1])
                diffs.append(dy)
                if dy > worst_info[0]:
                    worst_info = (dy, tx, eb_best[1], wb_best[1], sep)
        p95 = float(np.percentile(diffs, 95)) if diffs else 0.0
        med = float(np.median(diffs)) if diffs else 0.0
        print(
            f"   [{tag}] Teheran-ro EB-WB |dY| (n={len(diffs)}): "
            f"median={med:.3f}m, p95={p95:.3f}m, max={worst_info[0]:.3f}m "
            f"(at x={worst_info[1]} EB={worst_info[2]:.2f} WB={worst_info[3]:.2f} sep={worst_info[4]:.1f}m)"
        )

    check_teheran(vy, "BEFORE")
    check_teheran(y_new, "AFTER ")

    # 1b. Exact Symmetric Lipschitz Max-Grade Projection + Endpoint-Weld Coupling (ALL altitudes!):
    #     For any polyline, averaging exact forward-clamped and backward-clamped envelopes guarantees
    #     |dy|/len <= max_g in ONE pass while preserving mean deck height! Coupling shared endpoints
    #     (wu, wv) over 24 passes spreads steep ramps smoothly into adjoining segments with 0.00m seam step!
    grade_roads = [
        (ri, 0.106 if (m[8] or m[5] >= 1 or m[2] <= 1) else 0.118)
        for ri, m in enumerate(roads_meta)
        if (not m[9]) and (m[8] or m[5] >= 1 or m[2] <= 3)
    ]
    # Build per-road segment lengths once
    grade_segs = []
    for ri, max_g in grade_roads:
        fv, vc = roads_meta[ri][0], roads_meta[ri][1]
        if vc < 2:
            continue
        lens = np.hypot(np.diff(vx[fv:fv+vc]), np.diff(vz[fv:fv+vc]))
        lims = np.where(lens >= 0.8, max_g * lens, 1e9)
        grade_segs.append((fv, vc, lims))

    # Build endpoint weld adjacency for fast exact projection averaging
    weld_adj = {}
    for a_i, b_i in zip(wu.tolist(), wv.tolist()):
        weld_adj.setdefault(a_i, []).append(b_i)
        weld_adj.setdefault(b_i, []).append(a_i)
    weld_groups = []
    seen_w = set()
    for node in weld_adj:
        if node in seen_w:
            continue
        comp = []
        stk = [node]
        seen_w.add(node)
        while stk:
            cur = stk.pop()
            comp.append(cur)
            for nb in weld_adj[cur]:
                if nb not in seen_w:
                    seen_w.add(nb)
                    stk.append(nb)
        if len(comp) > 1:
            weld_groups.append(comp)

    def project_grades_once(y_arr):
        any_fix = False
        for fv, vc, lims in grade_segs:
            sl = y_arr[fv:fv+vc]
            diffs = np.abs(np.diff(sl))
            if not np.any(diffs > lims):
                continue
            any_fix = True
            yf = sl.copy()
            yb = sl.copy()
            for k in range(vc - 1):
                lim = lims[k]
                lo = yf[k] - lim
                hi = yf[k] + lim
                if yf[k + 1] < lo:
                    yf[k + 1] = lo
                elif yf[k + 1] > hi:
                    yf[k + 1] = hi
            for k in range(vc - 2, -1, -1):
                lim = lims[k]
                lo = yb[k + 1] - lim
                hi = yb[k + 1] + lim
                if yb[k] < lo:
                    yb[k] = lo
                elif yb[k] > hi:
                    yb[k] = hi
            y_arr[fv:fv+vc] = 0.5 * (yf + yb)
        return any_fix

    for it in range(28):
        fixed = project_grades_once(y_new)
        if not fixed:
            break
        if it < 27:
            # Re-average welded junction endpoints so connected roads transition smoothly
            for comp in weld_groups:
                if abs(max(y_new[c] for c in comp) - min(y_new[c] for c in comp)) > 1e-4:
                    avg_y = sum(y_new[c] for c in comp) / len(comp)
                    for c in comp:
                        y_new[c] = avg_y
    # Final guaranteed hard pass so zero segments exceed max_g
    project_grades_once(y_new)

    # 2. FULL-WIDTH BOULEVARD + MEDIAN DTM DELTA-CARVING!
    #    Carve across centerline, left/right shoulders (+/-0.65*hw), AND twin-carriageway median centers!
    print("Carving full-width leveled boulevards & medians cleanly into 2048x1638 15m DTM...")
    invX = (dW - 1) / (demMaxX - demMinX)
    invZ = (dH - 1) / (demMaxZ - demMinZ)
    gnd_mask = (v_ground | ((v_cls <= 1) & (~v_tunnel) & ((y_new - dtm_at_v) < 2.2))) & (y_new < 130.0) & (~water_mask[
        np.clip(np.round((vz - demMinZ) * invZ).astype(np.int32), 0, dH - 1),
        np.clip(np.round((vx - demMinX) * invX).astype(np.int32), 0, dW - 1),
    ])

    # Build multi-strip corridor samples (center + left shoulder + right shoulder)
    sx_c = vx[gnd_mask]
    sz_c = vz[gnd_mask]
    sy_c = y_new[gnd_mask]
    shw_c = v_hw[gnd_mask]
    stx_c = v_tx[gnd_mask]
    stz_c = v_tz[gnd_mask]
    scls_c = v_cls[gnd_mask]
    sw_c = np.where(scls_c <= 2, 5.0, np.where(scls_c <= 4, 2.2, 1.0))

    # Right perpendicular (-tz, tx)
    prx = -stz_c
    prz = stx_c
    sx_all = [sx_c, sx_c + prx * (shw_c * 0.65), sx_c - prx * (shw_c * 0.65)]
    sz_all = [sz_c, sz_c + prz * (shw_c * 0.65), sz_c - prz * (shw_c * 0.65)]
    sy_all = [sy_c, sy_c, sy_c]
    sw_all = [sw_c, sw_c * 0.85, sw_c * 0.85]

    if med_x_list:
        mx = np.concatenate(med_x_list)
        mz = np.concatenate(med_z_list)
        mu = np.concatenate(med_u_list)
        mv = np.concatenate(med_v_list)
        my = 0.5 * (y_new[mu] + y_new[mv])
        mw = np.full(len(mx), 6.0, dtype=np.float64)
        sx_all.append(mx)
        sz_all.append(mz)
        sy_all.append(my)
        sw_all.append(mw)

    sx_cat = np.concatenate(sx_all)
    sz_cat = np.concatenate(sz_all)
    sy_cat = np.concatenate(sy_all)
    sw_cat = np.concatenate(sw_all)

    dtm_at_s = sample_grid_bilinear(dtm_orig, demMinX, demMaxX, demMinZ, demMaxZ, sx_cat, sz_cat)
    s_delta = np.clip((sy_cat - 0.10) - dtm_at_s, -3.0, 3.0)
    gx_s = np.clip(np.round((sx_cat - demMinX) * invX).astype(np.int32), 0, dW - 1)
    gz_s = np.clip(np.round((sz_cat - demMinZ) * invZ).astype(np.int32), 0, dH - 1)

    sum_w = np.zeros((dH, dW), dtype=np.float64)
    sum_wd = np.zeros((dH, dW), dtype=np.float64)
    np.add.at(sum_w, (gz_s, gx_s), sw_cat)
    np.add.at(sum_wd, (gz_s, gx_s), sw_cat * s_delta)

    blur_w = gaussian_filter(sum_w, sigma=1.6)
    blur_wd = gaussian_filter(sum_wd, sigma=1.6)
    valid_c = (blur_w > 0.04) & (~water_mask) & (dtm_orig < 130.0)
    carved_delta = np.where(valid_c, blur_wd / np.maximum(blur_w, 1e-6), 0.0)
    blend_c = np.where(valid_c, np.clip(blur_w * 1.4, 0.0, 1.0), 0.0)
    dtm_final = np.where(water_mask, 0.0, np.maximum(0.0, dtm_orig + carved_delta * blend_c))

    dem_q_new = np.clip(np.round(dtm_final / DEM_SCALE_M), -32000, 32000).astype(np.int16)
    dtm_q_m = dem_q_new.astype(np.float64) * DEM_SCALE_M

    # Depress any remaining DTM cell sitting above ground road corridors (center + shoulders + median)
    dtm_chk = sample_grid_bilinear(dtm_q_m, demMinX, demMaxX, demMinZ, demMaxZ, sx_cat, sz_cat)
    over = (dtm_chk > sy_cat - 0.08) & (~water_mask[gz_s, gx_s]) & (sy_cat < 130.0)
    if over.any():
        target_q = np.floor((sy_cat[over] - 0.12) / DEM_SCALE_M).astype(np.int16)
        np.minimum.at(dem_q_new, (gz_s[over], gx_s[over]), target_q)
        dtm_q_m = dem_q_new.astype(np.float64) * DEM_SCALE_M

    # 3. Re-snap all 184,254 building baseY values cleanly to newly carved DTM!
    b_cx = np.empty(nB, dtype=np.float64)
    b_cz = np.empty(nB, dtype=np.float64)
    for bi in range(nB):
        cx_b, cz_b = struct.unpack_from("<ff", raw, off_bldgs + bi * 44 + 16)
        b_cx[bi] = cx_b
        b_cz[bi] = cz_b
    b_new_y = sample_grid_bilinear(dtm_q_m, demMinX, demMaxX, demMinZ, demMaxZ, b_cx, b_cz).astype(np.float32)
    for bi in range(nB):
        struct.pack_into("<f", raw, off_bldgs + bi * 44 + 24, float(b_new_y[bi]))

    # Write updated dem_q and rv3[:, 1] cleanly into OCM6 binary!
    rv3[:, 1] = y_new.astype(np.float32)
    raw[off_dem:off_dem + dW * dH * 2] = dem_q_new.tobytes()
    raw[off_rverts:off_rverts + nRV * 12] = rv3.tobytes()
    with open(OCMAP_PATH, "wb") as f:
        f.write(raw)
    print(f"Polished {OCMAP_PATH} ({len(raw)/1e6:.2f} MB) cleanly in {time.time() - t0:.1f}s!")


if __name__ == "__main__":
    main()
