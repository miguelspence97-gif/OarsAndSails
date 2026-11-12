import sys; sys.path.insert(0,'.')
import numpy as np
from nifmerge import Nif; from hull_v144_lib import load,pos
def shapes(path):
    n=Nif(open(path,'rb').read()); o={}
    for k,b in enumerate(n.h['blocks']):
        if b['type']!='BSTriShape': continue
        nm=n.names[int(np.frombuffer(n.blocks[k][:4],'<i4')[0])]
        try: s,vb,T=load(n.blocks[k]); o[nm]=(pos(vb).astype(float),T)
        except: pass
    return o
def ray(P,T,o,d):
    """nearest hit distance of ray o+t d against tris (Moller)"""
    V0,V1,V2=P[T[:,0]],P[T[:,1]],P[T[:,2]]; e1=V1-V0; e2=V2-V0
    h=np.cross(d,e2); a=np.einsum('ij,ij->i',e1,h); ok=np.abs(a)>1e-9; f=np.zeros_like(a); f[ok]=1/a[ok]
    s=o-V0; u=f*np.einsum('ij,ij->i',s,h); q=np.cross(s,e1); v=f*(q@d); t=f*np.einsum('ij,ij->i',e2,q)
    m=ok&(u>=0)&(v>=0)&(u+v<=1)&(t>0)
    return t[m].min() if m.any() else None
def sweep(path_pts, radii, sides=10, cap=True, twist=0.0, prof=None):
    """tube along a polyline, radius per point; prof(angle)->radius scale (stylised section)"""
    path_pts=np.asarray(path_pts,float); radii=np.asarray(radii,float); n=len(path_pts)
    Tg=np.gradient(path_pts,axis=0); Tg/=np.linalg.norm(Tg,axis=1,keepdims=True)
    up=np.array([0,0,1.0]); 
    N0=np.cross(Tg[0],up); 
    if np.linalg.norm(N0)<1e-6: N0=np.cross(Tg[0],[1,0,0])
    N0/=np.linalg.norm(N0); Ns=[N0]
    for i in range(1,n):
        Nn=Ns[-1]-Tg[i]*np.dot(Ns[-1],Tg[i]); Nn/=np.linalg.norm(Nn); Ns.append(Nn)
    V=[]; F=[]
    for i in range(n):
        Bv=np.cross(Tg[i],Ns[i])
        for j in range(sides):
            a=2*np.pi*j/sides+twist*i; r=radii[i]*(prof(a) if prof else 1.0)
            V.append(path_pts[i]+r*(np.cos(a)*Ns[i]+np.sin(a)*Bv))
    for i in range(n-1):
        for j in range(sides):
            a=i*sides+j; b=i*sides+(j+1)%sides; c=a+sides; d=b+sides
            F+= [[a,c,b],[b,c,d]]
    V=np.array(V)
    if cap:
        for i,flip in ((0,True),(n-1,False)):
            ci=len(V); V=np.vstack([V,path_pts[i]]); 
            for j in range(sides):
                a=i*sides+j; b=i*sides+(j+1)%sides
                F.append([ci,b,a] if flip else [ci,a,b])
    return V,np.array(F)
def slab(outline, axis_u, axis_v, normal, origin, thick, bevel=0.8):
    """a flat board: a 2D outline (u,v) extruded +-thick/2 along normal, edges bevelled"""
    o=np.asarray(outline,float); n=len(o); c=o.mean(0)
    inner=c+(o-c)*(1-bevel/np.max(np.linalg.norm(o-c,axis=1)))
    def P3(uv,w): return origin+uv[:,0:1]*axis_u+uv[:,1:2]*axis_v+w*normal
    rings=[P3(inner,-thick/2),P3(o,-thick/2*0.55),P3(o,thick/2*0.55),P3(inner,thick/2)]
    V=np.vstack(rings+[P3(c[None],-thick/2),P3(c[None],thick/2)]); F=[]
    for r in range(3):
        for j in range(n):
            a=r*n+j; b=r*n+(j+1)%n; F+=[[a,b,a+n],[b,b+n,a+n]]
    c0=4*n; c1=4*n+1
    for j in range(n):
        F.append([c0,(j+1)%n,j]); F.append([c1,3*n+j,3*n+(j+1)%n])
    return V,np.array(F)
def merge(parts):
    V=[];F=[];o=0
    for v,f in parts: V.append(v); F.append(f+o); o+=len(v)
    return np.vstack(V),np.vstack(F)
def rot_axis(P,o,ax,ang):
    ax=ax/np.linalg.norm(ax); K=np.array([[0,-ax[2],ax[1]],[ax[2],0,-ax[0]],[-ax[1],ax[0],0]])
    R=np.eye(3)+np.sin(ang)*K+(1-np.cos(ang))*K@K
    return (P-o)@R.T+o
