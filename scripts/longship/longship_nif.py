# RowRim: the Viking longship as its own model, RowRim_Longship.nif (LOCAL TEST, sailing work stays local).
# Model: "Viking longship" by massive-graphisme (Sketchfab), CC BY 4.0: credit + link + licence + "modified" on release.
# Parts from longship_prep.py (dec\*.obj, decimated 49.4k -> ~21k tris, UVs kept), in Skyrim model space: +y the bow (the
# dragon head), +z up, x to starboard (the steering oar's side). Model units are ~10 m: K = 1100 (RowRim's 110 a metre),
# so 2190 long, the sea chests 35 high (the rowboat thwart 32); the deck (it rises toward the ends) at z 46 amidships (lake 32).
# RULES.md: every shape its own shader / texture set / alpha, specular off, no collision.
# LS_SailRig (NiNode at the mast, on the deck) holds the yard, its robands and the square sail, so the plugin can brace
# the yard about the mast. The sail is almost flat (the model's belly cut to SAIL_BELLY) and a thin solid: two
# single-sided skins 0.35 each side of the cloth (v1.7.95: a moving two-sided single surface flickered).
# LS_Oars (the 24 rowing oars) is its own shape so the plugin can hide them under sail.
# usage: python longship_nif.py <template hull nif (RowRim_Hull.nif)> <dir with dec\*.obj> <out nif>
import sys, struct, numpy as np
from nifp import tri_shape
from nifmerge import Nif, node_block, av_fix, tex_set, e8
from hull_v144_lib import load, store
TPLNIF, SRC, OUT = sys.argv[1:4]
K = 1100.0; ZOFF = 449.0; XC = 0.0  # v1.8.8: the deck is lowest amidships (model 0.37 below its ends): there at z 46, 14 over the lake
SAIL_BELLY = 0.2          # 1 = the model's full belly (2.3 m deep); 0.2 = almost flat, still 3D
MAST_X = 0.04             # the mast's foot in model x
def to_sk(V): return np.stack([-V[:, 2] * K, -(V[:, 0] - XC) * K, V[:, 1] * K + ZOFF], 1)
td = open(TPLNIF, 'rb').read(); tn = Nif(td)
def tex_paths(nn, k):
    raw = nn.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1')); p += 4 + l
    return out
TPL = BASE_SH = None
for k, t in enumerate(tn.ti):
    if tn.types[t] != 'BSLightingShaderProperty': continue
    ts, = struct.unpack_from('<i', tn.blocks[k], 40)
    if ts >= 0 and 'boat_floor_d' in tex_paths(tn, ts)[0].lower():
        for s2, t2 in enumerate(tn.ti):
            if tn.types[t2] == 'BSTriShape':
                ti = tri_shape(td, tn.h, tn.h['blocks'][s2])
                if ti['shader'] == k and ti['vsize'] == 32: TPL, BASE_SH = tn.blocks[s2], tn.blocks[k]; break
    if TPL: break
assert TPL
st, _, _ = load(TPL)
n = Nif(td); n.blocks = []; n.ti = []; n.names = []
root = n.add('BSFadeNode', node_block(n.name('RowRim_Longship')))
def shader(paths, two_sided=False, skinned=False):
    t = n.add('BSShaderTextureSet', tex_set(paths))
    r, _ = av_fix(BASE_SH, 4); r = bytearray(r); struct.pack_into('<i', r, 40, t)
    f1, f2 = struct.unpack_from('<II', r, 16)
    f1 &= ~1                      # specular off (matte)
    if skinned: f1 |= 0x2         # v1.8.9: the sail is skinned to its bones
    else: f1 &= ~0x2
    if two_sided: f2 |= 0x10
    else: f2 &= ~0x10
    f2 &= ~0x20                   # no vertex colours
    struct.pack_into('<II', r, 16, f1, f2)
    return n.add('BSLightingShaderProperty', r)
def alpha(): return n.add('NiAlphaProperty', struct.pack('<iIiHB', -1, 0, -1, 0x00EC, 0))
def read_obj(path):
    V = []; T = []; F = []
    for l in open(path):
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('f '): F.append([tuple(int(x) - 1 for x in w.split('/')[:2]) for w in l.split()[1:4]])
    return np.array(V, float), np.array(T, float), F
CREASE = np.cos(np.radians(55))
def build(V, T, F, xf):
    """corners -> vertices: smooth normals across faces sharing a position unless they meet at a crease (> 55 deg),
    so the chests and planks keep their edges; split by uv and normal"""
    S = xf(V); Fv = np.array([[a[0] for a in f] for f in F]); Ft = np.array([[a[1] for a in f] for f in F])
    a, b, c = S[Fv[:, 0]], S[Fv[:, 1]], S[Fv[:, 2]]
    fn = np.cross(b - a, c - a); keep = np.linalg.norm(fn, axis=1) > 1e-9
    Fv, Ft, fn = Fv[keep], Ft[keep], fn[keep]
    fu = fn / np.linalg.norm(fn, axis=1, keepdims=True)
    _, pos = np.unique(np.round(S, 3), axis=0, return_inverse=True); pos = pos.ravel()
    at = {}
    for fi in range(len(Fv)):
        for k in range(3): at.setdefault(pos[Fv[fi, k]], []).append(fi)
    keys = {}; P = []; U = []; N = []; tris = []
    for fi in range(len(Fv)):
        tri = []
        for k in range(3):
            pv = pos[Fv[fi, k]]; nb = np.array(at[pv])
            ok = nb[(fu[nb] @ fu[fi]) > CREASE]
            nn = fn[ok].sum(0); nn /= np.linalg.norm(nn) + 1e-12
            key = (Fv[fi, k], Ft[fi, k], tuple(np.round(nn, 2)))
            if key not in keys: keys[key] = len(P); P.append(S[Fv[fi, k]]); U.append(T[Ft[fi, k]]); N.append(nn)
            tri.append(keys[key])
        tris.append(tri)
    return np.array(P), np.array(U), np.array(N), np.array(tris)
def tangents(P, U, N, F):
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    du1 = U[F[:, 1]] - U[F[:, 0]]; du2 = U[F[:, 2]] - U[F[:, 0]]
    r = du1[:, 0] * du2[:, 1] - du2[:, 0] * du1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    ft = ((b - a) * du2[:, 1:2] - (c - a) * du1[:, 1:2]) / r[:, None]
    Tg = np.zeros_like(P)
    for i in range(3): np.add.at(Tg, F[:, i], ft)
    Tg -= N * (Tg * N).sum(1, keepdims=True)
    bad = np.linalg.norm(Tg, axis=1) < 1e-6
    Tg[bad] = np.cross(N[bad], [0, 0, 1]); bad = np.linalg.norm(Tg, axis=1) < 1e-6; Tg[bad] = [1, 0, 0]
    return Tg / (np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9)
def shape(name, P, U, N, F, sh, alp, hidden=False):
    assert len(P) < 65535, (name, len(P)); assert F.min() >= 0 and F.max() < len(P)
    Tg = tangents(P, U, N, F); B = np.cross(N, Tg)
    vb = np.zeros((len(P), 32), np.uint8)
    vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    vb[:, 16:20] = np.ascontiguousarray(np.stack([U[:, 0], 1.0 - U[:, 1]], 1).astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 20:23] = e8(N); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2]); vb[:, 28:32] = 255
    raw = bytearray(store(st, vb, F)); raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
    struct.pack_into('<i', raw, 0, n.name(name))
    struct.pack_into('<I', raw, p0, 0x0008000E | (1 if hidden else 0))
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sh, alp)
    print(f'{name}: {len(F)} tris, {len(P)} verts, x {P[:,0].min():.0f}..{P[:,0].max():.0f} y {P[:,1].min():.0f}..{P[:,1].max():.0f} z {P[:,2].min():.0f}..{P[:,2].max():.0f}')
    return n.add('BSTriShape', raw)
def TX(m): return ['textures\\RowRim\\Longship\\%s_d.dds' % m, 'textures\\RowRim\\Longship\\%s_n.dds' % m]
def part(src, name, tx, two=False, xf=to_sk):
    P, U, N, F = build(*read_obj(f'{SRC}/{src}.obj'), xf)
    return shape(name, P, U, N, F, shader(TX(tx), two), alpha())
STATIC = [  # (obj, shape, texture set, two-sided)
    ('ldhull', 'LS_Hull', 'ldhull', False), ('bridge', 'LS_Deck', 'bridge', False),
    ('quillandcarvings', 'LS_Carvings', 'quillandcarvings', False), ('logs', 'LS_Frames', 'logs', False),
    ('bench', 'LS_Chests1', 'bench', False), ('bench2', 'LS_Chests2', 'bench2', False),
    ('shield1', 'LS_Shields1', 'shield1', False), ('shield2', 'LS_Shields2', 'shield2', False), ('shields3', 'LS_Shields3', 'shields3', False),
    ('spars', 'LS_Spars', 'mastsandpaddles', False), ('ropes', 'LS_Rigging', 'ropes', False),
    ('sail_spare', 'LS_SpareSail', 'sail', True), ('bucket', 'LS_Bucket', 'bucket', False),
    ('oars', 'LS_Oars', 'mastsandpaddles', False)]
kids = [part(*p) for p in STATIC]
# the sail rig: pivot on the mast at deck height; its parts in rig space (boat space minus the pivot, no turn)
PIV = to_sk(np.array([[MAST_X, (46 - ZOFF) / K, 0.0]]))[0]   # the mast foot on the deck (z 46)
def to_rig(V): return to_sk(V) - PIV
rig = [part('yard', 'LS_Yard', 'mastsandpaddles', False, to_rig), part('yardropes', 'LS_YardRopes', 'ropes', False, to_rig)]
# the sail: almost flat. The model's cloth hangs from the yard and bellies fore/aft (model x); every point keeps its
# height and width, its x deviation from the yard's line is cut to SAIL_BELLY. Then a thin solid.
V, T, F = read_obj(f'{SRC}/sail.obj')
Vy, _, _ = read_obj(f'{SRC}/yard.obj'); x0 = float(np.median(Vy[:, 0]))
V = V.copy(); V[:, 0] = x0 + (V[:, 0] - x0) * SAIL_BELLY
print(f'sail: belly x {V[:,0].min()-x0:+.3f}..{V[:,0].max()-x0:+.3f} model units about the yard (x {x0:.3f}), {SAIL_BELLY} of the original')
P, U, N, Fs = build(V, T, F, to_rig)
# v1.8.9: every sail triangle split in four (shared edge midpoints, no cracks): the cloth bends smoothly with its bones
def subdiv(P, U, N, F):
    P = list(P); U = list(U); N = list(N); mid = {}; out = []
    def m(a, b):
        k = (min(a, b), max(a, b))
        if k not in mid:
            mid[k] = len(P); P.append((P[a] + P[b]) / 2); U.append((U[a] + U[b]) / 2)
            nn = N[a] + N[b]; N.append(nn / (np.linalg.norm(nn) + 1e-9))
        return mid[k]
    for a, b, c in F:
        ab, bc, ca = m(a, b), m(b, c), m(c, a)
        out += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
    return np.array(P), np.array(U), np.array(N), np.array(out)
P, U, N, Fs = subdiv(P, U, N, Fs)
HALF = 0.35
P2 = np.vstack([P + N * HALF, P - N * HALF]); U2 = np.vstack([U, U]); N2 = np.vstack([N, -N])
F2 = np.vstack([Fs, Fs[:, ::-1] + len(P)])
# v1.8.9: the sail skinned to an 8 x 8 grid of bones (LS_SailBones: rows foot -> head, columns port -> starboard), as
# the sailing boat's: the plugin moves the bones (the wind filling it, a slow wave, a shiver), every edge held still.
# Skeleton root: LS_SailRig itself (the sail and its bones turn with the yard).
from skinsail import bone_grid, skin_blocks
R_, C_ = 8, 8
tt = (P2[:, 2] - P2[:, 2].min()) / max(np.ptp(P2[:, 2]), 1.0)
uu = (P2[:, 0] - P2[:, 0].min()) / max(np.ptp(P2[:, 0]), 1.0)
binds, idx, wts = bone_grid(P2, tt, uu, R_, C_)
binds[:, 0] = P2[:, 0].min() + np.tile(np.linspace(0, 1, C_), R_) * np.ptp(P2[:, 0])   # bones on a clean grid across ...
binds[:, 2] = P2[:, 2].min() + np.repeat(np.linspace(0, 1, R_), C_) * np.ptp(P2[:, 2])  # ... and up the sail
rig_id = n.add('NiNode', b'')
bones = [n.add('NiNode', node_block(n.name('LS_SailBone%d' % i), T=tuple(binds[i]))) for i in range(len(binds))]
Tg2 = tangents(P2, U2, N2, F2)
inst, desc = skin_blocks(n, e8, P2, N2, Tg2, U2, F2, idx, wts, binds, bones, rig_id)
hd = bytearray(st['raw'][:st['data_off']])
cen = P2.mean(0); rad = float(np.linalg.norm(P2 - cen, axis=1).max()) + 80.0
struct.pack_into('<4f', hd, st['bs_off'], float(cen[0]), float(cen[1]), float(cen[2]), rad)
struct.pack_into('<Q', hd, st['cnt_off'] - 8, desc)
struct.pack_into('<HHI', hd, st['cnt_off'], 0, 0, 0)
raw = bytes(hd) + struct.pack('<I', 0)
raw, p0 = av_fix(raw); raw = bytearray(raw)
struct.pack_into('<i', raw, 0, n.name('LS_Sail'))
struct.pack_into('<I', raw, p0, 0x0008000E)
struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
struct.pack_into('<3i', raw, p0 + 60 + 16, inst, shader(TX('sail'), False, skinned=True), alpha())
rig.append(n.add('BSTriShape', raw))
rig.append(n.add('NiNode', node_block(n.name('LS_SailBones'), kids=bones)))
print(f'LS_Sail: skinned, {len(F2)} tris, {len(P2)} verts, {len(binds)} bones')
n.blocks[rig_id] = node_block(n.name('LS_SailRig'), T=tuple(PIV), kids=rig)
kids.append(rig_id)
print('LS_SailRig pivot (mast foot on the deck):', np.round(PIV, 1))
n.blocks[root] = node_block(n.name('RowRim_Longship'), kids=kids)
open(OUT, 'wb').write(n.write())
print('->', OUT)
