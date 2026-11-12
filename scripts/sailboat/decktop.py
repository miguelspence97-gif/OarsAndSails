import sys,numpy as np; sys.path.insert(0,'.')
from openedges import shapes_raw
S=shapes_raw(sys.argv[1] if len(sys.argv)>1 else 'skin_furl.nif')
TT=[]
for nm in ('SB_HullPlanks','SB_HullFrame'):
    P,F,t=S[nm]; P=P.astype(float); TT.append(P[F])
T=np.vstack(TT)
def top(x,y,zmax=110):
    m=(T[:,:,0].min(1)<=x)&(T[:,:,0].max(1)>=x)&(T[:,:,1].min(1)<=y)&(T[:,:,1].max(1)>=y); best=None
    for a,b,c in T[m]:
        v0=b-a; v1=c-a; den=v0[0]*v1[1]-v0[1]*v1[0]
        if abs(den)<1e-9: continue
        u=((x-a[0])*v1[1]-(y-a[1])*v1[0])/den; v=(v0[0]*(y-a[1])-v0[1]*(x-a[0]))/den
        if u>=-1e-6 and v>=-1e-6 and u+v<=1+1e-6:
            z=a[2]+u*v0[2]+v*v1[2]
            if z<zmax and (best is None or z>best): best=z
    return best
