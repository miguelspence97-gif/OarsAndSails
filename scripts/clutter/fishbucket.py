# v1.7.45: a coopered tub full of fish baked into the hull floor (RowRim_Clutter_FishBucket), where the Fishing Supplies
# stood. The Creation Club supplies ref stays in the boat for its Use / script, but its own mesh is hidden by the plugin
# (Gear.inl) and it is kept on this tub's spot.
#   python fishbucket.py <RowRim_Hull.nif in> <out.nif> [<fish_d.dds out>]
# Works on any RowRim hull: finds the floorboards by their texture (boat_floor_d), appends one NiNode under the root.
import sys, struct, numpy as np
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nifmerge import Nif, node_block, av_fix, e8, tex_set, root_add_kids
from nifp import parse, tri_shape
from hull_v144_lib import load, store
from meshes import tube, ellipsoid, merge, xform, orient

SRC, OUT = sys.argv[1], sys.argv[2]
TEXOUT = sys.argv[3] if len(sys.argv) > 3 else None
PHOTO = sys.argv[4] if len(sys.argv) > 4 else None   # v1.7.49: Miguel's top-down picture of a fish bucket: the fish as a dome textured with it
# v1.7.49 photo crop: the bucket's inside in that picture (2400 x 1792): centre and radius in pixels, darkness
PH_CX, PH_CY, PH_R, PH_DARK = 642, 830, 445, 0.42   # v1.7.50: darker again (0.62 still too bright in game)
d = open(SRC, 'rb').read(); n = Nif(d)
assert 'RowRim_Clutter_FishBucket' not in n.names, 'the fish tub is already in this hull'
deg = np.pi / 180
rng = np.random.default_rng(45)

# ---- the spot: port side aft, as far out as the floorboards go (clear of the helmsman's feet and the aft thwart crew) ----
SPOT = np.array([-44.5, -143.0])
RB, RT, H = 8.0, 10.0, 15.0          # bottom radius, mouth radius, height (game units)
YAW = 0.0

# ---- the floorboards: template shape head and shader ----
def tex_paths(k):
    raw = n.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1').lower()); p += 4 + l
    return out
floor_shape = floor_shader = None
for k, t in enumerate(n.ti):
    if n.types[t] != 'BSLightingShaderProperty': continue
    ts, = struct.unpack_from('<i', n.blocks[k], 40)
    if ts >= 0 and n.types[n.ti[ts]] == 'BSShaderTextureSet' and any('boat_floor_d' in q for q in tex_paths(ts)):
        for s, t2 in enumerate(n.ti):
            if n.types[t2] == 'BSTriShape':
                ti = tri_shape(d, n.h, n.h['blocks'][s]) if s < n.h['nb'] else None
                if ti and ti['shader'] == k and ti['vsize'] == 32: floor_shape, floor_shader = s, k; break
    if floor_shape is not None: break
assert floor_shape is not None, 'no floorboards (boat_floor_d, 32-byte vertices) in this hull'
TPL = n.blocks[floor_shape]; SHADER = n.blocks[floor_shader]
_, fv, _ = load(TPL)
FP = fv[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
near = np.linalg.norm(FP[:, :2] - SPOT, axis=1) < RB + 12   # plank tops (the battens under them sit lower)
FLOOR = float(FP[near, 2].max()) if near.any() else 40.4
print('floorboards block', floor_shape, 'shader', floor_shader, 'floor top at the spot', round(FLOOR, 2))

def shader_with(paths, spec=True):
    t = n.add('BSShaderTextureSet', tex_set(paths))
    r, _ = av_fix(SHADER, 4); r = bytearray(r); struct.pack_into('<i', r, 40, t)
    if not spec:  # v1.7.50: matte (SLSF1 Specular off): the fish shone wet-white in game
        f1, = struct.unpack_from('<I', r, 16); struct.pack_into('<I', r, 16, f1 & ~1)
    return n.add('BSLightingShaderProperty', r)

def build(p, shader, col=None):
    """32-byte-vertex BSTriShape with vertex colours (RGB 0..1 per vertex, alpha 1)"""
    V, N, Tg, UV, F = orient(p)
    st, _, _ = load(TPL)
    N = N / np.linalg.norm(N, axis=1, keepdims=True)
    Tg = Tg - N * (Tg * N).sum(1, keepdims=True)
    Tg = Tg / np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-6)
    B = np.cross(N, Tg)
    vb = np.zeros((len(V), 32), np.uint8)
    vb[:, 0:12] = np.ascontiguousarray(V.astype('<f4')).view(np.uint8).reshape(-1, 12)
    vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    vb[:, 16:20] = np.ascontiguousarray(UV.astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 20:23] = e8(N); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2])
    c = np.ones((len(V), 3)) if col is None else np.broadcast_to(np.asarray(col, float), (len(V), 3))
    vb[:, 28:31] = np.clip(np.round(c * 255), 0, 255).astype(np.uint8); vb[:, 31] = 255
    assert len(V) < 65536
    raw = bytearray(store(st, vb, F))
    raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, shader, -1)
    return n.add('BSTriShape', raw), V

def quad_strip(rows, uv_rows, nrm_rows):
    """grid of points rows[i][j] -> mesh part"""
    R = np.asarray(rows, float); U = np.asarray(uv_rows, float); Nn = np.asarray(nrm_rows, float)
    a, b = R.shape[:2]
    V = R.reshape(-1, 3); UV = U.reshape(-1, 2); N = Nn.reshape(-1, 3)
    T = np.gradient(R, axis=1).reshape(-1, 3) + 1e-6
    F = []
    for i in range(a - 1):
        for j in range(b - 1):
            q = i * b + j; F += [(q, q + b, q + 1), (q + 1, q + b, q + b + 1)]
    return [V, N, T, UV, np.array(F)]

def box(c, size, R):
    sx, sy, sz = np.asarray(size) / 2
    parts = []
    for ax in range(3):
        for sg in (-1, 1):
            nrm = np.zeros(3); nrm[ax] = sg
            u_ax, v_ax = [a for a in range(3) if a != ax]
            pts = []; uvs = []
            for i in (0, 1):
                row = []; urow = []
                for j in (0, 1):
                    p = np.zeros(3); p[ax] = sg; p[u_ax] = -1 + 2 * j; p[v_ax] = -1 + 2 * i
                    row.append(p * [sx, sy, sz]); urow.append((j * 0.05, i * 0.05))
                pts.append(row); uvs.append(urow)
            parts.append(quad_strip(pts, uvs, np.broadcast_to(nrm, (2, 2, 3))))
    return xform(merge(parts), R, np.asarray(c))

def rz(a): c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.]])
def rx(a): c, s = np.cos(a), np.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def ry(a): c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def rad_at(z): return RB + (RT - RB) * z / H

# ---- 1. the tub: 14 staves of the floor wood, gaps between, two raised as ears ----
NS = 14; GAP = 0.018; TH = 0.8
staves = []
ears = (3, 10)   # fore and aft (about 90 and 270 deg), so the bail can lie down to port
for s in range(NS):
    a0 = 2 * np.pi * s / NS + GAP / 2; a1 = 2 * np.pi * (s + 1) / NS - GAP / 2
    top = H + rng.uniform(-0.25, 0.25) + (2.6 if s in ears else 0.0)
    m = 3; zs = np.linspace(0, top, 5); u0 = rng.uniform(0, 0.8)
    v0 = rng.uniform(0, 0.7)
    for side in (1, -1):  # outside, inside
        rows = []; uvs = []; nr = []
        for z in zs:
            r = rad_at(min(z, H)) + (0 if side > 0 else -TH)
            row = []; ur = []; nn = []
            for j in range(m + 1):
                a = a0 + (a1 - a0) * j / m
                row.append((r * np.cos(a), r * np.sin(a), z)); ur.append((u0 + 0.06 * j / m, v0 + z / 60))
                nn.append((side * np.cos(a), side * np.sin(a), 0))
            rows.append(row); uvs.append(ur); nr.append(nn)
        staves.append(quad_strip(rows, uvs, nr))
    # rim and edges
    for (aa, sgn) in ((a0, -1), (a1, 1)):
        rows = []; uvs = []; nr = []
        for z in zs:
            ro = rad_at(min(z, H)); row = [(ro * np.cos(aa), ro * np.sin(aa), z), ((ro - TH) * np.cos(aa), (ro - TH) * np.sin(aa), z)]
            rows.append(row); uvs.append([(u0, v0 + z / 60), (u0 + 0.01, v0 + z / 60)])
            t = np.array([-np.sin(aa), np.cos(aa), 0]) * sgn; nr.append([t, t])
        staves.append(quad_strip(rows, uvs, nr))
    rows = []; uvs = []; nr = []
    for j in range(m + 1):
        a = a0 + (a1 - a0) * j / m; ro = rad_at(H)
        rows.append([(ro * np.cos(a), ro * np.sin(a), top), ((ro - TH) * np.cos(a), (ro - TH) * np.sin(a), top)])
        uvs.append([(u0 + 0.06 * j / m, v0), (u0 + 0.06 * j / m, v0 + 0.01)]); nr.append([(0, 0, 1), (0, 0, 1)])
    staves.append(quad_strip(rows, uvs, nr))
# the bottom disc (seen through the gaps), 1.4 up inside the chime
k = 16; V = [(0, 0, 1.4)]; UVb = [(0.5, 0.5)]
for j in range(k + 1):
    a = 2 * np.pi * j / k; r = rad_at(1.4) - TH
    V.append((r * np.cos(a), r * np.sin(a), 1.4)); UVb.append((0.5 + 0.2 * np.cos(a), 0.5 + 0.2 * np.sin(a)))
F = [(0, j + 1, j + 2) for j in range(k)]
staves.append([np.array(V, float), np.tile((0, 0, 1.), (len(V), 1)), np.tile((1, 0, 0.), (len(V), 1)), np.array(UVb), np.array(F)])
tub = merge(staves)

# ---- 2. iron: three bands with rivets, ear pins, the bail (dark vertex colour on the floor wood) ----
iron = []
for zb in (2.0, 7.2, 12.6):
    rows = []; uvs = []; nr = []
    for z in (zb - 0.7, zb + 0.7):
        row = []; ur = []; nn = []
        for j in range(41):
            a = 2 * np.pi * j / 40; r = rad_at(z) + 0.18
            row.append((r * np.cos(a), r * np.sin(a), z)); ur.append((j / 40, 0.9)); nn.append((np.cos(a), np.sin(a), 0))
        rows.append(row); uvs.append(ur); nr.append(nn)
    iron.append(quad_strip(rows, uvs, nr))
    for j in range(5):
        a = 2 * np.pi * (j + 0.5) / 5 + zb; r = rad_at(zb) + 0.3
        iron.append(ellipsoid((r * np.cos(a), r * np.sin(a), zb), (0.45, 0.45, 0.45), 6, 4))
for e in ears:
    a = 2 * np.pi * (e + 0.5) / NS; r = rad_at(H) + 0.15
    iron.append(ellipsoid((r * np.cos(a), r * np.sin(a), H + 1.3), (0.6, 0.6, 0.6), 6, 4))
# bail: ear to ear, laid down outboard (over the side toward the hull, away from feet), 25 deg up from the rim
ea = 2 * np.pi * (ears[0] + 0.5) / NS; eb = 2 * np.pi * (ears[1] + 0.5) / NS
pa = np.array([(RT + 0.5) * np.cos(ea), (RT + 0.5) * np.sin(ea), H + 1.3])
pb = np.array([(RT + 0.5) * np.cos(eb), (RT + 0.5) * np.sin(eb), H + 1.3])
mid = (pa + pb) / 2; axis = (pb - pa) / np.linalg.norm(pb - pa)
out = np.array([-1.0, 0, 0]); out = out - axis * out.dot(axis); out /= np.linalg.norm(out)   # toward port (the hull)
up = np.array([0, 0, 1.0]); lean = np.cos(65 * deg) * up + np.sin(65 * deg) * out
half = np.linalg.norm(pb - pa) / 2
P = [mid + axis * (-half * np.cos(t)) + lean * (half * 0.85 * np.sin(t)) for t in np.linspace(0, np.pi, 24)]
iron.append(tube(P, 0.32, 6))
ironm = merge(iron)

# ---- 3. the fish ----
def fish(L, flat=0.42):
    """a fish along +X (head at +L/2), height on Z, width on Y; v: back 0 -> belly 1 on each side (mirrored)"""
    ts = np.linspace(0, 1, 18); m = 14
    rows = []; uvs = []; nr = []
    for t in ts:   # t 0 = snout, 1 = tail fin tip
        x = L / 2 - t * L
        if t <= 0.82:
            hh = L * 0.34 * (np.sin(np.pi * min(t / 0.82, 1) ** 0.75) * 0.92 + 0.08) if t > 0 else L * 0.02
            ww = hh * flat
            zc = -0.03 * L * np.sin(np.pi * t)
        else:   # the tail: thin, fanning out
            f = (t - 0.82) / 0.18
            hh = L * (0.06 + 0.30 * f ** 0.8); ww = L * 0.012; zc = 0
        row = []; ur = []; nn = []
        for j in range(m + 1):
            a = 2 * np.pi * j / m    # 0 = top
            y = ww / 2 * np.sin(a); z = zc + hh / 2 * np.cos(a)
            if t > 0.82 and abs(np.cos(a)) > 0.75:   # forked tail: the tips trail back
                x2 = x - L * 0.05 * (1 - abs(np.cos(a)) / 1.0) * 0
            row.append((x, y, z)); ur.append((t, (1 - np.cos(a)) / 2))
            nn.append((0.0 if 0 < t < 1 else (1 if t == 0 else -1), np.sin(a) / max(flat, 0.05), np.cos(a)))
        rows.append(row); uvs.append(ur); nr.append(nn)
    R = np.array(rows); R[-1, :, 0] -= L * 0.05 * np.abs(np.cos(np.linspace(0, 2 * np.pi, m + 1))) ** 2  # fork
    R[-1, :, 0] += L * 0.04
    return quad_strip(R, uvs, nr)

fishes = []
def put(L, R, t):
    fishes.append(xform(fish(L), R, np.asarray(t, float)))
# the heap under the top layer: a mound of small fish, lying on their sides at all angles
for i in range(9):
    a = 2 * np.pi * i / 9 + rng.uniform(-0.3, 0.3); r = rng.uniform(1.5, 4.5)
    L = rng.uniform(10.5, 13.0)
    z = rng.uniform(H - 3.2, H - 1.6)
    R = rz(rng.uniform(0, 2 * np.pi)) @ rx(np.pi / 2 + rng.uniform(-0.5, 0.5)) @ ry(rng.uniform(-0.35, 0.35))
    put(L, R, (r * np.cos(a), r * np.sin(a), z))
# some heads up against the staves, as in the picture
for i in range(4):
    a = 2 * np.pi * i / 4 + 0.6
    R = rz(a + np.pi) @ ry(-60 * deg) @ rx(np.pi / 2)
    put(11.0, R, (5.4 * np.cos(a), 5.4 * np.sin(a), H - 2.6))
# the big one across the top, its tail up over the rim (toward the stern, inboard of the bail)
R = rz(-80 * deg) @ ry(14 * deg) @ rx(np.pi / 2 + 0.25)
put(22.0, R, (0.5, 1.2, H + 0.2))

# keep every fish inside the tub (the big tail may go over the rim, above it)
fm = merge(fishes)
P = fm[0]; r = np.linalg.norm(P[:, :2], axis=1); lim = rad_at(np.clip(P[:, 2], 0, H)) - TH - 0.25
inside = (r > lim) & (P[:, 2] < H + 0.3)
P[inside, :2] *= (lim[inside] / r[inside])[:, None]
P[:, 2] = np.maximum(P[:, 2], 2.0)
fm[0] = P
# the hidden fill under the fish (dark, so the floor never shows between them)
k = 20; V = [(0, 0, H - 3.5)]; UVf = [(0.5, 0.15)]
for j in range(k + 1):
    a = 2 * np.pi * j / k; rr = rad_at(H - 3.5) - TH - 0.1
    V.append((rr * np.cos(a), rr * np.sin(a), H - 3.5)); UVf.append((0.5, 0.15))
fill = [np.array(V, float), np.tile((0, 0, 1.), (len(V), 1)), np.tile((1, 0, 0.), (len(V), 1)), np.array(UVf), np.array([(0, j + 1, j + 2) for j in range(k)])]

# v1.7.49: with the picture, the fish are one low dome filling the mouth, textured with the picture's fish (lit and
# shaded already), in place of the modelled fish
if PHOTO:
    rr = rad_at(H - 1.0) - TH - 0.1; NR, NA = 10, 40
    V = []; UVp = []; Nn = []; Tt = []
    for i in range(NR + 1):
        rho = rr * i / NR
        for j in range(NA + 1):
            a = 2 * np.pi * j / NA
            x, y = rho * np.cos(a), rho * np.sin(a)
            z = H - 1.6 + 2.2 * (1 - (rho / rr) ** 2)
            V.append((x, y, z)); UVp.append((0.5 + 0.5 * y / rr, 0.5 - 0.5 * x / rr))   # picture across = fore and aft
            sl = -4.4 * rho / rr ** 2
            nrm = np.array([-sl * np.cos(a), -sl * np.sin(a), 1.0]); Nn.append(nrm / np.linalg.norm(nrm)); Tt.append((0, 1, 0))
    Fp = []
    for i in range(NR):
        for j in range(NA):
            q = i * (NA + 1) + j; Fp += [(q, q + NA + 1, q + 1), (q + 1, q + NA + 1, q + NA + 2)]
    fm = [np.array(V, float), np.array(Nn), np.array(Tt, float), np.array(UVp), np.array(Fp)]

# ---- place in boat space ----
Rb = rz(YAW); Tb = np.array([SPOT[0], SPOT[1], FLOOR + 0.02])
tub = xform(tub, Rb, Tb); ironm = xform(ironm, Rb, Tb); fm = xform(fm, Rb, Tb); fill = xform(fill, Rb, Tb)

FLOORT = ['textures\\RowRim\\boat_floor_d.dds', 'textures\\RowRim\\boat_floor_n.dds']
FISHT = ['textures\\RowRim\\fish_d.dds', 'textures\\RowRim\\fishbone_n.dds']
s1, V1 = build(tub, shader_with(FLOORT), (0.82, 0.78, 0.72))
s2, V2 = build(ironm, shader_with(FLOORT), (0.16, 0.16, 0.17))
s3, V3 = build(fm, shader_with(FISHT, False))
s4, V4 = build(fill, shader_with(FISHT, False), (0.35, 0.35, 0.38))
node = n.add('NiNode', node_block(n.name('RowRim_Clutter_FishBucket'), kids=[s1, s2, s3, s4]))
root_add_kids(n, [node])

# no shader property used twice (v1.7.6)
from collections import Counter
used = Counter()
for k2, t in enumerate(n.ti):
    if n.types[t] == 'BSTriShape':
        r = n.blocks[k2]; ne, = struct.unpack_from('<I', r, 4); p = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4 + 16
        sk, sh, al = struct.unpack_from('<3i', r, p)
        if sh >= 0: used[sh] += 1
assert max(used.values()) == 1, 'a shader property used twice'

allV = np.concatenate([V1, V2, V3, V4])
print('fish tub: verts', len(allV), 'bounds', allV.min(0).round(1), allV.max(0).round(1))
open(OUT, 'wb').write(n.write())
print('wrote', OUT, len(n.write()), 'bytes')

# ---- v1.7.49: the fish texture from the picture: the bucket's inside, 256 x 256 DXT1 with mips, darkened ----
if TEXOUT and PHOTO:
    from PIL import Image
    import io
    im = Image.open(PHOTO).convert('RGB').crop((PH_CX - PH_R, PH_CY - PH_R, PH_CX + PH_R, PH_CY + PH_R)).resize((256, 256), Image.LANCZOS)
    a = np.asarray(im).astype(float) / 255 * PH_DARK
    img = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)); img.save(TEXOUT.rsplit('.', 1)[0] + '_preview.png')
    levels = []; cur = img; w = h = 256
    while True:
        b = io.BytesIO(); cur.save(b, 'DDS', pixel_format='DXT1'); levels.append(b.getvalue())
        if w == 1: break
        w = h = w // 2; cur = img.resize((w, h), Image.LANCZOS)
    hdr = bytearray(levels[0][:128]); f, = struct.unpack_from('<I', hdr, 8); struct.pack_into('<I', hdr, 8, f | 0x20000)
    struct.pack_into('<I', hdr, 28, len(levels)); c, = struct.unpack_from('<I', hdr, 108); struct.pack_into('<I', hdr, 108, c | 0x400008)
    open(TEXOUT, 'wb').write(bytes(hdr) + b''.join(l[128:] for l in levels))
    print('wrote', TEXOUT, '(from the picture)')
    TEXOUT = None
# ---- the fish texture: 256 x 128 DXT1 with mips (u snout -> tail, v back -> belly) ----
if TEXOUT:
    from PIL import Image
    import io
    W, Hh = 256, 128
    u, v = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(Hh) + 0.5) / Hh)
    back = np.array([0.36, 0.40, 0.34]); side = np.array([0.80, 0.70, 0.70]); belly = np.array([0.93, 0.90, 0.88])
    t = np.clip(v / 0.45, 0, 1)[..., None]
    c = back * (1 - t) + side * t
    t2 = np.clip((v - 0.55) / 0.35, 0, 1)[..., None]; c = c * (1 - t2) + belly * t2
    pink = np.exp(-((v - 0.55) / 0.12) ** 2)[..., None] * np.array([0.10, -0.06, -0.04]); c = c + pink
    # scales: overlapping arcs
    su, sv = u * 46, v * 20
    fu = np.mod(su + 0.5 * np.floor(sv), 1.0); fv = np.mod(sv, 1.0)
    arc = np.exp(-((np.hypot(fu - 0.5, fv - 0.1) - 0.55) / 0.10) ** 2)
    body = (u > 0.2) & (u < 0.84)
    c = c * (1 - 0.18 * arc * body)[..., None] + 0.06 * (1 - fv)[..., None] * body[..., None]
    # head: smoother, gill line, eye
    gill = np.exp(-((u - (0.21 - 0.04 * (v - 0.5) ** 2 * 4)) / 0.008) ** 2) * (v > 0.2) * (v < 0.85)
    c = c * (1 - 0.35 * gill)[..., None] + np.array([0.12, 0.02, 0.0]) * gill[..., None]
    e = np.hypot((u - 0.075) * W, (v - 0.40) * Hh)
    c = np.where((e < 5.5)[..., None], np.array([0.85, 0.48, 0.12]), c)
    c = np.where((e < 3.0)[..., None], np.array([0.05, 0.04, 0.04]), c)
    c = np.where((e < 1.0)[..., None], np.array([0.9, 0.9, 0.9]), c)
    c = np.where(((e >= 5.5) & (e < 6.5))[..., None], np.array([0.75, 0.75, 0.72]), c)
    # mouth
    c = c * (1 - 0.5 * np.exp(-((u - 0.012) / 0.01) ** 2) * np.exp(-((v - 0.52) / 0.06) ** 2))[..., None]
    # fins: the tail red, a red pectoral blush, the dorsal edge reddish
    fin = np.clip((u - 0.83) / 0.04, 0, 1)[..., None]
    rays = 0.85 + 0.15 * np.sin(v * 90)
    c = c * (1 - fin) + (np.array([0.78, 0.30, 0.25]) * rays[..., None]) * fin
    pec = np.exp(-(((u - 0.27) / 0.05) ** 2 + ((v - 0.62) / 0.08) ** 2))[..., None]
    c = c * (1 - 0.6 * pec) + np.array([0.85, 0.40, 0.33]) * 0.6 * pec
    c = c + rng.normal(0, 0.015, c.shape)
    c = c * 0.58 + 0.02   # v1.7.47: darker overall (was too bright in game): wet, dull scales, not white
    c = c * np.array([0.95, 0.97, 1.0])
    img = Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8))
    img.save(TEXOUT.rsplit('.', 1)[0] + '_preview.png')
    levels = []; im = img; w, h = W, Hh
    while True:
        b = io.BytesIO(); im.save(b, 'DDS', pixel_format='DXT1'); levels.append(b.getvalue())
        if w == 1 and h == 1: break
        w, h = max(1, w // 2), max(1, h // 2); im = img.resize((w, h), Image.LANCZOS)
    hdr = bytearray(levels[0][:128])
    flags, = struct.unpack_from('<I', hdr, 8); struct.pack_into('<I', hdr, 8, flags | 0x20000)
    struct.pack_into('<I', hdr, 28, len(levels))
    caps, = struct.unpack_from('<I', hdr, 108); struct.pack_into('<I', hdr, 108, caps | 0x400008)
    open(TEXOUT, 'wb').write(bytes(hdr) + b''.join(l[128:] for l in levels))
    print('wrote', TEXOUT, 'mips', len(levels))
