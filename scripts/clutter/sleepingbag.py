# v1.7.48: the sleeping bag in the hull (RowRim_Clutter_SleepingBag), drawn where the bed lies in place of the game's
# bedroll mesh (the plugin moves this node onto the bed's spot and shows it only while the bed is laid out).
# Model: "Sleeping bag" (https://skfb.ly/6xZyn) by Tactical_Beard, CC BY 4.0
# (http://creativecommons.org/licenses/by/4.0/). Converted to NIF, rescaled to the vanilla bedroll, retextured.
#   python sleepingbag.py <hull in> <hull out> <model.dae>
import sys, os, re, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nifmerge import Nif, node_block, av_fix, e8, tex_set, root_add_kids
from nifp import tri_shape
from hull_v144_lib import load, store
SRC, OUT, DAE = sys.argv[1:4]
d = open(SRC, 'rb').read(); n = Nif(d)
assert 'RowRim_Clutter_SleepingBag' not in n.names, 'the sleeping bag is already in this hull'
# the floorboards' shape head and shader (found by texture), as fishbucket.py
def tex_paths(k):
    raw = n.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1').lower()); p += 4 + l
    return out
tpl = shd = None
for k, t in enumerate(n.ti):
    if n.types[t] != 'BSLightingShaderProperty' or k >= n.h['nb']: continue
    ts, = struct.unpack_from('<i', n.blocks[k], 40)
    if ts >= 0 and any('boat_floor_d' in q for q in tex_paths(ts)):
        for s2, t2 in enumerate(n.ti):
            if s2 < n.h['nb'] and n.types[t2] == 'BSTriShape':
                ti = tri_shape(d, n.h, n.h['blocks'][s2])
                if ti['shader'] == k and ti['vsize'] == 32: tpl, shd = n.blocks[s2], n.blocks[k]; break
    if tpl: break
assert tpl, 'no floorboards in this hull'
s = open(DAE).read()
g = lambda key, m: np.array(re.search(r'id="meshId0-%s-array" count="\d+">([^<]*)<' % key, s).group(1).split(), float).reshape(-1, m)
P = g('positions', 3); Nn = g('normals', 3); Tg = g('tangents', 3); UV = g('tex0', 2)
F = np.array(re.search(r'<p>([^<]*)</p>', s).group(1).split(), int).reshape(-1, 3)
# dae Y up, length on Z (lining half at -Z) -> Skyrim: x = X, y = -Z, z = Y; the vanilla bedroll's size (177 long, base at 0)
R = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]], float)
SC = 88.5
V = (P @ R.T) * SC; V[:, 2] -= V[:, 2].min()
Nn = Nn @ R.T; Tg = Tg @ R.T
Nn /= np.linalg.norm(Nn, axis=1, keepdims=True)
Tg = Tg - Nn * (Tg * Nn).sum(1, keepdims=True); Tg /= np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-6)
B = np.cross(Nn, Tg)
UVn = np.c_[UV[:, 0], 1.0 - UV[:, 1]]
# winding: face normals agree with vertex normals
fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
bad = (fn * (Nn[F[:, 0]] + Nn[F[:, 1]] + Nn[F[:, 2]])).sum(1) < 0
F[bad] = F[bad][:, [0, 2, 1]]
st, _, _ = load(tpl)
vb = np.zeros((len(V), 32), np.uint8)
vb[:, 0:12] = np.ascontiguousarray(V.astype('<f4')).view(np.uint8).reshape(-1, 12)
vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
vb[:, 16:20] = np.ascontiguousarray(UVn.astype('<f2')).view(np.uint8).reshape(-1, 4)
vb[:, 20:23] = e8(Nn); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2]); vb[:, 28:32] = 255
raw = bytearray(store(st, vb, F)); raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
t = n.add('BSShaderTextureSet', tex_set(['textures\\RowRim\\boat_sleepingbag_d.dds', 'textures\\RowRim\\boat_sleepingbag_n.dds']))
r2, _ = av_fix(shd, 4); r2 = bytearray(r2); struct.pack_into('<i', r2, 40, t)
f1, = struct.unpack_from('<I', r2, 16); struct.pack_into('<I', r2, 16, f1 & ~1)   # v1.7.50: matte (Specular off): it shone like plastic
sp = n.add('BSLightingShaderProperty', r2)
struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sp, -1)
shape = n.add('BSTriShape', raw)
# the node starts hidden (flags 0xF, app culled): the plugin shows it on the bed's spot while the bed is laid out
nb = bytearray(node_block(n.name('RowRim_Clutter_SleepingBag'), kids=[shape])); struct.pack_into('<I', nb, 12, 0xF)
node = n.add('NiNode', bytes(nb))
root_add_kids(n, [node])
open(OUT, 'wb').write(n.write())
print('sleeping bag: verts', len(V), 'tris', len(F), 'bounds', V.min(0).round(1), V.max(0).round(1), '->', OUT)
