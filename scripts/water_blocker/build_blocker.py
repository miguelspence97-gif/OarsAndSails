# RowRim water blocker test nif: the OAS bilge floor hull (vanilla ShipRowBoat01 + BilgeFloor) plus flat, hull-shaped
# "water blocker" planes at model z ZB, one per material variant, each with its own shader (and alpha property):
#   WB_Debug  visible (hull texture), to check the outline
#   WB_A      alpha blend (src alpha / inv src alpha), material + vertex alpha 0, z write on
#   WB_B      as A plus the alpha property's "no sorter" bit
#   WB_C      blend ZERO / ONE (colour untouched) + alpha test ALWAYS, alpha 1, z write on
import sys, struct; sys.path.insert(0,'.')
import numpy as np
from nifp import *; from verts import *; from outline import outline
# v0.4.1: a set of planes at model z 30..48 (every 2), each cut to the hull's inside at its own height; the plugin shows
# the one just above the current waterline. Two materials share one shader each: WB_A_<z> (alpha 0, z write) and
# WB_D_<z> (debug, hull texture).
SRC=sys.argv[1]; OUT=sys.argv[2]
HEIGHTS=list(range(30,50,2))
d=open(SRC,'rb').read(); h=parse(d); nb=h['nb']
DIL=2.5
def plane(ZB):
    rows=outline(d,h,ZB)
    L=rows[:,1]-DIL; R=rows[:,2]+DIL; Y=rows[:,0]
    P=[];tris=[]
    for y,l,r in zip(Y,L,R): P+=[(l,y,ZB),(r,y,ZB)]
    for k in range(len(Y)-1):
        a,b,c,e=2*k,2*k+1,2*k+3,2*k+2
        tris+=[(a,b,c),(a,c,e)]
    P=np.array(P,dtype=np.float32); tris=np.array(tris)
    for t in range(len(tris)):
        pa,pb,pc=P[tris[t]]
        if np.cross(pb-pa,pc-pa)[2]<0: tris[t]=tris[t][[0,2,1]]
    return P,tris
b8=lambda a:np.clip(np.round((np.asarray(a,dtype=np.float32)+1)*127.5),0,255).astype(np.uint8)
def vdata(P,tris,alpha_byte):
    nv=len(P)
    uv=np.c_[0.5+P[:,0]/200.0, P[:,1]/200.0].astype(np.float16)
    vb=np.zeros((nv,32),dtype=np.uint8)
    vb[:,0:12]=P.view(np.uint8).reshape(nv,12)
    vb[:,12:16]=np.float32(0.0).tobytes()*1 and np.frombuffer(np.float32(0.0).tobytes(),np.uint8)  # bitangent x = 0
    vb[:,16:20]=uv.view(np.uint8).reshape(nv,4)
    vb[:,20:23]=b8([0,0,1]); vb[:,23]=b8(1.0)        # normal up, bitangent y = 1
    vb[:,24:27]=b8([1,0,0]); vb[:,27]=b8(0.0)        # tangent x, bitangent z = 0
    vb[:,28:31]=255; vb[:,31]=alpha_byte
    return vb.tobytes()+tris.astype('<u2').tobytes()
# template blocks
tb=h['blocks'][16]; traw=d[tb['start']:tb['end']]
p=4; ne,=struct.unpack_from("<I",traw,p); p+=4+4*ne; p+=4+4+12+36+4+4
bs_off=p; p+=16; refs_off=p; p+=12; p+=8; cnt_off=p; p+=8; data_off=p
sb=h['blocks'][14]; sraw=d[sb['start']:sb['end']]
materials=[('A',0x00ED,0.0,0),('D',None,1.0,255)]
names=list(h['names']); types=list(h['types'])
if 'NiAlphaProperty' not in types: types.append('NiAlphaProperty')
t_alpha=types.index('NiAlphaProperty'); t_shape=h['ti'][16]; t_shader=h['ti'][14]
new_blocks=[]; new_children=[]; nxt=nb; mat_refs={}
for key,aflags,alpha,vab in materials:   # one shader (+ alpha property) per material, shared by its planes
    shd=bytearray(sraw); struct.pack_into('<f',shd,64,alpha)
    new_blocks.append((t_shader,bytes(shd))); shader_i=nxt; nxt+=1
    alpha_i=-1
    if aflags is not None:
        new_blocks.append((t_alpha,struct.pack('<iIiHB',-1,0,-1,aflags,0))); alpha_i=nxt; nxt+=1
    mat_refs[key]=(shader_i,alpha_i,vab)
for ZB in HEIGHTS:
    P,tris=plane(ZB); nv=len(P); nt=len(tris)
    cen=P.mean(0); rad=float(np.linalg.norm(P-cen,axis=1).max())
    for key,_,_,_ in materials:
        shader_i,alpha_i,vab=mat_refs[key]
        names.append('WB_%s_%d'%(key,ZB))
        sh=bytearray(traw[:data_off])
        struct.pack_into('<i',sh,0,len(names)-1)
        struct.pack_into('<4f',sh,bs_off,float(cen[0]),float(cen[1]),float(cen[2]),rad)
        struct.pack_into('<iii',sh,refs_off,-1,shader_i,alpha_i)
        vd=vdata(P,tris,vab)
        struct.pack_into('<HHI',sh,cnt_off,nt,nv,len(vd))
        sh+=vd+struct.pack('<I',0)
        new_blocks.append((t_shape,bytes(sh))); new_children.append(nxt); nxt+=1
# root node: add the children
rs=h['hdr_end']; raw0=bytearray(d[rs:rs+h['sizes'][0]])
nch_off=4+4+4*1+4+4+12+36+4+4
nch,=struct.unpack_from('<I',raw0,nch_off)
ne0,=struct.unpack_from('<I',raw0,4); assert ne0==1, ne0
raw0[nch_off:nch_off+4]=struct.pack('<I',nch+len(new_children))
ins=nch_off+4+4*nch; raw0[ins:ins]=struct.pack('<%di'%len(new_children),*new_children)
# header
i=d.index(b'\n')+1
p=i+4+1+4; nb_off=p; p+=4+4
for _ in range(3):
    l=d[p]; p+=1+l
types_off=p; nt_types,=struct.unpack_from('<H',d,p); p+=2
for _ in range(nt_types):
    l,=struct.unpack_from('<I',d,p); p+=4+l
ti_off=p; p+=2*nb; p+=4*nb
nsx,mlx=struct.unpack_from('<II',d,p); p+=8
for _ in range(nsx):
    l,=struct.unpack_from('<I',d,p); p+=4+l
tail=d[p:h['hdr_end']]
nbn=nb+len(new_blocks)
hdr=bytearray(d[:nb_off])+struct.pack('<I',nbn)+d[nb_off+4:types_off]
hdr+=struct.pack('<H',len(types))
for t in types: hdr+=struct.pack('<I',len(t))+t.encode()
ti=list(h['ti'])+[b[0] for b in new_blocks]
hdr+=struct.pack('<%dH'%nbn,*ti)
sz=list(h['sizes']); sz[0]=len(raw0); sz+=[len(b[1]) for b in new_blocks]
hdr+=struct.pack('<%dI'%nbn,*sz)
hdr+=struct.pack('<II',len(names),max(len(n) for n in names))
for n in names: hdr+=struct.pack('<I',len(n))+n.encode('latin1')
hdr+=tail
body=bytearray(raw0)
for k in range(1,nb):
    st=h['hdr_end']+sum(h['sizes'][:k]); body+=d[st:st+h['sizes'][k]]
for _,b in new_blocks: body+=b
foot=d[h['hdr_end']+sum(h['sizes']):]
out=bytes(hdr)+bytes(body)+foot
open(OUT,'wb').write(out); print('written',OUT,len(out),'blocks',nbn,'planes',len(new_children))
h2=parse(out)
for b in h2['blocks'][nb:]:
    if b['type']=='BSTriShape':
        s=tri_shape(out,h2,b); v=decode(out,s); print(' check',s['name'],s['nv'],s['ntri'],'shader',s['shader'],'alpha',s['alpha'],'z',v['pos'][:,2].min(),v['pos'][:,2].max(),'col a',v['col'][:,3].max())
print(' types',h2['types'],' footer',foot.hex())
