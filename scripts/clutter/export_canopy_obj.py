# the canopy (RowRim_TentTarp + RowRim_TentPoles) out of a hull nif as an OBJ for Blender: hull model space, Z up,
# UVs (v flipped for OBJ), normals, one object each, a material pointing at the game's dds
import sys, struct, numpy as np
from nifp import parse, tri_shape
SRC, OUT, MTL = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(SRC, 'rb').read(); h = parse(d)
def xf(k):
    b = h['blocks'][k]; raw = d[b['start']:b['end']]; ne, = struct.unpack_from('<I', raw, 4); p = 4 + 4 + 4 * ne + 4 + 4
    return np.array(struct.unpack_from('<9f', raw, p + 12)).reshape(3, 3), np.array(struct.unpack_from('<3f', raw, p)), struct.unpack_from('<f', raw, p + 48)[0]
tent = [b['k'] for b in h['blocks'] if b['type'] == 'NiNode' and struct.unpack_from('<i', d, b['start'])[0] >= 0 and h['names'][struct.unpack_from('<i', d, b['start'])[0]] == 'RowRim_Clutter_Tent'][0]
lines = [f'# RowRim canopy from {SRC.split("/")[-1]}: hull model space, Z up, units = game units', f'mtllib {MTL.split("/")[-1]}']
base = 1; total = []
for b in h['blocks']:
    if b['type'] != 'BSTriShape': continue
    t = tri_shape(d, h, b)
    if t['name'] not in ('RowRim_TentTarp', 'RowRim_TentPoles'): continue
    V = np.frombuffer(d, np.uint8, t['nv'] * 32, t['data_off']).reshape(-1, 32)
    P = V[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    UV = V[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float)
    Nn = V[:, 20:23].astype(float) / 127.5 - 1; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-12
    I = np.frombuffer(d, np.uint16, t['ntri'] * 3, t['data_off'] + t['nv'] * 32).reshape(-1, 3)
    R, T, Sc = xf(b['k']); R2, T2, S2 = xf(tent)
    W = (P @ R.T * Sc + T) @ R2.T * S2 + T2; Wn = Nn @ R.T @ R2.T
    lines += [f'o {t["name"]}', f'usemtl {"canvas" if "Tarp" in t["name"] else "poles"}', 's 1']
    lines += [f'v {x:.4f} {y:.4f} {z:.4f}' for x, y, z in W]
    lines += [f'vt {u:.5f} {1 - v:.5f}' for u, v in UV]
    lines += [f'vn {x:.4f} {y:.4f} {z:.4f}' for x, y, z in Wn]
    lines += [f'f {a+base}/{a+base}/{a+base} {b2+base}/{b2+base}/{b2+base} {c+base}/{c+base}/{c+base}' for a, b2, c in I]
    base += len(W); total.append(f'{t["name"]} {len(W)} verts {len(I)} tris')
open(OUT, 'w', newline='').write('\r\n'.join(lines) + '\r\n')
open(MTL, 'w', newline='').write('\r\n'.join(['newmtl canvas', 'Kd 1 1 1', 'map_Kd ../Data/textures/RowRim/boat_tent_d.dds', 'map_Bump ../Data/textures/RowRim/boat_tent_n.dds',
                                               'newmtl poles', 'Kd 1 1 1', 'map_Kd ../Data/textures/RowRim/boat_tentpole_d.dds', '']))
print(OUT.split('/')[-1], '; '.join(total))
