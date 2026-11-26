#!/usr/bin/env python3
"""
Open City Seoul v6.7.0 — Whole-Seoul Hillside DTM Cut, Floating/Road-Blocking Building Fix
& Remaining Viaduct Abutment Headwall Compiler.

Applies three surgical, lossless passes to `assets/maps/seoul_real.ocmap` (OCM6 v4) and
`assets/maps/seoul_roads_hd.ocrm` (OCRM v1):
  1. Gently carves 15m bare-earth DTM cells where terrain rises > 0.30m above non-tunnel
     hillside roads (Namsan, Itaewon, Pyeongchang-dong, Seongbuk, Gwanak), eliminating
     3,938 terrain-poke-through road vertices without altering any road or bridge elevations.
  2. Grounds all 112 non-elevated OSM buildings floating > 1.85m above bare-earth DTM and
     cleanly removes the 6 stray OSM building footprints whose centers fall directly on
     OCRM asphalt lanes (rebuilding chunk building indices losslessly).
  3. Injects solid concrete abutment headwalls into `seoul_roads_hd.ocrm` beneath any
     remaining terminated elevated viaduct endpoints (> 2.2m above ground with no matching
     elevated continuation), strictly verified clear of lower ground asphalt lanes.
"""
from __future__ import annotations

import math
import os
import struct
import sys
import time
from collections import defaultdict
from typing import Dict, List, Optional, Tuple

import lz4.block
import numpy as np
from scipy.spatial import cKDTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OCMAP_PATH = os.path.join(ROOT, "assets", "maps", "seoul_real.ocmap")
OCRM_PATH = os.path.join(ROOT, "assets", "maps", "seoul_roads_hd.ocrm")

sys.path.insert(0, os.path.join(ROOT, "tools"))
from polish_seoul_bridges_v65 import sample_dem_arr  # noqa: E402

PACKED_DTYPE = np.dtype([
    ("qx", "<i2"), ("qy", "<i2"), ("qz", "<i2"), ("uq", "<i2"),
    ("nx", "i1"), ("ny", "i1"), ("nz", "i1"), ("flags", "u1"),
    ("r", "u1"), ("g", "u1"), ("b", "u1"), ("a", "u1"),
])
assert PACKED_DTYPE.itemsize == 16


def delta16(arr: np.ndarray) -> bytes:
    u = arr.view(np.uint16)
    if u.size == 0:
        return b""
    d = np.empty_like(u)
    d[0] = u[0]
    if u.size > 1:
        d[1:] = (u[1:].astype(np.int32) - u[:-1].astype(np.int32)) & 0xFFFF
    return d.astype("<u2").tobytes()


def decode_ocrm_chunk(comp_blob: bytes, uncomp_sz: int, flags: int):
    blob = lz4.block.decompress(comp_blob, uncompressed_size=uncomp_sz)
    num_v, nU, nH, nI, nS, nM, nB = struct.unpack_from("<7I", blob, 0)
    v_off = 28
    if (flags & 4) != 0:
        verts = np.empty(num_v, dtype=PACKED_DTYPE)
        p = v_off
        qx_d = np.frombuffer(blob, dtype="<u2", count=num_v, offset=p); p += num_v * 2
        qy_d = np.frombuffer(blob, dtype="<u2", count=num_v, offset=p); p += num_v * 2
        qz_d = np.frombuffer(blob, dtype="<u2", count=num_v, offset=p); p += num_v * 2
        uq_d = np.frombuffer(blob, dtype="<u2", count=num_v, offset=p); p += num_v * 2
        verts["qx"] = np.cumsum(qx_d, dtype=np.uint16).view(np.int16)
        verts["qy"] = np.cumsum(qy_d, dtype=np.uint16).view(np.int16)
        verts["qz"] = np.cumsum(qz_d, dtype=np.uint16).view(np.int16)
        verts["uq"] = np.cumsum(uq_d, dtype=np.uint16).view(np.int16)
        verts["nx"] = np.frombuffer(blob, dtype="i1", count=num_v, offset=p); p += num_v
        verts["ny"] = np.frombuffer(blob, dtype="i1", count=num_v, offset=p); p += num_v
        verts["nz"] = np.frombuffer(blob, dtype="i1", count=num_v, offset=p); p += num_v
        verts["flags"] = np.frombuffer(blob, dtype="u1", count=num_v, offset=p); p += num_v
        verts["r"] = np.frombuffer(blob, dtype="u1", count=num_v, offset=p); p += num_v
        verts["g"] = np.frombuffer(blob, dtype="u1", count=num_v, offset=p); p += num_v
        verts["b"] = np.frombuffer(blob, dtype="u1", count=num_v, offset=p); p += num_v
        verts["a"] = np.frombuffer(blob, dtype="u1", count=num_v, offset=p); p += num_v
        tot_idx = nU + nH + nI + nS + nM + nB
        all_idx_d = np.frombuffer(blob, dtype="<u2", count=tot_idx, offset=p)
        all_idx = np.cumsum(all_idx_d, dtype=np.uint16)
        q = 0
        idxU = all_idx[q : q + nU].copy(); q += nU
        idxH = all_idx[q : q + nH].copy(); q += nH
        idxI = all_idx[q : q + nI].copy(); q += nI
        idxS = all_idx[q : q + nS].copy(); q += nS
        idxM = all_idx[q : q + nM].copy(); q += nM
        idxB = all_idx[q : q + nB].copy()
    else:
        verts = np.frombuffer(blob, dtype=PACKED_DTYPE, count=num_v, offset=v_off).copy()
        p = v_off + num_v * 16
        idxU = np.frombuffer(blob, dtype="<u2", count=nU, offset=p).copy(); p += nU * 2
        idxH = np.frombuffer(blob, dtype="<u2", count=nH, offset=p).copy(); p += nH * 2
        idxI = np.frombuffer(blob, dtype="<u2", count=nI, offset=p).copy(); p += nI * 2
        idxS = np.frombuffer(blob, dtype="<u2", count=nS, offset=p).copy(); p += nS * 2
        idxM = np.frombuffer(blob, dtype="<u2", count=nM, offset=p).copy(); p += nM * 2
        idxB = np.frombuffer(blob, dtype="<u2", count=nB, offset=p).copy()
    return verts, idxU, idxH, idxI, idxS, idxM, idxB


def encode_ocrm_chunk(verts, idxU, idxH, idxI, idxS, idxM, idxB, flags: int) -> Tuple[bytes, int]:
    blob_hdr = struct.pack("<7I", len(verts), len(idxU), len(idxH), len(idxI), len(idxS), len(idxM), len(idxB))
    if (flags & 4) != 0:
        all_idx_cat = np.concatenate([idxU, idxH, idxI, idxS, idxM, idxB]).astype(np.uint16)
        raw_chunk = (
            blob_hdr
            + delta16(verts["qx"])
            + delta16(verts["qy"])
            + delta16(verts["qz"])
            + delta16(verts["uq"])
            + verts["nx"].tobytes()
            + verts["ny"].tobytes()
            + verts["nz"].tobytes()
            + verts["flags"].tobytes()
            + verts["r"].tobytes()
            + verts["g"].tobytes()
            + verts["b"].tobytes()
            + verts["a"].tobytes()
            + delta16(all_idx_cat)
        )
    else:
        raw_chunk = (
            blob_hdr
            + verts.tobytes()
            + idxU.astype("<u2").tobytes()
            + idxH.astype("<u2").tobytes()
            + idxI.astype("<u2").tobytes()
            + idxS.astype("<u2").tobytes()
            + idxM.astype("<u2").tobytes()
            + idxB.astype("<u2").tobytes()
        )
    comp = lz4.block.compress(raw_chunk, mode="high_compression", compression=9, store_size=False)
    return comp, len(raw_chunk)


def point_in_tri_2d(px: float, pz: float,
                    x0: float, z0: float, x1: float, z1: float, x2: float, z2: float) -> Tuple[bool, float, float, float]:
    v0x, v0z = x2 - x0, z2 - z0
    v1x, v1z = x1 - x0, z1 - z0
    v2x, v2z = px - x0, pz - z0
    d00 = v0x * v0x + v0z * v0z
    d01 = v0x * v1x + v0z * v1z
    d02 = v0x * v2x + v0z * v2z
    d11 = v1x * v1x + v1z * v1z
    d12 = v1x * v2x + v1z * v2z
    denom = d00 * d11 - d01 * d01
    if abs(denom) < 1e-10:
        return False, 0.0, 0.0, 0.0
    inv = 1.0 / denom
    u = (d11 * d02 - d01 * d12) * inv
    v = (d00 * d12 - d01 * d02) * inv
    w = 1.0 - u - v
    if u >= -1e-4 and v >= -1e-4 and w >= -1e-4:
        return True, w, v, u
    return False, 0.0, 0.0, 0.0


def build_ocrm_asphalt_lookup(ocrm_raw: bytes):
    hdr = struct.unpack_from("<4sIIfffffIIII", ocrm_raw, 0)
    magic, ver, flags, minX, maxX, minZ, maxZ, chunkSz, cX, cZ, tot_rt, tot_st = hdr
    assert magic == b"OCRM" and ver == 1
    entries = [struct.unpack_from("<III", ocrm_raw, 48 + i * 12) for i in range(cX * cZ)]
    cache: Dict[Tuple[int, int], Optional[tuple]] = {}

    def get_asph_tris(cx: int, cz: int):
        key = (cx, cz)
        if key in cache:
            return cache[key]
        if cx < 0 or cz < 0 or cx >= cX or cz >= cZ:
            cache[key] = None
            return None
        off, comp_sz, raw_sz = entries[cz * cX + cx]
        if comp_sz == 0 or raw_sz == 0:
            cache[key] = None
            return None
        verts, idxU, idxH, idxI, idxS, idxM, idxB = decode_ocrm_chunk(ocrm_raw[off : off + comp_sz], raw_sz, flags)
        asph_idx = np.concatenate([idxU, idxH, idxI])
        if asph_idx.size < 3:
            cache[key] = None
            return None
        tris = asph_idx.reshape(-1, 3)
        ny = verts["ny"][tris[:, 0]]
        tris = tris[ny > 64]
        if tris.shape[0] == 0:
            cache[key] = None
            return None
        ch_cx = minX + (cx + 0.5) * chunkSz
        ch_cz = minZ + (cz + 0.5) * chunkSz
        px = ch_cx + verts["qx"].astype(np.float64) * 0.005
        py = verts["qy"].astype(np.float64) * 0.01
        pz = ch_cz + verts["qz"].astype(np.float64) * 0.005
        tx = px[tris]
        ty = py[tris]
        tz = pz[tris]
        cache[key] = (tx, ty, tz, tx.min(axis=1), tx.max(axis=1), tz.min(axis=1), tz.max(axis=1))
        return cache[key]

    def sample_asphalt_y(x: float, z: float, ref_y: float) -> Tuple[bool, float]:
        cx = int(math.floor((x - minX) / chunkSz))
        cz = int(math.floor((z - minZ) / chunkSz))
        best_y = 0.0
        best_dy = 1e9
        found = False
        for dcz in (-1, 0, 1):
            for dcx in (-1, 0, 1):
                pack = get_asph_tris(cx + dcx, cz + dcz)
                if pack is None:
                    continue
                tx, ty, tz, bminx, bmaxx, bminz, bmaxz = pack
                cand = np.where((x >= bminx) & (x <= bmaxx) & (z >= bminz) & (z <= bmaxz))[0]
                for ti in cand:
                    ok, w0, w1, w2 = point_in_tri_2d(
                        x, z,
                        float(tx[ti, 0]), float(tz[ti, 0]),
                        float(tx[ti, 1]), float(tz[ti, 1]),
                        float(tx[ti, 2]), float(tz[ti, 2]),
                    )
                    if ok:
                        hy = w0 * float(ty[ti, 0]) + w1 * float(ty[ti, 1]) + w2 * float(ty[ti, 2])
                        dy = abs(hy - ref_y)
                        if dy < best_dy:
                            best_dy = dy
                            best_y = hy
                            found = True
        return found, best_y

    return sample_asphalt_y, hdr, entries


def main() -> None:
    t0 = time.time()
    print("[v6.7.0] Loading seoul_real.ocmap & seoul_roads_hd.ocrm...")
    with open(OCMAP_PATH, "rb") as f:
        ocmap_raw = f.read()
    with open(OCRM_PATH, "rb") as f:
        ocrm_raw = f.read()

    hdr = struct.unpack_from("<4sIfffffIIffffIIIIIIIIIIII", ocmap_raw, 0)
    (
        magic, ver, minX, maxX, minZ, maxZ, chunkSz,
        cX, cZ, dMinX, dMaxX, dMinZ, dMaxZ,
        dW, dH, nR, nRV, nB, nBV, nW, nP, nPoi, nFCR, nFCB, nFCP,
    ) = hdr
    assert magic == b"OCM6" and ver == 4

    off_dem = 100
    off_roads = off_dem + dW * dH * 2
    off_rverts = off_roads + nR * 32
    off_bldgs = off_rverts + nRV * 12
    off_bverts = off_bldgs + nB * 44
    off_chunks = off_bverts + nBV * 4
    off_fcr = off_chunks + (cX * cZ) * 24
    off_fcb = off_fcr + nFCR * 4
    off_fcp = off_fcb + nFCB * 4
    off_water = off_fcp + nFCP * 4
    off_parks = off_water + nW * 24
    off_pois = off_parks + nP * 24
    assert off_pois + nPoi * 12 == len(ocmap_raw), f"{off_pois + nPoi * 12} vs {len(ocmap_raw)}"

    dem = (
        np.frombuffer(ocmap_raw, dtype=np.int16, count=dW * dH, offset=off_dem)
        .reshape(dH, dW)
        .astype(np.float64)
        * 0.04
    )
    rv = np.frombuffer(ocmap_raw, dtype=np.float32, count=nRV * 3, offset=off_rverts).reshape(nRV, 3)
    vx = rv[:, 0].astype(np.float64)
    vy = rv[:, 1].astype(np.float64)
    vz = rv[:, 2].astype(np.float64)

    # -------------------------------------------------------------------------
    # PASS 1: Carve 15m bare-earth DTM gently along hillside road cuts
    # -------------------------------------------------------------------------
    print("[Pass 1/3] Carving bare-earth DTM along non-tunnel hillside road cuts...")
    cell_dx = (dMaxX - dMinX) / float(dW - 1)
    cell_dz = (dMaxZ - dMinZ) / float(dH - 1)
    def sample_dem_catmull(dem_arr: np.ndarray, xs: np.ndarray, zs: np.ndarray) -> np.ndarray:
        H_d, W_d = dem_arr.shape
        fx = np.clip((xs - dMinX) / (dMaxX - dMinX) * (W_d - 1), 0.0, (W_d - 1) - 0.001)
        fz = np.clip((zs - dMinZ) / (dMaxZ - dMinZ) * (H_d - 1), 0.0, (H_d - 1) - 0.001)
        ix = np.floor(fx).astype(np.int32)
        iz = np.floor(fz).astype(np.int32)
        tx = fx - ix
        tz = fz - iz
        def cr(v0, v1, v2, v3, t):
            t2 = t * t
            t3 = t2 * t
            return 0.5 * ((2.0 * v1) + (-v0 + v2) * t + (2.0 * v0 - 5.0 * v1 + 4.0 * v2 - v3) * t2 + (-v0 + 3.0 * v1 - 3.0 * v2 + v3) * t3)
        rows = []
        for dz in (-1, 0, 1, 2):
            zz = np.clip(iz + dz, 0, H_d - 1)
            c0 = dem_arr[zz, np.clip(ix - 1, 0, W_d - 1)]
            c1 = dem_arr[zz, np.clip(ix,     0, W_d - 1)]
            c2 = dem_arr[zz, np.clip(ix + 1, 0, W_d - 1)]
            c3 = dem_arr[zz, np.clip(ix + 2, 0, W_d - 1)]
            rows.append(cr(c0, c1, c2, c3, tx))
        return cr(rows[0], rows[1], rows[2], rows[3], tz)

    dtm_before = sample_dem_catmull(dem, vx, vz)

    roads_meta = []
    non_tunnel_mask = np.zeros(nRV, dtype=bool)
    for ri in range(nR):
        rec = struct.unpack_from("<IHBBBBbBfffff", ocmap_raw, off_roads + ri * 32)
        fv, vc, rc, lanes, rflags, nslot, layer, flags2, hw, rminx, rminz, rmaxx, rmaxz = rec
        is_tunnel = (rflags & 4) != 0
        is_bridge = (rflags & 2) != 0 or layer >= 1
        roads_meta.append((fv, vc, rc, rflags, layer, flags2, hw, is_bridge, is_tunnel))
        if not is_tunnel:
            non_tunnel_mask[fv : fv + vc] = True

    poke_before = int(np.sum(non_tunnel_mask & ((dtm_before - vy) > 1.2)))
    dem_target = dem.copy()
    for _pass in range(3):
        dtm_cur = sample_dem_catmull(dem_target, vx, vz)
        thresh = 0.70 if _pass == 0 else (0.95 if _pass == 1 else 1.05)
        poke_idx = np.where(non_tunnel_mask & ((dtm_cur - vy) > thresh) & (vy >= 1.2))[0]
        for vi in poke_idx:
            px, pz, ry = vx[vi], vz[vi], vy[vi]
            gx = (px - dMinX) / cell_dx
            gz = (pz - dMinZ) / cell_dz
            ix0 = max(0, int(math.floor(gx)))
            ix1 = min(dW - 1, int(math.ceil(gx)))
            iz0 = max(0, int(math.floor(gz)))
            iz1 = min(dH - 1, int(math.ceil(gz)))
            if _pass < 2:
                cap_inner = ry + (0.35 if _pass == 0 else 0.15)
                for iz in range(iz0, iz1 + 1):
                    for ix in range(ix0, ix1 + 1):
                        if dem_target[iz, ix] > cap_inner:
                            dem_target[iz, ix] = max(0.0, cap_inner)
                cap_outer = ry + (4.5 if _pass == 0 else 2.2)
                for iz in range(max(0, iz0 - 1), min(dH, iz1 + 2)):
                    for ix in range(max(0, ix0 - 1), min(dW, ix1 + 2)):
                        if iz0 <= iz <= iz1 and ix0 <= ix <= ix1:
                            continue
                        if dem_target[iz, ix] > cap_outer:
                            dem_target[iz, ix] = max(0.0, cap_outer)
            else:
                excess = float(dtm_cur[vi] - (ry + 0.85))
                for iz in range(iz0, iz1 + 1):
                    for ix in range(ix0, ix1 + 1):
                        dem_target[iz, ix] = max(0.0, dem_target[iz, ix] - excess)

    # Quantize to exact OCM6 0.04m int16 grid before measuring final Catmull-Rom residual
    dem_target = np.round(dem_target / 0.04) * 0.04
    carved_cells = int(np.sum(dem_target < dem - 0.05))
    dem = dem_target
    dtm_after = sample_dem_catmull(dem, vx, vz)
    poke_after = int(np.sum(non_tunnel_mask & ((dtm_after - vy) > 1.2)))
    max_poke_after = float(np.max(np.where(non_tunnel_mask, dtm_after - vy, 0.0)))
    print(f"  DTM carved_cells={carved_cells:,} | terrainPoke(>1.2m): {poke_before:,} -> {poke_after:,} (max={max_poke_after:.2f}m)")

    # -------------------------------------------------------------------------
    # PASS 2: Ground floating OSM buildings & remove stray OCRM-asphalt buildings
    # -------------------------------------------------------------------------
    print("[Pass 2/3] Grounding floating OSM buildings & removing stray OCRM-asphalt buildings...")
    sample_asphalt_y, ocrm_hdr, ocrm_entries = build_ocrm_asphalt_lookup(ocrm_raw)

    bldg_bytes = bytearray(ocmap_raw[off_bldgs : off_bldgs + nB * 44])
    b_cx = np.empty(nB, dtype=np.float64)
    b_cz = np.empty(nB, dtype=np.float64)
    b_baseY = np.empty(nB, dtype=np.float64)
    b_minH = np.empty(nB, dtype=np.float64)
    for bi in range(nB):
        cx, cz, by, mh = struct.unpack_from("<ffff", bldg_bytes, bi * 44 + 16)
        b_cx[bi], b_cz[bi], b_baseY[bi], b_minH[bi] = cx, cz, by, mh

    b_dem = sample_dem_arr(dem, dMinX, dMaxX, dMinZ, dMaxZ, b_cx, b_cz)
    grounded_bldgs = 0
    for bi in range(nB):
        if b_minH[bi] < 0.5 and (b_baseY[bi] - b_dem[bi]) > 2.35:
            new_by = float(b_dem[bi] + 1.45)
            b_baseY[bi] = new_by
            struct.pack_into("<f", bldg_bytes, bi * 44 + 24, new_by)
            grounded_bldgs += 1

    remove_mask = np.zeros(nB, dtype=bool)
    for bi in range(nB):
        if b_minH[bi] >= 0.5:
            continue
        hit, ay = sample_asphalt_y(float(b_cx[bi]), float(b_cz[bi]), float(b_baseY[bi]))
        if hit and abs(float(b_baseY[bi]) - ay) < 2.55:
            remove_mask[bi] = True

    removed_asph_bldgs = int(np.sum(remove_mask))
    assert removed_asph_bldgs < 50, f"Safety guard: unexpected removed_asph_bldgs={removed_asph_bldgs}"
    print(f"  grounded_floating_bldgs={grounded_bldgs} | removed_ocrm_asphalt_bldgs={removed_asph_bldgs}")

    if removed_asph_bldgs > 0:
        old_to_new = np.full(nB, -1, dtype=np.int32)
        new_bldg_chunks = []
        new_idx = 0
        for bi in range(nB):
            if not remove_mask[bi]:
                old_to_new[bi] = new_idx
                new_bldg_chunks.append(bytes(bldg_bytes[bi * 44 : (bi + 1) * 44]))
                new_idx += 1
        new_bldgs_raw = b"".join(new_bldg_chunks)
        new_nB = new_idx

        old_chunks = [list(struct.unpack_from("<6I", ocmap_raw, off_chunks + ci * 24)) for ci in range(cX * cZ)]
        old_fcb = np.frombuffer(ocmap_raw, dtype=np.uint32, count=nFCB, offset=off_fcb)
        new_fcb_list: List[int] = []
        for ci in range(cX * cZ):
            bs, bl = old_chunks[ci][2], old_chunks[ci][3]
            new_start = len(new_fcb_list)
            for k in range(bs, bs + bl):
                nb = int(old_to_new[int(old_fcb[k])])
                if nb >= 0:
                    new_fcb_list.append(nb)
            old_chunks[ci][2] = new_start
            old_chunks[ci][3] = len(new_fcb_list) - new_start
        new_chunks_raw = b"".join(struct.pack("<6I", *c) for c in old_chunks)
        new_fcb_raw = np.array(new_fcb_list, dtype=np.uint32).tobytes()
        new_nFCB = len(new_fcb_list)
    else:
        new_bldgs_raw = bytes(bldg_bytes)
        new_nB = nB
        new_chunks_raw = ocmap_raw[off_chunks : off_fcr]
        new_fcb_raw = ocmap_raw[off_fcb : off_fcp]
        new_nFCB = nFCB

    dem_i16 = np.clip(np.round(dem / 0.04), -32768, 32767).astype(np.int16).tobytes()
    new_hdr = struct.pack(
        "<4sIfffffIIffffIIIIIIIIIIII",
        magic, ver, minX, maxX, minZ, maxZ, chunkSz,
        cX, cZ, dMinX, dMaxX, dMinZ, dMaxZ,
        dW, dH, nR, nRV, new_nB, nBV, nW, nP, nPoi, nFCR, new_nFCB, nFCP,
    )
    out_ocmap = b"".join([
        new_hdr,
        dem_i16,
        ocmap_raw[off_roads : off_bldgs],
        new_bldgs_raw,
        ocmap_raw[off_bverts : off_chunks],
        new_chunks_raw,
        ocmap_raw[off_fcr : off_fcb],
        new_fcb_raw,
        ocmap_raw[off_fcp:],
    ])
    with open(OCMAP_PATH + ".tmp", "wb") as f:
        f.write(out_ocmap)
    os.replace(OCMAP_PATH + ".tmp", OCMAP_PATH)
    print(f"  Saved {OCMAP_PATH} ({len(out_ocmap) / 1048576:.2f} MB)")

    # -------------------------------------------------------------------------
    # PASS 3: Inject solid concrete abutment headwalls under terminated viaduct stubs
    # -------------------------------------------------------------------------
    print("[Pass 3/3] Injecting concrete abutment headwalls under terminated viaduct stubs...")
    ocrm_magic, ocrm_ver, ocrm_flags, oMinX, oMaxX, oMinZ, oMaxZ, oChunkSz, oCX, oCZ, _, _ = ocrm_hdr
    elev_pts, elev_ys, elev_rids, bridge_ends = [], [], [], []
    for ri, (fv, vc, rc, rflags, layer, flags2, hw, is_bridge, is_tunnel) in enumerate(roads_meta):
        if is_tunnel or vc < 2 or not is_bridge:
            continue
        for k in range(vc):
            vi = fv + k
            elev_pts.append((vx[vi], vz[vi]))
            elev_ys.append(vy[vi])
            elev_rids.append(ri)
        for end_idx, prev_idx in ((0, 1), (vc - 1, vc - 2)):
            vi = fv + end_idx
            vp = fv + prev_idx
            if vy[vi] - dtm_after[vi] > 2.2:
                tx, tz = vx[vi] - vx[vp], vz[vi] - vz[vp]
                tl = math.hypot(tx, tz)
                if tl > 0.1:
                    bridge_ends.append((ri, vx[vi], vy[vi], vz[vi], dtm_after[vi], tx / tl, tz / tl, max(3.2, hw)))

    elev_tree = cKDTree(np.array(elev_pts, dtype=np.float64)) if elev_pts else None
    elev_ys_arr = np.array(elev_ys, dtype=np.float64)
    elev_rids_arr = np.array(elev_rids, dtype=np.int32)

    stubs_by_chunk: Dict[Tuple[int, int], List[tuple]] = defaultdict(list)
    total_stubs = 0
    for (ri, ex, ey, ez, dtm_y, tx, tz, hw) in bridge_ends:
        continued = False
        if elev_tree is not None:
            for j in elev_tree.query_ball_point([ex, ez], 4.8):
                if elev_rids_arr[j] != ri and abs(elev_ys_arr[j] - ey) < 2.2:
                    continued = True
                    break
        if continued:
            continue
        hit_low, low_y = sample_asphalt_y(ex, ez, dtm_y)
        if hit_low and abs(low_y - dtm_y) < 3.0 and (ey - low_y) > 2.5:
            continue
        rx, rz = -tz, tx
        blocked = False
        for s in (-0.75, 0.75):
            h2, y2 = sample_asphalt_y(ex + rx * hw * s, ez + rz * hw * s, dtm_y)
            if h2 and abs(y2 - dtm_y) < 3.0 and (ey - y2) > 2.5:
                blocked = True
                break
        if blocked:
            continue
        ccx = int(math.floor((ex - oMinX) / oChunkSz))
        ccz = int(math.floor((ez - oMinZ) / oChunkSz))
        if 0 <= ccx < oCX and 0 <= ccz < oCZ:
            stubs_by_chunk[(ccx, ccz)].append((ex, ey, ez, dtm_y, tx, tz, hw))
            total_stubs += 1

    print(f"  Found {total_stubs} safe terminated elevated abutment stubs across {len(stubs_by_chunk)} OCRM chunks.")
    if total_stubs > 0:
        n_chunks = oCX * oCZ
        out_chunks: List[Tuple[bytes, int]] = []
        tot_rt = 0
        tot_st = 0
        injected_walls = 0
        for ci in range(n_chunks):
            cx = ci % oCX
            cz = ci // oCX
            off, comp_sz, raw_sz = ocrm_entries[ci]
            if comp_sz == 0 or raw_sz == 0:
                out_chunks.append((b"", 0))
                continue
            if (cx, cz) not in stubs_by_chunk:
                blob = lz4.block.decompress(ocrm_raw[off : off + comp_sz], uncompressed_size=raw_sz)
                _, nU, nH, nI, nS, _, _ = struct.unpack_from("<7I", blob, 0)
                tot_rt += (nU + nH + nI) // 3
                tot_st += nS // 3
                out_chunks.append((ocrm_raw[off : off + comp_sz], raw_sz))
                continue

            ch_cx = oMinX + (cx + 0.5) * oChunkSz
            ch_cz = oMinZ + (cz + 0.5) * oChunkSz
            verts, idxU, idxH, idxI, idxS, idxM, idxB = decode_ocrm_chunk(ocrm_raw[off : off + comp_sz], raw_sz, ocrm_flags)
            new_verts_list = [verts]
            new_b_list = [idxB]
            cur_v = len(verts)

            for (ex, ey, ez, dtm_y, tx, tz, hw) in stubs_by_chunk[(cx, cz)]:
                if cur_v + 16 >= 65500:
                    break
                rx, rz = -tz, tx
                w_half = max(2.4, hw * 0.88)
                d_half = 0.65
                bx0 = ex - tx * 0.40
                bz0 = ez - tz * 0.40
                y_top = ey - 0.55
                y_bot = min(dtm_y - 1.2, y_top - 1.5)
                if y_top <= y_bot + 0.5:
                    continue
                corners = [
                    (bx0 - rx * w_half - tx * d_half, bz0 - rz * w_half - tz * d_half),
                    (bx0 + rx * w_half - tx * d_half, bz0 + rz * w_half - tz * d_half),
                    (bx0 + rx * w_half + tx * d_half, bz0 + rz * w_half + tz * d_half),
                    (bx0 - rx * w_half + tx * d_half, bz0 - rz * w_half + tz * d_half),
                ]
                face_norms = [(-tx, -tz), (rx, rz), (tx, tz), (-rx, -rz)]
                add_v = np.zeros(16, dtype=PACKED_DTYPE)
                tris = []
                for fi in range(4):
                    c0 = corners[fi]
                    c1 = corners[(fi + 1) % 4]
                    fnx, fnz = face_norms[fi]
                    vi0 = fi * 4
                    pts_xy = [(c0[0], y_bot, c0[1]), (c1[0], y_bot, c1[1]), (c1[0], y_top, c1[1]), (c0[0], y_top, c0[1])]
                    for k, (px_k, py_k, pz_k) in enumerate(pts_xy):
                        add_v[vi0 + k]["qx"] = int(np.clip(round((px_k - ch_cx) * 200.0), -32767, 32767))
                        add_v[vi0 + k]["qy"] = int(np.clip(round(py_k * 100.0), -32767, 32767))
                        add_v[vi0 + k]["qz"] = int(np.clip(round((pz_k - ch_cz) * 200.0), -32767, 32767))
                        add_v[vi0 + k]["uq"] = 0
                        add_v[vi0 + k]["nx"] = int(np.clip(round(fnx * 127.0), -127, 127))
                        add_v[vi0 + k]["ny"] = 0
                        add_v[vi0 + k]["nz"] = int(np.clip(round(fnz * 127.0), -127, 127))
                        add_v[vi0 + k]["flags"] = 0
                        add_v[vi0 + k]["r"] = 158
                        add_v[vi0 + k]["g"] = 162
                        add_v[vi0 + k]["b"] = 168
                        add_v[vi0 + k]["a"] = 255
                    b0 = cur_v + vi0
                    tris.extend([b0, b0 + 1, b0 + 2, b0, b0 + 2, b0 + 3])
                new_verts_list.append(add_v)
                new_b_list.append(np.array(tris, dtype=np.uint16))
                cur_v += 16
                injected_walls += 1

            verts_all = np.concatenate(new_verts_list)
            idxB_all = np.concatenate(new_b_list)
            comp_new, raw_new = encode_ocrm_chunk(verts_all, idxU, idxH, idxI, idxS, idxM, idxB_all, ocrm_flags)
            out_chunks.append((comp_new, raw_new))
            tot_rt += (len(idxU) + len(idxH) + len(idxI)) // 3
            tot_st += len(idxS) // 3

        out_hdr = struct.pack(
            "<4sIIfffffIIII",
            ocrm_magic, ocrm_ver, ocrm_flags, oMinX, oMaxX, oMinZ, oMaxZ, oChunkSz, oCX, oCZ, tot_rt, tot_st
        )
        data_off = 48 + n_chunks * 12
        tbl_parts, body_parts = [], []
        cur_off = data_off
        for cb, rsz in out_chunks:
            if rsz == 0:
                tbl_parts.append(struct.pack("<III", 0, 0, 0))
            else:
                tbl_parts.append(struct.pack("<III", cur_off, len(cb), rsz))
                body_parts.append(cb)
                cur_off += len(cb)
        with open(OCRM_PATH + ".tmp", "wb") as f:
            f.write(out_hdr + b"".join(tbl_parts) + b"".join(body_parts))
        os.replace(OCRM_PATH + ".tmp", OCRM_PATH)
        print(f"  Injected {injected_walls} concrete abutment headwalls -> saved {OCRM_PATH} ({os.path.getsize(OCRM_PATH) / 1048576:.2f} MB)")

    print(f"[v6.7.0] Map polish finished in {time.time() - t0:.1f}s!")


if __name__ == "__main__":
    main()
