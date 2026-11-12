import sys; sys.path.insert(0,'.')
import numpy as np
from collections import Counter
from nifp import parse, tri_shape
def shapes_raw(path):
    d=open(path,'rb').read(); h=parse(d); out={}
    for b in h['blocks']:
        if b['type'] not in ('BSTriShape','BSDynamicTriShape'): continue
        try: t=tri_shape(d,h,b)
        except Exception as e: continue
        if t['nv']==0 or t['ntri']==0: continue
        if t['vsize']==0: continue
        V=np.frombuffer(d,np.uint8,t['nv']*t['vsize'],t['data_off']).reshape(t['nv'],t['vsize'])
        P=V[:,0:12].copy().view('<f4').reshape(-1,3).astype(float) if t['vsize']>=12 else None
        I=np.frombuffer(d,np.uint16,t['ntri']*3,t['data_off']+t['nv']*t['vsize']).reshape(-1,3).astype(int)
        out[t['name']]=(P,I,b['type'])
    return out
def open_loops(P,F,tol=1e-3):
    """boundary edges after welding by position; returns list of loops (arrays of welded vertex ids) and the welded positions"""
    key=np.round(P/tol).astype(np.int64); _,w=np.unique(key,axis=0,return_inverse=True); w=w.ravel()
    Fw=w[F]; Fw=Fw[(Fw[:,0]!=Fw[:,1])&(Fw[:,1]!=Fw[:,2])&(Fw[:,0]!=Fw[:,2])]
    E=np.sort(np.r_[Fw[:,[0,1]],Fw[:,[1,2]],Fw[:,[2,0]]],axis=1)
    c=Counter(map(tuple,E)); b=[e for e,k in c.items() if k==1]
    return b, w
if __name__=='__main__':
    S=shapes_raw(sys.argv[1])
    for nm,(P,F,ty) in sorted(S.items()):
        if P is None: continue
        b,w=open_loops(P,F)
        if b: print(f'{nm:24s} {ty:18s} verts {len(P):6d} tris {len(F):6d} OPEN edges {len(b)}')
def loops_of(b):
    adj={}
    for a,c in b: adj.setdefault(a,[]).append(c); adj.setdefault(c,[]).append(a)
    seen=set(); L=[]
    for s in adj:
        if s in seen: continue
        st=[s]; comp=[]
        while st:
            v=st.pop()
            if v in seen: continue
            seen.add(v); comp.append(v); st+=adj[v]
        L.append(comp)
    return L
