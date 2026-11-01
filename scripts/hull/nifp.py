import struct
def parse(d):
    i=d.index(b'\n')+1
    p=i; ver,=struct.unpack_from('<I',d,p); p+=4; endian=d[p]; p+=1; uver,=struct.unpack_from('<I',d,p); p+=4
    nb,=struct.unpack_from('<I',d,p); p+=4; uver2,=struct.unpack_from('<I',d,p); p+=4
    for _ in range(3): l=d[p]; p+=1+l
    nt,=struct.unpack_from('<H',d,p); p+=2
    types=[]
    for _ in range(nt):
        l,=struct.unpack_from('<I',d,p); p+=4; types.append(d[p:p+l].decode()); p+=l
    ti=list(struct.unpack_from('<%dH'%nb,d,p)); p+=2*nb
    sizes=list(struct.unpack_from('<%dI'%nb,d,p)); p+=4*nb
    ns,ml=struct.unpack_from('<II',d,p); p+=8
    names=[]
    for _ in range(ns):
        l,=struct.unpack_from('<I',d,p); p+=4; names.append(d[p:p+l].decode('latin1')); p+=l
    ng,=struct.unpack_from('<I',d,p); p+=4+4*ng
    hdr_end=p
    blocks=[]; q=hdr_end
    for k in range(nb):
        blocks.append(dict(k=k,type=types[ti[k]],start=q,end=q+sizes[k])); q+=sizes[k]
    return dict(nb=nb,types=types,ti=ti,sizes=sizes,names=names,hdr_end=hdr_end,blocks=blocks,uver2=uver2)
def tri_shape(d,h,b):
    raw=d[b['start']:b['end']]; p=0
    nameidx,=struct.unpack_from('<i',raw,p); p+=4
    ne,=struct.unpack_from('<I',raw,p); p+=4+4*ne
    p+=4  # controller
    p+=4  # flags
    p+=12+36+4  # translate rot scale
    p+=4  # collision
    bs=struct.unpack_from('<4f',raw,p); p+=16
    skin,shader,alpha=struct.unpack_from('<3i',raw,p); p+=12
    desc,=struct.unpack_from('<Q',raw,p); p+=8
    ntri,nv,dsz=struct.unpack_from('<HHI',raw,p); p+=8
    data_off=p
    vsize=(desc&0xF)*4
    return dict(name=h['names'][nameidx] if nameidx>=0 else '',bs=bs,shader=shader,alpha=alpha,desc=desc,ntri=ntri,nv=nv,dsz=dsz,vsize=vsize,data_off=b['start']+data_off,raw_data_off=data_off)
