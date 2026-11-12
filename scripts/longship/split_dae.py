# Longship: split the Sketchfab DAE (massive-graphisme, CC BY) into one OBJ per material, world transforms baked.
import collada, numpy as np, collections, sys
m = collada.Collada(sys.argv[1]); OUT = sys.argv[2]
agg = collections.defaultdict(list)
for g in m.scene.objects('geometry'):
    for p in g.primitives():
        t = p.triangleset() if not isinstance(p, collada.triangleset.BoundTriangleSet) else p
        mat = t.material.name if t.material else 'none'
        V = t.vertex[t.vertex_index].reshape(-1, 3)
        U = t.texcoordset[0][t.texcoord_indexset[0]].reshape(-1, 2)
        agg[mat].append((V, U))
for mat, L in agg.items():
    V = np.vstack([a for a, _ in L]); U = np.vstack([b for _, b in L])
    pv, vi = np.unique(np.round(V, 5), axis=0, return_inverse=True); vi = vi.ravel()
    pu, ui = np.unique(np.round(U, 6), axis=0, return_inverse=True); ui = ui.ravel()
    with open(f'{OUT}/{mat}.obj', 'w') as o:
        o.write(''.join(f'v {x:.5f} {y:.5f} {z:.5f}\n' for x, y, z in pv))
        o.write(''.join(f'vt {u:.6f} {v:.6f}\n' for u, v in pu))
        o.write(''.join(f'f {vi[i]+1}/{ui[i]+1} {vi[i+1]+1}/{ui[i+1]+1} {vi[i+2]+1}/{ui[i+2]+1}\n' for i in range(0, len(V), 3)))
    print(mat, len(V) // 3, np.round(V.min(0), 1), np.round(V.max(0), 1), 'uv', np.round(U.min(0), 2), np.round(U.max(0), 2))
