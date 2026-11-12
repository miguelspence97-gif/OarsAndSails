# RowRim v1.8.27: the rowboat's floorboards (RowRim_Floorboards: its own shader, textures, alpha) laid in the sailing
# boat's open well, at the rowboat's floor height (board tops z 40, 8 over the waterline as in the rowboat). The middle
# 178 of the rowboat's boards (y -89..89, where they are full width) are clipped out, moved to the well (y -232..-54) and
# narrowed board by board to the well's own width at z 40 (from the hull's inner skin, 3 clear of it).
# v1.8.27c: 8 higher (board tops z 48.6: the boat sits 10 lower in game, [height -10], so the boards stay at the rowboat's
# height over the water), and on forward under the foredeck to the bow (the rowboat's boards laid end to end, each copy
# narrowed to the hull there), as far as the hull's floor is under them.
# usage: python sb_floor.py <rowboat hull nif> <sailboat nif (without boards)> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape, root_add_kids
ROW, SB, OUT = sys.argv[1:4]
Y0, Y1 = -89.0, 89.0          # the part of the rowboat's boards taken
W0, W1 = -232.0, 200.0        # where it goes in the sailing boat (v1.8.27c: the well and on under the foredeck)
DZ = 8.0                      # v1.8.27c: raised (tops 40.6 -> 48.6)
ZC = 40.0 + DZ                # where the hull's width is read
CLEAR = 3.0
rd = open(ROW, 'rb').read(); rh = parse(rd)
fb = next(b for b in rh['blocks'] if b['type'] == 'BSTriShape' and tri_shape(rd, rh, b)['name'] == 'RowRim_Floorboards')
t = tri_shape(rd, rh, fb)
R = np.frombuffer(rd, np.uint8, t['nv'] * 32, t['data_off']).reshape(-1, 32)
P = R[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
UV = R[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float)
N = R[:, 20:23].astype(float) / 127.5 - 1.0
Tg = R[:, 24:27].astype(float) / 127.5 - 1.0
F = np.frombuffer(rd, '<u2', t['ntri'] * 3, t['data_off'] + t['nv'] * 32).reshape(-1, 3).astype(int)
print(f'rowboat boards: {t["nv"]} vertices, {t["ntri"]} triangles, z {P[:,2].min():.1f}..{P[:,2].max():.1f}')
# ---- clip to Y0..Y1 (each triangle against both planes, its attributes carried along)
A = np.hstack([P, UV, N, Tg])  # 11 attributes
def clip(poly, keep):
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        ka, kb = keep(a), keep(b)
        if ka >= 0: out.append(a)
        if (ka >= 0) != (kb >= 0):
            u = ka / (ka - kb); out.append(a + (b - a) * u)
    return out
verts = []; tris = []
for f in F:
    poly = [A[i] for i in f]
    poly = clip(poly, lambda v: v[1] - Y0)
    if len(poly) >= 3: poly = clip(poly, lambda v: Y1 - v[1])
    if len(poly) < 3: continue
    base = len(verts); verts += poly
    for k in range(1, len(poly) - 1): tris.append((base, base + k, base + k + 1))
V = np.array(verts); T = np.array(tris)
# weld identical vertices (clipping makes duplicates along shared edges)
key = {}; remap = np.zeros(len(V), int); keep = []
for i, v in enumerate(np.round(V, 4)):
    k = tuple(v)
    if k not in key: key[k] = len(keep); keep.append(i)
    remap[i] = key[k]
V = V[keep]; T = remap[T]
T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
# ---- the sailing boat's well: its inner half width at z 40 along y
sd = open(SB, 'rb').read(); sh = parse(sd)
HT = []
for b in sh['blocks']:
    if b['type'] != 'BSTriShape': continue
    s = tri_shape(sd, sh, b)
    if s['name'] not in ('SB_HullPlanks', 'SB_HullFrame'): continue
    Q = np.frombuffer(sd, np.uint8, s['nv'] * 32, s['data_off']).reshape(-1, 32)[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    G = np.frombuffer(sd, '<u2', s['ntri'] * 3, s['data_off'] + s['nv'] * 32).reshape(-1, 3).astype(int)
    HT.append(Q[G])
HT = np.vstack(HT)
cut = []
for a, b in ((0, 1), (1, 2), (2, 0)):
    pa, pb = HT[:, a], HT[:, b]; za, zb = pa[:, 2] - ZC, pb[:, 2] - ZC; m = za * zb < 0
    u = za[m] / (za[m] - zb[m]); cut.append(pa[m] + (pb[m] - pa[m]) * u[:, None])
cut = np.vstack(cut); cut = cut[np.abs(cut[:, 0]) > 15]
ys = np.arange(W0 - 10, W1 + 11, 5.0); ws = []
for y in ys:
    q = cut[np.abs(cut[:, 1] - y) < 6]
    ws.append(np.abs(q[:, 0]).min() if len(q) else np.nan)
ws = np.array(ws); ok = ~np.isnan(ws)
ws = np.interp(ys, ys[ok], ws[ok])
# smooth, and never wider than the narrowest skin nearby (the frames)
ws = np.convolve(np.pad(ws, 2, mode='edge'), np.ones(5) / 5, mode='valid')
def well_w(y): return float(np.interp(y, ys, ws)) - CLEAR
# ---- the boards moved and narrowed
rw = {}
def row_w(y):  # the rowboat boards' own half width at y (their outline)
    m = np.abs(V[:, 1] - y) < 6
    return float(np.abs(V[m, 0]).max()) if m.any() else 69.0
# the boards laid end to end from the stern of the well forward (each copy 178 long, the last one clipped at W1), each
# vertex narrowed to the hull's width where it lands
PP = []; TT = []
s0 = W0
while s0 < W1 - 1.0:
    Ak = V.copy(); Ak[:, 1] = s0 + (V[:, 1] - Y0); Ak[:, 2] += DZ
    for f in T:
        poly = clip([Ak[i] for i in f], lambda v: W1 - v[1])
        if len(poly) < 3: continue
        base = sum(len(p) for p in PP) + 0
        PP.append(np.array(poly)); TT += [(base, base + k, base + k + 1) for k in range(1, len(poly) - 1)]
    s0 += Y1 - Y0
Vall = np.vstack(PP); T = np.array(TT)
key = {}; remap = np.zeros(len(Vall), int); keep = []
for i, v in enumerate(np.round(Vall, 4)):
    k = tuple(v)
    if k not in key: key[k] = len(keep); keep.append(i)
    remap[i] = key[k]
Vall = Vall[keep]; T = remap[T]
T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
# each vertex narrowed to the hull's width where it lands (by the rowboat board outline where it came from)
src_y = Y0 + (Vall[:, 1] - W0) % (Y1 - Y0)
src_y[np.isclose(Vall[:, 1] - W0, (Y1 - Y0) * np.round((Vall[:, 1] - W0) / (Y1 - Y0))) & (Vall[:, 1] > W0 + 1)] = Y1
sx = np.array([max(well_w(a), 0.0) / row_w(b) for a, b in zip(Vall[:, 1], src_y)])
Pn = np.c_[Vall[:, 0] * sx, Vall[:, 1], Vall[:, 2]]
# drop what is out of the hull: boards narrower than 8 (the bow's point) and where the hull floor comes up through them
wid = np.array([well_w(y) for y in Pn[:, 1]])
bad = wid < 8.0
T = T[~bad[T].any(axis=1)]
used = np.unique(T); remap = -np.ones(len(Pn), int); remap[used] = np.arange(len(used))
Pn = Pn[used]; T = remap[T]; V = Vall[used]
assert len(Pn) < 65535
print(f'sailing boat boards: {len(Pn)} vertices, {len(T)} triangles, y {Pn[:,1].min():.0f}..{Pn[:,1].max():.0f}, half width {np.abs(Pn[:,0]).max():.1f} (well {well_w(W0):.0f} aft .. {well_w(W1):.0f} fwd), z {Pn[:,2].min():.1f}..{Pn[:,2].max():.1f}')
# normals and tangents as they were (the boards stay flat); renormalised
Nn = V[:, 5:8]; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
Tn = V[:, 8:11]; Tn /= np.linalg.norm(Tn, axis=1, keepdims=True) + 1e-9
# ---- into the sailing boat: its own shader, texture set and alpha (copies of the rowboat boards')
n = Nif(sd)
rblocks = [bytes(rd[b['start']:b['end']]) for b in rh['blocks']]
def copy_shader(k):
    r, p = av_fix(rblocks[k], 4); r = bytearray(r)
    tex, = struct.unpack_from('<i', r, p + 4 + 4 + 8 + 8)
    struct.pack_into('<i', r, p + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', rblocks[tex]))
    return n.add('BSLightingShaderProperty', r)
shd = copy_shader(t['shader'])
alp = n.add('NiAlphaProperty', av_fix(rblocks[t['alpha']])[0]) if t['alpha'] >= 0 else -1
k = build_shape(n, rblocks[fb['k']], Pn, Nn, Tn, V[:, 3:5], T, shd, alp)
raw = bytearray(n.blocks[k]); struct.pack_into('<i', raw, 0, n.name('RowRim_Floorboards')); n.blocks[k] = bytes(raw)
root_add_kids(n, [k])
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(sd):,} -> {len(out):,} bytes (shader {shd}, alpha {alp})')
