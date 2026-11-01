import sys, struct, numpy as np
from nifp import parse, tri_shape
d=open(sys.argv[1],'rb').read(); h=parse(d)
blocks=[bytes(d[b['start']:b['end']]) for b in h['blocks']]; types=[b['type'] for b in h['blocks']]
def xf(raw):
    ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4
    return np.array(struct.unpack_from('<9f',raw,p+12)).reshape(3,3),np.array(struct.unpack_from('<3f',raw,p)),struct.unpack_from('<f',raw,p+48)[0]
def walk(k,R,T,S,label):
    raw=blocks[k]; lR,lT,lS=xf(raw); R2=R@lR; T2=R@(lT*S)+T; S2=S*lS
    if types[k] in('BSFadeNode','NiNode'):
        ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4+12+36+4+4
        nc,=struct.unpack_from('<I',raw,p)
        ni,=struct.unpack_from('<i',raw,0); nm=h['names'][ni] if ni>=0 else ''
        for c in struct.unpack_from('<%di'%nc,raw,p+4):
            if c>=0: walk(c,R2,T2,S2,nm if k else label)
    elif types[k]=='BSTriShape':
        ti=tri_shape(d,h,h['blocks'][k]); vs=ti['vsize']
        V=np.frombuffer(raw,np.uint8,ti['nv']*vs,ti['raw_data_off']).reshape(-1,vs)
        P=V[:,:12].copy().view('<f4').reshape(-1,3).astype(float); W=(P*S2)@R2.T+T2
        print(label,k,W.min(0).round(1),W.max(0).round(1))
walk(0,np.eye(3),np.zeros(3),1.0,'root')
