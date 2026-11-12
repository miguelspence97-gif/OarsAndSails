# RowRim v1.8.30g: every rope on the sailing boat remeshed in high detail (Miguel). Each rope piece of the model's rope
# shapes (SB_Rigging, SB_BoomRope, SB_GaffRope, SB_Halyards, SB_MastLashings, SB_JawRopeS/P, SB_JibTack) is shrunk to
# its centre line (the mesh smoothed onto itself), the line ordered (a loop: round its plane; else end to end), smoothed
# and rebuilt as a round rope of three twisted strands: 12 sides, a ring every 0.8, the strands laid at three rope widths
# a turn. The skinned jib sheet the same, straight between its two bones. The shapes keep their names, shaders and
# places. SB_Blanket hidden (Miguel: no blanket in the hull).
# usage: python sb_ropes.py <in nif> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, e8
from hull_v144_lib import load, store, pos, components
from skinsail import skin_blocks
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {tri_shape(d, h, b)['name']: b['k'] for b in h['blocks'] if b['type'] == 'BSTriShape'}
SEG, STEP = 12, 0.8
def centreline(P, F):
    key = np.round(P, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    m = inv.max() + 1
    W = np.zeros((m, 3)); cnt = np.zeros(m)
    np.add.at(W, inv, P); np.add.at(cnt, inv, 1); X0 = W / cnt[:, None]
    E = np.vstack([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]]); E = inv[E]; E = E[E[:, 0] != E[:, 1]]
    E = np.unique(np.sort(E, 1), axis=0)
    X = X0.copy()
    for _ in range(90):
        acc = np.zeros_like(X); c = np.zeros(m)
        np.add.at(acc, E[:, 0], X[E[:, 1]]); np.add.at(acc, E[:, 1], X[E[:, 0]]); np.add.at(c, E[:, 0], 1); np.add.at(c, E[:, 1], 1)
        X = 0.5 * X + 0.5 * acc / np.maximum(c, 1)[:, None]
    return X0, X
def centres(Pc, Nc):
    """each vertex's point on the rope's axis: in from it along its normal by the rope's radius (the radius: half the gap
    to the nearest vertex facing the other way)"""
    D = np.linalg.norm(Pc[:, None, :] - Pc[None, :, :], axis=2)
    opp = (Nc @ Nc.T) < -0.6
    D = np.where(opp, D, 1e9)
    near = D.min(1)
    ok = near < 1e8
    r = float(np.median(near[ok]) / 2.0) if ok.any() else 0.5
    r = float(np.clip(r, 0.2, 3.0))
    return Pc - Nc * r, r
def order_line(C):
    span = np.ptp(C, 0).max()
    g = max(0.3, span / 220.0)
    cells = {}
    for p in C:
        k = tuple(np.floor(p / g).astype(int)); cells.setdefault(k, []).append(p)
    Q = np.array([np.mean(v, 0) for v in cells.values()])
    if len(Q) < 2: return Q, False
    c = Q.mean(0); dist = np.linalg.norm(Q - c, axis=1)
    u, s, vt = np.linalg.svd(Q - c, full_matrices=False)
    proj = (Q - c) @ vt[0]; res = np.linalg.norm((Q - c) - np.outer(proj, vt[0]), axis=1)
    if res.max() < 0.08 * np.ptp(proj) + 0.6:  # straight: in order along it
        return Q[np.argsort(proj)], False
    loop = len(Q) >= 8 and dist.min() > 0.4 * dist.mean() and s[1] > 0.3 * s[0]
    if loop:
        a = np.arctan2((Q - c) @ vt[1], (Q - c) @ vt[0]); o = np.argsort(a); L = Q[o]
        return np.vstack([L, L[:1]]), True
    start = int(np.argmax(dist)); used = np.zeros(len(Q), bool); path = [start]; used[start] = True
    for _ in range(len(Q) - 1):
        dd = np.linalg.norm(Q - Q[path[-1]], axis=1); dd[used] = 1e9; j = int(np.argmin(dd))
        path.append(j); used[j] = True
    return Q[path], False
def smooth_resample(L, closed):
    if len(L) < 2: return L
    for _ in range(3):
        if closed:
            L2 = (np.roll(L[:-1], 1, 0) + 2 * L[:-1] + np.roll(L[:-1], -1, 0)) / 4; L = np.vstack([L2, L2[:1]])
        elif len(L) > 2:
            L = np.vstack([L[:1], (L[:-2] + 2 * L[1:-1] + L[2:]) / 4, L[-1:]])
    seg = np.linalg.norm(np.diff(L, axis=0), axis=1); s = np.r_[0, np.cumsum(seg)]
    if s[-1] < 1e-3: return L[:1]
    t = np.arange(0.0, s[-1] + 1e-6, STEP); t = np.append(t, s[-1]) if t[-1] < s[-1] - 1e-3 else t
    return np.stack([np.interp(t, s, L[:, i]) for i in range(3)], 1)
def rope(L, r, closed, out):
    P, N, T, U, F = out
    if len(L) < 2: return
    Tg = np.gradient(L, axis=0); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    ref = np.array([0, 0, 1.0]) if abs(Tg[0][2]) < 0.9 else np.array([1.0, 0, 0])
    nrm = np.cross(Tg[0], ref); nrm /= np.linalg.norm(nrm)
    s = np.r_[0, np.cumsum(np.linalg.norm(np.diff(L, axis=0), axis=1))]
    pitch = 3.0 * 2 * r
    base = len(P)
    for i, (p, t) in enumerate(zip(L, Tg)):
        nrm = nrm - t * nrm.dot(t); nrm /= np.linalg.norm(nrm) + 1e-9; bi = np.cross(t, nrm)
        for k in range(SEG + 1):
            a = 2 * np.pi * k / SEG
            rr = r * (1.0 + 0.13 * np.cos(3.0 * (a - s[i] * 2 * np.pi / pitch)))  # three strands, laid round
            dv = np.cos(a) * nrm + np.sin(a) * bi
            P.append(p + dv * rr); N.append(dv); T.append(t); U.append((k / SEG * 0.5, s[i] / 10.0))
    for i in range(len(L) - 1):
        for k in range(SEG):
            a = base + i * (SEG + 1) + k; b = a + SEG + 1
            F += [(a, b, a + 1), (a + 1, b, b + 1)]
    if not closed:  # ends capped
        for i0, sgn in ((0, -1), (len(L) - 1, 1)):
            ci = len(P); P.append(L[i0] + Tg[i0] * sgn * r * 0.3); N.append(Tg[i0] * sgn); T.append(np.cross(Tg[i0], [0.31, 0.47, 0.83])); U.append((0.25, s[i0] / 10.0))
            ring = base + i0 * (SEG + 1)
            for k in range(SEG):
                F.append((ci, ring + k + 1, ring + k) if sgn < 0 else (ci, ring + k, ring + k + 1))
def remesh(name):
    k = shapes[name]; st, vb, F = load(n.blocks[k]); P0 = pos(vb).astype(float)
    out = ([], [], [], [], [])
    pieces = 0
    for c in components(P0, F):
        tri = F[np.isin(F, c).all(1)]
        if len(tri) < 2: continue
        used = np.unique(tri); re = -np.ones(len(P0), int); re[used] = np.arange(len(used))
        Pc = P0[used]; Fc = re[tri]
        Nc = vb[used, 20:23].astype(float) / 127.5 - 1.0; Nc /= np.linalg.norm(Nc, axis=1, keepdims=True) + 1e-9
        Cc, r = centres(Pc, Nc)
        L, closed = order_line(Cc)
        if len(L) < 2: continue
        L = smooth_resample(L, closed)
        if not closed:  # the shrink pulled the ends in: back out to the rope's real ends
            for e, nb in ((0, 1), (-1, -2)):
                dirv = L[e] - L[nb]; dirv /= np.linalg.norm(dirv) + 1e-9
                tipd = np.max((Pc - L[e]) @ dirv)
                if tipd > 0: L[e] = L[e] + dirv * max(0.0, tipd - r * 0.3)
        rope(L, r, closed, out)
        pieces += 1
    P, N, T, U, Fn = [np.array(x, float) for x in out[:4]] + [np.array(out[4], int)]
    if len(P) > 65000: raise SystemExit(f'{name}: {len(P)} vertices, too many')
    T = T - N * (T * N).sum(1, keepdims=True); T /= np.linalg.norm(T, axis=1, keepdims=True) + 1e-9
    B = np.cross(N, T)
    V = np.zeros((len(P), 32), np.uint8)
    V[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    V[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    V[:, 16:20] = np.ascontiguousarray(U.astype('<f2')).view(np.uint8).reshape(-1, 4)
    V[:, 20:23] = e8(N); V[:, 23] = e8(B[:, 1]); V[:, 24:27] = e8(T); V[:, 27] = e8(B[:, 2]); V[:, 28:32] = 255
    # keep the shape's own vertex colour byte layout (32-byte vertices) and everything else of its block
    n.blocks[k] = store(st, V, Fn)
    print(f'{name}: {pieces} pieces, {len(P0)} -> {len(P)} vertices, {len(F)} -> {len(Fn)} triangles')
for nm in ('SB_Rigging', 'SB_BoomRope', 'SB_GaffRope', 'SB_Halyards', 'SB_MastLashings', 'SB_JawRopeS', 'SB_JawRopeP', 'SB_JibTack'):
    if nm in shapes: remesh(nm)
# the jib sheet (skinned to its two bones): a straight rope between them, each vertex on the two by where it lies
k = shapes['SB_JibSheet']; raw = n.blocks[k]; _, p0 = av_fix(raw)
inst, sh, al = struct.unpack_from('<3i', raw, p0 + 60 + 16)
ir = n.blocks[inst]; sd_id, sp_id, root_id, nb = struct.unpack_from('<iiiI', ir, 0); bones = list(struct.unpack_from('<%di' % nb, ir, 16))
sdr = n.blocks[sd_id]; off = 36 + 12 + 4 + 4 + 1
binds = []
for b in range(nb):
    tr = np.array(struct.unpack_from('<3f', sdr, off + 36)); binds.append(-tr)
    nv, = struct.unpack_from('<H', sdr, off + 36 + 12 + 4 + 16); off += 36 + 12 + 4 + 16 + 2 + nv * 6
binds = np.array(binds)
L = smooth_resample(np.array([binds[0], binds[1]]), False)
out = ([], [], [], [], []); rope(L, 0.55, False, out)
P, N, T, U = [np.array(x, float) for x in out[:4]]; Fn = np.array(out[4], int)
T = T - N * (T * N).sum(1, keepdims=True); T /= np.linalg.norm(T, axis=1, keepdims=True) + 1e-9
dv = binds[1] - binds[0]; w = np.clip(((P - binds[0]) @ dv) / (dv @ dv), 0, 1)
idx = np.stack([np.zeros(len(P), int), np.ones(len(P), int), np.zeros(len(P), int), np.zeros(len(P), int)], 1)
wts = np.stack([1 - w, w, np.zeros(len(P)), np.zeros(len(P))], 1)
ninst, desc = skin_blocks(n, e8, P, N, T, U, Fn, idx, wts, binds, bones, root_id)
raw = bytearray(raw); struct.pack_into('<i', raw, p0 + 60 + 16, ninst); n.blocks[k] = bytes(raw)
print(f'SB_JibSheet: skinned, {len(P)} vertices')
# no blanket
k = shapes.get('SB_Blanket')
if k is not None:
    r = bytearray(n.blocks[k]); fl, = struct.unpack_from('<I', r, 12); struct.pack_into('<I', r, 12, fl | 1); n.blocks[k] = bytes(r); print('SB_Blanket: hidden')
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(d):,} -> {len(out):,} bytes')
