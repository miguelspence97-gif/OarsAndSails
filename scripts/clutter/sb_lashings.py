# RowRim v1.8.27d: the rope lashings round the sailing boat's mast (the bands the boom's and gaff's ropes tie to) turn with
# the main rig. They were in SB_Rigging, which stands still, so the ropes in SB_MainRig (SB_BoomRope, SB_Halyards) swung
# away from them as the boom turned. Every SB_Rigging piece wrapped round the mast (all its vertices within 13 of the mast's
# axis, x -2 y 83) moves to a new shape SB_MastLashings under SB_MainRig (its own shader, texture set, alpha: copies of
# SB_Rigging's), in the rig's own space (the rig node sits at -2 83 0 on the mast and turns about it).
# usage: python sb_lashings.py <sailboat nif> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix
from hull_v144_lib import load, store, pos, setpos, components
SRC, OUT = sys.argv[1:3]
MAST = np.array([-2.0, 83.0, 0.0]); R = 13.0
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
rig = next(b for b in h['blocks'] if b['type'] == 'BSTriShape' and tri_shape(d, h, b)['name'] == 'SB_Rigging')
main = next(b['k'] for b in h['blocks'] if b['type'] == 'NiNode' and h['names'][struct.unpack_from('<i', d, b['start'])[0]] == 'SB_MainRig')
t = tri_shape(d, h, rig)
st, vb, F = load(n.blocks[rig['k']])
P = pos(vb).astype(float)
comps = components(P, F)
lash = np.zeros(len(P), bool)
for c in comps:
    q = P[c]
    if np.hypot(q[:, 0] - MAST[0], q[:, 1] - MAST[1]).max() < R and q[:, 2].min() > 100: lash[c] = True
tl = lash[F].all(axis=1)
assert (lash[F].any(axis=1) == tl).all()
print(f'SB_Rigging: {len(P)} vertices, {len(F)} triangles; lashings {lash.sum()} vertices, {tl.sum()} triangles in {sum(1 for c in comps if lash[c[0]])} pieces')
def sub(mask_v, tris):
    idx = np.where(mask_v)[0]; re = -np.ones(len(P), int); re[idx] = np.arange(len(idx))
    return vb[idx].copy(), re[tris]
vk, fk = sub(~lash, F[~tl])
vl, fl = sub(lash, F[tl])
setpos(vl, pos(vl).astype(float) - MAST)  # into the rig's space
n.blocks[rig['k']] = store(st, vk, fk)
# the new shape: SB_Rigging's block with its own shader / texture set / alpha
raw = bytearray(store(st, vl, fl))
r2, p = av_fix(bytes(raw)); raw = bytearray(r2)
struct.pack_into('<i', raw, 0, n.name('SB_MastLashings'))
bs = p + 60
_, shd, alp = struct.unpack_from('<3i', raw, bs + 16)
sr, sp = av_fix(n.blocks[shd], 4); sr = bytearray(sr)
tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', n.blocks[tex]))
nshd = n.add('BSLightingShaderProperty', sr)
nalp = n.add('NiAlphaProperty', av_fix(n.blocks[alp])[0]) if alp >= 0 else -1
struct.pack_into('<3i', raw, bs + 16, -1, nshd, nalp)
new = n.add('BSTriShape', raw)
# under SB_MainRig
nr = bytearray(n.blocks[main]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
nc, = struct.unpack_from('<I', nr, q); kids = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + [new]
n.blocks[main] = bytes(nr[:q]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(nr[q + 4 + 4 * nc:])
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(d):,} -> {len(out):,} bytes (shape {new}, shader {nshd}, alpha {nalp})')
