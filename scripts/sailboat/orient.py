import sys; sys.path.insert(0,'.')
import numpy as np
from collections import Counter
from openedges import shapes_raw
from hull_v144_lib import components
def check(P,F,tol=1e-3):
    key=np.round(P/tol).astype(np.int64); _,w=np.unique(key,axis=0,return_inverse=True); w=w.ravel(); Fw=w[F]
    E=np.r_[Fw[:,[0,1]],Fw[:,[1,2]],Fw[:,[2,0]]]; c=Counter(map(tuple,E)); bad=sum(v-1 for v in c.values() if v>1)
    vol=[]
    for comp in components(P,F):
        cs=set(comp); m=np.isin(F,list(cs)).all(1); T=P[F[m]]
        vol.append(np.einsum('ij,ij->i',T[:,0],np.cross(T[:,1],T[:,2])).sum()/6)
    return bad, sum(1 for v in vol if v<0), len(vol)
if __name__=='__main__':
  S=shapes_raw(sys.argv[1])
  for nm,(P,F,ty) in sorted(S.items()):
      if P is None or not nm.startswith(('SB_','RowRim')): continue
      if len(F)>70000: continue
      bad,neg,nc=check(P,F)
      if bad or neg: print(f'{nm:22s} same-way edges {bad:5d}  inside-out parts {neg}/{nc}')
