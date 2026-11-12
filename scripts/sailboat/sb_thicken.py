# RowRim v1.8.36: the sailing boat's deck box and its stern seat made solid (Miguel). Both were open shells in the bought
# model (SB_Spars): the box with no bottom (its outline at z 69, y -8..53), the seat's top and front panel single sheets
# (outlines at y -258..-187 and y -192..-188), so from below or through the panel's opening you saw through them.
# Each part with one of those outlines is thickened: an inner skin 1.2 in from its faces (wound the other way) and the
# two skins joined round every open edge, so it is a closed solid however you look at it.
# usage: python sb_thicken.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifmerge import Nif, e8
from hull_v144_lib import load, store, pos
from solidlib import weld, boundary, loops, components_welded, solid_volume_sign
SRC, OUT = sys.argv[1:3]
n = Nif(open(SRC, 'rb').read())
def shape(name):
    for k, b in enumerate(n.h['blocks']):
        if b['type'] == 'BSTriShape' and n.names[struct.unpack_from('<i', n.blocks[k], 0)[0]] == name: return k
k = shape('SB_Spars'); st, vb, F = load(n.blocks[k]); F = np.asarray(F, int)
P = pos(vb).astype(float); w = weld(P)
L = loops(boundary(F, w), w)
TARGET = [((-37, -9, 68), (33, 54, 70)), ((-74, -259, 65), (75, -186, 74)), ((-73, -193, 30), (73, -187, 70))]  # box, seat top, seat front
hit = set()
for l in L:
    q = P[l]
    for lo, hi in TARGET:
        if np.all(q.min(0) >= np.array(lo)) and np.all(q.max(0) <= np.array(hi)): hit.update(w[l].tolist())
roots = components_welded(F, w)
parts = sorted({roots[t] for t in range(len(F)) if set(w[F[t]].tolist()) & hit})
T = 1.2
newv = [vb]; newf = [F]; base = len(vb)
for r in parts:
    tm = roots == r; sub = F[tm]; used = np.unique(sub)
    N = vb[used, 20:23].astype(float) / 127.5 - 1.0; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    # the weld groups share one offset (the average normal), so the inner skin stays closed where the outer one is
    wu = w[used]; avg = {}
    for g, nn in zip(wu, N): avg.setdefault(g, []).append(nn)
    No = np.array([np.mean(avg[g], 0) for g in wu]); No /= np.linalg.norm(No, axis=1, keepdims=True) + 1e-9
    inner = vb[used].copy(); Pi = P[used] - No * T
    inner[:, 0:12] = np.ascontiguousarray(Pi.astype('<f4')).view(np.uint8).reshape(-1, 12)
    # the inner skin faces in: normals flipped (with the bitangent)
    inner[:, 20:23] = e8(-(inner[:, 20:23].astype(float) / 127.5 - 1.0))
    inner[:, 23] = e8(-(inner[:, 23:24].astype(float) / 127.5 - 1.0))[:, 0]; inner[:, 27] = e8(-(inner[:, 27:28].astype(float) / 127.5 - 1.0))[:, 0]
    bx = inner[:, 12:16].copy().view('<f4').ravel(); inner[:, 12:16] = (-bx).astype('<f4').view(np.uint8).reshape(-1, 4)
    off = base + sum(len(a) for a in newv[1:]); remap = -np.ones(len(vb), int); remap[used] = off + np.arange(len(used))
    newv.append(inner); newf.append(remap[sub][:, [0, 2, 1]])
    # the rim: each open edge of the part joined to its inner twin (its own vertices, the rim's normal)
    be = [(x, y) for x, y in boundary(sub, w)]
    rim_v = []; rim_f = []; o2 = off + len(used)
    for x, y in be:
        a, b = P[x], P[y]; ai, bi = Pi[np.searchsorted(used, x)], Pi[np.searchsorted(used, y)]
        nrm = np.cross(a - b, ai - b); nrm = nrm / (np.linalg.norm(nrm) + 1e-9)  # the rim triangle (y, x, x')'s own facing
        rows = vb[[x, y, x, y]].copy()
        for j, p in enumerate((a, b, ai, bi)): rows[j, 0:12] = np.frombuffer(p.astype('<f4').tobytes(), np.uint8)
        rows[:, 20:23] = e8(np.tile(nrm, (4, 1)))
        i0 = o2 + 4 * len(rim_v); rim_v.append(rows)
        # (y, x, x') and (y, x', y'): runs y -> x, the open edge the other way round from its own triangle
        rim_f += [[i0 + 1, i0 + 0, i0 + 2], [i0 + 1, i0 + 2, i0 + 3]]
    if rim_v: newv.append(np.vstack(rim_v)); newf.append(np.array(rim_f, int))
    print(f'part {r}: {len(sub)} triangles, {len(be)} open edges joined, inner skin {len(used)} vertices')
V2 = np.vstack(newv); F2 = np.vstack(newf)
assert len(V2) < 65535 and len(F2) < 65535
n.blocks[k] = store(st, V2, F2)
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out))
