# RowRim v1.8.39: the furled main showed bits of the boom's rope through its cloth (Miguel's picture). SB_BoomRope's coils
# lash the boom at its fore end (y -70..-84), mid (-142..-155) and at the clew (-199..-215); the furl SB_MainFurl (same
# frame, SB_MainRig) ran aft to -206, half way through the clew's coils, and the fore coils stood 1-2 out of its top.
# Only the cloth is changed, the rope stays: the furl's aft end is drawn in to stop short of the clew's coils (they lash
# bare boom, as a furled sail's clew does), and where a coil still stands out of the cloth the cloth is eased out over it.
# usage: python sb_furlclear.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifmerge import Nif
from hull_v144_lib import load, store, pos, setpos, components
from solidlib import weld, face_normals_at
from scipy.spatial import cKDTree
SRC, OUT = sys.argv[1:3]
n = Nif(open(SRC, 'rb').read())
def named(nm):
    for k, b in enumerate(n.h['blocks']):
        if 'TriShape' in b['type'] and n.names[struct.unpack_from('<i', n.blocks[k], 0)[0]] == nm: return k
def inside(Q, P, F):  # ray +x parity against a closed mesh
    A, B, C = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]; res = np.zeros(len(Q), int)
    for i in range(0, len(Q), 400):
        q = Q[i:i + 400][:, None, :]
        v0 = (C - A)[None, :, 1:]; v1 = (B - A)[None, :, 1:]; v2 = q[:, :, 1:] - A[None, :, 1:]
        d00 = (v0 * v0).sum(-1); d01 = (v0 * v1).sum(-1); d11 = (v1 * v1).sum(-1); d20 = (v2 * v0).sum(-1); d21 = (v2 * v1).sum(-1)
        den = d00 * d11 - d01 * d01 + 1e-12; u = (d11 * d20 - d01 * d21) / den; v = (d00 * d21 - d01 * d20) / den
        x = A[None, :, 0] + u * (C - A)[None, :, 0] + v * (B - A)[None, :, 0]
        res[i:i + 400] = ((u >= 0) & (v >= 0) & (u + v <= 1) & (x > q[:, :, 0])).sum(1)
    return res % 2 == 1
kf = named('SB_MainFurl'); kr = named('SB_BoomRope')
st, vb, F = load(n.blocks[kf]); P = pos(vb).astype(float); F = np.asarray(F, int)
_, rvb, RF = load(n.blocks[kr]); R = pos(rvb).astype(float); RF = np.asarray(RF, int)
w = weld(P)
# 1) the aft end drawn in: y below Y0 squeezed so the end (was -206) stops at YE, short of the clew's first coil (-198.8)
Y0, YE = -180.0, -197.6; ymin = P[:, 1].min()
m = P[:, 1] < Y0; P[m, 1] = Y0 + (P[m, 1] - Y0) * (Y0 - YE) / (Y0 - ymin)
# 2) the cloth eased out over any coil part still standing out of it (only the coils that sit mostly under the cloth)
coils = [c for c in components(R, RF)]
for it in range(12):
    inn = inside(R, P, F); out_pts = []
    for c in coils:
        f = inn[c].mean()
        if 0.5 <= f < 1.0: out_pts.append(R[c][~inn[c]])
    if not out_pts: break
    Q = np.vstack(out_pts)
    N = face_normals_at(P, F); Nw = np.zeros((w.max() + 1, 3)); np.add.at(Nw, w, N)
    Nw /= np.linalg.norm(Nw, axis=1, keepdims=True) + 1e-9; Pw = np.zeros((w.max() + 1, 3)); Pw[w] = P
    kd = cKDTree(Pw); push = np.zeros(len(Pw))
    for q in Q:
        ids = kd.query_ball_point(q, 4.5)
        for i in ids:
            h = np.dot(q - Pw[i], Nw[i]) + 0.9  # how far the coil stands out over this cloth point, plus a margin
            if h <= 0: continue
            fall = max(0.0, 1.0 - np.linalg.norm(q - Pw[i]) / 4.5) ** 0.7
            push[i] = max(push[i], h * fall)
    Pw += Nw * push[:, None] * 0.6
    P = Pw[w]
    print('pass', it, 'coil points out', len(Q), 'max push %.2f' % push.max())
inn = inside(R, P, F)
for c in coils:
    f = inn[c].mean()
    if 0.0 < f < 1.0: print('STILL CROSSING', len(c), '%.2f' % f, R[c].min(0).round(1), R[c].max(0).round(1))
setpos(vb, P)
# normals: rebuilt from the faces (the eased and the squeezed parts lean a little), packed as the shape packs them
N = face_normals_at(P, F); Nw = np.zeros((w.max() + 1, 3)); np.add.at(Nw, w, N); N = Nw[w]; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
old = (vb[:, 20:23].astype(float) / 127.5 - 1.0)
d = (old * N).sum(1); print('normals turned: mean dot %.3f min %.3f' % (d.mean(), d.min()))
vb[:, 20:23] = np.clip(np.round((N + 1.0) * 127.5), 0, 255).astype(np.uint8)
n.blocks[kf] = store(st, vb, F)
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out))
