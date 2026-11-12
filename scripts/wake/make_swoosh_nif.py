# RowRim oar swoosh piece (v1.7.60): one flat quad lying on the water (local z up), ADDITIVE, white streak texture the
# code tints per piece through its own material. Layout from maginvabsorb_blue.nif blocks 21-23 (as the plankton sprite).
# usage: python make_wake_nif.py <maginvabsorb_blue.nif> <out.nif> <texture path> <half width x> <half length y>
from hull_v144_lib import *
import sys
SRC,OUT,TEX,HX,HY=sys.argv[1],sys.argv[2],sys.argv[3],float(sys.argv[4]),float(sys.argv[5])
d=open(SRC,'rb').read(); h=parse(d)
def node(name,child):
    out=struct.pack('<iIi',name,0,-1)+struct.pack('<I',14)+struct.pack('<3f',0,0,0)+struct.pack('<9f',1,0,0,0,1,0,0,0,1)+struct.pack('<f',1.0)+struct.pack('<i',-1)
    return out+struct.pack('<Ii',1,child)+struct.pack('<I',0)
st,vb,T=load(d[h['blocks'][21]['start']:h['blocks'][21]['end']])
def e8(a): return np.clip(np.round((np.asarray(a,float)+1)*127.5),0,255).astype(np.uint8)
V=[]
for (x,y,uu,vv) in ((-HX,-HY,0,1),(HX,-HY,1,1),(HX,HY,1,0),(-HX,HY,0,0)):
    r=np.zeros(32,np.uint8)
    r[0:12]=np.frombuffer(np.array([x,y,0],'<f4').tobytes(),np.uint8); r[12:16]=np.frombuffer(np.float32(0.0).tobytes(),np.uint8)
    r[16:20]=np.frombuffer(np.array([uu,vv],'<f2').tobytes(),np.uint8)
    r[20:23]=e8((0,0,1)); r[23]=e8(1.0); r[24:27]=e8((1,0,0)); r[27]=e8(0.0); r[28:32]=255
    V.append(r)
shape=bytearray(store(st,np.array(V,np.uint8),np.array([(0,1,2),(0,2,3)])))
struct.pack_into('<i',shape,0,-1); struct.pack_into('<f',shape,64,1.0)
struct.pack_into('<3i',shape,st['bs_off']+16,-1,2,3)   # skin, shader, alpha
s=d[h['blocks'][22]['start']:h['blocks'][22]['end']]
assert struct.unpack_from('<I',s,36)[0]==0
sh=bytearray(s[:36])+struct.pack('<I',len(TEX))+TEX.encode()+bytearray(s[40:])
o=40+len(TEX)
struct.pack_into('<4f',sh,o+20,0.3,0.85,1.0,1); struct.pack_into('<f',sh,o+36,1.0)
alpha=bytearray(d[h['blocks'][23]['start']:h['blocks'][23]['end']])
struct.pack_into('<H',alpha,12,0x100D)   # blend on, src alpha + dest ONE (additive)
alpha[14]=0
blocks=[node(0,1),bytes(shape),bytes(sh),bytes(alpha)]
types=['NiNode','BSTriShape','BSEffectShaderProperty','NiAlphaProperty']
names=['RowRim_SwooshPiece']
i=d.index(b'\n')+1; p=i+4+1+4; nb_off=p; p+=4+4
for _ in range(3):
    l=d[p]; p+=1+l
hdr=bytearray(d[:nb_off])+struct.pack('<I',len(blocks))+d[nb_off+4:p]
hdr+=struct.pack('<H',len(types))
for t in types: hdr+=struct.pack('<I',len(t))+t.encode()
hdr+=struct.pack('<%dH'%len(blocks),*range(len(blocks)))
hdr+=struct.pack('<%dI'%len(blocks),*[len(b) for b in blocks])
hdr+=struct.pack('<II',len(names),max(len(n) for n in names))
for n in names: hdr+=struct.pack('<I',len(n))+n.encode()
hdr+=struct.pack('<I',0)
out=bytes(hdr)+b''.join(blocks)+struct.pack('<Ii',1,0)
open(OUT,'wb').write(out)
h2=parse(out); print('written',OUT,len(out),[(b['k'],b['type'],b['end']-b['start']) for b in h2['blocks']])
