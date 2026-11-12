# RowRim v1.8.38: the rope loops at the mast that swung out of place as the boom turned (Miguel's picture: one at mid
# mast, z 271-279, one at the boom's jaw, z 145-152). SB_JawRopeS / SB_JawRopeP (in SB_MainRig, turning with the boom)
# were rings 9.6 aft of the mast's axis, so they swung round the mast with the boom and stood off it. The mid mast
# ones doubled the lashings SB_MastLashings already has there (round the mast): dropped. The boom jaw's are put round the
# mast (centred on its axis -2 83, moved out clear of its radius 6.4, their thickness kept), and both shapes go onto
# the root: they stay put round the mast, as the lashings do (v1.8.32); only the boom and gaff swing.
# usage: python sb_jawfix.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifmerge import Nif
from hull_v144_lib import load, store, pos, components
SRC, OUT = sys.argv[1:3]
n = Nif(open(SRC, 'rb').read())
def named(nm, kind):
    for k, b in enumerate(n.h['blocks']):
        if kind in b['type'] and n.names[struct.unpack_from('<i', n.blocks[k], 0)[0]] == nm: return k
def kids_of(k):
    r = n.blocks[k]; ne, = struct.unpack_from('<I', r, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', r, q); return q, list(struct.unpack_from('<%di' % nc, r, q + 4))
def set_kids(k, kids):
    r = n.blocks[k]; q, old = kids_of(k)
    n.blocks[k] = bytes(r[:q]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(r[q + 4 + 4 * len(old):])
rig = named('SB_MainRig', 'Node'); r = n.blocks[rig]; ne, = struct.unpack_from('<I', r, 4)
RT = np.array(struct.unpack_from('<3f', r, 4 + 4 + 4 * ne + 4 + 4))
MAST = np.array([-2.0, 83.0]); LOWER = np.array([-5.14, 74.05])
for nm in ('SB_JawRopeS', 'SB_JawRopeP'):
    k = named(nm, 'TriShape'); st, vb, F = load(n.blocks[k]); P = pos(vb).astype(float) + RT; F = np.asarray(F, int)
    keepF = np.ones(len(F), bool); RM = 6.4  # the mast's radius
    for c in components(P, F):
        q = P[c]; zc = q[:, 2].mean()
        if 255.0 < zc < 300.0:  # mid mast: the very lashings SB_MastLashings has round the mast there, 9.6 off: dropped
            keepF &= ~np.isin(F, c).any(1); continue
        # v1.8.38b: at the boom's jaw the mast is the lower mast (its own pole, axis -5.14 74.05, radius 5.0, z 69..286; the
        # topmast -2 83 starts at 174): the coil goes round that one
        AX, RR = (LOWER, 5.0) if zc < 174.0 else (MAST, RM)
        cen = (q[:, :2].min(0) + q[:, :2].max(0)) / 2
        P[c, 0:2] += AX - cen  # round the mast's axis
        d = P[c, 0:2] - AX; r = np.linalg.norm(d, axis=1) + 1e-9
        if r.max() < 8.5: r2 = r + (RR + 0.6 - r.min())  # a ring: moved out whole, its thickness kept, clear of the mast
        else: r2 = np.maximum(r, RR + 0.9)             # the jaw's knot: hugging the mast, never through it
        P[c, 0:2] = AX + d / r[:, None] * r2[:, None]
    used = np.unique(F[keepF]); re = -np.ones(len(P), int); re[used] = np.arange(len(used))
    vb = vb[used].copy(); vb[:, 0:12] = np.ascontiguousarray(P[used].astype('<f4')).view(np.uint8).reshape(-1, 12)
    n.blocks[k] = store(st, vb, re[F[keepF]])
    _, rk = kids_of(rig); set_kids(rig, [c for c in rk if c != k]); _, root = kids_of(0); set_kids(0, root + [k])
    print(nm, ': rings round the mast, onto the root')
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out))
