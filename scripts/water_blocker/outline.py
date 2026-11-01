import sys; sys.path.insert(0,'.')
from nifp import *; from verts import *
import numpy as np
def outline(d,h,ZB,step=3.0):
    s=tri_shape(d,h,h['blocks'][10]); v=decode(d,s); P=v['pos']; T=v['tris'].astype(int)
    segs=[]
    for t in P[T]:
        pts=[]
        for p,q in ((t[0],t[1]),(t[1],t[2]),(t[2],t[0])):
            if (p[2]-ZB)*(q[2]-ZB)<0:
                f=(ZB-p[2])/(q[2]-p[2]); pts.append(p+(q-p)*f)
        if len(pts)==2: segs.append(pts)
    rows=[]
    for y in np.arange(-340,341,step):
        cr=[]
        for s0,s1 in segs:
            if (s0[1]-y)*(s1[1]-y)<0:
                f=(y-s0[1])/(s1[1]-s0[1]); cr.append(float(s0[0]+(s1[0]-s0[0])*f))
        neg=[x for x in cr if x<0]; pos=[x for x in cr if x>0]
        if not neg or not pos: continue
        l,r=max(neg),min(pos)
        if r-l<6: continue
        rows.append((y,l,r))
    rows=np.array(rows)
    # keep the contiguous run through y=0
    ys=rows[:,0]; i0=int(np.argmin(np.abs(ys)))
    w=rows[:,2]-rows[:,1]
    # walk out from the middle; the interior only narrows toward the ends (a widening = the hull's outer skin past the
    # point where the inner walls met)
    a=i0
    while a>0 and ys[a]-ys[a-1]<step+0.1 and w[a-1]<=w[a]+4: a-=1
    b=i0
    while b<len(ys)-1 and ys[b+1]-ys[b]<step+0.1 and w[b+1]<=w[b]+4: b+=1
    return rows[a:b+1]
if __name__=='__main__':
    d=open('bilge.nif','rb').read(); h=parse(d)
    r=outline(d,h,46)
    w=r[:,2]-r[:,1]
    print(len(r), r[0].round(1), r[-1].round(1), 'max jump', np.abs(np.diff(w)).max().round(1))
    for row in r[::8]: print(row.round(1))
