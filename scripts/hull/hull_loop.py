# One level of Loop subdivision with sharp creases, for SSE BSTriShape buffers (32-byte verts).
# Works on welded positions (UV seams are not creases). Creases: open edges and edges whose faces meet at >= CREASE deg;
# they follow the curve rule (corners where 1 or 3+ creases meet stay put), so flat boards stay flat and box edges sharp,
# while curved surfaces (hull skins, gunwale, ribs) are smoothed in outline. Normals rebuilt per smoothing group.
import numpy as np
def f2(b): return b.astype(np.float32)/127.5-1
def e8(x): return np.clip(np.round((x+1)*127.5),0,255).astype(np.uint8)
def loop(vb,T,crease=55.0,rigid=None):
    T=np.asarray(T,int); nv=len(vb)
    P=vb[:,0:12].copy().view('<f4').reshape(-1,3).astype(np.float64)
    wkey={}; wid=np.array([wkey.setdefault(tuple(k),len(wkey)) for k in np.round(P,3)]); nw=len(wkey)
    WP=np.zeros((nw,3)); WP[wid]=P
    W=wid[T]
    FN=np.cross(WP[W[:,1]]-WP[W[:,0]],WP[W[:,2]]-WP[W[:,0]]); FN/=np.maximum(np.linalg.norm(FN,axis=1),1e-12)[:,None]
    E={}
    for f,t in enumerate(W):
        for c in range(3):
            a,b=t[c],t[(c+1)%3]; E.setdefault((min(a,b),max(a,b)),[]).append((f,t[(c+2)%3]))
    cs=np.cos(np.radians(crease))
    rig=np.zeros(len(T),bool) if rigid is None else np.asarray(rigid,bool)
    sharp={e:(not(len(v)==2 and FN[v[0][0]]@FN[v[1][0]]>cs)) or any(rig[f] for f,_ in v) for e,v in E.items()}
    nbr=[set() for _ in range(nw)]; cr=[[] for _ in range(nw)]
    for (a,b),s in sharp.items():
        nbr[a].add(b); nbr[b].add(a)
        if s: cr[a].append(b); cr[b].append(a)
    NP=np.zeros((nw,3))
    for v in range(nw):
        k=len(cr[v])
        if k==0:
            n=len(nbr[v]); beta=3/16 if n==3 else 3/(8*n)
            NP[v]=(1-n*beta)*WP[v]+beta*sum(WP[u] for u in nbr[v])
        elif k==2: NP[v]=0.75*WP[v]+0.125*(WP[cr[v][0]]+WP[cr[v][1]])
        else: NP[v]=WP[v]
    EP={}
    for e,v in E.items():
        a,b=e
        if sharp[e]: EP[e]=(WP[a]+WP[b])/2
        else: EP[e]=0.375*(WP[a]+WP[b])+0.125*(WP[v[0][1]]+WP[v[1][1]])
    out=vb.copy()
    out[:,0:12]=np.ascontiguousarray(NP[wid].astype('<f4')).view(np.uint8).reshape(-1,12)
    rows=[]; cache={}; newT=[]
    def mid(a,b):
        k=(min(a,b),max(a,b))
        if k in cache: return cache[k]
        va,vbb=vb[a],vb[b]; v=np.zeros(32,np.uint8)
        we=(min(wid[a],wid[b]),max(wid[a],wid[b]))
        v[0:12]=np.frombuffer(EP[we].astype('<f4').tobytes(),np.uint8)
        bx=(np.frombuffer(va[12:16].tobytes(),'<f4')[0]+np.frombuffer(vbb[12:16].tobytes(),'<f4')[0])/2
        v[12:16]=np.frombuffer(np.float32(bx).tobytes(),np.uint8)
        uv=(np.frombuffer(va[16:20].tobytes(),'<f2').astype(np.float32)+np.frombuffer(vbb[16:20].tobytes(),'<f2').astype(np.float32))/2
        v[16:20]=np.frombuffer(uv.astype('<f2').tobytes(),np.uint8)
        for o in (20,24):
            u=f2(va[o:o+3])+f2(vbb[o:o+3]); u/=np.linalg.norm(u)+1e-9
            v[o:o+3]=e8(u); v[o+3]=np.uint8(round((int(va[o+3])+int(vbb[o+3]))/2))
        v[28:32]=((va[28:32].astype(np.int32)+vbb[28:32].astype(np.int32))//2).astype(np.uint8)
        rows.append(v); cache[k]=nv+len(rows)-1; return cache[k]
    for t in T:
        a,b,c=int(t[0]),int(t[1]),int(t[2])
        ab,bc,ca=mid(a,b),mid(b,c),mid(c,a)
        newT+=[(a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)]
    out=np.concatenate([out,np.array(rows,np.uint8)]); newT=np.array(newT,int)
    newR=np.repeat(rig,4)
    return renormal(out,newT,crease,newR,vb),newT
def renormal(vb,T,crease,rig,orig):
    P=vb[:,0:12].copy().view('<f4').reshape(-1,3).astype(np.float64)
    wkey={}; wid=np.array([wkey.setdefault(tuple(k),len(wkey)) for k in np.round(P,3)])
    FN=np.cross(P[T[:,1]]-P[T[:,0]],P[T[:,2]]-P[T[:,0]]); FN/=np.maximum(np.linalg.norm(FN,axis=1),1e-12)[:,None]
    # corner angle weights
    ang=np.zeros((len(T),3))
    for c in range(3):
        u=P[T[:,(c+1)%3]]-P[T[:,c]]; v=P[T[:,(c+2)%3]]-P[T[:,c]]
        ang[:,c]=np.arccos(np.clip(np.einsum('ij,ij->i',u,v)/(np.linalg.norm(u,axis=1)*np.linalg.norm(v,axis=1)+1e-12),-1,1))
    # faces per welded vertex
    vf={}
    for f,t in enumerate(T):
        for c in range(3): vf.setdefault(wid[t[c]],[]).append((f,c))
    cs=np.cos(np.radians(crease))
    acc=np.zeros((len(vb),3))
    for w,lst in vf.items():
        for f,c in lst:   # this corner's normal: faces at the vertex within the crease angle of this face
            n=np.zeros(3)
            for g,d in lst:
                if FN[g]@FN[f]>cs: n+=FN[g]*ang[g,d]
            acc[T[f][c]]+=n
    on=f2(vb[:,20:23])
    keep=np.zeros(len(vb),bool); keep[np.unique(T[rig])]=True
    N=acc/np.maximum(np.linalg.norm(acc,axis=1),1e-12)[:,None]
    bad=(np.linalg.norm(acc,axis=1)<1e-9)|keep; N[bad]=on[bad]
    # keep the original facing (the mesh's own normal side)
    flip=np.einsum('ij,ij->i',N,on)<0; N[flip]*=-1
    t=f2(vb[:,24:27]); bz=np.stack([vb[:,12:16].copy().view('<f4').reshape(-1),f2(vb[:,23]),f2(vb[:,27])],1)
    sgn=np.sign(np.einsum('ij,ij->i',np.cross(on,t),bz)); sgn[sgn==0]=1
    t=t-N*np.einsum('ij,ij->i',t,N)[:,None]; t/=np.maximum(np.linalg.norm(t,axis=1),1e-9)[:,None]
    B=np.cross(N,t)*sgn[:,None]
    vb=vb.copy(); vb[:,20:23]=e8(N); vb[:,24:27]=e8(t)
    vb[:,12:16]=np.ascontiguousarray(B[:,0].astype('<f4')).view(np.uint8).reshape(-1,4); vb[:,23]=e8(B[:,1]); vb[:,27]=e8(B[:,2])
    return vb
