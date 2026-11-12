# RowRim v1.8.32: the sailing boat made solid (RULES.md: every part a closed 3D solid, right way out).
# Measured on v1.8.31d: our ropes (sb_ropes), the furl bundles and their ties, the tack line, the lashings, the jaw knots,
# the rudder's straps, the anchor's line and chain links were built inside out (wound inward: the game culls the faces
# turned away, so they looked see-through), rope and spar ends were open, and the sails were two skins 0.7 apart with
# open edges. This pass, on every shape:
# - caps each open end (boundary loops up to 64 vertices and under 16 across) with a fan of its own vertices (the cap's
#   normal), wound with the part;
# - turns every closed part that is inside out the right way (its triangles flipped; its normals too where they pointed in);
# - the sails (two skins): joins the two skins round every edge with a strip;
# - SB_MastLashings out of SB_MainRig onto the root (they stay put round the mast, only the boom and gaff swing);
# - SB_JibSheet a round rope (r 0.85, 12 sides, capped, closed), bound along +z 0..100 (the plugin turns its two bones to
#   the sheet's line).
# Left open on purpose: RowRim_Floorboards' undersides (on the bilge) and the hull model's own big outlines.
# usage: python sb_solid.py <in nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, e8
from hull_v144_lib import load, store, pos
from skinsail import skin_blocks
from skinread import skinned_geom
from solidlib import cap_loops, right_way_out, face_normals_at, weld, boundary
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {tri_shape(d, h, b)['name']: b['k'] for b in h['blocks'] if b['type'] == 'BSTriShape'}
def un8(x): return x.astype(float) / 127.5 - 1.0
def flip_normals(vb, m, off_bx):
    """negate the normal (and the bitangent with it) of the vertices in m: 32 / 40-byte layout share the offsets"""
    Nn = -un8(vb[m, 20:23]); vb[m, 20:23] = e8(Nn)
    vb[m, 23] = e8(-un8(vb[m, 23:24]))[:, 0]; vb[m, 27] = e8(-un8(vb[m, 27:28]))[:, 0]
    bx = vb[m, 12:16].copy().view('<f4').ravel(); vb[m, 12:16] = (-bx).astype('<f4').view(np.uint8).reshape(-1, 4)
def solidify(vb, F, name, max_span=16.0):
    """vb: vertex rows (32 or 40 bytes); returns (vb, F, caps, flipped parts)"""
    P = vb[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float); F = np.asarray(F, int)
    caps = cap_loops(P, F, max_span=max_span); newv = []; newf = []; base = len(vb)
    for ring in caps:
        RP = P[ring]; c = RP.mean(0); m = len(ring)
        nrm = np.zeros(3)
        for i in range(m): nrm += np.cross(RP[i] - c, RP[(i + 1) % m] - c)
        if np.linalg.norm(nrm) < 1e-12: continue
        nrm /= np.linalg.norm(nrm)
        tg = np.cross(nrm, [0.0, 0.0, 1.0])
        if np.linalg.norm(tg) < 1e-3: tg = np.cross(nrm, [1.0, 0.0, 0.0])
        tg /= np.linalg.norm(tg); bt = np.cross(nrm, tg)
        rows = vb[ring].copy()
        rows[:, 20:23] = e8(np.tile(nrm, (m, 1))); rows[:, 24:27] = e8(np.tile(tg, (m, 1)))
        rows[:, 23] = e8(np.full((m, 1), bt[1]))[:, 0]; rows[:, 27] = e8(np.full((m, 1), bt[2]))[:, 0]
        rows[:, 12:16] = np.frombuffer(np.float32(bt[0]).tobytes() * m, np.uint8).reshape(m, 4)
        cen = rows[0].copy(); cen[0:12] = np.frombuffer(c.astype('<f4').tobytes(), np.uint8)
        uv = rows[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float).mean(0); cen[16:20] = np.frombuffer(uv.astype('<f2').tobytes(), np.uint8)
        i0 = base + sum(len(a) for a in newv)
        newv.append(np.vstack([rows, cen[None]]))
        for i in range(m): newf.append([i0 + m, i0 + i, i0 + (i + 1) % m])  # runs ring[i] -> ring[i+1]: the cap's order
    if newv: vb = np.vstack([vb] + newv); F = np.vstack([F, np.array(newf, int)])
    P = vb[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    F2, flipped = right_way_out(P, F)
    nflip = 0
    if flipped.any():
        # normals pointing in on a flipped part: turn them out too
        fnv = face_normals_at(P, F2); Nn = un8(vb[:, 20:23])
        inward = flipped & (np.einsum('ij,ij->i', fnv, Nn) < 0)
        if inward.any(): flip_normals(vb, inward, 12)
        nflip = int(flipped.sum())
    return vb, F2, len(newv), nflip
report = []
for name in sorted(shapes):
    if not name.startswith('SB_') and name != 'RowRim_Floorboards': continue
    if name == 'RowRim_Floorboards': continue  # undersides on the bilge: left
    k = shapes[name]
    st, vb, F = load(n.blocks[k])
    if len(vb) == 0 or vb.shape[1] != 32: continue
    vb2, F2, nc, nf = solidify(vb.copy(), F, name)
    if nc or nf:
        assert len(vb2) < 65535 and len(F2) < 65535, name
        n.blocks[k] = store(st, vb2, F2); report.append(f'{name}: {nc} end(s) capped, {nf} vertices on parts turned right way out')
# ---- the skinned shapes ----
def skin_parts(k):
    raw = n.blocks[k]; _, p0 = av_fix(raw); inst, sh, al = struct.unpack_from('<3i', raw, p0 + 60 + 16)
    ir = n.blocks[inst]; sd_id, sp_id, root_id, nb = struct.unpack_from('<iiiI', ir, 0); bones = list(struct.unpack_from('<%di' % nb, ir, 16))
    sdr = n.blocks[sd_id]; off = 36 + 12 + 4 + 4 + 1; binds = []
    for b in range(nb):
        binds.append(-np.array(struct.unpack_from('<3f', sdr, off + 36)))
        nv, = struct.unpack_from('<H', sdr, off + 36 + 12 + 4 + 16); off += 36 + 12 + 4 + 16 + 2 + nv * 6
    return raw, p0, bones, root_id, np.array(binds)
def skin_in_place(k, P, N, Tg, U, F, idx, wts, binds, bones, root_id):
    """the shape's skin rebuilt into its own NiSkinInstance / NiSkinData / NiSkinPartition blocks (no orphans left)"""
    raw = n.blocks[k]; _, p0 = av_fix(raw); inst, = struct.unpack_from('<i', raw, p0 + 60 + 16)
    old_sd, old_sp = struct.unpack_from('<ii', n.blocks[inst], 0)
    L0 = len(n.blocks)
    ninst, _ = skin_blocks(n, e8, P, N, Tg, U, F, idx, wts, binds, bones, root_id)
    assert ninst == L0 + 2 and len(n.blocks) == L0 + 3
    n.blocks[old_sd] = n.blocks[L0]; n.blocks[old_sp] = n.blocks[L0 + 1]
    n.blocks[inst] = struct.pack('<ii', old_sd, old_sp) + n.blocks[L0 + 2][8:]
    del n.blocks[L0:]; del n.ti[L0:]
def write_skinned(k, vb, F, bones, root_id, binds, raw, p0):
    P = vb[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    U = vb[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float); U[:, 1] = 1.0 - U[:, 1]
    N = un8(vb[:, 20:23]); Tg = un8(vb[:, 24:27])
    wts = vb[:, 28:36].copy().view('<f2').reshape(-1, 4).astype(float); idx = vb[:, 36:40].astype(int)
    skin_in_place(k, P, N, Tg, U, F, idx, wts, binds, bones, root_id)
for name in ('SB_SailMainS', 'SB_SailMainP', 'SB_SailJibS', 'SB_SailJibP'):
    k = shapes[name]; P, F, vb = skinned_geom(n, k); raw, p0, bones, root_id, binds = skin_parts(k)
    half = len(P) // 2; w = np.arange(len(P))
    be = [(x, y) for x, y in boundary(F, w) if x < half and y < half]
    strip = []
    for x, y in be: strip += [[y, x, x + half], [y, x + half, y + half]]
    F2 = np.vstack([F, np.array(strip, int)])
    write_skinned(k, vb.copy(), F2, bones, root_id, binds, raw, p0)
    report.append(f'{name}: the two skins joined round {len(be)} edges')
for name in ('SB_AnchorRope', 'SB_AnchorChain'):
    k = shapes[name]; P, F, vb = skinned_geom(n, k); raw, p0, bones, root_id, binds = skin_parts(k)
    vb2, F2, nc, nf = solidify(vb.copy(), F, name)
    write_skinned(k, vb2, F2, bones, root_id, binds, raw, p0)
    report.append(f'{name}: {nc} end(s) capped, {nf} vertices on parts turned right way out')
# ---- the mast lashings onto the root ----
def kids_of(k):
    r = n.blocks[k]; ne, = struct.unpack_from('<I', r, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', r, q); return q, list(struct.unpack_from('<%di' % nc, r, q + 4))
def set_kids(k, kids):
    r = n.blocks[k]; q, old = kids_of(k)
    n.blocks[k] = bytes(r[:q]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(r[q + 4 + 4 * len(old):])
def node_named(nm):
    for k, b in enumerate(n.h['blocks']):
        if 'Node' in b['type'] and n.names[struct.unpack_from('<i', n.blocks[k], 0)[0]] == nm: return k
rig = node_named('SB_MainRig'); lk = shapes['SB_MastLashings']
_, rk = kids_of(rig)
if lk in rk:
    set_kids(rig, [c for c in rk if c != lk]); _, root = kids_of(0); set_kids(0, root + [lk])
    r = n.blocks[rig]; ne, = struct.unpack_from('<I', r, 4); RT = np.array(struct.unpack_from('<3f', r, 4 + 4 + 4 * ne + 4 + 4))
    st, vb, F = load(n.blocks[lk]); P = pos(vb).astype(float) + RT
    vb = vb.copy(); vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12); n.blocks[lk] = store(st, vb, F)
    report.append(f'SB_MastLashings: onto the root (moved by {RT.tolist()})')
# ---- the jib sheet: a round, closed rope along +z, 0..100 ----
k = shapes['SB_JibSheet']; raw, p0, bones, root_id, _ = skin_parts(k)
SIDES, R, LEN, NR = 12, 0.85, 100.0, 61
P = []; N = []; U = []
for i in range(NR):
    z = LEN * i / (NR - 1); fade = min(1.0, i / 3.0, (NR - 1 - i) / 3.0)
    for j in range(SIDES):
        a = 2 * np.pi * j / SIDES; rr = R + fade * 0.08 * np.cos(3 * a + z * 2 * np.pi / (6 * R))  # a three-strand lay
        P.append([rr * np.cos(a), rr * np.sin(a), z]); N.append([np.cos(a), np.sin(a), 0.0]); U.append([j / SIDES, z / 12.0])
F = []
for i in range(NR - 1):
    for j in range(SIDES):
        a = i * SIDES + j; b2 = i * SIDES + (j + 1) % SIDES; c = a + SIDES; dd = b2 + SIDES; F += [[a, b2, c], [b2, dd, c]]
for i, sgn in ((0, -1.0), (NR - 1, 1.0)):
    c0 = len(P); P.append([0, 0, LEN * i / (NR - 1)]); N.append([0, 0, sgn]); U.append([0.5, 0.5])
    for j in range(SIDES):
        a, b2 = i * SIDES + j, i * SIDES + (j + 1) % SIDES
        F.append([c0, b2, a] if sgn < 0 else [c0, a, b2])
P = np.array(P, float); N = np.array(N, float); U = np.array(U, float); F = np.array(F, int)
T = np.cross(N, [0.0, 0.0, 1.0]); bad = np.linalg.norm(T, axis=1) < 1e-6; T[bad] = [1.0, 0.0, 0.0]; T /= np.linalg.norm(T, axis=1, keepdims=True)
wz = np.clip(P[:, 2] / LEN, 0, 1)
idx = np.stack([np.zeros(len(P), int), np.ones(len(P), int), np.zeros(len(P), int), np.zeros(len(P), int)], 1)
wts = np.stack([1 - wz, wz, np.zeros(len(P)), np.zeros(len(P))], 1)
skin_in_place(k, P, N, T, U, F, idx, wts, np.array([[0.0, 0.0, 0.0], [0.0, 0.0, LEN]]), bones, root_id)
report.append(f'SB_JibSheet: a round closed rope, {len(P)} vertices, {len(F)} triangles, bound along +z 0..{LEN:.0f}')
print('\n'.join(report))
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(d), '->', len(out), 'bytes')
