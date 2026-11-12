# RowRim v1.8.51: a short bench on the sailing boat's port side where the first passenger (Lund) sits (Miguel's sketch):
# not across the boat, from the port planking in to 14 off the centreline, so the rower's feet stay clear. Its top at
# the chair sit's height over his seat (root z 48 in the model, the sit 20.8 above it in the model's units), two board
# legs to the floorboards. Wood: the hull frame's own texture (BigWoodenBoat02), a plain patch of its deck, the grain
# along the bench. One shape SB_Bench on the root (model frame), its own shader (copied from SB_HullFrame, no
# specular), texture set and alpha. No collision (passengers are held on their seats).
# usage: python sb_bench.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape
from solidlib import consistent_outward
from scipy.spatial import ConvexHull
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {}
for b in h['blocks']:
    if b['type'] == 'BSTriShape':
        try: shapes[tri_shape(d, h, b)['name']] = b['k']
        except Exception: pass
assert 'SB_Bench' not in shapes, 'already has a bench'
def shader_of(k):
    raw = n.blocks[k]; _, p = av_fix(raw)
    return struct.unpack_from('<2i', raw, p + 60 + 20)
def copy_shader(k):
    sr, sp = av_fix(n.blocks[k], 4); sr = bytearray(sr)
    f1, = struct.unpack_from('<I', sr, sp); struct.pack_into('<I', sr, sp, f1 & ~1)  # no specular
    tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', n.blocks[tex]))
    return n.add('BSLightingShaderProperty', sr)
def copy_alpha(k): return n.add('NiAlphaProperty', av_fix(n.blocks[k])[0]) if k >= 0 else -1
def rename(k, nm):
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm)); n.blocks[k] = bytes(r)
def add_kids(node_k, kids):
    nr = bytearray(n.blocks[node_k]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + list(kids)
    n.blocks[node_k] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])

TOP, THICK, CH = 68.5, 2.4, 0.5          # the top's face, its thickness, the chamfer on its edges
X0, X1, Y0, Y1 = -76.0, -14.0, -97.0, -73.0
LEG_T, FLOOR = 2.6, 46.0                 # the legs' thickness (along x), their feet just into the floorboards
def box(x0, x1, y0, y1, z0, z1, ch=0.0):
    pts = []
    for z, c in ((z0, 0.0), (z1 - ch, 0.0), (z1, ch)):
        if ch == 0.0 and z == z1 - ch: continue
        pts += [(x0 + c, y0 + c, z), (x1 - c, y0 + c, z), (x1 - c, y1 - c, z), (x0 + c, y1 - c, z)]
    if ch:  # the lower edges chamfered too
        pts = [(x0 + ch, y0 + ch, z0), (x1 - ch, y0 + ch, z0), (x1 - ch, y1 - ch, z0), (x0 + ch, y1 - ch, z0),
               (x0, y0, z0 + ch), (x1, y0, z0 + ch), (x1, y1, z0 + ch), (x0, y1, z0 + ch)] + pts[4:]
    P = np.array(pts, float); hull = ConvexHull(P)
    return P, hull.simplices.copy()
parts = [box(X0, X1, Y0, Y1, TOP - THICK, TOP, CH)]
for xc in (X0 + 7.0, X1 - 6.0):
    parts.append(box(xc - LEG_T / 2, xc + LEG_T / 2, Y0 + 2.0, Y1 - 2.0, FLOOR, TOP - THICK + 0.2, 0.3))
# UVs: a plain patch of the atlas's deck (u 0.40-0.50, v 0.20-0.40), the grain (along v in the atlas) along the bench
DEN = 0.0026
def uv_of(V, ax):
    u = np.zeros(len(V)); v = np.zeros(len(V))
    if ax == 2: u, v = (V[:, 1] - Y0) * DEN, (V[:, 0] - X0) * DEN
    elif ax == 1: u, v = (V[:, 2] - FLOOR) * DEN, (V[:, 0] - X0) * DEN
    else: u, v = (V[:, 1] - Y0) * DEN, (V[:, 2] - FLOOR) * DEN
    return np.c_[0.40 + u, 0.20 + v]
PP, NN, TT, UU, FF = [], [], [], [], []; base = 0
for P, F in parts:
    F = consistent_outward(P, F)
    V = P[F].reshape(-1, 3); F2 = np.arange(len(V)).reshape(-1, 3)
    fn = np.cross(V[F2[:, 1]] - V[F2[:, 0]], V[F2[:, 2]] - V[F2[:, 0]]); fn /= np.linalg.norm(fn, axis=1, keepdims=True) + 1e-9
    Nv = np.repeat(fn, 3, 0)  # flat: a plank's faces are flat
    ax = np.repeat(np.argmax(np.abs(fn), 1), 3)
    UV = np.zeros((len(V), 2))
    for a in range(3):
        m = ax == a; UV[m] = uv_of(V[m], a)
    Tg = np.zeros_like(Nv)
    for a in range(3):  # tangent along +u of each face's projection
        m = ax == a
        Tg[m] = np.array([0, 1.0, 0]) if a in (0, 2) else np.array([0, 0, 1.0])
    Tg = Tg - Nv * (Tg * Nv).sum(1, keepdims=True); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    PP.append(V); NN.append(Nv); TT.append(Tg); UU.append(UV); FF.append(F2 + base); base += len(V)
P, N, Tt, U2, F = np.vstack(PP), np.vstack(NN), np.vstack(TT), np.vstack(UU), np.vstack(FF)
tpl = n.blocks[shapes['SB_HullFrame']]
sh, al = shader_of(shapes['SB_HullFrame'])
k = build_shape(n, tpl, P, N, Tt, U2, F, copy_shader(sh), copy_alpha(al)); rename(k, 'SB_Bench')
add_kids(0, [k])
print('SB_Bench', len(P), 'vertices', len(F), 'triangles; top', TOP, 'x', X0, X1, 'y', Y0, Y1)
np.save('bench_PF.npy', np.array([P, F], dtype=object), allow_pickle=True)
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out), 'bytes (was %d)' % len(d))
