# RowRim: a 3D admiralty anchor (iron): shank, ring, stock (a crossbar near the ring, at right angles to the arms),
# two curved arms ending in spade flukes and pointed bills. Units: game units, ~62 tall (about 0.9 m). Writes anchor.obj.
import numpy as np
V = []; F = []; UV = []
TILE = 28.0  # v1.8.29d: the rust texture repeats every 28 units
def add(verts, faces, uvs):
    b = len(V); V.extend(verts); UV.extend(uvs); F.extend([[b + i for i in f] for f in faces])
def tube(path, radius, seg=12, cap=True):
    """a round bar along a polyline (n,3), radius per point (n,)"""
    path = np.asarray(path, float); r = np.broadcast_to(np.asarray(radius, float), (len(path),))
    T = np.gradient(path, axis=0); T /= np.linalg.norm(T, axis=1, keepdims=True)
    up = np.array([0, 0, 1.0]) if abs(T[0][2]) < 0.9 else np.array([1.0, 0, 0])
    N = np.cross(T[0], up); N /= np.linalg.norm(N); Ns = []
    for t in T:  # parallel transport
        N = N - t * np.dot(N, t); N /= np.linalg.norm(N); Ns.append(N.copy())
    verts = []; uvs = []
    L = np.r_[0.0, np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))]
    for p, t, n, rr, l in zip(path, T, Ns, r, L):
        b = np.cross(t, n)
        for k in range(seg):
            a = 2 * np.pi * k / seg
            verts.append(p + rr * (np.cos(a) * n + np.sin(a) * b)); uvs.append((k / seg * 0.35, l / TILE))
    faces = []
    for i in range(len(path) - 1):
        for k in range(seg):
            a = i * seg + k; b2 = i * seg + (k + 1) % seg
            faces += [[a, b2, b2 + seg], [a, b2 + seg, a + seg]]
    if cap:
        c0 = len(verts); verts.append(path[0]); c1 = len(verts); verts.append(path[-1]); uvs += [(0.5, 0.0), (0.5, L[-1] / TILE)]
        last = (len(path) - 1) * seg
        for k in range(seg):
            faces.append([c0, (k + 1) % seg, k]); faces.append([c1, last + k, last + (k + 1) % seg])
    add(verts, faces, uvs)
def torus(c, R, r, axis='y', nu=24, nv=8):
    verts = []; uvs = []
    for i in range(nu):
        a = 2 * np.pi * i / nu
        for j in range(nv):
            uvs.append((j / nv * 0.3, i / nu * 2 * np.pi * R / TILE))
            b = 2 * np.pi * j / nv
            q = np.array([(R + r * np.cos(b)) * np.cos(a), r * np.sin(b), (R + r * np.cos(b)) * np.sin(a)])  # ring in x z
            if axis == 'x': q = q[[1, 0, 2]]
            verts.append(np.asarray(c) + q)
    faces = []
    for i in range(nu):
        for j in range(nv):
            a = i * nv + j; b = ((i + 1) % nu) * nv + j; c2 = ((i + 1) % nu) * nv + (j + 1) % nv; d = i * nv + (j + 1) % nv
            faces += [[a, b, c2], [a, c2, d]]
    add(verts, faces, uvs)
def plate(outline, thick, normal):
    """a flat plate (fluke) from a convex-ish outline (n,3) and its normal, thickness thick"""
    o = np.asarray(outline, float); n = np.asarray(normal, float); n /= np.linalg.norm(n)
    top = o + n * thick / 2; bot = o - n * thick / 2; m = len(o)
    c_t = top.mean(0); c_b = bot.mean(0)
    verts = list(top) + list(bot) + [c_t, c_b]
    e1 = o[1] - o[0]; e1 /= np.linalg.norm(e1); e2 = np.cross(n, e1)
    uvs = [((q - c_t) @ e1 / TILE + 0.5, (q - c_t) @ e2 / TILE + 0.5) for q in verts]
    faces = []
    for i in range(m):
        j = (i + 1) % m
        faces.append([2 * m, i, j]); faces.append([2 * m + 1, m + j, m + i])
        faces += [[i, m + i, m + j], [i, m + j, j]]
    add(verts, faces, uvs)
# --- shank: tapering iron bar from the crown (z 0) to the ring eye (z 56)
z = np.linspace(2, 56, 14)
tube(np.c_[np.zeros_like(z), np.zeros_like(z), z], np.interp(z, [2, 56], [2.3, 1.6]))
# eye boss and ring (the ring through the eye, in the x z plane... turned so it hangs across the stock)
tube([[0, -1.9, 57.5], [0, 1.9, 57.5]], 2.4, seg=12)
torus([0, 0, 62.0], 4.2, 0.75, axis='x')
# --- stock: a straight iron bar through the shank below the eye, across y (at right angles to the arms, which lie in x)
y = np.linspace(-24, 24, 13)
tube(np.c_[np.zeros_like(y), y, np.full_like(y, 50.0)], np.interp(np.abs(y), [0, 24], [1.5, 1.1]))
# its ball ends
for s in (-1, 1): tube([[0, s * 24.0, 50.0], [0, s * 25.6, 50.0]], 1.6, seg=10)
# --- the arms: a curve from the crown up and out each side (x), round bar thinning to the bill
for s in (-1, 1):
    t = np.linspace(0, 1, 16)
    ang = t * 1.15  # sweep up from the crown
    R = 20.0
    x = s * R * np.sin(ang); zz = R * (1 - np.cos(ang)) + 0.0 - 1.0
    arm = np.c_[x, np.zeros_like(x), zz]
    tube(arm, np.interp(t, [0, 1], [2.4, 1.2]))
    # the fluke: a spade on the arm's last part, facing up-and-in; the bill a point past it
    tip = arm[-1]; d = arm[-1] - arm[-4]; d /= np.linalg.norm(d)
    side = np.array([0, 1.0, 0]); nrm = np.cross(d, side); nrm /= np.linalg.norm(nrm)
    base = tip - d * 9.0
    outline = [base - side * 1.2, base - side * 5.2 + d * 3.0, tip - side * 4.0 + d * 1.0, tip + d * 4.0, tip + side * 4.0 + d * 1.0, base + side * 5.2 + d * 3.0, base + side * 1.2]
    plate(outline, 0.9, nrm)
# the crown: a ball where the arms meet the shank
torus([0, 0, 0.6], 1.0, 2.0, axis='x', nu=12, nv=8)
V = np.array(V); F = np.array(F); UV = np.array(UV, float)
with open('anchor.obj', 'w') as f:
    f.write('# RowRim anchor (iron), game units\no RowRim_Anchor\n')
    for v in V: f.write('v %.4f %.4f %.4f\n' % tuple(v))
    for u in UV: f.write('vt %.4f %.4f\n' % tuple(u))
    for t in F: f.write('f %d/%d %d/%d %d/%d\n' % (t[0] + 1, t[0] + 1, t[1] + 1, t[1] + 1, t[2] + 1, t[2] + 1))
np.save('anchor_v.npy', V); np.save('anchor_f.npy', F); np.save('anchor_uv.npy', UV)
print(f'anchor.obj: {len(V)} vertices, {len(F)} triangles, x {V[:,0].min():.0f}..{V[:,0].max():.0f} y {V[:,1].min():.0f}..{V[:,1].max():.0f} z {V[:,2].min():.0f}..{V[:,2].max():.0f}')
