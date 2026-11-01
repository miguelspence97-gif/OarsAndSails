# Generic SSE nif append: new NiNode wrappers under the root, each holding shapes imported from other nifs (blocks copied,
# refs renumbered, controllers / collision / extra data dropped) or built from vertex arrays.
import struct, numpy as np
from nifp import parse
def mat_rz(a): c,s=np.cos(a),np.sin(a); return np.array([[c,-s,0],[s,c,0],[0,0,1]],float)
def mat_rx(a): c,s=np.cos(a),np.sin(a); return np.array([[1,0,0],[0,c,-s],[0,s,c]],float)
def mat_ry(a): c,s=np.cos(a),np.sin(a); return np.array([[c,0,s],[0,1,0],[-s,0,c]],float)

class Nif:
    def __init__(s,d):
        s.d=d; s.h=parse(d)
        s.blocks=[bytes(d[b['start']:b['end']]) for b in s.h['blocks']]
        s.types=list(s.h['types']); s.ti=list(s.h['ti']); s.names=list(s.h['names'])
        # header pieces
        i=d.index(b'\n')+1; p=i+4+1+4; s.nb_off=p; p+=4+4
        for _ in range(3): l=d[p]; p+=1+l
        s.pre=d[:s.nb_off]; s.mid=d[s.nb_off+4:p]  # up to the type count
        nt,=struct.unpack_from('<H',d,p); p+=2
        for _ in range(nt): l,=struct.unpack_from('<I',d,p); p+=4+l
        p+=2*s.h['nb']+4*s.h['nb']
        ns,s.ml=struct.unpack_from('<II',d,p); q=p+8
        for _ in range(ns): l,=struct.unpack_from('<I',d,q); q+=4+l
        s.grp=d[q:s.h['hdr_end']]
        s.foot=d[s.h['hdr_end']+sum(s.h['sizes']):]
    def tid(s,t):
        if t not in s.types: s.types.append(t)
        return s.types.index(t)
    def add(s,t,raw): s.blocks.append(bytes(raw)); s.ti.append(s.tid(t)); return len(s.blocks)-1
    def name(s,n):
        if n in s.names: return s.names.index(n)
        s.names.append(n); return len(s.names)-1
    def write(s):
        hdr=bytearray(s.pre)+struct.pack('<I',len(s.blocks))+s.mid
        hdr+=struct.pack('<H',len(s.types))
        for t in s.types: hdr+=struct.pack('<I',len(t))+t.encode()
        hdr+=struct.pack('<%dH'%len(s.blocks),*s.ti)+struct.pack('<%dI'%len(s.blocks),*[len(b) for b in s.blocks])
        hdr+=struct.pack('<II',len(s.names),max(max(len(n) for n in s.names),s.ml))
        for n in s.names: hdr+=struct.pack('<I',len(n))+n.encode('latin1')
        return bytes(hdr)+s.grp+b''.join(s.blocks)+s.foot

def node_block(name, R=np.eye(3), T=(0,0,0), S=1.0, kids=()):
    out=struct.pack('<iIi',name,0,-1)+struct.pack('<I',14)+struct.pack('<3f',*T)
    out+=struct.pack('<9f',*[float(R[r][c]) for r in range(3) for c in range(3)])+struct.pack('<f',S)+struct.pack('<i',-1)
    out+=struct.pack('<I',len(kids))+struct.pack('<%di'%len(kids),*kids)+struct.pack('<I',0)
    return out

def av_fix(raw, off=0):
    """NiObjectNET/NiAVObject head at off: name kept -1, extra data dropped, controller -1. returns (raw', pos after controller)"""
    ne,=struct.unpack_from('<I',raw,off+4)
    r=raw[:off]+struct.pack('<iI',-1,0)+struct.pack('<i',-1)+raw[off+8+4*ne+4:]
    return r, off+12

def node_transform(raw):
    ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4
    T=np.array(struct.unpack_from('<3f',raw,p)); R=np.array(struct.unpack_from('<9f',raw,p+12)).reshape(3,3); S=struct.unpack_from('<f',raw,p+48)[0]
    return R,T,S

def import_shapes(dst, srcd, shaders=True):
    """copy every BSTriShape (and its shader / texture set / alpha) of a source nif. Returns new shape block ids, each
    with its transform composed with its parents' up to (not including) the root."""
    sh=parse(srcd); blocks=[bytes(srcd[b['start']:b['end']]) for b in sh['blocks']]; types=[b['type'] for b in sh['blocks']]
    # parents of shapes (one level of NiNode below root handled; deeper composed)
    parent={}
    for k,t in enumerate(types):
        if t in('NiNode','BSFadeNode','BSMultiBoundNode','BSLeafAnimNode'):
            raw=blocks[k]; ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4+12+36+4+4
            nc,=struct.unpack_from('<I',raw,p); kids=struct.unpack_from('<%di'%nc,raw,p+4)
            for c in kids:
                if c>=0: parent[c]=k
    remap={}
    def copy(k):
        if k<0: return -1
        if k in remap: return remap[k]
        t=types[k]; raw=bytearray(blocks[k])
        if t=='BSShaderTextureSet':
            remap[k]=dst.add(t,raw); return remap[k]
        if t=='NiAlphaProperty':
            r,_=av_fix(bytes(raw)); remap[k]=dst.add(t,r); return remap[k]
        if t=='BSLightingShaderProperty':
            r,p=av_fix(bytes(raw),4)  # shader type uint32 first
            r=bytearray(r); tex,=struct.unpack_from('<i',r,p+4+4+8+8)
            struct.pack_into('<i',r,p+4+4+8+8,copy(tex))
            remap[k]=dst.add(t,r); return remap[k]
        if t=='BSEffectShaderProperty':
            r,_=av_fix(bytes(raw)); remap[k]=dst.add(t,r); return remap[k]
        raise Exception('unhandled '+t)
    out=[]
    for k,t in enumerate(types):
        if t not in ('BSTriShape','BSLODTriShape'): continue
        raw=blocks[k]
        r,p=av_fix(raw)
        r=bytearray(r)
        # p: flags; then T R S collision
        T=np.array(struct.unpack_from('<3f',r,p+4)); R=np.array(struct.unpack_from('<9f',r,p+16)).reshape(3,3); S=struct.unpack_from('<f',r,p+52)[0]
        q=parent.get(k)
        while q is not None and q!=0:
            PR,PT,PS=node_transform(blocks[q]); T=PR@(T*PS)+PT; R=PR@R; S=S*PS; q=parent.get(q)
        if 0 in parent.values() or True:
            if parent.get(k) is not None:
                # include the root's own transform too
                PR,PT,PS=node_transform(blocks[0]); T=PR@(T*PS)+PT; R=PR@R; S*=PS
        struct.pack_into('<3f',r,p+4,*T); struct.pack_into('<9f',r,p+16,*R.reshape(-1)); struct.pack_into('<f',r,p+52,S)
        struct.pack_into('<i',r,p+56,-1)  # collision
        bs=p+60
        skin,shd,alp=struct.unpack_from('<3i',r,bs+16)
        struct.pack_into('<3i',r,bs+16,-1,copy(shd) if shaders else -1,copy(alp) if shaders else -1)
        out.append(dst.add(t,r))
    return out

def e8(a): return np.clip(np.round((np.asarray(a,float)+1)*127.5),0,255).astype(np.uint8)
def build_shape(dst, template_raw, P, N, Tg, UV, tris, shader_id, alpha_id=-1):
    """a 32-byte-vertex BSTriShape (desc 0x3b00007650408) from arrays; template: any such shape block (for its head)"""
    from hull_v144_lib import load, store
    st,_,_=load(template_raw)
    P=np.asarray(P,float); N=np.asarray(N,float); Tg=np.asarray(Tg,float); UV=np.asarray(UV,float)
    B=np.cross(N,Tg)
    V=np.zeros((len(P),32),np.uint8)
    V[:,0:12]=np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1,12)
    V[:,12:16]=np.ascontiguousarray(B[:,0:1].astype('<f4')).view(np.uint8).reshape(-1,4)
    V[:,16:20]=np.ascontiguousarray(UV.astype('<f2')).view(np.uint8).reshape(-1,4)
    V[:,20:23]=e8(N); V[:,23]=e8(B[:,1]); V[:,24:27]=e8(Tg); V[:,27]=e8(B[:,2]); V[:,28:32]=255
    raw=bytearray(store(st,V,np.asarray(tris)))
    raw,p=av_fix(bytes(raw)); raw=bytearray(raw)
    struct.pack_into('<3f',raw,p+4,0,0,0); struct.pack_into('<9f',raw,p+16,1,0,0,0,1,0,0,0,1); struct.pack_into('<f',raw,p+52,1.0)
    struct.pack_into('<i',raw,p+56,-1)
    struct.pack_into('<3i',raw,p+60+16,-1,shader_id,alpha_id)
    return dst.add('BSTriShape',raw)

def tex_set(paths):
    paths=list(paths)+['']*(9-len(paths))
    return struct.pack('<I',9)+b''.join(struct.pack('<I',len(q))+q.encode() for q in paths)

def root_add_kids(dst,kids):
    raw=bytearray(dst.blocks[0]); ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4+12+36+4+4
    nc,=struct.unpack_from('<I',raw,p); old=list(struct.unpack_from('<%di'%nc,raw,p+4))
    new=old+list(kids)
    dst.blocks[0]=bytes(raw[:p])+struct.pack('<I',len(new))+struct.pack('<%di'%len(new),*new)+bytes(raw[p+4+4*nc:])
