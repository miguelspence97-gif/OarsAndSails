# RowRim longship (Viking longship by massive-graphisme, Sketchfab, CC BY 4.0): cut the per-material OBJs (split_dae.py)
# into the parts the nif needs, then decimate each (pymeshlab, UVs kept). Model units: x along (stern +x), y up, z across.
#  oars:   the 30 rowing oars (mastsandpaddles, sticking out past the gunwale) -> their own shape, the plugin can hide them
#  yard:   the yard spar + the robands lashing the sail to it (ropes along the yard) -> the sail rig
#  sail:   the square sail (the big sail piece); the folded spare sail on the crutches stays static (sail_spare)
import numpy as np, trimesh, sys, pymeshlab
def rd(p):
    V = []; T = []; F = []
    for l in open(p):
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('f '): F.append([tuple(int(x) for x in w.split('/')[:2]) for w in l.split()[1:4]])
    return np.array(V, float), T, F
def cut(mat, rules):
    V, T, F = rd(f'parts/{mat}.obj')
    Fv = np.array([[a[0] - 1 for a in f] for f in F])
    lab = trimesh.graph.connected_component_labels(trimesh.Trimesh(V, Fv, process=False).face_adjacency, len(F))
    out = {}
    for c in range(lab.max() + 1):
        fi = np.where(lab == c)[0]; vv = V[np.unique(Fv[fi])]; lo, hi = vv.min(0), vv.max(0)
        name = next(n for n, fn in rules if fn(lo, hi, len(fi)))
        out.setdefault(name, []).extend(fi.tolist())
    for name, fi in out.items():
        with open(f'cut/{name}.obj', 'w') as o:
            o.write(''.join(f'v {x} {y} {z}\n' for x, y, z in V)); o.write(''.join(f'vt {u} {v}\n' for u, v in T))
            o.write(''.join('f ' + ' '.join(f'{a}/{b}' for a, b in F[i]) + '\n' for i in sorted(fi)))
        print(mat, '->', name, len(fi))
ALL = lambda lo, hi, n: True
cut('mastsandpaddles', [('oars', lambda lo, hi, n: n == 194 and max(abs(lo[2]), abs(hi[2])) > 0.5),
                        ('yard', lambda lo, hi, n: lo[1] > 0.3 and hi[2] - lo[2] > 0.8), ('spars', ALL)])
cut('ropes', [('yardropes', lambda lo, hi, n: lo[1] > 0.33 and hi[1] < 0.38 and hi[2] - lo[2] < 0.05), ('ropes', ALL)])
cut('sail', [('sail', lambda lo, hi, n: n > 500), ('sail_spare', ALL)])
for m in ['ldhull', 'bridge', 'quillandcarvings', 'shield1', 'shield2', 'shields3', 'bucket', 'logs', 'bench', 'bench2']:
    open(f'cut/{m}.obj', 'w').write(open(f'parts/{m}.obj').read())
# v1.8.8c: Miguel wants the original mesh: no decimation (targets kept for reference; FULL = 1 keeps every triangle)
FULL = 1
# decimation targets (49.4k tris in all -> about 23k)
TGT = {'ldhull': 4200, 'quillandcarvings': 2600, 'ropes': 2400, 'yardropes': 700, 'shield1': 1300, 'shield2': 1300, 'shields3': 1300,
       'oars': 1800, 'spars': 1000, 'yard': 92, 'sail': 684, 'sail_spare': 240, 'bucket': 200, 'logs': 1200, 'bench': 900, 'bench2': 850, 'bridge': 84}
tot = 0
for nm, t in TGT.items():
    ms = pymeshlab.MeshSet(); ms.load_new_mesh(f'cut/{nm}.obj')
    ms.meshing_remove_unreferenced_vertices()
    if not FULL and ms.current_mesh().face_number() > t:
        ms.meshing_decimation_quadric_edge_collapse_with_texture(targetfacenum=t, qualitythr=0.5, preserveboundary=True, boundaryweight=2.0,
                                                               optimalplacement=True, preservenormal=True, planarquadric=True)
    m = ms.current_mesh(); tot += m.face_number(); print(nm, m.face_number())
    ms.save_current_mesh(f'dec/{nm}.obj', save_wedge_texcoord=True, save_textures=False)
print('total', tot)
