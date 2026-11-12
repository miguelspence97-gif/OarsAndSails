# RowRim v1.8.30: the sailing boat's sails rolled up and tied. Static bundles the plugin slides into view as the cloth
# comes in (hidden in the nif):
# - SB_MainFurl (canvas) + SB_MainFurlTies (rope): the main stowed: a full roll on top of the boom, fattest a third of
#   the way aft, tied to it with six rope bands (under SB_MainRig: it swings with the boom).
# - SB_MainReef + SB_MainReefTies: reefed: the lower part of the main rolled thinner along the boom, tied with reef
#   points (small bands) every 28.
# - SB_JibFurl (canvas) + SB_JibFurlWrap (rope): the jib rolled round its stay (roller furled), thick at the tack and
#   thin at the head, its sheet wound round it in a spiral (under SB_JibRig: the stay is its z axis).
# Each its own shader, texture set and alpha: the canvas a copy of SB_SailMainS's (unskinned), the rope SB_Rigging's.
# usage: python sb_furl.py <sailboat nif> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {tri_shape(d, h, b)['name']: b['k'] for b in h['blocks'] if b['type'] == 'BSTriShape'}
nodes = {h['names'][struct.unpack_from('<i', d, b['start'])[0]]: b['k'] for b in h['blocks'] if b['type'] in ('NiNode', 'BSFadeNode')}
def shader_of(k):
    raw = n.blocks[k]; _, p = av_fix(raw)
    return struct.unpack_from('<2i', raw, p + 60 + 20)
FURL_D = 'textures\\RowRim\\SailBoat\\BoatFabric02_furl_d.dds'  # v1.8.30f: the furled cloth darker (folded, in its own shade)
def with_diffuse(raw, path):
    c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p + 4:p + 4 + l]); p += 4 + l
    out[0] = path.encode()
    return struct.pack('<I', c) + b''.join(struct.pack('<I', len(q)) + q for q in out)
def copy_shader(k, unskin=False, two=False):
    sr, sp = av_fix(n.blocks[k], 4); sr = bytearray(sr)
    tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    ts = with_diffuse(n.blocks[tex], FURL_D) if unskin else n.blocks[tex]
    struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', ts))
    f1, f2 = struct.unpack_from('<II', sr, 16)
    if unskin: f1 &= ~0x2
    if two: f2 |= 0x10
    else: f2 &= ~0x10
    struct.pack_into('<II', sr, 16, f1, f2)
    return n.add('BSLightingShaderProperty', sr)
def copy_alpha(k): return n.add('NiAlphaProperty', av_fix(n.blocks[k])[0]) if k >= 0 else -1
def add_kids(node_k, kids):
    nr = bytearray(n.blocks[node_k]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + list(kids)
    n.blocks[node_k] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])
def finish(k, nm):
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm))
    fl, = struct.unpack_from('<I', r, 12); struct.pack_into('<I', r, 12, fl | 1)  # hidden: the plugin shows it
    n.blocks[k] = bytes(r)
tpl = n.blocks[shapes['SB_Ironwork']]
canvas_sh, canvas_al = shader_of(shapes['SB_SailMainS'])
rope_sh, rope_al = shader_of(shapes['SB_Rigging'])
class Mesh:
    def __init__(s): s.P = []; s.N = []; s.T = []; s.U = []; s.F = []
    def tube(s, C, R, B, seg=14, vt=30.0, lumpy=0.0, seed=0):
        """a tube along centre points C (n,3) with radius R (n,), binormal hints B (n,3) for its frame"""
        rng = np.random.default_rng(seed)
        C = np.asarray(C, float); R = np.asarray(R, float)
        T = np.gradient(C, axis=0); T /= np.linalg.norm(T, axis=1, keepdims=True)
        L = np.r_[0.0, np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))]
        base = len(s.P); wob = rng.uniform(0, 6.28, 3)
        for i, (c, t, r, l) in enumerate(zip(C, T, R, L)):
            nrm = np.asarray(B[i], float); nrm = nrm - t * nrm.dot(t); nrm /= np.linalg.norm(nrm); bi = np.cross(t, nrm)
            for k in range(seg + 1):
                a = 2 * np.pi * k / seg
                rr = r * (1.0 + lumpy * (0.5 * np.sin(3 * a + l * 0.11 + wob[0]) + 0.3 * np.sin(5 * a - l * 0.07 + wob[1])))
                dirv = np.cos(a) * nrm + np.sin(a) * bi
                s.P.append(c + dirv * rr); s.N.append(dirv); s.T.append(t); s.U.append((k / seg, l / vt))
        for i in range(len(C) - 1):
            for k in range(seg):
                a = base + i * (seg + 1) + k; b = a + seg + 1
                s.F += [(a, b, a + 1), (a + 1, b, b + 1)]
        # caps
        for end, i0 in ((0, 0), (1, len(C) - 1)):
            ci = len(s.P); s.P.append(C[i0]); s.N.append(-T[i0] if end == 0 else T[i0]); s.T.append(np.cross(T[i0], [0, 0, 1.0]) + [1e-3, 0, 0]); s.U.append((0.5, L[i0] / vt))
            ring = base + i0 * (seg + 1)
            for k in range(seg):
                s.F.append((ci, ring + k + 1, ring + k) if end == 0 else (ci, ring + k, ring + k + 1))
    def loop(s, centre, ax_u, ax_v, ru, rv, wire, seg=28, wseg=10):
        """a rope band: an ellipse (centre, its two in-plane axes and radii) of wire thickness"""
        pts = [centre + ax_u * ru * np.cos(2 * np.pi * i / seg) + ax_v * rv * np.sin(2 * np.pi * i / seg) for i in range(seg + 1)]
        pts[-1] = pts[0]
        nrm = np.cross(ax_u, ax_v)
        s.tube(np.array(pts), np.full(seg + 1, wire), [nrm] * (seg + 1), seg=wseg, vt=12.0)
    def add(s, nm, sh, al, parent):
        P = np.array(s.P); N = np.array(s.N, float); T = np.array(s.T, float); U = np.array(s.U)
        N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
        T = T - N * (T * N).sum(1, keepdims=True); T /= np.linalg.norm(T, axis=1, keepdims=True) + 1e-9
        k = build_shape(n, tpl, P, N, T, U, np.array(s.F), sh, al); finish(k, nm); add_kids(parent, [k])
        print(f'{nm}: {len(P)} verts, {len(s.F)} tris, x {P[:,0].min():.0f}..{P[:,0].max():.0f} y {P[:,1].min():.0f}..{P[:,1].max():.0f} z {P[:,2].min():.0f}..{P[:,2].max():.0f}')
        return k
# ---------- v1.8.30d: furled as Miguel's reference, gentler: the cloth under its spar in soft swags, snug lashings ----------
class Swag:
    """the furled cloth under a spar A0..A1, hanging toward dn: soft swags between the ties, creased, its bottom rippled"""
    def __init__(s, A0, A1, dn, Rs, ties_at, D, W, seed):
        rng = np.random.default_rng(seed)
        s.A0 = np.asarray(A0, float); s.A1 = np.asarray(A1, float); s.L = np.linalg.norm(s.A1 - s.A0)
        s.ax = (s.A1 - s.A0) / s.L
        dn = np.asarray(dn, float); dn = dn - s.ax * dn.dot(s.ax); s.dn = dn / np.linalg.norm(dn); s.sd = np.cross(s.ax, s.dn)
        s.edges = [0.0] + list(ties_at) + [1.0]; s.ties = list(ties_at)
        s.depth = [D * rng.uniform(0.8, 1.1) for _ in range(len(s.edges) - 1)]
        s.depth[len(s.depth) // 2] *= 1.15
        s.ph = rng.uniform(0, 6.28, 4); s.Rs = Rs; s.W = W
    def section(s, t, a):
        j = min(int(np.searchsorted(s.edges, t, side='right') - 1), len(s.edges) - 2)
        u = (t - s.edges[j]) / (s.edges[j + 1] - s.edges[j])
        sw = np.sin(np.pi * u) ** 1.4
        h = 1.0 + s.depth[j] * sw
        w = s.W * (0.45 + 0.55 * np.sin(np.pi * u) ** 0.6)
        tip = min(t, 1.0 - t) * s.L
        if tip < 6.0: w *= 0.4 + 0.6 * tip / 6.0; h = 1.0 + (h - 1.0) * (0.3 + 0.7 * tip / 6.0)
        ca, sa = np.cos(a), np.sin(a); down = max(0.0, -sa)
        crease = 1.0 + 0.10 * np.sin(9.0 * u * np.pi + s.ph[0] + 2.0 * a) * down + 0.05 * np.sin(19.0 * t * np.pi + s.ph[1])
        r_side = (s.Rs + 0.8 + w * (0.3 + 0.7 * down)) * crease
        r_dn = s.Rs + 0.8 + h * down ** 0.9 if sa < 0 else s.Rs + 1.2
        off = s.sd * ca * r_side - s.dn * sa * r_dn
        if down > 0.85: off = off + s.dn * (0.5 * np.sin(17.0 * t * np.pi + s.ph[2]) + 0.3 * np.sin(31.0 * t * np.pi + s.ph[3])) * (down - 0.85) / 0.15 * min(1.0, h / 5.0)
        return s.A0 + (s.A1 - s.A0) * t + off
    def cloth(s, mesh, n_s=100, seg=16):
        base = len(mesh.P)
        for t in np.linspace(0.0, 1.0, n_s):
            for k in range(seg + 1):
                a = 2 * np.pi * k / seg
                p = s.section(t, a); mesh.P.append(p)
                c = s.A0 + (s.A1 - s.A0) * t; nr = p - c; mesh.N.append(nr / (np.linalg.norm(nr) + 1e-9)); mesh.T.append(s.ax); mesh.U.append((k / seg, t * s.L / 30.0))
        for i in range(n_s - 1):
            for k in range(seg):
                a = base + i * (seg + 1) + k; b = a + seg + 1
                mesh.F += [(a, a + 1, b), (a + 1, b + 1, b)]
    def lashings(s, mesh):
        """each tie: two turns of rope pulled tight round the cloth and spar, lying on the cloth (no gap), side by side"""
        for t0 in s.ties:
            for dt in (-0.6, 0.6):
                t = t0 + dt / s.L
                pts = []
                for k in range(25):
                    a = 2 * np.pi * k / 24
                    p = s.section(t, a); c = s.A0 + (s.A1 - s.A0) * t
                    v = p - c; v -= s.ax * v.dot(s.ax)
                    pts.append(p + v / (np.linalg.norm(v) + 1e-9) * 0.42)
                pts[-1] = pts[0]
                mesh.tube(np.array(pts), np.full(25, 0.45), [s.ax] * 25, seg=10, vt=10.0)
up = np.array([0, 0, 1.0])
# the main on its boom (SB_MainRig space): the boom from just aft of the mast (y -22, z 140) to its end (y -205, z 169)
BX = 0.9; BR = 3.7
def boom_z(y): return 140.0 + (-(y + 20.0)) * 0.157
sw = Swag([BX, -22.0, boom_z(-22.0)], [BX, -205.0, boom_z(-205.0)], -up, BR, [0.2, 0.42, 0.62, 0.82], 7.0, 5.0, 21)
m = Mesh(); sw.cloth(m); m.add('SB_MainFurl', copy_shader(canvas_sh, unskin=True), copy_alpha(canvas_al), nodes['SB_MainRig'])
t = Mesh(); sw.lashings(t); t.add('SB_MainFurlTies', copy_shader(rope_sh), copy_alpha(rope_al), nodes['SB_MainRig'])
# v1.8.30e: the jib furled hanging beside the mast (Miguel's picture), on its forward side: a rolled bundle hung from a
# lashing at z 362, tied round in three places, its foot just over the boom; under SB_JibFurlHang (root) at the hanging
# point so the plugin can let it swing as the boat rocks. Its stay (the forestay) is taken down and coiled on the bowsprit.
from nifmerge import node_block
HANG = np.array([-2.0, 97.0, 362.0])
zs = np.linspace(0.0, -205.0, 70)          # down from the hanging point
u = -zs / 205.0
Rj = 4.8 * (0.55 + 0.45 * np.sin(np.pi * np.clip(u * 0.9 + 0.08, 0, 1)) ** 0.7)
tie_u = [0.06, 0.36, 0.66, 0.94]
for tu in tie_u: Rj = Rj * (1.0 - 0.16 * np.exp(-((u - tu) / 0.02) ** 2))
Rj = Rj * (1.0 + 0.05 * np.sin(u * 23.0 + 0.7) + 0.04 * np.sin(u * 41.0 + 2.1))
Cj = np.c_[0.8 * np.sin(u * 3.1), 1.2 * np.sin(u * 2.3 + 0.4), zs]
m = Mesh(); m.tube(Cj, Rj, [[1.0, 0, 0]] * len(zs), seg=16, lumpy=0.07, seed=31, vt=26.0)
t = Mesh()
for tu in tie_u:
    i = int(np.argmin(np.abs(u - tu))); c = Cj[i]; r = Rj[i] + 0.4
    for dz in (-0.6, 0.6):
        t.loop(c + np.array([0, 0, dz]), np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), r, r, 0.45, seg=22)
# the top lashing to the mast: a short rope from the bundle's head round the mast (the mast's front face at y 89.4)
# (in the hanging node's space: the mast's centre is 14 aft of the hanging point)
t.tube(np.array([[0.0, -3.0, 1.5], [0.0, -5.5, 3.5], [0.0, -7.6, 4.5]]), np.full(3, 0.6), [[1.0, 0, 0]] * 3, seg=10, vt=10.0)
t.loop(np.array([0.0, -14.0, 4.5]), np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), 7.1, 7.1, 0.6, seg=24)
jk = m.add('SB_JibFurl', copy_shader(canvas_sh, unskin=True), copy_alpha(canvas_al), 0)
wk = t.add('SB_JibFurlWrap', copy_shader(rope_sh), copy_alpha(rope_al), 0)
# move them under their own hanging node (the root got them: take them off it)
def take_off_root(kids):
    nr = bytearray(n.blocks[0]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = [k for k in struct.unpack_from('<%di' % nc, nr, q + 4) if k not in kids]
    n.blocks[0] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])
take_off_root([jk, wk])
hang = n.add('NiNode', node_block(n.name('SB_JibFurlHang'), T=tuple(HANG), kids=[jk, wk]))
add_kids(0, [hang])
# v1.8.30f: the jib's tack line (from its iron hook at the tack down to the bowsprit's end) and the hook come off when it
# is furled (Miguel's ScreenShot117), the line coiled on the bowsprit; the forestay stays
from hull_v144_lib import load, store, pos, components
def split_out(shape, test, name):
    k = shapes[shape]; st2, vb2, F2 = load(n.blocks[k]); P2 = pos(vb2).astype(float)
    sel = np.zeros(len(P2), bool)
    for c in components(P2, F2):
        if test(P2[c]): sel[c] = True
    assert sel.any(), name
    ts = sel[F2].all(1)
    def sub(mask, tris):
        idx = np.where(mask)[0]; re = -np.ones(len(P2), int); re[idx] = np.arange(len(idx)); return vb2[idx].copy(), re[tris]
    vk, fk = sub(~sel, F2[~ts]); n.blocks[k] = store(st2, vk, fk)
    vs, fs = sub(sel, F2[ts])
    sh, al = shader_of(k)
    raw = bytearray(store(st2, vs, fs)); r2, p = av_fix(bytes(raw)); raw = bytearray(r2)
    struct.pack_into('<i', raw, 0, n.name(name)); struct.pack_into('<3i', raw, p + 60 + 16, -1, copy_shader(sh), copy_alpha(al))
    nk = n.add('BSTriShape', raw); add_kids(0, [nk])
    print(f'{name}: {sel.sum()} vertices out of {shape}')
split_out('SB_Rigging', lambda q: (q[:, 1].min() > 285 and q[:, 1].max() > 400 and q[:, 2].max() > 150 and q[:, 2].max() < 170) or (q[:, 1].min() > 306 and q[:, 1].max() < 318 and q[:, 2].min() > 146), 'SB_JibTack')  # v1.8.30g: and its seizing by the hook
split_out('SB_Ironwork', lambda q: q[:, 1].min() > 280 and q[:, 1].max() < 305 and q[:, 2].min() > 150, 'SB_JibTackIron')
def bs_z(y): return 103.0 + (y - 255.0) * (14.5 / 160.0)
# v1.8.30g: coiled properly: a flat coil lying on the foredeck just aft of the stem (starboard), five turns, its end led
# forward over the stem head to the bowsprit
CC = np.array([9.0, 228.0, 81.5]); turns = 5.0; nP = 520
t = np.linspace(0.0, 1.0, nP)
ang = t * turns * 2 * np.pi
rad = 8.8 - 3.6 * t                             # from the outside in
lay = 0.55 * np.sin(ang * 0.5) * 0.4             # turns lying a little on each other
C = np.c_[CC[0] + rad * np.cos(ang), CC[1] + rad * np.sin(ang), CC[2] + 0.6 + lay + 0.5 * t]
end = np.array([CC[0] + 8.8, CC[1], CC[2] + 0.6])
lead = np.array([end + (np.array([-1.0, 258.0, 108.0]) - end) * k for k in np.linspace(0.0, 1.0, 30)[1:]])
lead[:, 2] += np.sin(np.linspace(0, np.pi, len(lead))) * 2.0 + np.maximum(0, (lead[:, 1] - 236.0)) * 0.0
C = np.vstack([lead[::-1], C])
c = Mesh(); c.tube(C, np.full(len(C), 0.55), [[0, 0, 1.0]] * len(C), seg=10, vt=10.0)
ck = c.add('SB_JibCoil', copy_shader(rope_sh), copy_alpha(rope_al), 0)
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(d):,} -> {len(out):,} bytes')
