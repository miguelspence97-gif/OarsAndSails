import numpy as np, collections, trimesh
p='obj/MedievalSailingBoat.obj'
V=[];VT=[];faces=collections.defaultdict(list);cur=None
for l in open(p,errors='replace'):
    if l.startswith('v '): V.append(l.split()[1:4])
    elif l.startswith('vt '): VT.append(l.split()[1:3])
    elif l.startswith('g '): cur=l.split()[1]
    elif l.startswith('f '):
        idx=[t.split('/') for t in l.split()[1:]]
        vv=[int(a[0])-1 for a in idx]; tt=[int(a[1])-1 for a in idx]
        for k in range(1,len(vv)-1): faces[cur].append((vv[0],vv[k],vv[k+1],tt[0],tt[k],tt[k+1]))
V=np.array(V,float); VT=np.array(VT,float)
def write(name,f):
    f=np.array(f); vi=np.unique(f[:,:3]); ti=np.unique(f[:,3:])
    vm=-np.ones(len(V),int); vm[vi]=np.arange(len(vi)); tm=-np.ones(len(VT),int); tm[ti]=np.arange(len(ti))
    with open(f'parts/{name}.obj','w') as o:
        o.write(''.join(f'v {x} {y} {z}\n' for x,y,z in V[vi]))
        o.write(''.join(f'vt {u} {v}\n' for u,v in VT[ti]))
        o.write(''.join(f'f {vm[a]+1}/{tm[d]+1} {vm[b]+1}/{tm[e]+1} {vm[c]+1}/{tm[g]+1}\n' for a,b,c,d,e,g in f))
    print(name,len(f))
for n,f in faces.items():
    if n!='MSBBody': write(n,f); continue
    f=np.array(f); m=trimesh.Trimesh(V,f[:,:3],process=False)
    lab=trimesh.graph.connected_component_labels(m.face_adjacency,len(f))
    cnt=collections.Counter(lab); big=[l for l,_ in cnt.most_common(2)]
    write('MSBSailMain',f[lab==big[0]]); write('MSBSailJib',f[lab==big[1]]); write('MSBHull',f[(lab!=big[0])&(lab!=big[1])])
