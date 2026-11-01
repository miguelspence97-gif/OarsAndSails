# RowRim: the plankton sprite. One small camera-facing glow quad (NiBillboardNode), additive, with its own texture
# textures\RowRim\plankton_glow.dds. Block layouts copied from the vanilla-style spell glow maginvabsorb_blue.nif (blocks 20-23).
# usage: python make_plankton_nif.py <template maginvabsorb_blue.nif> <out RowRim_Plankton.nif> [soft]
# 'soft' makes RowRim_PlanktonSoft.nif: the wide, dim, diffuse glow for plankton under the surface (plankton_soft.dds,
# deeper blue-green, soft falloff 12 so it melts into the water surface it lies on).
from hull_v144_lib import *
import sys
SRC,OUT=sys.argv[1],sys.argv[2]
d=open(SRC,'rb').read(); h=parse(d)
SOFT=len(sys.argv)>3 and sys.argv[3]=='soft'
TEX='textures\\RowRim\\plankton_soft.dds' if SOFT else 'textures\\RowRim\\plankton_glow.dds'
COLOR=(0.16,0.62,0.85,1.0) if SOFT else (0.30,0.85,1.0,1.0)   # sea-sparkle blue-green (additive); the soft one deeper and dimmer
SCALE=0.9 if SOFT else 2.2
SOFTDEPTH=12.0 if SOFT else 2.0
R=1.0                                     # quad half size (the code scales each sprite)
def node(name,child,billboard=None):
    out=struct.pack('<iIi',name,0,-1)+struct.pack('<I',14)+struct.pack('<3f',0,0,0)+struct.pack('<9f',1,0,0,0,1,0,0,0,1)+struct.pack('<f',1.0)+struct.pack('<i',-1)
    out+=struct.pack('<Ii',1,child)+struct.pack('<I',0)
    if billboard is not None: out+=struct.pack('<H',billboard)
    return out
# check our node writer against the template billboard node
tb=d[h['blocks'][20]['start']:h['blocks'][20]['end']]
mine=node(0x0e,0x15,2); assert mine==tb, (mine.hex(),tb.hex())
# shape: the template's header, our 4 verts
st,vb,T=load(d[h['blocks'][21]['start']:h['blocks'][21]['end']])
def e8(a): return np.clip(np.round((np.asarray(a,float)+1)*127.5),0,255).astype(np.uint8)
V=[]
for (x,y,u,v) in ((-R,-R,0,1),(R,-R,1,1),(R,R,1,0),(-R,R,0,0)):
    r=np.zeros(32,np.uint8)
    r[0:12]=np.frombuffer(np.array([x,y,0],'<f4').tobytes(),np.uint8); r[12:16]=np.frombuffer(np.float32(0.0).tobytes(),np.uint8)  # bitangent x
    r[16:20]=np.frombuffer(np.array([u,v],'<f2').tobytes(),np.uint8)
    r[20:23]=e8((0,0,1)); r[23]=e8(1.0)            # normal +z, bitangent y
    r[24:27]=e8((1,0,0)); r[27]=e8(0.0)            # tangent +x, bitangent z
    r[28:32]=255
    V.append(r)
shape=bytearray(store(st,np.array(V,np.uint8),np.array([(0,1,2),(0,2,3)])))
struct.pack_into('<i',shape,0,-1)                 # no name
struct.pack_into('<f',shape,64,1.0)               # scale 1 (template 1.19)
struct.pack_into('<3i',shape,st['bs_off']+16,-1,3,4)  # skin, shader, alpha
# effect shader: template block 22 with our texture, colour and strength
s=d[h['blocks'][22]['start']:h['blocks'][22]['end']]
assert struct.unpack_from('<I',s,36)[0]==0     # template has an empty source texture
sh=bytearray(s[:36])+struct.pack('<I',len(TEX))+TEX.encode()+bytearray(s[40:])
o=40+len(TEX)                                     # clamp/light/lod/pad at o, falloff 4f at o+4, colour at o+20
struct.pack_into('<4f',sh,o+20,*COLOR); struct.pack_into('<f',sh,o+36,SCALE); struct.pack_into('<f',sh,o+40,SOFTDEPTH)
alpha=bytearray(d[h['blocks'][23]['start']:h['blocks'][23]['end']])
struct.pack_into('<H',alpha,12,0x100D)            # blend on, src alpha + dest ONE (additive), no sorting
alpha[14]=0
blocks=[node(0,1),node(-1,2,2),bytes(shape),bytes(sh),bytes(alpha)]
types=['NiNode','NiBillboardNode','BSTriShape','BSEffectShaderProperty','NiAlphaProperty']
names=['RowRim_PlanktonSoft' if SOFT else 'RowRim_Plankton']
# header: copy up to the block count, then our tables
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
h2=parse(out); print('written',OUT,len(out),[(b['k'],b['type'],b['end']-b['start']) for b in h2['blocks']],h2['names'])
