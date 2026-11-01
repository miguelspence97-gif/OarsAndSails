# RowRim: clutter in the hull (meshes\RowRim\RowRim_Hull.nif). Vanilla meshes imported as they are (their own
# textures), placed by a wrapper NiNode each; a bedroll roll, a fishing rod with its line, and fish bones built here.
import sys, struct, numpy as np
from nifmerge import *
from meshes import *
SRC,OUT,NIFS=sys.argv[1],sys.argv[2],sys.argv[3]
d=open(SRC,'rb').read(); n=Nif(d)
assert n.names[-1]=='RowRim_BowRope' and len(n.blocks)==22, 'expects the rope hull (22 blocks): run on blender/RowRim_Hull_before_clutter.nif'
deg=np.pi/180
FLOOR=40.3
TPL=n.blocks[16]                     # floorboards shape: the 32-byte vertex head
SHADER=n.blocks[17]                  # floorboards lighting shader (matte wood)
def shader_with(paths, src=SHADER):
    t=n.add('BSShaderTextureSet',tex_set(paths))
    r=bytearray(src); r,_=av_fix(bytes(r),4); r=bytearray(r); struct.pack_into('<i',r,40,t)
    return n.add('BSLightingShaderProperty',r)
def wrap(name,kids,R=np.eye(3),T=(0,0,0),S=1.0):
    return n.add('NiNode',node_block(n.name(name),R,T,S,kids))
# v1.7.6: EVERY shape gets its own shader property (a shader property shared by two shapes, ours or the hull's, greyed the
# hull's textures and froze the game; vanilla nifs share texture sets at most, never shader properties)
FLOORT=['textures\\RowRim\\boat_floor_d.dds','textures\\RowRim\\boat_floor_n.dds']
ROPET=['textures\\RowRim\\boat_rope_d.dds','textures\\RowRim\\boat_rope_n.dds']
KWILAT=['textures\\RowRim\\boat_oar_kwila_d.dds','textures\\RowRim\\boat_floor_n.dds']
def custom(p,tex,alpha=-1):
    p=orient(p); return build_shape(n,TPL,p[0],p[1],p[2],p[3],p[4],shader_with(tex),alpha)
def imp(f): return import_shapes(n,open(NIFS+'/'+f,'rb').read(),shaders=False)  # v1.7.5: no vanilla shaders / textures brought in
def use_shader(shapes, tex, alpha=-1):
    # v1.7.5: placeholder textures (our own only); v1.7.6: a new shader property for each shape
    for k in shapes:
        shd=shader_with(tex)
        r=bytearray(n.blocks[k]); struct.pack_into('<2i',r,72+16+4,shd,alpha); n.blocks[k]=bytes(r)
    return shapes
kids=[]

# 1. bucket by the stern, on the boards, right of the stern seat
kids.append(wrap('RowRim_Clutter_Bucket',use_shader(imp('bucket01.nif'),FLOORT),mat_rz(25*deg),(24,-148,FLOOR),0.7))

# 2. the spare oar, laid lengthways on the floor along the left side under the three thwarts (the boat's own kwila)
oar=imp('shipoar01.nif')
use_shader([oar[0]],KWILAT)
use_shader([k for k in oar if k!=oar[0]],ROPET)   # the rope wrap: the hull's rope
kids.append(wrap('RowRim_Clutter_SpareOar',oar,mat_rz(90*deg)@mat_rx(4*deg),(-52,6,FLOOR+3.2),1.0))

# 3. (v1.7.1: the ale keg is out for now: it clipped the aft thwart)

# 4. bedroll: the game's own bedroll (bedroll01.nif, its real textures and UVs) rolled up into a spiral and tied under the
# bow thwart (y 101.8..125.5, underside 62.6) with two rope straps round the plank
from nifmerge import av_fix
bd=open(NIFS+'/bedroll01.nif','rb').read(); bh=parse(bd)
bshader=bytes(bd[bh['blocks'][9]['start']:bh['blocks'][9]['end']])
balpha=bytes(bd[bh['blocks'][11]['start']:bh['blocks'][11]['end']])

from hull_v144_lib import load as _load
_,bv,bt=_load(bytes(bd[bh['blocks'][8]['start']:bh['blocks'][8]['end']]))
def dec(V):
    P=V[:,0:12].copy().view('<f4').reshape(-1,3).astype(float)
    UV=V[:,16:20].copy().view('<f2').reshape(-1,2).astype(float)
    N=V[:,20:23].astype(float)/127.5-1; T=V[:,24:27].astype(float)/127.5-1
    return P,UV,N,T
P,UV,N,T=dec(bv); F=[tuple(t) for t in bt]
# split every edge longer than 6 along the bedroll's length (y), so the roll bends smoothly
P=list(P);UV=list(UV);N=list(N);T=list(T)
for it in range(8):
    mid={}; out=[]; changed=False
    def m(a,b):
        k=(min(a,b),max(a,b))
        if k not in mid:
            P.append((P[a]+P[b])/2); UV.append((UV[a]+UV[b])/2)
            nn=N[a]+N[b]; N.append(nn/(np.linalg.norm(nn)+1e-9)); tt=T[a]+T[b]; T.append(tt/(np.linalg.norm(tt)+1e-9))
            mid[k]=len(P)-1
        return mid[k]
    for (a,b_,c) in F:
        e=[(abs(P[a][1]-P[b_][1]),0),(abs(P[b_][1]-P[c][1]),1),(abs(P[c][1]-P[a][1]),2)]
        L,i=max(e)
        if L<=6.0: out.append((a,b_,c)); continue
        changed=True
        if i==0: x=m(a,b_); out+= [(a,x,c),(x,b_,c)]
        elif i==1: x=m(b_,c); out+= [(a,b_,x),(a,x,c)]
        else: x=m(c,a); out+= [(a,b_,x),(x,b_,c)]
    F=out
    if not changed: break
P=np.array(P)*0.85; UV=np.array(UV); N=np.array(N); T=np.array(T); F=np.array(F)
ymax=P[:,1].max(); zmid=np.median(P[:,2])
A0=1.2; B0=1.8/(2*np.pi)                      # core radius, 1.8 per turn
s_=ymax-P[:,1]
th=(-A0+np.sqrt(A0*A0+2*B0*s_))/B0
r=A0+B0*th+(P[:,2]-zmid)*0.3
er=np.c_[np.zeros(len(P)),np.cos(th),np.sin(th)]; et=np.c_[np.zeros(len(P)),-np.sin(th),np.cos(th)]
def rot(v): return np.c_[v[:,0],np.zeros(len(v)),np.zeros(len(v))]+(-v[:,1:2])*et+v[:,2:3]*er
RP=np.c_[P[:,0],np.zeros(len(P)),np.zeros(len(P))]+r[:,None]*er
RN=rot(N); RT=rot(T)
RR=float(r.max())+0.3
RC=np.array([0.0,113.7,62.6-0.4-RR])
RP=RP+RC
print('bedroll rolled: %d verts, %.1f turns, outer radius %.1f, length %.1f'%(len(RP),th.max()/(2*np.pi),RR,P[:,0].max()-P[:,0].min()))
straps=[]
for sx in (-19.0,19.0):
    rect=[(101.4,66.6),(126.0,66.6),(126.0,62.3),(101.4,62.3)]
    circ=[(RC[1]+(RR+0.6)*np.cos(a),RC[2]+(RR+0.6)*np.sin(a)) for a in np.linspace(0,2*np.pi,40,endpoint=False)]
    P2=np.array(rect+circ); P2=P2[np.lexsort((P2[:,1],P2[:,0]))]
    def cr(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    lo=[];up=[]
    for p in P2:
        while len(lo)>=2 and cr(lo[-2],lo[-1],p)<=0: lo.pop()
        lo.append(tuple(p))
    for p in P2[::-1]:
        while len(up)>=2 and cr(up[-2],up[-1],p)<=0: up.pop()
        up.append(tuple(p))
    hull=lo[:-1]+up[:-1]
    H=np.array(hull+[hull[0]]); L=np.concatenate([[0],np.cumsum(np.linalg.norm(np.diff(H,axis=0),axis=1))])
    tt=np.linspace(0,L[-1],60,endpoint=False)
    straps.append(tube(np.c_[np.full(60,sx),np.interp(tt,L,H[:,0]),np.interp(tt,L,H[:,1])],0.55,m=6,vscale=8.0,closed=True))
rollshape=build_shape(n,TPL,RP,RN,RT,UV,F,shader_with(ROPET),-1)   # v1.7.5: placeholder: the hull's rope texture (no vanilla bedroll texture)
bed=[rollshape,custom(merge(straps),ROPET)]
kids.append(wrap('RowRim_Clutter_Bedroll',bed))

# 5. fishing rod standing in a holder fixed to the inside of the right side by the stern, leaning out over the gunwale
# (v1.7.1: no line; the butt in a holder tube strapped to a wooden mount block on the hull's inner side)
BUTT=np.array([47.0,-173.0,49.0]); EL=np.radians(55); AZ=np.radians(22)
D=np.array([np.cos(EL)*np.cos(AZ),-np.cos(EL)*np.sin(AZ),np.sin(EL)])
side=np.cross(D,[0,0,1.]); side/=np.linalg.norm(side); upv=np.cross(side,D)
ROD_R=np.c_[D,side,upv]           # columns: rod x, y, z in model space
L=150.0
xs=np.linspace(0,L,16)
pole=tube(np.c_[xs,np.zeros(16),np.zeros(16)],1.5-1.15*(xs/L),m=8,vscale=30.0,caps=True)
grip=tube(np.c_[np.linspace(0,26,6),np.zeros(6),np.zeros(6)],2.1,m=10,vscale=6.0,caps=True)
reel=tube(np.c_[np.full(5,21.0),np.linspace(-1.6,1.6,5),np.full(5,-4.6)],3.4,m=14,vscale=6.0,caps=True)
reelarm=tube(np.array([[21,0,-1.2],[21,0,-2.6]]),0.6,m=6)
handle=tube(np.array([[21,2.0,-4.6],[19.5,3.6,-7.2]]),0.45,m=6)
guides=[tube(np.array([[x,0,1.6-1.0*x/L],[x,0,3.4-1.6*x/L]]),0.22,m=5) for x in (60,90,115,140)]
rod_wood=xform(merge([pole,reelarm,handle]+guides),ROD_R,BUTT)
rod_grip=xform(grip,ROD_R,BUTT)
rod_reel=xform(reel,ROD_R,BUTT)
# the holder: a short wooden tube round the grip's lower end, and a mount block on the hull's inner side
holder=xform(tube(np.c_[np.linspace(-2,16,4),np.zeros(4),np.zeros(4)],2.9,m=12,vscale=8.0,caps=True),ROD_R,BUTT)
hc=BUTT+D*7.0                         # the holder's middle
def box(c,size,R):
    sx,sy,sz=np.asarray(size)/2; parts=[]
    for ax,sg in ((0,1),(0,-1),(1,1),(1,-1),(2,1),(2,-1)):
        nrm=np.zeros(3); nrm[ax]=sg
        u=np.zeros(3); u[(ax+1)%3]=1; v=np.cross(nrm,u)
        ext=np.array([sx,sy,sz])
        corners=[nrm*ext[ax]+uu*u*ext[(ax+1)%3]+vv*v*ext[(ax+2)%3] for uu,vv in ((-1,-1),(1,-1),(1,1),(-1,1))]
        V=np.array(corners)@R.T+c; Nn=np.tile(nrm@R.T,(4,1)); Tg=np.tile(u@R.T,(4,1))
        UVv=np.array([[0,0],[1,0],[1,1],[0,1]])*np.array([max(ext[(ax+1)%3],1)/8,max(ext[(ax+2)%3],1)/8])
        parts.append([V,Nn,Tg,UVv,np.array([[0,1,2],[0,2,3]])])
    return merge(parts)
# the block: against the inner side (x ~63-67 there), under the gunwale
wall_tilt=np.radians(18)              # the side leans out going up
BR=mat_ry(-wall_tilt)
block=box(np.array([62.6,-176.0,57.0]),(3.2,12.0,17.0),BR)
# a cross piece from the block to the holder, and two rope bands round the holder
arm=box((hc+np.array([62.0,-176.0,55.5]))/2,(np.linalg.norm(hc[:2]-np.array([62.0,-176.0]))+3.0,4.0,3.0),mat_rz(np.arctan2(hc[1]+176.0,hc[0]-62.0)))
bands=[]
for t in (2.0,12.0):
    c0=BUTT+D*t
    ang=np.linspace(0,2*np.pi,24,endpoint=False)
    ring=c0+np.outer(np.cos(ang),side)*3.3+np.outer(np.sin(ang),upv)*3.3
    bands.append(tube(ring,0.5,m=5,vscale=6.0,closed=True))
rod=[custom(rod_wood,KWILAT),custom(rod_grip,ROPET),custom(merge([rod_reel,holder,block,arm]),FLOORT),custom(merge(bands),ROPET)]
kids.append(wrap('RowRim_Clutter_Rod',rod))
print('rod butt',BUTT,'tip',(BUTT+D*L).round(1))

# 6. fish bones: a flat sprite (Miguel's Gemini picture, textures\RowRim\fishbones_d.dds 512x128, alpha tested) lying on
# the hull's bottom by the coiled rope
alp_fb=n.add('NiAlphaProperty',struct.pack('<iIiHB',-1,0,-1,0x12EC,110))  # alpha test, no blend
FL=26.0; FW=FL/3.575
q=np.array([[-FL/2,-FW/2,0],[FL/2,-FW/2,0],[FL/2,FW/2,0],[-FL/2,FW/2,0]])
fb=[q,np.tile([0,0,1.],(4,1)),np.tile([1.,0,0],(4,1)),np.array([[0,1],[1,1],[1,0],[0,0]],float),np.array([[0,1,2],[0,2,3]])]
# v1.7.3: not on the boards: down in the hull's bottom left of the rope coil, between two frames (y 213-241), laid lengthways on the planking's slope
FB_R=np.array([[0.0, -0.9866, 0.1629], [0.9487, -0.0515, -0.3119], [0.3162, 0.1545, 0.936]])
fb=xform(fb,FB_R,np.array([-21.00,227.00,47.72]))
kids.append(wrap('RowRim_Clutter_FishBones',[build_shape(n,TPL,*orient(fb)[:4],orient(fb)[4],shader_with(['textures\\RowRim\\fishbones_d.dds','textures\\RowRim\\fishbone_n.dds']),alp_fb)]))

root_add_kids(n,kids)
from collections import Counter
use=Counter()
for k,b in enumerate(n.blocks):
    if n.types[n.ti[k]]=='BSTriShape':
        sh,al=struct.unpack_from('<2i',b,72+16+4); use[sh]+=1
        if al>=0: use[('a',al)]+=1
dup={k:v for k,v in use.items() if v>1 and k!=-1}
assert not dup, ('shared properties',dup)
out=n.write(); open(OUT,'wb').write(out)
h2=parse(out); print('written',OUT,len(out),'blocks',h2['nb'],'names',h2['names'][8:])
