# RowRim: the medieval sailing boat (PolyCrate, bought by Miguel; local test only, not for release without permission)
# as its own model, RowRim_SailBoat.nif. Parts from blender\sailboat\*.obj (decimated, UVs kept), turned into Skyrim
# model space: +y the bow, +z up, sized to the rowboat (110 units a metre: beam 194 like the rowboat's), the floorboards at
# z 34 (the lake at 32) so the stern bench top is at 67 (the rowboat's 66): the helm seat at (0, -222, 35).
# Every shape its own shader, texture set and alpha. No collision (the hull ref keeps the rowboat's).
# v1.7.90: the rig moves. SB_MainRig (at the mast, x -2 y 83) carries the boom, the main sail (a BSDynamicTriShape the
# plugin reshapes: fill, flutter, either side) and SB_Gaff (the gaff, lowered by the plugin to reef or stow the sail),
# all turned about the mast by the plugin (v1.7.96: each sail a thin solid skinned to a grid of bones the plugin moves:
# the belly to the lee side, the fill, the flapping); built straightened (the model's boom lay 23.75 deg to port). SB_JibRig at the
# jib's tack, its z axis up the forestay (the luff), the clew at its -y: the plugin swings it about the stay; the jib is a
# BSDynamicTriShape too. Everything else stays put.
# usage: python sailboat_nif.py <template hull nif> <dir with the part objs> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, node_block, av_fix, tex_set, e8
from hull_v144_lib import load, store
TPLNIF, SRC, OUT = sys.argv[1:4]
K = 110.0; XOFF = 0.30; ZOFF = 16.0   # v1.7.89: floorboards at z 36, above the lake (32): no water showing through the floor
def to_sk(V): return np.stack([-V[:, 2] * K, -(V[:, 0] + XOFF) * K, V[:, 1] * K + ZOFF], 1)
td = open(TPLNIF, 'rb').read(); tn = Nif(td)
def tex_paths(nn, k):
    raw = nn.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1')); p += 4 + l
    return out
TPL = BASE_SH = None
for k, t in enumerate(tn.ti):
    if tn.types[t] != 'BSLightingShaderProperty': continue
    ts, = struct.unpack_from('<i', tn.blocks[k], 40)
    if ts >= 0 and 'boat_floor_d' in tex_paths(tn, ts)[0].lower():
        for s2, t2 in enumerate(tn.ti):
            if tn.types[t2] == 'BSTriShape':
                ti = tri_shape(td, tn.h, tn.h['blocks'][s2])
                if ti['shader'] == k and ti['vsize'] == 32: TPL, BASE_SH = tn.blocks[s2], tn.blocks[k]; break
    if TPL: break
assert TPL
st, _, _ = load(TPL)
# a fresh nif: the template's header, no blocks; a BSFadeNode root first
n = Nif(td); n.blocks = []; n.ti = []
root = n.add('BSFadeNode', node_block(n.name('RowRim_SailBoat')))
def shader(paths, two_sided=False, skinned=False):
    t = n.add('BSShaderTextureSet', tex_set(paths))
    r, _ = av_fix(BASE_SH, 4); r = bytearray(r); struct.pack_into('<i', r, 40, t)
    f1, f2 = struct.unpack_from('<II', r, 16)
    f1 &= ~1                      # specular off (matte)
    if two_sided: f2 |= 0x10
    if skinned: f1 |= 0x2         # SLSF1_Skinned
    f2 &= ~0x20                   # no vertex colours
    struct.pack_into('<II', r, 16, f1, f2)
    return n.add('BSLightingShaderProperty', r)
def alpha(): return n.add('NiAlphaProperty', struct.pack('<iIiHB', -1, 0, -1, 0x00EC, 0))
def read_obj(path):
    V = []; T = []; F = []
    for l in open(path):
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('f '): F.append([tuple(int(x) - 1 for x in w.split('/')[:2]) for w in l.split()[1:4]])
    V = np.array(V, float); T = np.array(T, float)
    keys = {}; P = []; U = []; tris = []
    for f in F:
        tri = []
        for vi, ti in f:
            k = (vi, ti)
            if k not in keys: keys[k] = len(P); P.append(V[vi]); U.append(T[ti])
            tri.append(keys[k])
        tris.append(tri)
    return to_sk(np.array(P)), np.array(U), np.array(tris)
def normals_tangents(P, U, F):
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    fn = np.cross(b - a, c - a)
    # smooth normals by position (wedges split by UV share them)
    key = np.round(P, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    acc = np.zeros((inv.max() + 1, 3))
    for i in range(3): np.add.at(acc, inv[F[:, i]], fn)
    N = acc[inv]; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    du1 = U[F[:, 1]] - U[F[:, 0]]; du2 = U[F[:, 2]] - U[F[:, 0]]
    r = du1[:, 0] * du2[:, 1] - du2[:, 0] * du1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    ft = ((b - a) * du2[:, 1:2] - (c - a) * du1[:, 1:2]) / r[:, None]
    Tg = np.zeros_like(P)
    for i in range(3): np.add.at(Tg, F[:, i], ft)
    Tg -= N * (Tg * N).sum(1, keepdims=True)
    bad = np.linalg.norm(Tg, axis=1) < 1e-6
    Tg[bad] = np.cross(N[bad], [0, 0, 1]); bad = np.linalg.norm(Tg, axis=1) < 1e-6; Tg[bad] = [1, 0, 0]
    Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    return N, Tg
def shape(name, P, N, Tg, U, F, sh, alp):
    B = np.cross(N, Tg)
    vb = np.zeros((len(P), 32), np.uint8)
    vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    vb[:, 16:20] = np.ascontiguousarray(np.stack([U[:, 0], 1.0 - U[:, 1]], 1).astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 20:23] = e8(N); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2]); vb[:, 28:32] = 255
    raw = bytearray(store(st, vb, F)); raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
    struct.pack_into('<i', raw, 0, n.name(name))
    struct.pack_into('<I', raw, p0, 0x0008000E)
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sh, alp)
    return n.add('BSTriShape', raw)
# v1.7.88: the boat's own textures (textures\RowRim\SailBoat\<set>_d/_n.dds, made by sbtex.py); parts split by material
def TX(m): return ['textures\\RowRim\\SailBoat\\%s_d.dds' % m, 'textures\\RowRim\\SailBoat\\%s_n.dds' % m]
PARTS = [  # (part obj, shape name, texture set, two-sided)
    ('BigWoodenBoat03', 'SB_HullPlanks', 'BigWoodenBoat03', False), ('BigWoodenBoat02', 'SB_HullFrame', 'BigWoodenBoat02', False),
    ('BigWoodenBoat01_clean', 'SB_Spars', 'BigWoodenBoat01', False), ('RustMetal01', 'SB_Ironwork', 'RustMetal01', False),
    ('Rope02_clean', 'SB_Rigging', 'Rope02', False),  # v1.7.89: no rope coil at the bow, no rope round the (gone) life ring, no oars
    ('RicePaper01', 'SB_Sack', 'RicePaper01', False), ('BoatFabric01', 'SB_Blanket', 'BoatFabric01', True),
    ('BoatFabric02_Main', 'SB_SailMain', 'BoatFabric02', True), ('BoatFabric02_Jib', 'SB_SailJib', 'BoatFabric02', True),
    ('BoatFabric02_Rest', 'SB_SailTies', 'BoatFabric02', True)]
from rigclass import read, sk, classify, groups, MAST
import trimesh
TH0 = np.radians(23.75)                      # the model's boom angle to port: turned back to the centreline
def rz(a): c, s_ = np.cos(a), np.sin(a); return np.array([[c, -s_, 0], [s_, c, 0], [0, 0, 1.0]])
PIV = np.array([MAST[0], MAST[1], 0.0])
def to_rig(P): return (P - PIV) @ rz(TH0).T
def wedge(S, T, F, faces, xf=None):
    keys = {}; P = []; U = []; tris = []
    for i in faces:
        tri = []
        for vi, ti in F[i]:
            k = (vi, ti)
            if k not in keys: keys[k] = len(P); P.append(S[vi]); U.append(T[ti])
            tri.append(keys[k])
        tris.append(tri)
    P = np.array(P); P = xf(P) if xf is not None else P
    return P, np.array(U), np.array(tris)
def dynshape(name, P, U, F, sh, alp):
    N, Tg = normals_tangents(P, U, F); B = np.cross(N, Tg)
    vb = np.zeros((len(P), 12), np.uint8)
    vb[:, 0:4] = np.ascontiguousarray(np.stack([U[:, 0], 1.0 - U[:, 1]], 1).astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 4:7] = e8(N); vb[:, 7] = e8(B[:, 1]); vb[:, 8:11] = e8(Tg); vb[:, 11] = e8(B[:, 2])
    desc = 3 | (4 << 4) | (0 << 8) | (1 << 16) | (2 << 20) | (0x41A << 44)
    tris = np.asarray(F).astype('<u2'); data = vb.tobytes() + tris.tobytes()
    hd = bytearray(st['raw'][:st['data_off']])
    cen = P.mean(0); rad = float(np.linalg.norm(P - cen, axis=1).max()) + 60.0   # room to swing and billow
    struct.pack_into('<4f', hd, st['bs_off'], float(cen[0]), float(cen[1]), float(cen[2]), rad)
    struct.pack_into('<Q', hd, st['cnt_off'] - 8, desc)
    struct.pack_into('<HHI', hd, st['cnt_off'], len(tris), len(vb), len(data))
    dyn = np.zeros((len(P), 4), '<f4'); dyn[:, 0:3] = P; dyn[:, 3] = B[:, 0]
    raw = bytes(hd) + data + struct.pack('<I', 0) + struct.pack('<I', dyn.nbytes) + dyn.tobytes()
    raw, p0 = av_fix(raw); raw = bytearray(raw)
    struct.pack_into('<i', raw, 0, n.name(name))
    struct.pack_into('<I', raw, p0, 0x0008000E)
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sh, alp)
    print(f'{name}: dynamic, {len(F)} tris, {len(P)} verts, x {P[:,0].min():.0f}..{P[:,0].max():.0f} y {P[:,1].min():.0f}..{P[:,1].max():.0f} z {P[:,2].min():.0f}..{P[:,2].max():.0f}')
    return n.add('BSDynamicTriShape', raw)
def put(name, P, U, F, tx, two):
    assert len(P) < 65535, (name, len(P))
    N, Tg = normals_tangents(P, U, F)
    print(f'{name}: {len(F)} tris, {len(P)} verts, x {P[:,0].min():.0f}..{P[:,0].max():.0f} y {P[:,1].min():.0f}..{P[:,1].max():.0f} z {P[:,2].min():.0f}..{P[:,2].max():.0f}')
    return shape(name, P, N, Tg, U, F, shader(TX(tx), two), alpha())
def thick(P, U, F, half=0.35, mirror=False):
    """v1.7.95: the sail as a thin solid: two single-sided skins half each side of the cloth (a two-sided single
    surface flickered); mirror: the same sail bellied the other way (x flipped)."""
    P = np.array(P, float)
    F = np.array(F)
    if mirror: P[:, 0] *= -1; F = F[:, ::-1]
    N, _ = normals_tangents(P, U, F)
    P2 = np.vstack([P + N * half, P - N * half]); U2 = np.vstack([U, U])
    F2 = np.vstack([F, F[:, ::-1] + len(P)])
    return P2, U2, F2
from skinsail import sail_tu, bone_grid, skin_blocks
def skinned_sail(name, P, U, F, jib, R, C):
    """v1.7.96: one thin solid sail skinned to an R x C grid of bones (SB_<name>Bones: its children row-major, rows up
    the sail (t), columns luff to leech (u)); the plugin moves the bones: the wind side, the fill, the flapping"""
    t, u = sail_tu(P, jib)
    binds, idx, wts = bone_grid(P, t, u, R, C)
    P2, U2, F2 = thick(P, U, F)
    idx2 = np.vstack([idx, idx]); wts2 = np.vstack([wts, wts])
    N, Tg = normals_tangents(P2, U2, F2)
    bones = [n.add('NiNode', node_block(n.name('SB_%sBone%d' % (name, i)), T=tuple(binds[i]))) for i in range(len(binds))]
    sh = shader(TX('BoatFabric02'), False, skinned=True); al = alpha()
    inst, desc = skin_blocks(n, e8, P2, N, Tg, U2, F2, idx2, wts2, binds, bones, root)
    hd = bytearray(st['raw'][:st['data_off']])
    cen = P2.mean(0); rad = float(np.linalg.norm(P2 - cen, axis=1).max()) + 80.0
    struct.pack_into('<4f', hd, st['bs_off'], float(cen[0]), float(cen[1]), float(cen[2]), rad)
    struct.pack_into('<Q', hd, st['cnt_off'] - 8, desc)
    struct.pack_into('<HHI', hd, st['cnt_off'], 0, 0, 0)
    raw = bytes(hd) + struct.pack('<I', 0)
    raw, p0 = av_fix(raw); raw = bytearray(raw)
    struct.pack_into('<i', raw, 0, n.name('SB_' + name))
    struct.pack_into('<I', raw, p0, 0x0008000E)
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, inst, sh, al)
    shp = n.add('BSTriShape', raw)
    box = n.add('NiNode', node_block(n.name('SB_%sBones' % name), kids=bones))
    print(f'SB_{name}: skinned, {len(F2)} tris, {len(P2)} verts, {len(binds)} bones, belly x {binds[:,0].min():.0f}..{binds[:,0].max():.0f}')
    return [shp, box]
def node(name, kids, T=(0, 0, 0), R=np.eye(3)):
    return n.add('NiNode', node_block(n.name(name), R=R, T=T, kids=kids))
RIGGED = {'BigWoodenBoat01_clean': ('Spars', 'BigWoodenBoat01', 'Wood01'), 'RustMetal01': ('Iron', 'RustMetal01', 'Iron'), 'Rope02_clean': ('Rope', 'Rope02', 'Rope02')}
kids = []; boom = []; gaff = []; hal = []; total = 0
for src, name, tx, two in PARTS:
    if src.startswith('BoatFabric02'): continue          # the sails: below
    if src in RIGGED:
        tag, txs, mat = RIGGED[src]
        S, T, F, g = groups(f'{SRC}/{src}.obj', mat)
        if g.get('static'): kids.append(put(name, *wedge(S, T, F, g['static']), txs, two))
        if g.get('boom'): boom.append(put('SB_Boom' + tag, *wedge(S, T, F, g['boom'], to_rig), txs, two))
        if g.get('gaff'): gaff.append(put('SB_Gaff' + tag, *wedge(S, T, F, g['gaff'], to_rig), txs, two))
        if g.get('halyard'): hal.append(put('SB_Halyards', *wedge(S, T, F, g['halyard'], to_rig), txs, two))
        continue
    S, T, F = read(f'{SRC}/{src}.obj'); S = sk(S)
    kids.append(put(name, *wedge(S, T, F, range(len(F))), tx, two))
# the main sail, straightened into the rig
S, T, F = read(f'{SRC}/BoatFabric02_Main.obj'); S = sk(S)
mainP, mainU, mainF = wedge(S, T, F, range(len(F)), to_rig)
mainS = skinned_sail('SailMain', mainP, mainU, mainF, False, 5, 5)
gaffN = node('SB_Gaff', gaff)
mainN = node('SB_MainRig', boom + hal + mainS + [gaffN], T=tuple(PIV))
# the jib: its own frame on the forestay
S, T, F = read(f'{SRC}/BoatFabric02_Jib.obj'); J = sk(S)
tack = J[J[:, 1].argmax()]; head = J[J[:, 2].argmax()]; low = J[J[:, 2] < 260]; clew = low[low[:, 1].argmin()]
zj = head - tack; zj /= np.linalg.norm(zj); cv = clew - tack; cv -= zj * (cv @ zj); yj = -cv / np.linalg.norm(cv); xj = np.cross(yj, zj)
RJ = np.stack([xj, yj, zj], 1)            # columns: the jib frame's axes in boat space
jibP, jibU, jibF = wedge(J, T, F, range(len(F)), lambda P: (P - tack) @ RJ)
jibS = skinned_sail('SailJib', jibP, jibU, jibF, True, 5, 4)
jibN = node('SB_JibRig', jibS, T=tuple(tack), R=RJ)
print('jib frame: tack', np.round(tack, 1), 'luff', np.round(zj, 3), 'clew dir', np.round(-yj, 3))
kids += [mainN, jibN]
n.blocks[root] = node_block(n.name('RowRim_SailBoat'), kids=kids)
open(OUT, 'wb').write(n.write())
print('->', OUT)
