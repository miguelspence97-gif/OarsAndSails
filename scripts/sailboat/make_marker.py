# RowRim v1.8.51: meshes\RowRim\RowRim_SeatMarker.nif, the pink seat markers (Settings / Crew: Show seat markers):
# three figures on the root, each its own shape, shader and texture set: Chair (the chair sit: root under the seat at
# the floor), Ledge (the ledge sit: root on the edge's top, legs hanging over), Dog (sitting). Game units, facing +y,
# each with a flat arrow at its root. Pink, glowing a little (own emit) so they show in the dark; no specular, no
# alpha, no collision. textures\RowRim\RowRim_SeatMarker_d.dds: plain pink, DXT1 with mips (mean luminance 0.29).
# usage: python make_marker.py <RowRim_WindStreak.nif (a small nif to start from)> <a nif with SB_BowBitt (its shape
# and shader as templates)> <out nif> <out dds>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape, tex_set
from solidlib import consistent_outward
from bc1 import save_dds
from PIL import Image
import figs
BASE, TPL, OUTN, OUTT = sys.argv[1:5]
td = open(TPL, 'rb').read(); th = parse(td); tn = Nif(td)
tk = {}
for b in th['blocks']:
    if b['type'] == 'BSTriShape':
        try: tk[tri_shape(td, th, b)['name']] = b['k']
        except Exception: pass
shape_raw = tn.blocks[tk['SB_BowBitt']]
_, p = av_fix(shape_raw); _, shk, _ = struct.unpack_from('<3i', shape_raw, p + 60 + 16)
shader_raw, sp = av_fix(tn.blocks[shk], 4)

n = Nif(open(BASE, 'rb').read())
n.blocks = n.blocks[:1]; n.ti = n.ti[:1]
DIF = 'textures\\RowRim\\RowRim_SeatMarker_d.dds'; NRM = 'textures\\RowRim\\SailBoat\\RowRim_Flat_n.dds'
PINK = (0.80, 0.12, 0.50)
def shader():
    r = bytearray(shader_raw)
    f1, = struct.unpack_from('<I', r, sp)
    struct.pack_into('<I', r, sp, (f1 & ~1) | 0x00400000)       # no specular; own emit
    struct.pack_into('<i', r, sp + 24, n.add('BSShaderTextureSet', tex_set([DIF, NRM])))
    struct.pack_into('<4f', r, sp + 28, 1.0, 0.30, 0.70, 0.55)  # emissive colour, multiple
    return n.add('BSLightingShaderProperty', r)
def merge(parts):
    PP, NN, TT, UU, FF = [], [], [], [], []; base = 0
    for P, F in parts:
        F = consistent_outward(P, F)
        V = P[F].reshape(-1, 3); F2 = np.arange(len(V)).reshape(-1, 3)
        fn = np.cross(V[F2[:, 1]] - V[F2[:, 0]], V[F2[:, 2]] - V[F2[:, 0]]); fn /= np.linalg.norm(fn, axis=1, keepdims=True) + 1e-9
        key = np.round(V, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
        acc = np.zeros((inv.max() + 1, 3))
        for i in range(3): np.add.at(acc, inv[F2[:, i]], fn)
        Nv = acc[inv]; Nv /= np.linalg.norm(Nv, axis=1, keepdims=True) + 1e-9
        hard = (Nv * np.repeat(fn, 3, 0)).sum(1) < 0.7; Nv[hard] = np.repeat(fn, 3, 0)[hard]
        Tg = np.cross(Nv, [0.31, 0.47, 0.83]); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
        UV = np.full((len(V), 2), 0.5)
        PP.append(V); NN.append(Nv); TT.append(Tg); UU.append(UV); FF.append(F2 + base); base += len(V)
    return np.vstack(PP), np.vstack(NN), np.vstack(TT), np.vstack(UU), np.vstack(FF)
kids = []
for nm, fn in (('Chair', figs.chair), ('Ledge', figs.ledge), ('Dog', figs.dog)):
    P, N, T, U, F = merge(fn())
    k = build_shape(n, shape_raw, P, N, T, U, F, shader(), -1)
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm)); n.blocks[k] = bytes(r)
    kids.append(k); print(nm, len(P), 'vertices', len(F), 'triangles')
# the root: its name, its children the three figures
root = bytearray(n.blocks[0]); struct.pack_into('<i', root, 0, n.name('RowRim_SeatMarker'))
ne, = struct.unpack_from('<I', root, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
nc, = struct.unpack_from('<I', root, q)
n.blocks[0] = bytes(root[:q]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(root[q + 4 + 4 * nc:])
n.names = [s for s in n.names]  # (the old piece name stays in the table, unused)
used = sorted(set(n.ti)); remap = {o: i for i, o in enumerate(used)}
n.types = [n.types[o] for o in used]; n.ti = [remap[t] for t in n.ti]  # only the block types in use
out = n.write(); open(OUTN, 'wb').write(out); print(OUTN, len(out), 'bytes')
img = np.ones((16, 16, 3)) * np.array(PINK)
lum = 0.2126 * PINK[0] + 0.7152 * PINK[1] + 0.0722 * PINK[2]
print('texture mean luminance %.3f' % lum, save_dds(OUTT, Image.fromarray((img * 255).astype(np.uint8))))
