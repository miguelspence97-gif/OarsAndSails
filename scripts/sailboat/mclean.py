# v1.7.89: cleanup asked by Miguel: no rope coil or rope pile at the bow (Rope01 whole, Rope02's pile), no rope that hung round the life ring (Rope02
# pieces inside the ring's box), no boat oars (BigWoodenBoat01's two 1128-tri oars). Writes mparts/<name>_clean.obj.
import numpy as np, trimesh
def rw(src, dst, keep_fn):
    V=[];T=[];F=[]
    lines=open(src).read().splitlines()
    for l in lines:
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('f '): F.append([tuple(int(x) for x in w.split('/')[:2]) for w in l.split()[1:4]])
    V=np.array(V,float); Fv=np.array([[a[0]-1 for a in f] for f in F])
    m=trimesh.Trimesh(V,Fv,process=False); lab=trimesh.graph.connected_component_labels(m.face_adjacency,len(Fv))
    keep=np.ones(len(Fv),bool); dropped=0
    for c in range(lab.max()+1):
        fi=np.where(lab==c)[0]; vv=V[np.unique(Fv[fi])]
        if not keep_fn(vv.min(0),vv.max(0),len(fi)): keep[fi]=False; dropped+=1
    with open(dst,'w') as o:
        o.write(''.join(f'v {x} {y} {z}\n' for x,y,z in V)); o.write(''.join(f'vt {u} {v}\n' for u,v in T))
        o.write(''.join('f '+' '.join(f'{a}/{b}' for a,b in F[i])+'\n' for i in np.where(keep)[0]))
    print(dst, 'dropped', dropped, 'parts', (~keep).sum(), 'tris, kept', keep.sum())
RING_LO=np.array([-2.25,0.08,0.36]); RING_HI=np.array([-1.73,0.75,0.72])
PILE_LO=np.array([-2.4,0.0,-0.5]); PILE_HI=np.array([-1.2,0.8,0.72])   # the messy rope pile on the foredeck (7340 tris)
rw('mparts/Rope02.obj','mparts/Rope02_clean.obj', lambda lo,hi,n: not (np.all(lo>=RING_LO) and np.all(hi<=RING_HI)) and not (np.all(lo>=PILE_LO) and np.all(hi<=PILE_HI)))
OARS=[(np.array([0.87,0.25,0.02]),np.array([1.14,1.09,1.44])),(np.array([0.54,0.26,-1.52]),np.array([1.37,0.94,-0.2]))]
rw('mparts/BigWoodenBoat01.obj','mparts/BigWoodenBoat01_clean.obj', lambda lo,hi,n: not any(np.allclose(lo,a,atol=0.02) and np.allclose(hi,b,atol=0.02) for a,b in OARS))
