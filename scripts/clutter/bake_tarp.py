# RowRim v1.8.25: Miguel's Blender canopy (one sheet, OBJ) baked into the hull as cloth: the sheet is the inner skin,
# an outer skin THICK outside it, a hem along every open edge; kept clear of the poles and the boat (lifted out along
# its normal where it comes too close); the poles kept as they are in the hull unless the OBJ moved them.
import sys, struct, numpy as np
from collections import defaultdict
from scipy.spatial import cKDTree
from nifp import parse, tri_shape
from nifmerge import Nif
SRC, OBJ, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
THICK = 1.6; CLEAR_POLE = 1.2; CLEAR_HULL = 2.5
# ---- the OBJ
objs = {}; cur = None; Vs = []; VTs = []; VNs = []
for l in open(OBJ, encoding='utf-8', errors='replace'):
    if l.startswith('o '): cur = l.split(None, 1)[1].strip(); objs[cur] = []
    elif l.startswith('v '): Vs.append([float(x) for x in l.split()[1:4]])
    elif l.startswith('vt '): VTs.append([float(x) for x in l.split()[1:3]])
    elif l.startswith('vn '): VNs.append([float(x) for x in l.split()[1:4]])
    elif l.startswith('f '):
        c = [tuple(int(y) - 1 if y else -1 for y in (x.split('/') + ['', ''])[:3]) for x in l.split()[1:]]
        for k in range(1, len(c) - 1): objs[cur].append((c[0], c[k], c[k + 1]))  # fan any n-gon
Vs, VTs, VNs = np.array(Vs), np.array(VTs) if VTs else np.zeros((1, 2)), np.array(VNs) if VNs else np.zeros((1, 3))
tarpF = objs['RowRim_TentTarp']
# corners -> vertices (one per position + uv + normal)
key = {}; Pv = []; UVv = []; Nv = []; pos_of = []
F = []
for tri in tarpF:
    ids = []
    for (vi, ti, ni) in tri:
        k = (vi, ti, ni)
        if k not in key:
            key[k] = len(Pv); Pv.append(Vs[vi]); UVv.append(VTs[ti] if ti >= 0 else (0, 0)); Nv.append(VNs[ni] if ni >= 0 else (0, 0, 0)); pos_of.append(vi)
        ids.append(key[k])
    F.append(ids)
Pv = np.array(Pv, float); UVv = np.array(UVv, float); UVv[:, 1] = 1 - UVv[:, 1]; Nv = np.array(Nv, float); F = np.array(F); pos_of = np.array(pos_of)
# area-weighted normals per position (smooth across uv seams), oriented like the OBJ's own (Blender's) normals
fn = np.cross(Pv[F[:, 1]] - Pv[F[:, 0]], Pv[F[:, 2]] - Pv[F[:, 0]])
npos = np.zeros((len(Vs), 3))
for c in range(3): np.add.at(npos, pos_of[F[:, c]], fn)
Ns = npos[pos_of]; Ns /= np.linalg.norm(Ns, axis=1, keepdims=True) + 1e-12
if (Nv ** 2).sum() > 0:
    flip = (Ns * Nv).sum(1) < 0
    if flip.mean() > 0.5: Ns = -Ns; F = F[:, [0, 2, 1]]
# outward = away from the boat's centre line and up: check the majority
cen = np.array([0.0, Pv[:, 1].mean(), 60.0])
outw = ((Ns * (Pv - cen)).sum(1) > 0).mean()
if outw < 0.5: Ns = -Ns; F = F[:, [0, 2, 1]]
print(f'canvas: {len(Vs)} points, {len(F)} triangles, {len(Pv)} vertices with their uv; normals outward {max(outw, 1 - outw):.0%}')
# ---- the hull, its poles and its shapes (obstacles)
d = open(SRC, 'rb').read(); h = parse(d)
def nm(k): i = struct.unpack_from('<i', d, h['blocks'][k]['start'])[0]; return h['names'][i] if i >= 0 else ''
def xf(k):
    b = h['blocks'][k]; raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4
    return np.array(struct.unpack_from('<9f', raw, p + 12)).reshape(3, 3), np.array(struct.unpack_from('<3f', raw, p)), struct.unpack_from('<f', raw, p + 48)[0]
parent = {}; hidden = set()
for b in h['blocks']:
    if b['type'] in ('NiNode', 'BSFadeNode', 'BSTriShape'):
        raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4)
        if struct.unpack_from('<I', raw, 4 + 4 + 4 * ne + 4)[0] & 1: hidden.add(b['k'])
    if b['type'] in ('NiNode', 'BSFadeNode'):
        raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
        n, = struct.unpack_from('<I', raw, p)
        for c in struct.unpack_from('<%di' % n, raw, p + 4):
            if c >= 0: parent[c] = b['k']
def chain(k):
    out = []
    while k is not None and k != 0: out.append(k); k = parent.get(k)
    return out
def shape(k):
    t = tri_shape(d, h, h['blocks'][k])
    V = np.frombuffer(d, np.uint8, t['nv'] * t['vsize'], t['data_off']).reshape(-1, t['vsize'])
    P = V[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    I = np.frombuffer(d, np.uint16, t['ntri'] * 3, t['data_off'] + t['nv'] * t['vsize']).reshape(-1, 3)
    for q in chain(k): R, T, Sc = xf(q); P = P @ R.T * Sc + T
    return t, V, P, I
shapes = {b['k']: nm(b['k']) for b in h['blocks'] if b['type'] == 'BSTriShape'}
kt = [k for k, n in shapes.items() if n == 'RowRim_TentTarp'][0]; kp = [k for k, n in shapes.items() if n == 'RowRim_TentPoles'][0]
# the tarp's own transform chain (its points are stored in the shape's space)
Rc = np.eye(3); Tc = np.zeros(3); Sc_ = 1.0
for q in chain(kt):
    R, T, S = xf(q); Rc, Tc, Sc_ = R @ Rc, R @ Tc * S + T, S * Sc_
_, _, Pp, Ip = shape(kp)
objP = Vs[sorted({c[0] for tri in objs.get('RowRim_TentPoles', []) for c in tri})] if 'RowRim_TentPoles' in objs else None
if objP is not None:
    dp = cKDTree(Pp).query(objP)[0].max()
    print(f'poles in the OBJ: furthest from the hull ones {dp:.3f} ({"unchanged, kept" if dp < 0.05 else "MOVED"})')
def sample(T3, step):
    out = []
    for tri in T3:
        L = max(np.linalg.norm(tri[1] - tri[0]), np.linalg.norm(tri[2] - tri[0]), np.linalg.norm(tri[2] - tri[1]))
        m = int(min(80, max(1, L / step)))
        a, b2 = np.meshgrid(np.linspace(0, 1, m + 1), np.linspace(0, 1, m + 1)); s = a + b2 <= 1
        out.append(tri[0] + (tri[1] - tri[0]) * a[s][:, None] + (tri[2] - tri[0]) * b2[s][:, None])
    return np.vstack(out)
POLE = sample(Pp[Ip], 0.6)
hp = []
for k, n in shapes.items():
    if k in (kt, kp) or any(q in hidden for q in chain(k)): continue
    _, _, Pk, Ik = shape(k); T3 = Pk[Ik]
    keep = ((T3[:, :, 1] > 25) & (T3[:, :, 1] < 335) & (T3[:, :, 2] > 40) & (T3[:, :, 2] < 130)).any(1)
    if keep.any(): hp.append(sample(T3[keep], 1.5))
HULL = np.vstack(hp); HULL = HULL[np.isfinite(HULL).all(1) & (np.abs(HULL[:, 0]) > 6)]
# ---- clearance: the inner skin (the sheet) off the poles and the boat; the poles must stay under the cloth
def clear(P, N):
    lifted = np.zeros(len(P))
    for _ in range(10):
        mv = 0
        for tree, src, c in ((cKDTree(HULL), HULL, CLEAR_HULL), (cKDTree(POLE), POLE, CLEAR_POLE)):
            dd, ii = tree.query(P, k=8, distance_upper_bound=c)
            for j in range(8):
                ok = dd[:, j] < c
                if not ok.any(): continue
                v = P[ok] - src[ii[ok, j]]; along = (v * N[ok]).sum(1); perp2 = np.maximum((v * v).sum(1) - along ** 2, 0)
                need = np.clip(np.sqrt(np.maximum(c * c - perp2, 0)) - along, 0, None)
                P[ok] += N[ok] * need[:, None]; lifted[ok] += need; mv = max(mv, need.max())
        if mv < 0.01: break
    return P, lifted
# by position (all corners of a point move together)
uP = np.zeros((len(Vs), 3)); uN = np.zeros((len(Vs), 3)); used = np.unique(pos_of)
uP[pos_of] = Pv; uN[pos_of] = Ns
# ---- v1.8.25b: unfold. Where the sheet folds back on itself (the boxy steps along the eave poles) its triangles face the
# wrong way: the game does not draw a triangle's back, so those spots showed as see-through holes. There and a few rings
# round it the sheet is smoothed, held only by "never below a pole" (a height rule: it cannot make new folds), until
# every triangle faces the way the sheet does.
FP = pos_of[F]
nbp = defaultdict(set)
for a_, b_, c_ in FP: nbp[a_] |= {b_, c_}; nbp[b_] |= {a_, c_}; nbp[c_] |= {a_, b_}
nbp = {k: np.array(sorted(v)) for k, v in nbp.items()}
POLE2 = cKDTree(POLE[:, :2])
HIGH = POLE[:, 2] > 100   # the eave and ridge poles (the posts and the crossbar's ends are lower)
POLEH = POLE[HIGH]; POLEH2 = cKDTree(POLEH[:, :2])
def facing(P):
    fn_ = np.cross(P[FP[:, 1]] - P[FP[:, 0]], P[FP[:, 2]] - P[FP[:, 0]]); fn_ /= np.linalg.norm(fn_, axis=1, keepdims=True) + 1e-12
    vn_ = np.zeros_like(P)
    for c in range(3): np.add.at(vn_, FP[:, c], fn_)
    for _ in range(8):
        v2 = vn_.copy()
        for k, v in nbp.items(): v2[k] = vn_[v].sum(0) + vn_[k]
        vn_ = v2 / (np.linalg.norm(v2, axis=1, keepdims=True) + 1e-12)
    sh = vn_[FP].sum(1); sh /= np.linalg.norm(sh, axis=1, keepdims=True) + 1e-12
    tip = P[FP].mean(1)[:, 1] > 305   # the bow tip closes to a point
    return np.flatnonzero(((fn_ * sh).sum(1) < 0.05) & ~tip)
def floor_z(q):
    z = np.full(len(q), -1e9)
    for n_, near in enumerate(POLEH2.query_ball_point(q[:, :2], 9.0)):
        if not near: continue
        pq = POLEH[near]; dxy = np.linalg.norm(pq[:, :2] - q[n_, :2], axis=1)
        z[n_] = (pq[:, 2] + 1.2 - 0.35 * np.maximum(dxy - 1.0, 0)).max()
    return z
b0 = facing(uP); print(f'triangles folded the wrong way in your sheet: {len(b0)}')
UNFOLD = False  # v1.8.25b: off: it ribbed the front walls; the two-sided canvas covers any fold instead
for rnd in range(12 if UNFOLD else 0):
    badf = facing(uP)
    if not len(badf): break
    reg = set(FP[badf].ravel())
    for _ in range(2 + rnd // 3): reg |= {x for k in list(reg) for x in nbp.get(k, ())}
    reg = np.array(sorted(reg))
    for _ in range(30):
        avg = np.array([uP[nbp[k]].mean(0) for k in reg])
        uP[reg] += 0.5 * (avg - uP[reg])
        q = uP[reg]; fz = floor_z(q); up_ = (fz > q[:, 2]) & (q[:, 2] > 90); q[up_, 2] = fz[up_]; uP[reg] = q
    print(f'  unfold round {rnd + 1}: {len(badf)} folded, smoothed {len(reg)} points round them')
left = facing(uP); print(f'folded the wrong way now: {len(left)}')
if len(left): print('   still at (y, z, |x|):', np.round(uP[FP[left]].mean(1)[:, [1, 2, 0]][:8] * [1, 1, 1]).tolist())
uN[used] = 0
fn2 = np.cross(uP[FP[:, 1]] - uP[FP[:, 0]], uP[FP[:, 2]] - uP[FP[:, 0]])
for c in range(3): np.add.at(uN, FP[:, c], fn2)
uN /= np.linalg.norm(uN, axis=1, keepdims=True) + 1e-12
Ns = uN[pos_of]
d0h = cKDTree(HULL).query(uP[used])[0]; d0p = cKDTree(POLE).query(uP[used])[0]
print(f'your sheet: closest to the boat {d0h.min():.2f}, to a pole {d0p.min():.2f}; points nearer than {CLEAR_HULL} / {CLEAR_POLE}: {(d0h < CLEAR_HULL).sum()} / {(d0p < CLEAR_POLE).sum()}')
cp, lifted = clear(uP[used].copy(), uN[used])
# the lift spread softly over the cloth round it (no spikes), at most MAX_LIFT (an edge point's normal can point anywhere)
MAX_LIFT = 4.0
lift = np.zeros(len(Vs)); lift[used] = np.minimum(lifted, MAX_LIFT)
nb = defaultdict(set)
for f in F:
    a_, b_, c_ = pos_of[f]
    nb[a_] |= {b_, c_}; nb[b_] |= {a_, c_}; nb[c_] |= {a_, b_}
nbl = {k: np.array(sorted(v)) for k, v in nb.items()}
soft = lift.copy()
for _ in range(12):
    avg = soft.copy()
    for k, v in nbl.items(): avg[k] = soft[v].mean()
    soft = np.maximum(lift, 0.5 * (soft + avg))
uP[used] = uP[used] + uN[used] * soft[used][:, None]
lifted = soft[used]
print(f'lifted out: {(lifted > 0.05).sum()} points, most {lifted.max():.2f}')
Pv = uP[pos_of]
# ---- skins and hems
newV = []; newT = []
e8 = lambda x: np.clip(np.round((np.asarray(x, float) + 1) * 127.5), 0, 255).astype(np.uint8)
# tangents from the uv (u along the boat)
tan = np.zeros_like(Pv)
for a, b, c in F:
    e1 = Pv[b] - Pv[a]; e2 = Pv[c] - Pv[a]; du1, dv1 = UVv[b] - UVv[a]; du2, dv2 = UVv[c] - UVv[a]; r = du1 * dv2 - du2 * dv1
    if abs(r) < 1e-12: continue
    tt = (e1 * dv2 - e2 * dv1) / r; tan[a] += tt; tan[b] += tt; tan[c] += tt
def packv(p, uv, n, t_, c):
    # into the tarp shape's own space
    pl = ((p - Tc) / Sc_) @ Rc; nl = n @ Rc; tl = t_ @ Rc
    tl = tl - nl * np.dot(nl, tl)
    if np.linalg.norm(tl) < 1e-9: tl = np.cross(nl, [0, 0, 1]) if abs(nl[2]) < 0.9 else np.cross(nl, [1, 0, 0])
    tl /= np.linalg.norm(tl); b = np.cross(nl, tl)
    r = np.zeros(32, np.uint8)
    r[0:12] = np.frombuffer(np.asarray(pl, '<f4').tobytes(), np.uint8); r[12:16] = np.frombuffer(np.float32(b[0]).tobytes(), np.uint8)
    r[16:20] = np.frombuffer(np.asarray(uv, '<f2').tobytes(), np.uint8)
    r[20:23] = e8(nl); r[23] = e8(b[1]); r[24:27] = e8(tl); r[27] = e8(b[2]); r[28:31] = c; r[31] = 255
    newV.append(r); return len(newV) - 1
outer = np.array([packv(Pv[i] + Ns[i] * THICK, UVv[i], Ns[i], tan[i], (255, 255, 255)) for i in range(len(Pv))])
inner = np.array([packv(Pv[i], UVv[i], -Ns[i], tan[i], (205, 205, 205)) for i in range(len(Pv))])
newT += [tuple(outer[f]) for f in F] + [tuple(inner[f[[0, 2, 1]]]) for f in F]
# open edges (by position): a hem quad each, facing away from the cloth
ecount = defaultdict(list)
for fi, f in enumerate(F):
    for a, b in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
        ecount[tuple(sorted((pos_of[a], pos_of[b])))].append((fi, a, b))
hems = 0
for e, lst in ecount.items():
    if len(lst) != 1: continue
    fi, a, b = lst[0]; f = F[fi]; c = [x for x in f if x not in (a, b)][0]
    pa, pb, pc = Pv[a], Pv[b], Pv[c]
    edge = pb - pa; mid = (pa + pb) / 2
    nrm = (Ns[a] + Ns[b]); nrm /= np.linalg.norm(nrm) + 1e-12
    away = np.cross(edge, nrm); away /= np.linalg.norm(away) + 1e-12
    if np.dot(away, mid - pc) < 0: away = -away
    t_ = edge / (np.linalg.norm(edge) + 1e-12)
    oa = packv(pa + Ns[a] * THICK, UVv[a], away, t_, (225, 225, 225)); ob = packv(pb + Ns[b] * THICK, UVv[b], away, t_, (225, 225, 225))
    ia = packv(pa, UVv[a], away, t_, (200, 200, 200)); ib = packv(pb, UVv[b], away, t_, (200, 200, 200))
    q = [np.frombuffer(newV[x][0:12].tobytes(), '<f4') for x in (oa, ob, ib)]
    if np.dot(np.cross(q[1] - q[0], q[2] - q[0]), away @ Rc) > 0: newT += [(oa, ob, ib), (oa, ib, ia)]
    else: newT += [(oa, ib, ob), (oa, ia, ib)]
    hems += 1
VB = np.array(newV, np.uint8); TB = np.array(newT, np.uint16)
assert len(VB) < 65536 and len(newT) < 65536 and TB.max() < len(VB), (len(VB), len(newT))
print(f'built: {len(VB)} vertices, {len(TB)} triangles ({hems} hem edges)')
# ---- into the hull
blk = h['blocks'][kt]; t = tri_shape(d, h, blk); o = t['data_off']
tail = d[o + t['nv'] * 32 + t['ntri'] * 6: blk['end']]
raw = bytearray(d[blk['start']:o]); hp_ = t['raw_data_off']
Pl = VB[:, 0:12].copy().view('<f4').reshape(-1, 3)
ctr = (Pl.min(0) + Pl.max(0)) / 2; rad = float(np.linalg.norm(Pl - ctr, axis=1).max())
ne, = struct.unpack_from('<I', raw, 4); bs_off = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
struct.pack_into('<4f', raw, bs_off, *ctr, rad)
struct.pack_into('<HHI', raw, hp_ - 8, len(TB), len(VB), len(VB) * 32 + len(TB) * 6)
newblk = bytes(raw) + VB.tobytes() + TB.tobytes() + tail
n_ = Nif(d); n_.blocks[kt] = newblk
# v1.8.25b: the canvas two-sided (SLSF2 Double_Sided): cloth seen from either side, never a see-through spot
shk = t['shader']; sr = bytearray(n_.blocks[shk]); ne2, = struct.unpack_from('<I', sr, 8); fo = 12 + 4 * ne2 + 4
f1_, f2_ = struct.unpack_from('<II', sr, fo); struct.pack_into('<I', sr, fo + 4, f2_ | 0x10); n_.blocks[shk] = bytes(sr)
print(f'canvas shader flags2 {f2_:#x} -> {f2_ | 0x10:#x} (two-sided)'); outb = n_.write(); open(OUT, 'wb').write(outb)
print(f'tarp block {blk["end"] - blk["start"]:,} -> {len(newblk):,}; hull {len(d):,} -> {len(outb):,}')
