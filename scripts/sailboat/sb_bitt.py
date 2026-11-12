# RowRim v1.8.39: the anchor line's bitt on the foredeck (Miguel's sketch): a sturdy iron plate bolted mid foredeck with
# a U bitt on it, the line made fast round the bitt's crown (a round turn and two half hitches, its tail seized), and
# the line laid from it over the cambered deck and the starboard rail to the cathead, where the anchor's own line starts
# (kAnCat 39 232 98). Always shown. When the anchor is stowed its line is in: SB_BowRopeStow, a short drop from the
# cathead to the stowed ring (kAnStowRing 41 230 93), is shown instead (the plugin shows it only then).
# Shapes on the root (model frame), each its own shader, texture set and alpha: SB_BowBittPlate (black iron, with four
# bolt heads), SB_BowBitt (cast iron), SB_BowRope and SB_BowRopeStow (Rope02, as the anchor's line). Every part closed.
# usage: python sb_bitt.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape
from solidlib import consistent_outward
SRC, OUT = sys.argv[1:3]
import os; _dt = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'decktop.py')
sys.argv = ['x', SRC]; exec(open(_dt if os.path.exists(_dt) else '../decktop.py').read())  # top(x, y): the deck's top
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {}
for b in h['blocks']:
    if b['type'] == 'BSTriShape':
        try: shapes[tri_shape(d, h, b)['name']] = b['k']
        except Exception: pass
def shader_of(k):
    raw = n.blocks[k]; _, p = av_fix(raw)
    return struct.unpack_from('<2i', raw, p + 60 + 20)
def tex_set(raw, diffuse=None, normal=None):
    c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p + 4:p + 4 + l]); p += 4 + l
    if diffuse: out[0] = diffuse.encode()
    if normal: out[1] = normal.encode()
    return struct.pack('<I', c) + b''.join(struct.pack('<I', len(q)) + q for q in out)
TX = 'textures\\RowRim\\SailBoat\\'
def copy_shader(k, diffuse=None, normal=None):
    sr, sp = av_fix(n.blocks[k], 4); sr = bytearray(sr)
    tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', tex_set(n.blocks[tex], diffuse, normal)))
    return n.add('BSLightingShaderProperty', sr)
def copy_alpha(k): return n.add('NiAlphaProperty', av_fix(n.blocks[k])[0]) if k >= 0 else -1
def rename(k, nm):
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm)); n.blocks[k] = bytes(r)
def add_kids(node_k, kids):
    nr = bytearray(n.blocks[node_k]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + list(kids)
    n.blocks[node_k] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])

# ---- the layout
CAT = np.array([39.0, 232.0, 98.0]); RING = np.array([41.0, 230.0, 93.0])
B = np.array([0.0, 224.0]); u2 = (CAT[:2] - B) / np.linalg.norm(CAT[:2] - B)  # toward the cathead, in plan
w2 = np.array([-u2[1], u2[0]])
U = np.r_[u2, 0.0]; Wv = np.r_[w2, 0.0]; Z = np.array([0.0, 0.0, 1.0])
PL_L, PL_W, PL_T, PL_R = 17.0, 11.0, 1.6, 2.2  # the plate: along w, along u, thick, corner radius
cz = [top(*(B + a * u2 * PL_W / 2 + b * w2 * PL_L / 2)) for a in (-1, 1) for b in (-1, 1)]
PL_Z0 = min(cz) - 0.15; PL_Z1 = PL_Z0 + PL_T  # its foot just into the deck at its lowest corner (the deck is cambered)
BAR_R, LEG_H, ARC_R = 1.45, 6.0, 5.0
ROPE_R = 0.85
print('plate z %.2f..%.2f (deck under it %.2f..%.2f)' % (PL_Z0, PL_Z1, min(cz), top(*B)))

# ---- meshes: lists of (P, F) parts, each closed and right way out
def rrect(L, Wd, r, nseg=6):
    """a rounded rectangle in the (w, u) plane, anticlockwise from above"""
    pts = []
    for cx, cy, a0 in ((L / 2 - r, Wd / 2 - r, 0.0), (-L / 2 + r, Wd / 2 - r, 90.0), (-L / 2 + r, -Wd / 2 + r, 180.0), (L / 2 - r, -Wd / 2 + r, 270.0)):
        for k in range(nseg + 1):
            a = np.radians(a0 + 90.0 * k / nseg); pts.append((cx + r * np.cos(a), cy + r * np.sin(a)))
    return np.array(pts)
def to3(q2, z):  # (along w, along u) -> model
    return np.array([B[0] + q[0] * w2[0] + q[1] * u2[0] for q in q2]), np.array([B[1] + q[0] * w2[1] + q[1] * u2[1] for q in q2])
def slab(L, Wd, r, z0, z1, bevel, centre=None):
    """an extruded rounded rectangle with a bevelled top edge; centre: (w, u) offset"""
    ring = rrect(L, Wd, r); inner = rrect(L - 2 * bevel, Wd - 2 * bevel, max(0.3, r - bevel))
    if centre is not None: ring = ring + centre; inner = inner + centre
    m = len(ring); P = []
    for q2, z in ((ring, z0), (ring, z1 - bevel), (inner, z1)):
        X, Y = to3(q2, z); P += [np.c_[X, Y, np.full(m, z)]]
    P = np.vstack(P); cb = len(P); c2 = np.mean(ring, 0); X, Y = to3([c2], 0)
    P = np.vstack([P, [X[0], Y[0], z0], [X[0], Y[0], z1]])
    F = []
    for a in range(2):
        for k in range(m):
            i, j = a * m + k, a * m + (k + 1) % m
            F += [(i, j, j + m), (i, j + m, i + m)]
    for k in range(m):
        F.append((cb, (k + 1) % m, k)); F.append((cb + 1, 2 * m + k, 2 * m + (k + 1) % m))
    return P, np.array(F)
def cyl(c, ax, r, h, seg=12, dome=0.0):
    """a short solid cylinder from c along ax (unit), radius r, height h, its top a low dome"""
    ax = ax / np.linalg.norm(ax); e1 = np.cross(ax, [1.0, 0, 0] if abs(ax[0]) < 0.9 else [0, 1.0, 0]); e1 /= np.linalg.norm(e1); e2 = np.cross(ax, e1)
    ang = np.linspace(0, 2 * np.pi, seg, endpoint=False)
    ringv = np.cos(ang)[:, None] * e1 + np.sin(ang)[:, None] * e2
    P = np.vstack([c + r * ringv, c + ax * h + r * ringv, c + ax * h + 0.6 * r * ringv + ax * dome * 0.6, [c], [c + ax * (h + dome)]])
    F = []
    for k in range(seg):
        j = (k + 1) % seg
        F += [(k, j, seg + j), (k, seg + j, seg + k), (seg + k, seg + j, 2 * seg + j), (seg + k, 2 * seg + j, 2 * seg + k)]
        F += [(3 * seg, j, k), (3 * seg + 1, 2 * seg + k, 2 * seg + j)]
    return P, np.array(F)
def tube(L, r, strands=False, seg=12):
    """a closed tube along polyline L (capped ends); strands: three laid strands (as the boat's ropes)"""
    L = np.asarray(L, float)
    Tg = np.gradient(L, axis=0); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    ref = np.array([0, 0, 1.0]) if abs(Tg[0][2]) < 0.9 else np.array([1.0, 0, 0])
    nrm = np.cross(Tg[0], ref); nrm /= np.linalg.norm(nrm)
    s = np.r_[0, np.cumsum(np.linalg.norm(np.diff(L, axis=0), axis=1))]
    pitch = 3.0 * 2 * r; P = []; N = []; TT = []; UV = []
    for i, (p, t) in enumerate(zip(L, Tg)):
        nrm = nrm - t * nrm.dot(t); nrm /= np.linalg.norm(nrm) + 1e-9; bi = np.cross(t, nrm)
        for k in range(seg + 1):
            a = 2 * np.pi * k / seg
            rr = r * (1.0 + 0.13 * np.cos(3.0 * (a - s[i] * 2 * np.pi / pitch))) if strands else r
            dv = np.cos(a) * nrm + np.sin(a) * bi
            P.append(p + dv * rr); N.append(dv); TT.append(t); UV.append((k / seg * 0.5, s[i] / 10.0))
    F = []
    for i in range(len(L) - 1):
        for k in range(seg):
            a = i * (seg + 1) + k; b = a + seg + 1
            F += [(a, b, a + 1), (a + 1, b, b + 1)]
    for i0, sgn in ((0, -1), (len(L) - 1, 1)):
        ci = len(P); P.append(L[i0] + Tg[i0] * sgn * r * 0.35); N.append(Tg[i0] * sgn); TT.append(np.cross(Tg[i0], [0.31, 0.47, 0.83])); UV.append((0.25, s[i0] / 10.0))
        ring = i0 * (seg + 1)
        for k in range(seg): F.append((ci, ring + k + 1, ring + k) if sgn < 0 else (ci, ring + k, ring + k + 1))
    return np.array(P), np.array(N), np.array(TT), np.array(UV), np.array(F)
def resample(L, step):
    L = np.asarray(L, float); s = np.r_[0, np.cumsum(np.linalg.norm(np.diff(L, axis=0), axis=1))]
    t = np.linspace(0, s[-1], max(2, int(np.ceil(s[-1] / step)) + 1))
    return np.stack([np.interp(t, s, L[:, i]) for i in range(3)], 1)
def smooth(L, it=3, fix_ends=True):
    L = np.asarray(L, float).copy()
    for _ in range(it): L[1:-1] = (L[:-2] + 2 * L[1:-1] + L[2:]) / 4
    return L

# the plate and its bolt heads
plate = [slab(PL_L, PL_W, PL_R, PL_Z0, PL_Z1, 0.45)]
for a in (-1, 1):
    for b in (-1, 1):
        q = np.array([a * (PL_L / 2 - 2.3), b * (PL_W / 2 - 2.0)]); X, Y = to3([q], 0)
        plate.append(cyl(np.array([X[0], Y[0], PL_Z1 - 0.2]), Z, 0.95, 0.65, 8, 0.35))
# the bitt: a round bar bent in a U, its legs into the plate, its crown across the line's way
ARC_C = np.r_[B, PL_Z1 + LEG_H]
bar = [np.r_[B + w2 * ARC_R, PL_Z0 + 0.3]]
bar += [np.r_[B + w2 * ARC_R, PL_Z1 + LEG_H * f] for f in np.linspace(0.0, 1.0, 6)[1:]]
th = np.linspace(0.0, np.pi, 25)[1:-1]
bar += [ARC_C + ARC_R * (np.cos(t) * Wv + np.sin(t) * Z) for t in th]
bar += [np.r_[B - w2 * ARC_R, PL_Z1 + LEG_H * f] for f in np.linspace(1.0, 0.0, 6)[:-1]]
bar += [np.r_[B - w2 * ARC_R, PL_Z0 + 0.3]]
bitt = [tube(resample(bar, 0.8), BAR_R)]
# the collars where the legs go into the plate
for sgn in (1, -1):
    bitt.append(cyl(np.r_[B + sgn * w2 * ARC_R, PL_Z1 - 0.3], Z, BAR_R + 0.55, 1.0, 12, 0.0))

# ---- the line
rope = []
RB = BAR_R + ROPE_R + 0.06
def on_bar(t, phi):  # a point RB off the bar's crown at arc angle t, round it by phi (0 out from the arc's centre, pi/2 toward the cathead)
    n1 = np.cos(t) * Wv + np.sin(t) * Z
    return ARC_C + ARC_R * n1 + RB * (np.cos(phi) * n1 + np.sin(phi) * U)
# a round turn: two turns round the crown, sliding along it, leaving on the cathead side going down
turns = 2.0; phis = np.linspace(np.pi / 2 - turns * 2 * np.pi, np.pi / 2, 90)
ts = np.pi / 2 + np.linspace(-0.42, 0.30, len(phis))
helix = np.array([on_bar(t, ph) for t, ph in zip(ts, phis)])
# the standing part: down off the crown and out toward the cathead, onto the deck
E = helix[-1]; dE = helix[-1] - helix[-2]; dE /= np.linalg.norm(dE)
def deck_z(p2): z = top(p2[0], p2[1]); return (z if z is not None else 93.0) + ROPE_R - 0.12
xs = np.linspace(0.0, 1.0, 200)
plan = [B + u2 * (PL_W / 2 + 2.5 + f * 60.0) for f in xs]
plan = [p for p in plan if np.dot(p - B, u2) < np.dot(CAT[:2] - B, u2)]
prof = np.array([deck_z(p) for p in plan])
# the rope rests on the deck; it cannot bend into a hollow tighter than 7 (a closing of the profile by a 7 disc)
sstep = np.linalg.norm(plan[1] - plan[0]); Rd = 7.0
def closing(z):
    k = int(Rd / sstep) + 1; off = np.arange(-k, k + 1) * sstep; cap = np.sqrt(np.maximum(0, Rd * Rd - off * off))
    zp = np.pad(z, k, mode='edge')
    dil = np.array([np.max(zp[i:i + 2 * k + 1] + cap) for i in range(len(z))])
    dp = np.pad(dil, k, mode='edge')
    return np.array([np.min(dp[i:i + 2 * k + 1] - cap) for i in range(len(z))])
prof = np.maximum(prof, closing(prof))
# where the rail ends outboard the line leaves the wood: from its crest it runs straight to the cathead
air = [i for i, p in enumerate(plan) if top(p[0], p[1]) is None]
i_end = (air[0] if air else len(plan)) - 1
deck_pts = np.c_[np.array(plan[:i_end + 1]), prof[:i_end + 1]]
# the line lies on the deck out to the rail's crest (the highest point of its outer quarter), then runs free to the cathead
q0 = int(len(deck_pts) * 0.6); i_last = q0 + int(np.argmax(deck_pts[q0:, 2]))
lay = deck_pts[:i_last + 1]
# from the crown down to the deck: a smooth drop (Hermite), leaving the bitt along the helix, landing along the deck
D = lay[0]; tD = lay[min(4, len(lay) - 1)] - lay[0]; tD /= np.linalg.norm(tD)
gap = np.linalg.norm(D - E)
drop = [E + (3 * s * s - 2 * s ** 3) * (D - E) + (s ** 3 - 2 * s * s + s) * dE * gap * 0.9 + (s ** 3 - s * s) * tD * gap * 1.2 for s in np.linspace(0, 1, 24)]
span = [lay[-1] + f * (CAT - lay[-1]) for f in np.linspace(0, 1, 12)[1:]]
line = np.vstack([helix, drop[1:], lay[1:], span])
line = resample(smooth(resample(line, 0.6), 2), 0.6)
line[-1] = CAT
rope.append(tube(line, ROPE_R, True))
# the tail: out of the round turn's start, tucked back along the standing part and seized
T0 = helix[0]; tT = helix[0] - helix[1]; tT /= np.linalg.norm(tT)
tail = [T0 + tT * s for s in np.linspace(0, 3.0, 5)]
tail = np.array(tail); rope.append(tube(resample(tail, 0.6), ROPE_R * 0.95, True))
# two half hitches round the standing part, just off the bitt
i0 = len(helix) + 2
for kk, off in enumerate((1.2, 3.4)):
    s_at = np.r_[0, np.cumsum(np.linalg.norm(np.diff(line, axis=0), axis=1))]
    base_s = s_at[np.argmin(np.abs(np.linalg.norm(line - E, axis=1)))] + off
    j = int(np.searchsorted(s_at, base_s)); c = line[j]; ax = line[min(j + 2, len(line) - 1)] - line[max(j - 2, 0)]; ax /= np.linalg.norm(ax)
    e1 = np.cross(ax, Z if abs(ax[2]) < 0.9 else U); e1 /= np.linalg.norm(e1); e2 = np.cross(ax, e1)
    ph = np.linspace(0, 2 * np.pi * 1.05, 40) + kk * 1.9
    loop = np.array([c + ax * (q / (2 * np.pi) * 1.9 - 0.9) + (2 * ROPE_R + 0.05) * (np.cos(q) * e1 + np.sin(q) * e2) for q in ph])
    rope.append(tube(loop, ROPE_R, True))
# stowed: the short drop from the cathead to the stowed ring
stow = np.array([CAT + f * (RING - CAT) + np.array([0.6, 0.0, 0.0]) * np.sin(np.pi * f) for f in np.linspace(0, 1, 12)])
stowrope = [tube(resample(stow, 0.6), ROPE_R, True)]

# ---- write
def merge_solid(parts):
    """parts of (P, F) or (P, N, T, UV, F): one vertex list; iron parts get planar UVs and smooth normals"""
    PP, NN, TT, UU, FF = [], [], [], [], []; base = 0
    for prt in parts:
        if len(prt) == 2:
            P, F = prt; F = consistent_outward(P, F); V = P[F].reshape(-1, 3); F2 = np.arange(len(V)).reshape(-1, 3)
            fn = np.cross(V[F2[:, 1]] - V[F2[:, 0]], V[F2[:, 2]] - V[F2[:, 0]])
            key = np.round(V, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
            acc = np.zeros((inv.max() + 1, 3))
            for i in range(3): np.add.at(acc, inv[F2[:, i]], fn)
            fnn = fn / (np.linalg.norm(fn, axis=1, keepdims=True) + 1e-9)
            Nv = acc[inv]; Nv /= np.linalg.norm(Nv, axis=1, keepdims=True) + 1e-9
            # a hard edge where the faces turn more than 40 degrees (the plate's sides and top): the face's own normal
            hard = (Nv * np.repeat(fnn, 3, 0)).sum(1) < 0.77; Nv[hard] = np.repeat(fnn, 3, 0)[hard]
            ax = np.argmax(np.abs(fn), 1); UV = np.zeros((len(V), 2))
            for a, (i, j) in enumerate(((1, 2), (0, 2), (0, 1))):
                m = np.repeat(ax == a, 3); UV[m] = V[m][:, [i, j]] / 20.0
            Tg = np.cross(Nv, [0.31, 0.47, 0.83]); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
            P, N, Tt, U2, F = V, Nv, Tg, UV, F2
        else:
            P, N, Tt, U2, F = prt; F = consistent_outward(P, F)
        PP.append(P); NN.append(N); TT.append(Tt); UU.append(U2); FF.append(F + base); base += len(P)
    return np.vstack(PP), np.vstack(NN), np.vstack(TT), np.vstack(UU), np.vstack(FF)
tpl = n.blocks[shapes['SB_Ironwork']]
iron_sh, iron_al = shader_of(shapes['SB_Ironwork'])
rope_sh, rope_al = shader_of(shapes['SB_JibCoil'])
kids = []
for nm, parts, sh, al, dif, nrm in (('SB_BowBittPlate', plate, iron_sh, iron_al, TX + 'RowRim_IronBlack_d.dds', TX + 'RowRim_Flat_n.dds'),
                                    ('SB_BowBitt', bitt, iron_sh, iron_al, TX + 'RowRim_CastIron_d.dds', TX + 'RowRim_Flat_n.dds'),
                                    ('SB_BowRope', rope, rope_sh, rope_al, None, None),
                                    ('SB_BowRopeStow', stowrope, rope_sh, rope_al, None, None)):
    P, N, Tt, U2, F = merge_solid(parts)
    k = build_shape(n, tpl, P, N, Tt, U2, F, copy_shader(sh, dif, nrm), copy_alpha(al)); rename(k, nm); kids.append(k)
    print(nm, len(P), 'vertices', len(F), 'triangles')
add_kids(0, kids)
np.save('../bow_line.npy', line)
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out), 'bytes')
