# v1.8.30h: the sailing boat's hull back to the original model (BigWoodenBoat03 = SB_HullPlanks, BigWoodenBoat02 = SB_HullFrame,
# straight from the OBJs in blender/sailboat/bymat); the mast, spars and ropes are left as they are. The blanket and the sacks
# are taken off the boat (unlinked from the root), and so is what was left of the life ring: its hook on the port gunwale and
# the peg beside it.
import sys, struct; sys.path.insert(0, '.')
import numpy as np
from nifmerge import Nif, e8
from hull_v144_lib import load, pos, store, components
from objload import obj
SRC, DST, BYMAT = sys.argv[1], sys.argv[2], sys.argv[3]
n = Nif(open(SRC, 'rb').read())
def shape(name):
    for k, b in enumerate(n.h['blocks']):
        if b['type'] == 'BSTriShape' and n.names[int(np.frombuffer(n.blocks[k][:4], '<i4')[0])] == name: return k
    raise KeyError(name)
def tangents(P, N, UV, F):
    T = np.zeros_like(P); Bt = np.zeros_like(P)
    p0, p1, p2 = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]; u0, u1, u2 = UV[F[:, 0]], UV[F[:, 1]], UV[F[:, 2]]
    e1, e2 = p1 - p0, p2 - p0; d1, d2 = u1 - u0, u2 - u0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]; b = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    for j in range(3): np.add.at(T, F[:, j], t); np.add.at(Bt, F[:, j], b)
    T -= N * np.sum(T * N, 1, keepdims=True); bad = np.linalg.norm(T, axis=1) < 1e-9
    T[bad] = np.cross(N[bad], [0, 0, 1.0]) + 1e-6; T /= np.linalg.norm(T, axis=1, keepdims=True)
    return T
def vbuf(P, N, UV, F):
    Tg = tangents(P, N, UV, F); B = np.cross(N, Tg)
    V = np.zeros((len(P), 32), np.uint8)
    V[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    V[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    V[:, 16:20] = np.ascontiguousarray(np.c_[UV[:, 0], 1.0 - UV[:, 1]].astype('<f2')).view(np.uint8).reshape(-1, 4)
    V[:, 20:23] = e8(N); V[:, 23] = e8(B[:, 1]); V[:, 24:27] = e8(Tg); V[:, 27] = e8(B[:, 2]); V[:, 28:32] = 255
    return V
def drop(P, F, test):
    keep = np.ones(len(F), bool)
    for c in components(P, F):
        q = P[c]
        if test(q.min(0), q.max(0), len(c)):
            keep &= ~np.isin(F, c).any(1); print('  dropped a part of', len(c), 'vertices at', q.min(0).round(0), q.max(0).round(0))
    F = F[keep]; used = np.unique(F); re = -np.ones(len(P), int); re[used] = np.arange(len(used))
    return used, re[F]
PEG = lambda lo, hi, nv: nv < 40 and np.all(lo > [-66, 158, 84]) and np.all(hi < [-57, 168, 100])
HOOK = lambda lo, hi, nv: nv < 200 and np.all(lo > [-58, 178, 86]) and np.all(hi < [-49, 190, 96])
for name, src, test in (('SB_HullPlanks', 'BigWoodenBoat03', None), ('SB_HullFrame', 'BigWoodenBoat02', PEG)):
    k = shape(name); st, vb, T = load(n.blocks[k])
    P, UV, N, F = obj(BYMAT + '/' + src + '.obj')
    if test:
        used, F = drop(P, F, test); P, UV, N = P[used], UV[used], N[used]
    n.blocks[k] = store(st, vbuf(P, N, UV, F), F)
    print(f'{name}: {len(vb)} -> {len(P)} vertices, {len(T)} -> {len(F)} triangles (original {src})')
k = shape('SB_Ironwork'); st, vb, T = load(n.blocks[k]); P = pos(vb).astype(float)
used, F = drop(P, T, HOOK); n.blocks[k] = store(st, vb[used], F)
# blanket and sacks off the boat
raw = bytearray(n.blocks[0]); ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
nc, = struct.unpack_from('<I', raw, p); kids = list(struct.unpack_from('<%di' % nc, raw, p + 4))
gone = [shape('SB_Blanket'), shape('SB_Sack')]; kids = [c for c in kids if c not in gone]
n.blocks[0] = bytes(raw[:p]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(raw[p + 4 + 4 * nc:])
for g in gone:  # and empty, so nothing of them is drawn even if something links them again
    st, vb, T = load(n.blocks[g]); n.blocks[g] = store(st, vb[:3], np.array([[0, 1, 2]]) * 0)
print('SB_Blanket, SB_Sack: removed')
out = n.write(); open(DST, 'wb').write(out); print(DST, len(out), 'bytes')
