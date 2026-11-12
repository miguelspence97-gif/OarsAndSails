# RowRim v1.8.31: the sailing boat's rudder and tiller (Miguel's sketch).
# - SB_RudderRig (NiNode at the hinge's foot, the plugin turns it about the sternpost): SB_Rudder, a triangular blade hung
#   on the sternpost, its head just under the stern box (bottom z 65.7), and SB_RudderIron, its three straps.
# - SB_TillerRig (NiNode at the tiller's post, the plugin turns it about +z), on the stern bench at the helmsman's right:
#   SB_TillerBase (a black iron plate), SB_Tiller (the iron stem and cap), SB_TillerHandle (a flat wooden handle pointing
#   at the bow at rest; v1.8.31b, Miguel's picture; was an Imperial L all in cast iron).
# Textures: RowRim_RudderWood_d (the espresso grain toned to the hull's wood), RowRim_CastIron_d (the weathered cast
# iron), RowRim_IronBlack_d (it blackened), RowRim_Flat_n. Each shape its own shader, texture set and alpha (copies of SB_HullFrame's / SB_Ironwork's).
# UVs per face, projected on the face's main axis (planar per side); normals smooth.
# usage: python sb_rudder.py <sailboat nif> <out nif>
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape, node_block
from hull_v144_lib import load
import rudder_build as RB
SRC, OUT = sys.argv[1:3]
RB._S = RB.shapes(SRC)['SB_HullPlanks']
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
shapes = {tri_shape(d, h, b)['name']: b['k'] for b in h['blocks'] if b['type'] == 'BSTriShape'}
def shader_of(k):
    raw = n.blocks[k]; _, p = av_fix(raw)
    return struct.unpack_from('<2i', raw, p + 60 + 20)
def tex_set(raw, diffuse, normal):
    c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p + 4:p + 4 + l]); p += 4 + l
    out[0] = diffuse.encode(); out[1] = normal.encode()
    return struct.pack('<I', c) + b''.join(struct.pack('<I', len(q)) + q for q in out)
T = 'textures\\RowRim\\SailBoat\\'
def copy_shader(k, diffuse):
    sr, sp = av_fix(n.blocks[k], 4); sr = bytearray(sr)
    tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', tex_set(n.blocks[tex], T + diffuse, T + 'RowRim_Flat_n.dds')))
    return n.add('BSLightingShaderProperty', sr)
def copy_alpha(k): return n.add('NiAlphaProperty', av_fix(n.blocks[k])[0]) if k >= 0 else -1
def rename(k, nm):
    r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name(nm)); n.blocks[k] = bytes(r)
def add_kids(node_k, kids):
    nr = bytearray(n.blocks[node_k]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', nr, q); old = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + list(kids)
    n.blocks[node_k] = bytes(nr[:q]) + struct.pack('<I', len(old)) + struct.pack('<%di' % len(old), *old) + bytes(nr[q + 4 + 4 * nc:])
def unweld_uv(P, F, scale, turn_grain=False):
    """one vertex per corner, UVs projected per face on its main axis; normals smooth over shared positions, tangents per face"""
    V = P[F].reshape(-1, 3); FF = np.arange(len(V)).reshape(-1, 3)
    fn = np.cross(V[FF[:, 1]] - V[FF[:, 0]], V[FF[:, 2]] - V[FF[:, 0]])
    ax = np.argmax(np.abs(fn), 1)
    UV = np.zeros((len(V), 2))
    for a, (i, j) in enumerate(((1, 2), (0, 2), (0, 1))):
        m = np.repeat(ax == a, 3); UV[m] = V[m][:, [i, j]] / scale
    UV[:, 1] = -UV[:, 1]
    if turn_grain: UV = UV[:, ::-1].copy()  # v1.8.31b: the grain runs along the texture's v: turned to lie fore and aft
    key = np.round(V, 3); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    acc = np.zeros((inv.max() + 1, 3))
    for i in range(3): np.add.at(acc, inv[FF[:, i]], fn)
    N = acc[inv]; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    a, b, c = V[FF[:, 0]], V[FF[:, 1]], V[FF[:, 2]]
    du1 = UV[FF[:, 1]] - UV[FF[:, 0]]; du2 = UV[FF[:, 2]] - UV[FF[:, 0]]
    r = du1[:, 0] * du2[:, 1] - du2[:, 0] * du1[:, 1]; r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    ft = ((b - a) * du2[:, 1:2] - (c - a) * du1[:, 1:2]) / r[:, None]
    Tg = np.repeat(ft, 3, 0); Tg -= N * (Tg * N).sum(1, keepdims=True)
    bad = np.linalg.norm(Tg, axis=1) < 1e-6; Tg[bad] = np.cross(N[bad], [0.0, 0.0, 1.0]) + 1e-3
    Tg /= np.linalg.norm(Tg, axis=1, keepdims=True) + 1e-9
    return V, N, Tg, UV, FF
tpl = n.blocks[shapes['SB_Ironwork']]
wood_sh, wood_al = shader_of(shapes['SB_HullFrame'])
iron_sh, iron_al = shader_of(shapes['SB_Ironwork'])
# the rudder, in its rig's frame (the hinge's foot at the origin)
RV, RF, RI = RB.rudder_d(0.0)
H0, AX = RB.RUDDER_H0, RB.RUDDER_AX
kids = []
for mask, nm, tex, sh, al, sc in ((~RI, 'SB_Rudder', 'RowRim_RudderWood_d.dds', wood_sh, wood_al, 60.0), (RI, 'SB_RudderIron', 'RowRim_CastIron_d.dds', iron_sh, iron_al, 30.0)):
    V, N, Tg, UV, FF = unweld_uv(RV - H0, RF[mask], sc, nm == 'SB_Rudder')  # v1.8.31b: the rudder's grain horizontal
    k = build_shape(n, tpl, V, N, Tg, UV, FF, copy_shader(sh, tex), copy_alpha(al)); rename(k, nm); kids.append(k)
    print(nm, len(V), 'vertices', len(FF), 'triangles')
rr = n.add('NiNode', node_block(n.name('SB_RudderRig'), T=tuple(H0), kids=kids))
# v1.8.31b (Miguel's picture): the tiller in its rig's frame (its post's foot at the origin): a black iron base plate, an
# iron stem and cap, a flat wooden handle pointing at the bow at rest
TV, TF, TP = RB.tiller_f(0.0)
TB = np.array([RB.TILLER_BASE[0], RB.TILLER_BASE[1], 0.0])
tk = []
for part, nm, tex, sh, al, sc in ((0, 'SB_TillerBase', 'RowRim_IronBlack_d.dds', iron_sh, iron_al, 20.0), (1, 'SB_Tiller', 'RowRim_CastIron_d.dds', iron_sh, iron_al, 20.0),
                                  (2, 'SB_TillerHandle', 'RowRim_RudderWood_d.dds', wood_sh, wood_al, 40.0)):
    V, N, Tg, UV, FF = unweld_uv(TV - TB, TF[TP == part], sc)
    k = build_shape(n, tpl, V, N, Tg, UV, FF, copy_shader(sh, tex), copy_alpha(al)); rename(k, nm); tk.append(k)
    print(nm, len(V), 'vertices', len(FF), 'triangles')
tr = n.add('NiNode', node_block(n.name('SB_TillerRig'), T=tuple(TB), kids=tk))
add_kids(0, [rr, tr])
print('rudder hinge foot', np.round(H0, 3).tolist(), 'axis', np.round(AX, 5).tolist(), '| tiller post', TB.tolist())
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out), 'bytes')
