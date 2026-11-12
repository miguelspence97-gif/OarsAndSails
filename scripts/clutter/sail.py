# RowRim v1.7.73 (v1.7.82: own textures, tapered spars, mast head): the mast and lug sail, baked into RowRim_Hull.nif (hidden until the mast is stepped).
#   RowRim_Sail              (hidden) at the mast step, model (0, 132, 0): just forward of the bow thwart, clear of the
#                            rolled bedroll (y 101-125) and between the bow thwart's two passengers (x +-30)
#     mast, partner          the mast (keel to 305) and the wooden partner bolted to the thwart's front edge
#     RowRim_SailRig         turned about z (the mast) by the plugin to follow the wind: everything on it keeps its
#                            distance to the mast at any angle
#       boom                 along the sail's foot at 185-192, above a standing head
#       RowRim_SailFull / RowRim_SailReefed / RowRim_SailFurled   one shown at a time, faded across
# No collision anywhere (no bhk blocks): nothing to snag on. The sail hangs on the rig's +x side, its luff kD off the
# mast axis; near the mast the cloth never comes closer than kD (mast radius 3): it cannot clip the mast.
# usage: python sail.py <in hull nif> <out hull nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, node_block, av_fix, tex_set, root_add_kids, e8
from hull_v144_lib import load, store
SRC, OUT = sys.argv[1], sys.argv[2]
MY = 132.0          # mast step, model y
MR = 3.0            # mast radius
MTOP = 305.0
kD = 7.5            # the sail's plane off the mast axis (+x of the rig)
FOOT0, FOOT1 = 185.0, 192.0   # boom height at the tack / the clew
d = open(SRC, 'rb').read(); n = Nif(d)
if 'RowRim_Sail' in n.names and any(n.types[n.ti[i]] == 'NiNode' and struct.unpack_from('<i', b, 0)[0] == n.names.index('RowRim_Sail') for i, b in enumerate(n.blocks)):
    raise SystemExit('the hull already has RowRim_Sail: build from the hull without it')

def tex_paths(k):
    raw = n.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1')); p += 4 + l
    return out
def find_shader(key):
    for k, t in enumerate(n.ti):
        if n.types[t] != 'BSLightingShaderProperty': continue
        ts, = struct.unpack_from('<i', n.blocks[k], 40)
        if ts >= 0 and any(key in q.lower() for q in tex_paths(ts)):
            for s2, t2 in enumerate(n.ti):
                if n.types[t2] == 'BSTriShape':
                    ti = tri_shape(d, n.h, n.h['blocks'][s2])
                    if ti['shader'] == k and ti['vsize'] == 32: return n.blocks[s2], n.blocks[k], tex_paths(ts)
    raise SystemExit('no shader with ' + key)
TPL, FLOOR_SH, _ = find_shader('boat_floor_d')
_, WOOD_SH, WOOD_TX = find_shader('kwila')
_, ROPE_SH, ROPE_TX = find_shader('boat_rope_d')
st, _, _ = load(TPL)
def shader(base, paths, two_sided=False, matte=True, vcol=True):
    t = n.add('BSShaderTextureSet', tex_set(paths))
    r, _ = av_fix(base, 4); r = bytearray(r); struct.pack_into('<i', r, 40, t)
    f1, f2 = struct.unpack_from('<II', r, 16)
    if matte: f1 &= ~1
    if two_sided: f2 |= 0x10
    if not vcol: f2 &= ~0x20   # SLSF2_Vertex_Colors off (the cloth has none)
    struct.pack_into('<II', r, 16, f1, f2)
    return n.add('BSLightingShaderProperty', r)
def alpha(): return n.add('NiAlphaProperty', struct.pack('<iIiHB', -1, 0, -1, 0x00EC, 0))
WOOD = (WOOD_SH, WOOD_TX); ROPE = (ROPE_SH, ROPE_TX)
def shape(geo, sh, alp=-1):
    P, N, Tg, UV, F = geo
    P = np.asarray(P, float); N = np.asarray(N, float); Tg = np.asarray(Tg, float); UV = np.asarray(UV, float)
    N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9; Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    B = np.cross(N, Tg)
    vb = np.zeros((len(P), 32), np.uint8)
    vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    vb[:, 16:20] = np.ascontiguousarray(UV.astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 20:23] = e8(N); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2]); vb[:, 28:32] = 255
    raw = bytearray(store(st, vb, np.asarray(F))); raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
    struct.pack_into('<I', raw, p0, 0x0008000E)   # flags: plain, shown
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sh, alp)
    return n.add('BSTriShape', raw)
def dynshape(geo, sh, alp):
    """v1.7.83: the cloth as a BSDynamicTriShape (as the game's face meshes): its positions are not in the vertex buffer
    but a float4 array the plugin rewrites every frame (the cloth moving in the wind). Vertex: UV, normal, tangent,
    (12 bytes); dynamic: x, y, z, bitangent x. Same vertex order as sail() makes (row-major, the plugin relies on it)."""
    P, N, Tg, UV, F = geo
    P = np.asarray(P, float); N = np.asarray(N, float); Tg = np.asarray(Tg, float); UV = np.asarray(UV, float)
    N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9; Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    B = np.cross(N, Tg)
    vb = np.zeros((len(P), 12), np.uint8)
    vb[:, 0:4] = np.ascontiguousarray(UV.astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 4:7] = e8(N); vb[:, 7] = e8(B[:, 1]); vb[:, 8:11] = e8(Tg); vb[:, 11] = e8(B[:, 2])
    desc = 3 | (4 << 4) | (0 << 8) | (1 << 16) | (2 << 20) | (0x41A << 44)   # no colours: keeps the hull from growing
    tris = np.asarray(F).astype('<u2')
    data = vb.tobytes() + tris.tobytes()
    hd = bytearray(st['raw'][:st['data_off']])
    cen = P.mean(0); rad = float(np.linalg.norm(P - cen, axis=1).max()) + 30.0   # room for the cloth to move
    struct.pack_into('<4f', hd, st['bs_off'], float(cen[0]), float(cen[1]), float(cen[2]), rad)
    struct.pack_into('<Q', hd, st['cnt_off'] - 8, desc)
    struct.pack_into('<HHI', hd, st['cnt_off'], len(tris), len(vb), len(data))
    dyn = np.zeros((len(P), 4), '<f4'); dyn[:, 0:3] = P; dyn[:, 3] = B[:, 0]
    raw = bytes(hd) + data + struct.pack('<I', 0) + struct.pack('<I', dyn.nbytes) + dyn.tobytes()
    raw, p0 = av_fix(raw); raw = bytearray(raw)
    struct.pack_into('<I', raw, p0, 0x0008000E)
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sh, alp)
    return n.add('BSDynamicTriShape', raw)
def merge(*gs):
    P = []; N = []; T = []; U = []; F = []; o = 0
    for g in gs:
        P.append(g[0]); N.append(g[1]); T.append(g[2]); U.append(g[3]); F.append(np.asarray(g[4]) + o); o += len(g[0])
    return np.vstack(P), np.vstack(N), np.vstack(T), np.vstack(U), np.vstack(F)
def cyl(a, b, r, seg=12, ulen=60.0, caps=True, r1=None, urep=1.0):
    """a round bar a -> b, radius r (r1 at b: a taper); the texture once round it (urep times), its length every ulen"""
    a = np.array(a, float); b = np.array(b, float); ax = b - a; L = np.linalg.norm(ax); ax /= L
    u = np.cross(ax, [1, 0, 0] if abs(ax[0]) < 0.9 else [0, 1, 0]); u /= np.linalg.norm(u); v = np.cross(ax, u)
    r1 = r if r1 is None else r1
    P = []; N = []; T = []; U = []; F = []
    for i in range(seg + 1):
        t = 2 * np.pi * i / seg; o = np.cos(t) * u + np.sin(t) * v
        for e, s, rr in ((a, 0, r), (b, 1, r1)): P.append(e + o * rr); N.append(o); T.append(ax); U.append((urep * i / seg, s * L / ulen))
    for i in range(seg): q = 2 * i; F += [(q, q + 3, q + 1), (q, q + 2, q + 3)]
    if caps:
        for e, sgn in ((a, -1), (b, 1)):
            if caps == 'a' and sgn > 0 or caps == 'b' and sgn < 0: continue   # one end only (the other meets its twin)
            c0 = len(P); P.append(e); N.append(ax * sgn); T.append(u); U.append((0.5, 0.5))
            re = r if sgn < 0 else r1
            for i in range(seg):
                t = 2 * np.pi * i / seg; P.append(e + (np.cos(t) * u + np.sin(t) * v) * re); N.append(ax * sgn); T.append(u); U.append((0.5 + 0.5 * np.cos(t) * 0.2, 0.5 + 0.5 * np.sin(t) * 0.2))
            for i in range(seg):
                i0, i1 = c0 + 1 + i, c0 + 1 + (i + 1) % seg
                F.append((c0, i0, i1) if sgn > 0 else (c0, i1, i0))
    return np.array(P), np.array(N), np.array(T), np.array(U), np.array(F)
def box(lo, hi):
    lo = np.array(lo, float); hi = np.array(hi, float); P = []; N = []; T = []; U = []; F = []
    for ax in range(3):
        for sgn in (-1, 1):
            nrm = np.zeros(3); nrm[ax] = sgn; a1, a2 = [i for i in range(3) if i != ax]
            c0 = len(P)
            for (s1, s2) in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = np.where(sgn > 0, hi, lo).astype(float).copy(); p[a1] = (lo, hi)[s1][a1]; p[a2] = (lo, hi)[s2][a2]
                P.append(p); N.append(nrm); tg = np.zeros(3); tg[a1] = 1; T.append(tg); U.append((s1 * 0.4, s2 * 0.4))
            F += [(c0, c0 + 1, c0 + 2), (c0, c0 + 2, c0 + 3)] if sgn > 0 else [(c0, c0 + 2, c0 + 1), (c0, c0 + 3, c0 + 2)]
    return np.array(P), np.array(N), np.array(T), np.array(U), np.array(F)
def sail(tack, clew, peak, throat, belly, reef, seed, nu=22, nv=17):
    """the cloth, in rig space: luff at the mast (x = kD), billowing to +x; never nearer the mast than kD"""
    tack, clew, peak, throat = [np.array(x, float) for x in (tack, clew, peak, throat)]
    rng = np.random.default_rng(seed); ph = rng.uniform(0, 6.3, 4)
    G = np.zeros((nv + 1, nu + 1, 3)); UV = np.zeros((nv + 1, nu + 1, 2))
    for j in range(nv + 1):
        s = j / nv
        for i in range(nu + 1):
            r = i / nu
            lo = tack + (clew - tack) * r; hi = throat + (peak - throat) * r
            p = lo + (hi - lo) * s
            prof = np.sin(np.pi * r ** 0.8) * (1.0 - 0.25 * r)
            fill = belly * prof * (0.75 + 0.45 * np.sin(np.pi * (0.15 + 0.7 * s)))
            twist = 6.0 * s * r ** 1.5                          # the head falls off to leeward
            wr = 1.1 * np.sin(9.0 * (r * 0.8 + s) + ph[0]) * np.sin(np.pi * r) * np.sin(np.pi * s)
            wr += 0.7 * np.sin(15.0 * (r - 0.6 * s) + ph[1]) * np.sin(np.pi * r) * np.sin(np.pi * s)
            fl = 1.5 * np.sin(18.0 * s + ph[2]) * r ** 4
            x = kD + fill + twist + wr + fl
            near = abs(p[1]) < 14.0                             # beside the mast (rig y 0): never closer than kD
            x = max(x, kD) if near else max(x, kD - 3.0)
            p[0] = x
            p[2] += -5.0 * np.sin(np.pi * r) * (1 - s) ** 8       # the foot sags between tack and clew
            p[1] += -3.0 * np.sin(np.pi * s) * r ** 6             # the leech curls in
            G[j, i] = p; UV[j, i] = (3.0 * (1 - r), 2.0 * (1 - s * reef))   # v1.7.82: panels: 3 across, 2 up
    P = G.reshape(-1, 3); U = UV.reshape(-1, 2); F = []
    for j in range(nv):
        for i in range(nu):
            q = j * (nu + 1) + i; F += [(q, q + nu + 2, q + 1), (q, q + nu + 1, q + nu + 2)]
    F = np.array(F)
    du = np.gradient(G, axis=1).reshape(-1, 3); dv = np.gradient(G, axis=0).reshape(-1, 3)
    N = np.cross(du, dv)
    return P, N, du, U, F

# ---- the parts (rig space: origin on the mast axis at z 0, +y the bow) ----
# every shape gets its own shader, texture set and alpha (shared shader blocks hang the game)
# v1.7.82: the rig's own textures (Miguel's 4/11 wood, rope and sailcloth), every shader from the matte floor one
WOODS = lambda: shader(FLOOR_SH, ['textures\\RowRim\\boat_sailwood_d.dds', 'textures\\RowRim\\boat_sailwood_n.dds'])
ROPES = lambda: shader(FLOOR_SH, ['textures\\RowRim\\boat_sailrope_d.dds', 'textures\\RowRim\\boat_sailrope_n.dds'])
SAILS = lambda: shader(FLOOR_SH, ['textures\\RowRim\\boat_sail_d.dds', 'textures\\RowRim\\boat_canvas_n.dds'], two_sided=True, vcol=False)
# v1.7.82: the mast tapers from 3 at the partner to 2.2 at the head, a round truck on top and the halyard's sheave block
mast = merge(cyl((0, 0, 8), (0, 0, 80), MR, 16, 90.0, urep=1.0), cyl((0, 0, 80), (0, 0, MTOP), MR, 16, 90.0, r1=2.2, urep=1.0),
             cyl((0, 0, MTOP), (0, 0, MTOP + 4.5), 3.0, 16, 20.0), box((-1.6, 2.0, MTOP - 16), (1.6, 4.6, MTOP - 6)))
partner = merge(box((-7, -22, 66), (7, -MR - 0.2, 72)), box((-7, MR + 0.2, 66), (7, 6, 72)), box((-7, -MR - 0.3, 66), (-MR - 0.2, MR + 0.3, 72)), box((MR + 0.2, -MR - 0.3, 66), (7, MR + 0.3, 72)))
TACK = (kD, -4.0, FOOT0); CLEW = (kD, -150.0, FOOT1)
b0 = np.array((kD, -4.0, FOOT0 - 2.5)); b1 = np.array((kD, -150.0, FOOT1 - 2.5)); bm = (b0 + b1) / 2
boom = merge(cyl(b0, bm, 1.6, 8, 60.0, r1=2.1, caps='a'), cyl(bm, b1, 2.1, 8, 60.0, r1=1.5, caps='b'))
def state(peak, throat, belly, reef, seed):
    t0 = np.array((throat[0], throat[1], throat[2] + 2.0)); t1 = np.array((peak[0], peak[1], peak[2] + 2.0)); tm = (t0 + t1) / 2
    yard = merge(cyl(t0, tm, 1.4, 8, 60.0, r1=2.0, caps='a'), cyl(tm, t1, 2.0, 8, 60.0, r1=1.3, caps='b'))
    hal = cyl((0.0, 3.3, MTOP - 11.0), (throat[0], throat[1] - 18.0, throat[2] + 6.0), 0.55, 6, 3.5, caps=False)
    return yard, hal, sail(TACK, CLEW, peak, throat, belly, reef, seed)
full = state((kD, -140.0, 335.0), (kD, 22.0, 272.0), 24.0, 1.0, 4)
reef = state((kD, -140.0, 282.0), (kD, 22.0, 236.0), 18.0, 0.55, 5)
# furled: the yard down on the boom, the cloth rolled along it and tied
roll = cyl((kD, -6.0, FOOT0 + 3.0), (kD, -146.0, FOOT1 + 3.0), 5.0, 14, 50.0, r1=4.4)
furlyard = cyl((kD, 20.0, FOOT0 + 6.5), (kD, -140.0, FOOT1 + 6.5), 1.8, 10, 60.0)
ties = merge(*[cyl((kD, y, FOOT0 - 3.5 + (FOOT1 - FOOT0) * (-y / 150.0)), (kD, y + 0.01, FOOT0 + 9.5 + (FOOT1 - FOOT0) * (-y / 150.0)), 5.6, 10, 4.0, urep=4.0) for y in (-30.0, -72.0, -114.0)])
def node(name, kids, hidden=False, T=(0, 0, 0)):
    nb = bytearray(node_block(n.name(name), T=T, kids=kids))
    struct.pack_into('<I', nb, 12, 0xF if hidden else 0xE)
    return n.add('NiNode', bytes(nb))
def mirror(g):
    P, N, Tg, UV, F = [np.array(x, float) for x in g[:4]] + [np.array(g[4])]
    P[:, 0] *= -1; N[:, 0] *= -1; Tg[:, 0] *= -1
    return P, N, Tg, UV, F[:, ::-1].copy()
# v1.7.78: each set state twice: on the rig's +x side (the boom out to starboard, the cloth bellying forward of it) and
# mirrored on -x (the boom out to port): the cloth always bellies downwind. The plugin fades across as the boom crosses
# the centre (gybe or tack). The boom goes with the cloth (the foot is laced to it).
def state_node(name, parts, port=False):
    yard, hal, cloth = parts
    bm = boom
    if port: yard, hal, cloth, bm = mirror(yard), mirror(hal), mirror(cloth), mirror(boom)
    return node(name, [shape(yard, WOODS(), alpha()), shape(hal, ROPES(), alpha()), dynshape(cloth, SAILS(), alpha()), shape(bm, WOODS(), alpha())])
nFull = state_node('RowRim_SailFull', full)
nReef = state_node('RowRim_SailReefed', reef)
nFullP = state_node('RowRim_SailFullP', full, True)
nReefP = state_node('RowRim_SailReefedP', reef, True)
nFurl = node('RowRim_SailFurled', [shape(roll, SAILS(), alpha()), shape(furlyard, WOODS(), alpha()), shape(ties, ROPES(), alpha()), shape(boom, WOODS(), alpha())])
rig = node('RowRim_SailRig', [nFull, nReef, nFurl, nFullP, nReefP])
# v1.7.75: the sheet: from the boom's end (the clew) to a cleat on the starboard gunwale by the helm. It must follow the
# boom as it swings, so it is RowRim_SailSheet0..15: short rope pieces the plugin lays end to end along the line (each
# long enough to overlap its neighbours at any boom angle: no stretching, no gaps). The cleat is fixed.
SHEET_N, SHEET_LEN = 16, 28.0
CLEAT = (60.0, -190.0 - MY, 74.0)   # model (60, -190, 74): the inner edge of the starboard gunwale, by the helm
cleat = merge(box((CLEAT[0] - 4, CLEAT[1] - 7, CLEAT[2] - 1), (CLEAT[0] + 3, CLEAT[1] + 7, CLEAT[2] + 2.5)),
              box((CLEAT[0] - 2.5, CLEAT[1] - 9, CLEAT[2] + 2.5), (CLEAT[0] + 1.5, CLEAT[1] + 9, CLEAT[2] + 4.5)))
sheet = [node('RowRim_SailSheet%d' % i, [shape(cyl((0.0, -SHEET_LEN / 2, 0.0), (0.0, SHEET_LEN / 2, 0.0), 0.65, 6, 4.0, caps=False), ROPES(), alpha())]) for i in range(SHEET_N)]
top = node('RowRim_Sail', [shape(mast, WOODS(), alpha()), shape(partner, WOODS(), alpha()), shape(cleat, WOODS(), alpha()), rig] + sheet, hidden=True, T=(0.0, MY, 0.0))
root_add_kids(n, [top])
open(OUT, 'wb').write(n.write())
print('->', OUT, 'sail added at y', MY, 'sheet pieces', SHEET_N)
