import numpy as np, trimesh, sys
sys.path.insert(0,'.')
K=110.0; XOFF=0.30; ZOFF=16.0
MAST=np.array([-2.0,83.0])
def sk(V): return np.stack([-V[:,2]*K, -(V[:,0]+XOFF)*K, V[:,1]*K+ZOFF],1)
def read(path):
    V=[];T=[];F=[]
    for l in open(path):
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('f '): F.append([tuple(int(x)-1 for x in w.split('/')[:2]) for w in l.split()[1:4]])
    return np.array(V,float),np.array(T,float),F
def classify(lo,hi,mat):
    c=(lo+hi)/2; r=np.hypot(c[0]-MAST[0],c[1]-MAST[1])
    if mat=='Rope02' and hi[2]>580 and lo[0]<-10: return 'halyard'
    if c[1]>100: return 'static'                    # forward of the mast: bow, stays, jib gear
    aft = c[1] < MAST[1] + 2                         # the boom and gaff gear all lies aft of the mast
    if lo[2]>=355 and r>4 and aft: return 'gaff'
    if lo[2]>=128 and hi[2]<=200 and r>4 and aft: return 'boom'
    if mat=='Rope02' and 260<=lo[2] and hi[2]<=290 and r>4: return 'boom'   # the jaw lashings
    return 'static'
def groups(path, mat):
    V,T,F=read(path); S=sk(V)
    Fv=np.array([[a[0] for a in f] for f in F])
    m=trimesh.Trimesh(V,Fv,process=False); lab=trimesh.graph.connected_component_labels(m.face_adjacency,len(Fv))
    out={}
    for c in range(lab.max()+1):
        fi=np.where(lab==c)[0]; vv=S[np.unique(Fv[fi])]; g=classify(vv.min(0),vv.max(0),mat)
        out.setdefault(g,[]).extend(fi.tolist())
    return S,T,F,out
if __name__=='__main__':
    for p,mat in [('../medboat/mdec/BigWoodenBoat01_clean.obj','Wood01'),('../medboat/mdec/RustMetal01.obj','Iron'),('../medboat/mdec/Rope02_clean.obj','Rope02')]:
        S,T,F,g=groups(p,mat); print(mat,{k:len(v) for k,v in g.items()})
