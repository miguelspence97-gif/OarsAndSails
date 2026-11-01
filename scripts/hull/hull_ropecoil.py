# RowRim mesh step: the bow rope: knotted round the bow post, down the inside of the bow, a messy coil on the bottom just
# forward of the floorboards (looks only, no collision). New shape RowRim_BowRope with our rope texture
# textures\RowRim\boat_rope_d.dds / _n.dds (made by make_rope_tex.py); the shader is copied from byohfisherybuoyrope00.nif.
# usage: python hull_ropecoil.py <in RowRim_Hull.nif> <out nif> [buoy rope nif] [seed]
from hull_v144_lib import *
import sys
SRC,OUT=sys.argv[1],sys.argv[2]
BUOY=sys.argv[3] if len(sys.argv)>3 else 'byohfisherybuoyrope00.nif'
rng=np.random.default_rng(int(sys.argv[4]) if len(sys.argv)>4 else 7)
d=open(SRC,'rb').read(); h=parse(d)
assert h['nb']==19 and h['names'][-1]=='RowRim_Floorboards', 'expects the floorboards hull (19 blocks)'
R=1.1                  # rope radius
SIDES=6
V0,V1=0.0,1.0          # our rope texture: v once round the rope
UPER=1.0/(2*3.5*2*R)   # u per unit of length: the tile is two lay lengths, a lay 3.5 rope diameters
# ground: the highest hull / details / floorboard surface under (x, y), below a ceiling
TRS=[]
for k in (10,13,16):
    b_=h['blocks'][k]; s_,vb_,T_=load(d[b_['start']:b_['end']]); TRS.append(pos(vb_)[T_])
TR=np.concatenate(TRS)
def surf(x,y,zmax=1e9):
    a,b_,c=TR[:,0],TR[:,1],TR[:,2]
    v0=b_[:,:2]-a[:,:2]; v1=c[:,:2]-a[:,:2]; v2=np.array([x,y])-a[:,:2]
    den=v0[:,0]*v1[:,1]-v1[:,0]*v0[:,1]; ok=np.abs(den)>1e-9
    s_=(v2[:,0]*v1[:,1]-v1[:,0]*v2[:,1])/np.where(ok,den,1); r=(v0[:,0]*v2[:,1]-v2[:,0]*v0[:,1])/np.where(ok,den,1)
    m=ok&(s_>=-1e-6)&(r>=-1e-6)&(s_+r<=1+1e-6)
    z=a[m,2]+s_[m]*(b_[m,2]-a[m,2])+r[m]*(c[m,2]-a[m,2]); z=z[z<zmax]
    return z.max() if len(z) else -99.0
def rest(p,lift=0.15):   # never inside the hull: at least on the surface under it
    g=surf(p[0],p[1],p[2]+3.0)
    return np.array([p[0],p[1],max(p[2],g+R+lift)])
# ---- 1. the knot round the bow post (the post leans forward: centre y and size by height, measured from the mesh)
PZ=np.array([100,104,108,112.0]); PY=np.array([320.0,323.2,325.2,326.1]); HX=np.array([4.9,3.5,3.1,2.8]); HY=np.array([5.2,4.2,3.1,2.6])
def post(z): return np.interp(z,PZ,PY),np.interp(z,PZ,HX),np.interp(z,PZ,HY)
knot=[]
# loose end sticking up and out from the top of the wraps
knot+= [np.array([-1.5,330.5,112.5]),np.array([-2.6,329.5,110.6])]
# three round turns going down (from 109.5 to 103), starting on the bow side
for t in np.linspace(0,3,3*28):
    z=109.5-t*2.15; yc,hx,hy=post(z); th=np.pi/2+2*np.pi*t   # +y side first
    knot.append(np.array([(hx+R+0.25)*np.cos(th)*-1,yc+(hy+R+0.25)*np.sin(th),z]))
# a half hitch: back across the bow face over the turns, round the aft side, out down the inside
for t in np.linspace(0,1,30):
    z=103.0+np.sin(np.pi*t)*3.2-t*1.6; yc,hx,hy=post(z); th=np.pi/2+2*np.pi*t*1.5
    knot.append(np.array([(hx+R*2.1)*np.cos(th)*-1,yc+(hy+R*2.1)*np.sin(th),z]))
knot=np.array(knot)
# ---- 2. down the inside of the bow onto the bottom (lying on the inner face, a little slack)
dsc=[]
x0=knot[-1][0]
for y in np.arange(knot[-1][1]-2,236,-3.0):
    f=(knot[-1][1]-y)/(knot[-1][1]-236)
    x=x0*(1-f)+(-4.0+3.0*np.sin(f*5))*f
    dsc.append(np.array([x,y,surf(x,y,101.0)+R+0.3]))
dsc=np.array(dsc)
# ---- 3. a messy coil on the bottom just forward of the floorboards (y 186..212 is a shallow hollow, 38..41)
CX,CY=1.0,199.0
loops=6
rad=np.array([9.8,8.4,10.6,7.6,9.2,6.9,8.8])+rng.uniform(-0.6,0.6,7)
lay=np.array([4.8,4.0,3.1,2.3,1.5,0.7,0.0])            # top loop first (the rope comes down onto the top of the pile)
ecc=rng.uniform(0.75,1.0,7)                          # squashed loops
rot=rng.uniform(0,np.pi,7)
off=rng.uniform(-2.6,2.6,(7,2)); off[3]+=[3.5,-1.0]   # one loop slid off to the side
tilt=rng.uniform(-0.12,0.12,(7,2))
def lerp(a,t):
    i=np.clip(np.floor(t).astype(int),0,len(a)-2); f=t-i; f=f*f*(3-2*f)
    return a[i]*(1-f)[...,None]+a[i+1]*f[...,None] if a.ndim>1 else a[i]*(1-f)+a[i+1]*f
start_th=np.arctan2(dsc[-1][1]-CY,dsc[-1][0]-CX)
th=np.linspace(0,2*np.pi*loops,loops*40+1); tl=th/(2*np.pi)
r_=lerp(rad,tl); e_=lerp(ecc,tl); ro=lerp(rot,tl); oo=lerp(off,tl); tt=lerp(tilt,tl); lz=lerp(lay,tl)
ang=start_th-th          # winding direction
lx=r_*np.cos(ang-ro); ly=r_*e_*np.sin(ang-ro)
x=CX+oo[:,0]+lx*np.cos(ro)-ly*np.sin(ro); y=CY+oo[:,1]+lx*np.sin(ro)+ly*np.cos(ro)
coil=[]
for i in range(len(th)):
    base=surf(x[i],y[i],60.0)
    coil.append(np.array([x[i],y[i],base+R+0.15+lz[i]+tt[i,0]*(x[i]-CX)+tt[i,1]*(y[i]-CY)]))
coil=np.array(coil)
coil=np.array([rest(p) for p in coil])
# ---- 4. the end runs out of the bottom of the pile across the hollow
e0=coil[-1]; tdir=coil[-1]-coil[-3]; tdir[2]=0; tdir/=np.linalg.norm(tdir)
tail=[rest(e0+tdir*s+np.array([np.sin(s*0.15)*2.0,0,0])*0) for s in np.arange(2,22,2.0)]
path=np.vstack([knot,dsc,coil,np.array(tail)])
# smooth the joins a little, resample every ~1.3 units, keep it off the hull
for _ in range(2):
    path[1:-1]=path[1:-1]*0.5+(path[:-2]+path[2:])*0.25
seg=np.linalg.norm(np.diff(path,axis=0),axis=1); s=np.r_[0,np.cumsum(seg)]
S=np.arange(0,s[-1],1.6); path=np.c_[[np.interp(S,s,path[:,k]) for k in range(3)]].T
nk=int(np.searchsorted(S, np.r_[0,np.cumsum(np.linalg.norm(np.diff(knot,axis=0),axis=1))][-1]))
path=np.vstack([path[:nk],np.array([rest(p) for p in path[nk:]])])
# ---- the tube (rotation-minimising frames)
T=np.gradient(path,axis=0); T/=np.linalg.norm(T,axis=1)[:,None]
n0=np.cross(T[0],[0,0,1.0]); n0/=np.linalg.norm(n0); N=[n0]
for i in range(1,len(T)):
    v=N[-1]-T[i]*np.dot(N[-1],T[i]); N.append(v/np.linalg.norm(v))
N=np.array(N); B=np.cross(T,N)
def e8(a): return np.clip(np.round((np.asarray(a,float)+1)*127.5),0,255).astype(np.uint8)
V=[]; F=[]
L=np.r_[0,np.cumsum(np.linalg.norm(np.diff(path,axis=0),axis=1))]
for i,(p,t,n,b) in enumerate(zip(path,T,N,B)):
    for k in range(SIDES+1):
        a=2*np.pi*k/SIDES; nr=np.cos(a)*n+np.sin(a)*b
        q=p+nr*R; bit=np.cross(nr,t)
        rr=np.zeros(32,np.uint8)
        rr[0:12]=np.frombuffer(q.astype('<f4').tobytes(),np.uint8); rr[12:16]=np.frombuffer(np.float32(bit[0]).tobytes(),np.uint8)
        rr[16:20]=np.frombuffer(np.array([L[i]*UPER, V0+(V1-V0)*k/SIDES],'<f2').tobytes(),np.uint8)
        rr[20:23]=e8(nr); rr[23]=e8(bit[1]); rr[24:27]=e8(t); rr[27]=e8(bit[2]); rr[28:32]=255
        V.append(rr)
W=SIDES+1
for i in range(len(path)-1):
    for k in range(SIDES):
        a=i*W+k; b_=a+1; c=a+W; dd=c+1
        F+= [(a,c,b_),(b_,c,dd)]
# end caps (the tail end a little frayed: a flat cap pushed out)
for i,sgn in ((0,-1),(len(path)-1,1)):
    cidx=len(V); rr=V[i*W].copy(); cp=path[i]+T[i]*sgn*0.4
    rr[0:12]=np.frombuffer(cp.astype('<f4').tobytes(),np.uint8); rr[20:23]=e8(T[i]*sgn); V.append(rr)
    for k in range(SIDES):
        a=i*W+k; b_=a+1
        F.append((a,b_,cidx) if sgn>0 else (b_,a,cidx))
VB=np.array(V,np.uint8); FT=np.array(F,int)
# orient faces outward (check one)
P_=pos(VB); f0=FT[5]; nrm=np.cross(P_[f0[1]]-P_[f0[0]],P_[f0[2]]-P_[f0[0]]); cen=P_[f0].mean(0)
vb_n=VB[f0[0],20:23].astype(float)/127.5-1
if np.dot(nrm,vb_n)<0: FT=FT[:,[0,2,1]]
print('rope length %.0f, %d verts, %d tris, z %.1f..%.1f, x %.1f..%.1f, y %.1f..%.1f'%(L[-1],len(VB),len(FT),P_[:,2].min(),P_[:,2].max(),P_[:,0].min(),P_[:,0].max(),P_[:,1].min(),P_[:,1].max()))
# ---- blocks: shape (from the floorboards shape, block 16), shader + texture set from the buoy rope nif
nb=h['nb']; SH,SHD,TX=nb,nb+1,nb+2
st,_,_=load(d[h['blocks'][16]['start']:h['blocks'][16]['end']])
shape=bytearray(store(st,VB,FT)); struct.pack_into('<i',shape,0,len(h['names']))
struct.pack_into('<i',shape,st['bs_off']+20,SHD)
o=open(BUOY,'rb').read(); ho=parse(o)
shader=bytearray(o[ho['blocks'][2]['start']:ho['blocks'][2]['end']]); 
ref,=struct.unpack_from("<i",shader,40); assert ref==3; struct.pack_into("<i",shader,40,TX)
# matte: the buoy rope's shader has specular on (strength 3, gloss 20) and our rope looked wet. Specular flag off, strength 0.
f1,=struct.unpack_from('<I',shader,16); struct.pack_into('<I',shader,16,f1 & ~1)
struct.pack_into('<f',shader,72,2.0); struct.pack_into('<f',shader,88,0.0)
traw=o[ho['blocks'][3]['start']:ho['blocks'][3]['end']]
n_,=struct.unpack_from('<I',traw,0); p_=4; paths=[]
for _ in range(n_):
    l_,=struct.unpack_from('<I',traw,p_); p_+=4; paths.append(traw[p_:p_+l_].decode()); p_+=l_
paths[0]='textures\\RowRim\\boat_rope_d.dds'; paths[1]='textures\\RowRim\\boat_rope_n.dds'
tex=struct.pack('<I',len(paths))+b''.join(struct.pack('<I',len(q))+q.encode() for q in paths)
print('textures',[q for q in paths if q])
# ---- write
i=d.index(b'\n')+1; p=i+4+1+4; nb_off=p; p+=4+4
for _ in range(3):
    l=d[p]; p+=1+l
nt,=struct.unpack_from('<H',d,p); p+=2; types=[]
for _ in range(nt):
    l,=struct.unpack_from('<I',d,p); types.append(d[p+4:p+4+l].decode()); p+=4+l
ti_off=p; str_off=ti_off+2*nb+4*nb
ns,ml=struct.unpack_from('<II',d,str_off); q=str_off+8
for _ in range(ns):
    l,=struct.unpack_from('<I',d,q); q+=4+l
grp=d[q:h['hdr_end']]
newname='RowRim_BowRope'
blk=lambda k: bytes(d[h['blocks'][k]['start']:h['blocks'][k]['end']])
root=bytearray(blk(0)); nch_off=4+4+4*1+4+4+12+36+4+4
nch,=struct.unpack_from('<I',root,nch_off); kids=list(struct.unpack_from('<%di'%nch,root,nch_off+4))+[SH]
root=bytes(root[:nch_off])+struct.pack('<I',len(kids))+struct.pack('<%di'%len(kids),*kids)+bytes(root[nch_off+4+4*nch:])
blocks=[root]+[blk(k) for k in range(1,nb)]+[bytes(shape),bytes(shader),tex]
tid=lambda t: types.index(t)
ti=list(h['ti'])+[tid('BSTriShape'),tid('BSLightingShaderProperty'),tid('BSShaderTextureSet')]
hdr=bytearray(d[:nb_off])+struct.pack('<I',len(blocks))+d[nb_off+4:ti_off]
hdr+=struct.pack('<%dH'%len(blocks),*ti)+struct.pack('<%dI'%len(blocks),*[len(x) for x in blocks])
names=h['names']+[newname]
hdr+=struct.pack('<II',len(names),max(ml,len(newname)))
for nm in names: hdr+=struct.pack('<I',len(nm))+nm.encode('latin1')
hdr+=grp
foot=bytes(d[h['hdr_end']+sum(h['sizes']):])
out=bytes(hdr)+b''.join(blocks)+foot
open(OUT,'wb').write(out)
h2=parse(out); print('written',OUT,len(out),[(x['k'],x['type']) for x in h2['blocks'][16:]],h2['names'][-3:])
