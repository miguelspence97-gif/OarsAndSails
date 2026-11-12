# RowRim v1.8.29: the sailing boat's anchor and its line, and the boom's jaw knots for either side.
# - SB_Anchor (NiNode, the plugin places it) holding SB_AnchorIron: the iron stock anchor (anchor.py, ~67 tall, its crown
#   at the origin, the shank up +z to the ring at z 62).
# - SB_AnchorBones: 31 bones the plugin moves (its line simulated in the world): 0..24 the rope (from the cathead), 24..30
#   the short chain, 30 at the anchor's ring. Bound in a straight line down from the origin: the rope 0..-120 (5 apart),
#   the chain on to -160.
# - SB_AnchorRope (rope, skinned), SB_AnchorChain (iron links, skinned): each vertex on the two bones either side of it.
#   The rope hidden in the nif (the plugin shows it once some is paid out).
# - SB_JawRopeS / SB_JawIronS: the knots and iron at the boom's and gaff's jaws (out of SB_BoomRope / SB_BoomIron) as made,
#   SB_JawRopeP / SB_JawIronP: the same mirrored across the mast (x flipped): the plugin shows the pair that suits the side
#   the boom is on (as the sails' S / P).
# Every shape its own shader, texture set and alpha (copies of SB_Rigging's / SB_Ironwork's; the skinned ones flagged).
# usage: python sb_anchor.py <sailboat nif> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape, node_block, e8
from hull_v144_lib import load, store, pos, components
from skinsail import skin_blocks
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {tri_shape(d, h, b)['name']: b['k'] for b in h['blocks'] if b['type'] == 'BSTriShape'}
nodes = {h['names'][struct.unpack_from('<i', d, b['start'])[0]]: b['k'] for b in h['blocks'] if b['type'] in ('NiNode', 'BSFadeNode')}
def shader_of(k):
    raw = n.blocks[k]; _, p = av_fix(raw)
    return struct.unpack_from('<2i', raw, p + 60 + 20)
RUST = 'textures\\RowRim\\SailBoat\\RowRim_AnchorRust_d.dds'
def tex_with_diffuse(raw, path):
    c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p + 4:p + 4 + l]); p += 4 + l
    out[0] = path.encode()
    return struct.pack('<I', c) + b''.join(struct.pack('<I', len(q)) + q for q in out)
def copy_shader(k, skinned=False, rust=False):
    sr, sp = av_fix(n.blocks[k], 4); sr = bytearray(sr)
    tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    ts = n.blocks[tex] if not rust else tex_with_diffuse(n.blocks[tex], RUST)
    struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', ts))
    if skinned:
        f1, = struct.unpack_from('<I', sr, 16); struct.pack_into('<I', sr, 16, f1 | 0x2)
    return n.add('BSLightingShaderProperty', sr)
def copy_alpha(k): return n.add('NiAlphaProperty', av_fix(n.blocks[k])[0]) if k >= 0 else -1
def add_kids(node_k, kids):
    nr = bytearray(n.blocks[node_k]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + list(kids)
    n.blocks[node_k] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])
def hide(k):
    r = bytearray(n.blocks[k]); fl, = struct.unpack_from('<I', r, 12); struct.pack_into('<I', r, 12, fl | 1); n.blocks[k] = bytes(r)
def rename(k, nm):
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm)); n.blocks[k] = bytes(r)
def smooth_nt(P, U, F):
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    fn = np.cross(b - a, c - a)
    key = np.round(P, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    acc = np.zeros((inv.max() + 1, 3))
    for i in range(3): np.add.at(acc, inv[F[:, i]], fn)
    N = acc[inv]; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    du1 = U[F[:, 1]] - U[F[:, 0]]; du2 = U[F[:, 2]] - U[F[:, 0]]
    r = du1[:, 0] * du2[:, 1] - du2[:, 0] * du1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    ft = ((b - a) * du2[:, 1:2] - (c - a) * du1[:, 1:2]) / r[:, None]
    Tg = np.zeros_like(P)
    for i in range(3): np.add.at(Tg, F[:, i], ft)
    Tg = Tg - N * (Tg * N).sum(1, keepdims=True)
    bad = np.linalg.norm(Tg, axis=1) < 1e-6
    Tg[bad] = np.cross(N[bad], [0.0, 0.0, 1.0]) + np.cross(N[bad], [1.0, 0.0, 0.0]) * 0.01
    Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    return N, Tg
root = 0
rig_sh, rig_al = shader_of(shapes['SB_Rigging'])
iron_sh, iron_al = shader_of(shapes['SB_Ironwork'])
tpl = n.blocks[shapes['SB_Ironwork']]
st, _, _ = load(tpl)
# ---------------- the anchor ----------------
AV = np.load('anchor_v.npy'); AF = np.load('anchor_f.npy')
AU = np.load('anchor_uv.npy')  # v1.8.29d: tiling UVs for the rust
AN, AT = smooth_nt(AV, AU, AF)
ak = build_shape(n, tpl, AV, AN, AT, AU, AF, copy_shader(iron_sh, rust=True), copy_alpha(iron_al)); rename(ak, 'SB_AnchorIron')
anode = n.add('NiNode', node_block(n.name('SB_Anchor'), kids=[ak]))
# ---------------- the line: bones ----------------
NB, C0 = 31, 24
binds = np.array([[0.0, 0.0, -5.0 * i] if i <= C0 else [0.0, 0.0, -120.0 - (i - C0) * 40.0 / (NB - 1 - C0)] for i in range(NB)])
bones = [n.add('NiNode', node_block(n.name('SB_AnchorBone%d' % i), T=tuple(binds[i]))) for i in range(NB)]
def weights_along(z):
    """each vertex on the two bones either side of its depth along the bind line"""
    s = -z; bz = -binds[:, 2]
    j = np.clip(np.searchsorted(bz, s) - 1, 0, NB - 2)
    w = np.clip((s - bz[j]) / (bz[j + 1] - bz[j]), 0.0, 1.0)
    idx = np.stack([j, j + 1, np.zeros_like(j), np.zeros_like(j)], 1)
    wts = np.stack([1 - w, w, np.zeros_like(w), np.zeros_like(w)], 1)
    return idx, wts
def skinned_shape(name, P, U, F, sh, al, hidden=False):
    N, Tg = smooth_nt(P, U, F)
    idx, wts = weights_along(P[:, 2])
    inst, desc = skin_blocks(n, e8, P, N, Tg, U, F, idx, wts, binds, bones, root)
    hd = bytearray(st['raw'][:st['data_off']])
    struct.pack_into('<4f', hd, st['bs_off'], 0.0, 0.0, -80.0, 2500.0)  # the line reaches far: a big bound
    struct.pack_into('<Q', hd, st['cnt_off'] - 8, desc)
    struct.pack_into('<HHI', hd, st['cnt_off'], 0, 0, 0)
    raw = bytes(hd) + struct.pack('<I', 0)
    raw, p0 = av_fix(raw); raw = bytearray(raw)
    struct.pack_into('<i', raw, 0, n.name(name))
    struct.pack_into('<I', raw, p0, 0x0008000E | (1 if hidden else 0))
    struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
    struct.pack_into('<3i', raw, p0 + 60 + 16, inst, sh, al)
    k = n.add('BSTriShape', raw)
    print(f'{name}: skinned, {len(F)} tris, {len(P)} verts, {NB} bones')
    return k
# the rope: a round strand 0.8 thick, rings every 2.5 down the 120 of the rope part (and 2 into the chain's first link)
SEG = 8; zs = np.arange(0.0, -122.01, -2.5); R = 0.8
P = []; U = []
for z in zs:
    for k in range(SEG + 1):
        a = 2 * np.pi * k / SEG
        P.append((R * np.cos(a), R * np.sin(a), z)); U.append((k / SEG * 0.25, -z / 12.0))
P = np.array(P); U = np.array(U); F = []
for i in range(len(zs) - 1):
    for k in range(SEG):
        a = i * (SEG + 1) + k; b = a + SEG + 1
        F += [(a, a + 1, b), (a + 1, b + 1, b)]
F = np.array(F)
rope = skinned_shape('SB_AnchorRope', P, U, F, copy_shader(rig_sh, True), copy_alpha(rig_al), hidden=True)
# the chain: oval links 5.6 long, 3.4 wide, wire 0.55, each turned 90 deg from the last, from z -120 down to the ring
P = []; U = []; F = []
L, Wd, r = 5.6, 3.4, 0.55; step = L - 2 * r - 0.3
zc = -120.0 - L / 2 + 0.6; li = 0
while zc > -160.0 + L / 2 - 0.6:
    base = len(P); NU, NV = 16, 6
    for i in range(NU):
        t = 2 * np.pi * i / NU
        # the link's centre line: a stadium (two half circles joined by straights), drawn as a squashed ellipse
        cx, cz = (Wd / 2 - r) * np.cos(t), (L / 2 - r) * np.sin(t)
        tx, tz = -(Wd / 2 - r) * np.sin(t), (L / 2 - r) * np.cos(t)
        tl = np.hypot(tx, tz); nx, nz = tz / tl, -tx / tl  # out of the loop, in its plane
        for j in range(NV):
            b = 2 * np.pi * j / NV
            off = np.array([nx * np.cos(b), 0.0, nz * np.cos(b)]) * r + np.array([0.0, np.sin(b), 0.0]) * r
            q = np.array([cx, 0.0, cz + zc]) + off
            if li % 2: q = np.array([q[1], q[0], q[2]])  # every other link turned about the chain
            P.append(q); U.append((i / NU * 0.6, j / NV * 0.15 + li * 0.37))
    for i in range(NU):
        for j in range(NV):
            a = base + i * NV + j; b = base + ((i + 1) % NU) * NV + j; c = base + ((i + 1) % NU) * NV + (j + 1) % NV; dd = base + i * NV + (j + 1) % NV
            F += [(a, b, c), (a, c, dd)]
    zc -= step; li += 1
P = np.array(P); U = np.array(U); F = np.array(F)
print(f'chain: {li} links')
chain = skinned_shape('SB_AnchorChain', P, U, F, copy_shader(iron_sh, True, rust=True), copy_alpha(iron_al), hidden=True)
box = n.add('NiNode', node_block(n.name('SB_AnchorBones'), kids=bones))
add_kids(root, [anode, box, rope, chain])
# ---------------- the jaw knots and iron, both ways ----------------
def split_jaw(name, outS, outP):
    k = shapes[name]
    stj, vb, Fj = load(n.blocks[k]); Pj = pos(vb).astype(float)
    jaw = np.zeros(len(Pj), bool)
    for c in components(Pj, Fj):
        if Pj[c, 1].min() > -30.0: jaw[c] = True
    tj = jaw[Fj].all(1)
    def sub(mask, tris):
        idx = np.where(mask)[0]; re = -np.ones(len(Pj), int); re[idx] = np.arange(len(idx)); return vb[idx].copy(), re[tris]
    vk, fk = sub(~jaw, Fj[~tj]); n.blocks[k] = store(stj, vk, fk)
    sh, al = shader_of(k)
    vs, fs = sub(jaw, Fj[tj])
    def mk(v, f, nm):
        raw = bytearray(store(stj, v, f)); r2, p = av_fix(bytes(raw)); raw = bytearray(r2)
        struct.pack_into('<i', raw, 0, n.name(nm)); struct.pack_into('<3i', raw, p + 60 + 16, -1, copy_shader(sh), copy_alpha(al))
        return n.add('BSTriShape', raw)
    s_k = mk(vs, fs, outS)
    vm = vs.copy(); pm = pos(vm).astype(float); pm[:, 0] *= -1.0
    vm[:, 0:12] = np.ascontiguousarray(pm.astype('<f4')).view(np.uint8).reshape(-1, 12)
    bx = vm[:, 12:16].copy().view('<f4').reshape(-1); vm[:, 12:16] = np.ascontiguousarray((-bx).astype('<f4')).view(np.uint8).reshape(-1, 4)
    vm[:, 20] = 255 - vm[:, 20]; vm[:, 24] = 255 - vm[:, 24]  # the normal's and tangent's x
    p_k = mk(vm, fs[:, ::-1], outP); hide(p_k)
    print(f'{name}: {jaw.sum()} of {len(Pj)} vertices to {outS} / {outP}')
    return [s_k, p_k]
jaws = split_jaw('SB_BoomRope', 'SB_JawRopeS', 'SB_JawRopeP') + split_jaw('SB_BoomIron', 'SB_JawIronS', 'SB_JawIronP')
add_kids(nodes['SB_MainRig'], jaws)
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(d):,} -> {len(out):,} bytes')
