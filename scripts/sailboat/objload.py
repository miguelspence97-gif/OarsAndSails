import numpy as np
def obj(p, s=110.0):
    """original sailing boat OBJ -> nif space (x=-z, y=-x, z=y; *110; y-33, z+16); one vertex per v/vt/vn corner"""
    V=[];T=[];N=[];C=[]
    for l in open(p):
        if l.startswith('v '): V.append(l.split()[1:4])
        elif l.startswith('vt '): T.append(l.split()[1:3])
        elif l.startswith('vn '): N.append(l.split()[1:4])
        elif l.startswith('f '):
            w=[tuple(int(x)-1 for x in t.split('/')) for t in l.split()[1:]]
            for k in range(1,len(w)-1): C.append((w[0],w[k],w[k+1]))
    V=np.array(V,float); T=np.array(T,float); N=np.array(N,float)
    key={}; F=[]
    for tri in C:
        F.append([key.setdefault(c,len(key)) for c in tri])
    K=np.array(list(key.keys()))
    P=V[K[:,0]]; UV=T[K[:,1]]; NN=N[K[:,2]]
    P=np.c_[-P[:,2]*s, -P[:,0]*s-33, P[:,1]*s+16]; NN=np.c_[-NN[:,2],-NN[:,0],NN[:,1]]
    NN/=np.linalg.norm(NN,axis=1,keepdims=True)+1e-9
    return P,UV,NN,np.array(F)
