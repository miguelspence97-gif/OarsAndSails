import struct, numpy as np
from nifmerge import av_fix
def skinned_geom(n, k):
    raw=n.blocks[k]; _,p0=av_fix(raw); inst,sh,al=struct.unpack_from('<3i',raw,p0+60+16)
    if inst<0: return None
    ir=n.blocks[inst]; sd_id,sp_id,root_id,nb=struct.unpack_from('<iiiI',ir,0)
    sp=n.blocks[sp_id]; npart,dsize,vsz,desc=struct.unpack_from('<IIIQ',sp,0); nv=dsize//vsz
    vb=np.frombuffer(sp,np.uint8,dsize,20).reshape(nv,vsz)
    P=vb[:,0:12].copy().view('<f4').reshape(-1,3).astype(float)
    q=20+dsize; F=[]
    for _ in range(npart):
        pnv,pnt,pnb,pns,pnw=struct.unpack_from('<5H',sp,q); q+=10+2*pnb
        hm,=struct.unpack_from('<B',sp,q); q+=1
        vm=np.frombuffer(sp,'<u2',pnv,q) if hm else None; q+=2*pnv if hm else 0
        hw,=struct.unpack_from('<B',sp,q); q+=1; q+=4*pnv*pnw if hw else 0
        if pns: raise ValueError('strips')
        hf,=struct.unpack_from('<B',sp,q); q+=1
        f=np.frombuffer(sp,'<u2',pnt*3,q).reshape(-1,3).astype(int); q+=6*pnt
        hb,=struct.unpack_from('<B',sp,q); q+=1; q+=pnv*pnw if hb else 0
        F.append(vm[f] if vm is not None else f)
        # SSE: lod / global vb fields
    return P, np.vstack(F), vb
