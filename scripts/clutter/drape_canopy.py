# RowRim: the canopy's side walls draped smooth over what they lie on (both sides), as one sheet for Blender.
# The outer skin grid (110 x 30 a side, tent2.py) is smoothed in both directions from the eave down (the roof kept),
# and every few steps pushed back out wherever it comes closer than CLEAR_HULL to the boat or CLEAR_POLE to a pole's
# axis (tent2.py had lifted whole squares over them: the steps). The halves join only along the ridge.
import sys, struct, numpy as np
from scipy.spatial import cKDTree
from nifp import parse, tri_shape
SRC, OUT, PREV = sys.argv[1], sys.argv[2], sys.argv[3]
NY, NC, NR = 110, 30, 8
CLEAR_HULL, CLEAR_POLE = 3.5, 1.6   # the pole now as its surface, not its axis
ITER, LAM = 360, 0.22   # small steps, pushed out after every one (bigger ones slipped past the poles)
d = open(SRC, 'rb').read(); h = parse(d)
def xf(k):
    b = h['blocks'][k]; raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4
    return np.array(struct.unpack_from('<9f', raw, p + 12)).reshape(3, 3), np.array(struct.unpack_from('<3f', raw, p)), struct.unpack_from('<f', raw, p + 48)[0]
def nm(k): i = struct.unpack_from('<i', d, h['blocks'][k]['start'])[0]; return h['names'][i] if i >= 0 else ''
parent = {}
for b in h['blocks']:
    if b['type'] in ('NiNode', 'BSFadeNode'):
        raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
        n, = struct.unpack_from('<I', raw, p)
        for c in struct.unpack_from('<%di' % n, raw, p + 4):
            if c >= 0: parent[c] = b['k']
def world(k, P):
    while k in parent and parent[k] != 0:
        k2 = k; R, T, Sc = xf(k2); P = P @ R.T * Sc + T; k = parent[k2]
    R, T, Sc = xf(k); return P @ R.T * Sc + T  # last: the shape itself when it hangs straight off the root
def shape(k):
    t = tri_shape(d, h, h['blocks'][k])
    V = np.frombuffer(d, np.uint8, t['nv'] * t['vsize'], t['data_off']).reshape(-1, t['vsize'])
    P = V[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)  # SSE: positions always full floats
    I = np.frombuffer(d, np.uint16, t['ntri'] * 3, t['data_off'] + t['nv'] * t['vsize']).reshape(-1, 3)
    # the shape's own transform, then its parents
    R, T, Sc = xf(k); P = P @ R.T * Sc + T; q = parent.get(k)
    while q is not None and q != 0:
        R, T, Sc = xf(q); P = P @ R.T * Sc + T; q = parent.get(q)
    return t, V, P, I
shapes = {b['k']: nm(b['k']) for b in h['blocks'] if b['type'] == 'BSTriShape'}
kt = [k for k, n in shapes.items() if n == 'RowRim_TentTarp'][0]; kp = [k for k, n in shapes.items() if n == 'RowRim_TentPoles'][0]
t, V, P, I = shape(kt)
UV = V[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float); col = V[:, 28]
# obstacles: the boat in the tent's reach (shown parts only: hidden nodes are the sail, tent, bed etc.)
hidden = set()
for b in h['blocks']:
    if b['type'] in ('NiNode', 'BSTriShape'):
        raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); fl, = struct.unpack_from('<I', raw, 4 + 4 + 4 * ne + 4)
        if fl & 1: hidden.add(b['k'])
def is_hidden(k):
    while k is not None:
        if k in hidden: return True
        k = parent.get(k)
    return False
pts = []
for k, n in shapes.items():
    if k in (kt, kp) or is_hidden(k): continue
    _, _, Pk, Ik = shape(k)
    T3 = Pk[Ik]
    keep = ((T3[:, :, 1] > 25) & (T3[:, :, 1] < 335) & (T3[:, :, 2] > 40) & (T3[:, :, 2] < 130)).any(1)
    for tri in T3[keep]:
        L = max(np.linalg.norm(tri[1] - tri[0]), np.linalg.norm(tri[2] - tri[0]), np.linalg.norm(tri[2] - tri[1]))
        m = int(min(30, max(1, L / 1.5)))
        a, b2 = np.meshgrid(np.linspace(0, 1, m + 1), np.linspace(0, 1, m + 1)); s = a + b2 <= 1
        pts.append(tri[0] + (tri[1] - tri[0]) * a[s][:, None] + (tri[2] - tri[0]) * b2[s][:, None])
HULL = np.vstack(pts); HULL = HULL[np.isfinite(HULL).all(1) & (np.abs(HULL[:, 0]) > 6)]  # the keel line far below is no matter
_, Vp, Pp, Ip = shape(kp)
# the poles' surface, sampled every ~0.6
pp = []
for tri in Pp[Ip]:
    Lp = max(np.linalg.norm(tri[1] - tri[0]), np.linalg.norm(tri[2] - tri[0]), np.linalg.norm(tri[2] - tri[1]))
    m = int(min(80, max(1, Lp / 0.6)))
    a, b2 = np.meshgrid(np.linspace(0, 1, m + 1), np.linspace(0, 1, m + 1)); s_ = a + b2 <= 1
    pp.append(tri[0] + (tri[1] - tri[0]) * a[s_][:, None] + (tri[2] - tri[0]) * b2[s_][:, None])
POLE = np.vstack(pp)
hullT, poleT = cKDTree(HULL), cKDTree(POLE)
# v2: over a pole the cloth stays on top of it: a soft tent of allowed heights over each pole (1.2 over its top, falling
# 0.35 a unit to the side), as tent2.py's cone but gentle; the roof and the eave band only (the walls hang beside posts)
POLE2 = cKDTree(POLE[:, :2])
def pole_floor(flat):
    zmin = np.full(len(flat), -1e9)
    for n_, idx in enumerate(POLE2.query_ball_point(flat[:, :2], 9.0)):
        if not idx: continue
        q = POLE[idx]; dxy = np.linalg.norm(q[:, :2] - flat[n_, :2], axis=1)
        zmin[n_] = (q[:, 2] + 1.2 - 0.35 * np.maximum(dxy - 1.0, 0)).max()
    return zmin
print(f'obstacles: {len(HULL)} boat points, {len(POLE)} pole points')
pieces = [np.arange(q * NY * NC, (q + 1) * NY * NC).reshape(NY, NC) for q in range(4)]
outer = [p for p in pieces if col[p[0, 0]] == 255]
w = np.clip((np.arange(NC) - (NR - 4)) / 3.0, 0, 1); w = w * w * (3 - 2 * w)   # the roof's top half kept; from there over the eave pole down, smoothed
grids = []
for ids in outer:
    G0 = P[ids].copy(); G = G0.copy()
    side = np.sign(G0[:, NR, 0].mean())
    def normals(G):
        dr = np.gradient(G, axis=0); dc = np.gradient(G, axis=1); n = np.cross(dr, dc)
        n /= np.linalg.norm(n, axis=2, keepdims=True) + 1e-12
        # outward: up on the roof, out to the side on the walls
        out = np.where(np.arange(NC)[None, :, None] <= NR, np.array([0, 0, 1.0]), np.array([side, 0, 0.3]))
        n[(n * out).sum(2) < 0] *= -1
        return n
    def push(G):
        n = normals(G); moved = 0.0
        flat = G.reshape(-1, 3); nf = n.reshape(-1, 3); wf = np.repeat(w[None, :], NY, 0).ravel()
        for tree, src, c in ((hullT, HULL, CLEAR_HULL), (poleT, POLE, CLEAR_POLE)):
            dd, ii = tree.query(flat, k=8, distance_upper_bound=c)
            for j in range(8):
                ok = (dd[:, j] < c) & (wf > 0)
                if not ok.any(): continue
                q = src[ii[ok, j]]; p = flat[ok]
                # move out along the normal until that point is c away
                v = p - q; along = (v * nf[ok]).sum(1); perp2 = np.maximum((v * v).sum(1) - along ** 2, 0)
                need = np.sqrt(np.maximum(c * c - perp2, 0)) - along
                step = np.clip(need, 0, None)
                flat[ok] += nf[ok] * step[:, None]; moved = max(moved, float(step.max()) if len(step) else 0)
        band = np.repeat((np.arange(NC) <= NR + 3)[None, :], NY, 0).ravel() & (wf > 0)
        if band.any():
            zf = pole_floor(flat[band]); sub = flat[band]
            lift = np.maximum(zf - sub[:, 2], 0); sub[:, 2] += lift; flat[band] = sub
            moved = max(moved, float(lift.max()))
        return flat.reshape(G.shape), moved
    G, _ = push(G)
    for it in range(ITER):
        # inside: the four neighbours; on an edge only the two along it (the hem keeps its line, no shrinking in)
        lap = np.zeros_like(G); cnt = np.zeros(G.shape[:2])
        edge_r = np.zeros((NY, NC), bool); edge_r[[0, -1], :] = True
        edge_c = np.zeros((NY, NC), bool); edge_c[:, -1] = True
        for dj, di in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sl_t = (slice(max(dj, 0), NY + min(dj, 0)), slice(max(di, 0), NC + min(di, 0)))
            sl_s = (slice(max(-dj, 0), NY + min(-dj, 0)), slice(max(-di, 0), NC + min(-di, 0)))
            m_ = np.ones((NY, NC), bool)
            if dj != 0: m_ &= ~edge_r   # an edge row takes no neighbour across
            if di != 0: m_ &= ~edge_c   # the hang's bottom edge takes none up the wall
            m_ = m_[sl_s]
            tmp = np.zeros_like(G); tmp[sl_s] = G[sl_t] * m_[..., None]; lap += tmp
            c_ = np.zeros((NY, NC)); c_[sl_s] = m_; cnt += c_
        lap = np.where(cnt[..., None] > 0, lap / np.maximum(cnt, 1)[..., None] - G, 0.0)
        lap[(edge_r & edge_c)] = 0.0  # the corners stay
        G = G + LAM * w[None, :, None] * lap
        G, mv = push(G)
    for _ in range(6): G, mv = push(G)
    G[:, 0] = G0[:, 0]  # the ridge row stays on the ridge
    moved = np.linalg.norm(G - G0, axis=2)
    print(f'side {int(side):+d}: moved up to {moved.max():.1f} (median on the walls {np.median(moved[:, NR:]):.1f}), last push {mv:.2f}')
    grids.append((ids, G, side))
# one sheet: both halves, joined along the ridge only (the ridge row of one side replaces the other's)
out_P, out_UV, faces = [], [], []
ridge = None
for n_, (ids, G, side) in enumerate(grids):
    idx = np.zeros((NY, NC), int)
    for j in range(NY):
        for i in range(NC):
            if n_ == 1 and i == 0: idx[j, i] = ridge[j]; continue
            out_P.append(G[j, i]); out_UV.append(UV[ids[j, i]]); idx[j, i] = len(out_P) - 1
    if n_ == 0: ridge = idx[:, 0].copy()
    # winding: facing outward (normal of the old outer skin)
    for j in range(NY - 1):
        for i in range(NC - 1):
            a, b2, c2, e = idx[j, i], idx[j + 1, i], idx[j + 1, i + 1], idx[j, i + 1]
            pa, pb, pc = G[j, i], G[j + 1, i], G[j + 1, i + 1]
            nn = np.cross(pb - pa, pc - pa); out = np.array([0, 0, 1.0]) if i < NR else np.array([side, 0, 0.3])
            faces += [(a, b2, c2), (a, c2, e)] if np.dot(nn, out) > 0 else [(a, c2, b2), (a, e, c2)]
out_P = np.array(out_P); out_UV = np.array(out_UV)
L = ['# RowRim canopy, one sheet, side walls draped smooth (drape.py): hull model space, Z up, game units', 'mtllib RowRim_Canopy.mtl',
     'o RowRim_TentTarp', 'usemtl canvas', 's 1']
L += [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in out_P] + [f'vt {u:.5f} {1 - v:.5f}' for u, v in out_UV]
L += [f'f {a+1}/{a+1} {b+1}/{b+1} {c+1}/{c+1}' for a, b, c in faces]
base = len(out_P) + 1
L += ['o RowRim_TentPoles', 'usemtl poles', 's 1'] + [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in Pp]
L += [f'f {a+base} {b+base} {c+base}' for a, b, c in Ip]
open(OUT, 'w', newline='').write('\r\n'.join(L) + '\r\n')
np.savez(PREV, P=out_P, F=np.array(faces), PP=Pp, PI=Ip, OLD=np.vstack([P[ids] .reshape(-1, 3) for ids, _, _ in grids]))
print(f'{OUT.split("/")[-1]}: {len(out_P)} points, {len(faces)} triangles')
