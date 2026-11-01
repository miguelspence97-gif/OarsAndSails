# One level of curved subdivision for a BSTriShape vertex buffer (32-byte SSE vertices: pos f32x3, bitangent.x f32,
# uv f16x2, normal u8x3 + bitangent.y, tangent u8x3 + bitangent.z, colour u8x4).
# Each triangle -> 4. Original vertices stay where they are (seams with other shapes do not move). Edge midpoints are
# Phong-tessellated with the vertex normals averaged per position, so both sides of a split (seam) vertex agree and no
# cracks open; open boundary edges (the gunwale line, the transom) stay straight. Other attributes are interpolated.
import numpy as np
def subdivide(vb, tris, alpha=0.75):
    nv=len(vb); P=vb[:,0:12].copy().view('<f4').reshape(-1,3).astype(np.float64)
    N=vb[:,20:23].astype(np.float64)/127.5-1.0
    key=[tuple(k) for k in np.round(P,3)]
    weld={}; wid=np.zeros(nv,dtype=int)
    for i,k in enumerate(key):
        if k not in weld: weld[k]=len(weld)
        wid[i]=weld[k]
    WN=np.zeros((len(weld),3))
    for i in range(nv): WN[wid[i]]+=N[i]
    WN/=np.linalg.norm(WN,axis=1)[:,None]+1e-9
    WP=np.zeros((len(weld),3))
    for i in range(nv): WP[wid[i]]=P[i]
    ecount={}
    for t in tris:
        for a,b in ((t[0],t[1]),(t[1],t[2]),(t[2],t[0])):
            e=(min(wid[a],wid[b]),max(wid[a],wid[b])); ecount[e]=ecount.get(e,0)+1
    def mid_pos(a,b):
        wa,wb=wid[a],wid[b]; e=(min(wa,wb),max(wa,wb))
        pa,pb=WP[wa],WP[wb]; m=(pa+pb)/2
        if ecount[e]<2: return m
        na,nb=WN[wa],WN[wb]
        qa=m-np.dot(m-pa,na)*na; qb=m-np.dot(m-pb,nb)*nb
        return (1-alpha)*m+alpha*(qa+qb)/2
    out=[vb[i].copy() for i in range(nv)]; newtris=[]; cache={}
    def lerp_vertex(a,b):
        k=(min(a,b),max(a,b))
        if k in cache: return cache[k]
        va,vb_=vb[a],vb[b]
        v=np.zeros(32,dtype=np.uint8)
        p=mid_pos(a,b).astype('<f4'); v[0:12]=np.frombuffer(p.tobytes(),np.uint8)
        bx=(np.frombuffer(va[12:16].tobytes(),'<f4')[0]+np.frombuffer(vb_[12:16].tobytes(),'<f4')[0])/2
        v[12:16]=np.frombuffer(np.float32(bx).tobytes(),np.uint8)
        uv=(np.frombuffer(va[16:20].tobytes(),'<f2').astype(np.float32)+np.frombuffer(vb_[16:20].tobytes(),'<f2').astype(np.float32))/2
        v[16:20]=np.frombuffer(uv.astype('<f2').tobytes(),np.uint8)
        for o in (20,24):   # normal / tangent: average, renormalise; the 4th byte (bitangent y/z) averaged
            u=(va[o:o+3].astype(np.float32)/127.5-1)+(vb_[o:o+3].astype(np.float32)/127.5-1)
            u/=np.linalg.norm(u)+1e-9
            v[o:o+3]=np.clip(np.round((u+1)*127.5),0,255).astype(np.uint8)
            v[o+3]=np.uint8(round((int(va[o+3])+int(vb_[o+3]))/2))
        v[28:32]=((va[28:32].astype(np.int32)+vb_[28:32].astype(np.int32))//2).astype(np.uint8)
        out.append(v); cache[k]=len(out)-1; return cache[k]
    for t in tris:
        a,b,c=int(t[0]),int(t[1]),int(t[2])
        ab,bc,ca=lerp_vertex(a,b),lerp_vertex(b,c),lerp_vertex(c,a)
        newtris+=[(a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)]
    return np.array(out,dtype=np.uint8),np.array(newtris,dtype=int)

# Smooth shading: every vertex normal = the area-weighted average of the faces around its position, within its own
# connected piece (the inner and outer skins never mix). Removes the hard (split-normal) creases. Tangent and bitangent
# re-made perpendicular to the new normal, keeping the UV handedness.
def smooth_normals(vb, tris):
    nv=len(vb); P=vb[:,0:12].copy().view('<f4').reshape(-1,3).astype(np.float64)
    key=[tuple(k) for k in np.round(P,3)]
    par=list(range(nv))
    def f(a):
        while par[a]!=a: par[a]=par[par[a]]; a=par[a]
        return a
    for t in tris: par[f(t[0])]=f(t[1]); par[f(t[1])]=f(t[2])
    comp=np.array([f(i) for i in range(nv)])
    wk={}; wid=np.zeros(nv,dtype=int)
    for i in range(nv):
        k=(comp[i],)+key[i]
        if k not in wk: wk[k]=len(wk)
        wid[i]=wk[k]
    acc=np.zeros((len(wk),3))
    F=P[tris]; fn=np.cross(F[:,1]-F[:,0],F[:,2]-F[:,0])   # length = 2 x area
    for c in range(3): np.add.at(acc,wid[tris[:,c]],fn)
    n=acc[wid]; n/=np.linalg.norm(n,axis=1)[:,None]+1e-12
    t_old=vb[:,24:27].astype(np.float64)/127.5-1
    b_old=np.c_[vb[:,12:16].copy().view('<f4').reshape(-1).astype(np.float64), vb[:,23].astype(np.float64)/127.5-1, vb[:,27].astype(np.float64)/127.5-1]
    t=t_old-np.sum(t_old*n,axis=1)[:,None]*n
    bad=np.linalg.norm(t,axis=1)<1e-6
    t[bad]=np.cross(n[bad],[0,0,1.0]); t/=np.linalg.norm(t,axis=1)[:,None]+1e-12
    b=np.cross(n,t); sgn=np.sign(np.sum(b*b_old,axis=1)); sgn[sgn==0]=1; b*=sgn[:,None]
    q=lambda a:np.clip(np.round((a+1)*127.5),0,255).astype(np.uint8)
    out=vb.copy()
    out[:,20:23]=q(n); out[:,24:27]=q(t)
    out[:,12:16]=np.ascontiguousarray(b[:,0].astype('<f4')).view(np.uint8).reshape(-1,4)
    out[:,23]=q(b[:,1]); out[:,27]=q(b[:,2])
    return out
