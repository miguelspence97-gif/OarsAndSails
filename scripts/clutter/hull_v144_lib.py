import sys, struct; sys.path.insert(0,'.')
import numpy as np
from nifp import *
def load(raw):
    p=4; ne,=struct.unpack_from('<I',raw,p); p+=4+4*ne; p+=4+4+12+36+4+4
    bs_off=p; p+=16+12+8; cnt_off=p; ntri,nv,dsz=struct.unpack_from('<HHI',raw,p); p+=8; data_off=p
    vb=np.frombuffer(raw,dtype=np.uint8,count=nv*32,offset=data_off).reshape(nv,32).copy()
    tris=np.frombuffer(raw,dtype='<u2',count=ntri*3,offset=data_off+nv*32).reshape(-1,3).astype(int)
    return dict(raw=raw,bs_off=bs_off,cnt_off=cnt_off,data_off=data_off,tail=raw[data_off+dsz:]),vb,tris
def pos(vb): return vb[:,0:12].copy().view('<f4').reshape(-1,3)
def setpos(vb,P): vb[:,0:12]=np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1,12)
def store(s,vb,tris):
    data=vb.tobytes()+np.asarray(tris).astype('<u2').tobytes()
    hd=bytearray(s['raw'][:s['data_off']])
    P=pos(vb); cen=P.mean(0); rad=float(np.linalg.norm(P-cen,axis=1).max())
    struct.pack_into('<4f',hd,s['bs_off'],float(cen[0]),float(cen[1]),float(cen[2]),rad)
    struct.pack_into('<HHI',hd,s['cnt_off'],len(tris),len(vb),len(data))
    return bytes(hd)+data+s['tail']
def components(P,T):
    par=list(range(len(P)))
    def f(a):
        while par[a]!=a: par[a]=par[par[a]]; a=par[a]
        return a
    for t in T: par[f(t[0])]=f(t[1]); par[f(t[1])]=f(t[2])
    key={}
    for i,p in enumerate(np.round(P,2)):
        kk=tuple(p)
        if kk in key: par[f(i)]=f(key[kk])
        else: key[kk]=i
    r=np.array([f(i) for i in range(len(P))])
    return [np.where(r==u)[0] for u in np.unique(r)]
def rebuild(d,h,newblock):
    # same block count: replace block bytes and sizes
    nb=h['nb']
    i=d.index(b'\n')+1; p=i+4+1+4+4+4
    for _ in range(3):
        l=d[p]; p+=1+l
    nt,=struct.unpack_from('<H',d,p); p+=2
    for _ in range(nt):
        l,=struct.unpack_from('<I',d,p); p+=4+l
    ti_off=p; sz_off=ti_off+2*nb
    blk=lambda k: newblock.get(k) or bytes(d[h['blocks'][k]['start']:h['blocks'][k]['end']])
    hdr=bytearray(d[:h['hdr_end']])
    struct.pack_into('<%dI'%nb,hdr,sz_off,*[len(blk(k)) for k in range(nb)])
    body=b''.join(blk(k) for k in range(nb))
    foot=bytes(d[h['hdr_end']+sum(h['sizes']):])
    return bytes(hdr)+body+foot
