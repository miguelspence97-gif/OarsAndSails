# RowRim v1.4.6 mesh step: (1) the three main thwarts 8.3 more toward the bow (middle thwart centred on the rower's seat,
# y 15); (2) a placed floorboard: loose boards (9 wide on the texture's 10 grid, 1 gaps, 1.5 thick, top z 40.3) lying on the
# frames (z 38.6) over the floor (z 38), each board cut square 4 short of the hull's curve, plus four cross battens
# under them; its own shape RowRim_Floorboards with the OAS floor texture (textures\RowRim\boat_floor_d/_n.dds).
from hull_v144_lib import *
import sys
SRC,OUT=sys.argv[1],sys.argv[2]
PLANK_DY=float(sys.argv[3]) if len(sys.argv)>3 else 8.3
d=open(SRC,'rb').read(); h=parse(d); nbk={}
GY=np.array([-194,-147,-98,-49,0,49,98,147,194.0]); GZ=np.array([73.5,69.9,68.1,67.0,66.6,67.0,68.1,69.9,73.5])
gz=lambda y:float(np.interp(y,GY,GZ))
log=[]
# ---- thwarts
b=h['blocks'][13]; s,vb,T=load(d[b['start']:b['end']]); P=pos(vb); n=0
for c in components(P,T):
    Q=P[c]; mn,mx=Q.min(0),Q.max(0)
    yc=float((mn[1]+mx[1])/2)
    if mx[2]-mn[2]<6.5 and mx[0]-mn[0]>150 and abs(yc)<130:
        dz=gz(yc+PLANK_DY)-gz(yc); P[c,1]+=PLANK_DY; P[c,2]+=dz; n+=1
        log.append('thwart y %.1f -> %.1f'%(yc,yc+PLANK_DY))
assert n==3
setpos(vb,P); nbk[13]=store(s,vb,T)
# ---- floor profile: half width of the flat floor (z <= 38.3) per y, from the hull skins
b=h['blocks'][10]; s10,vb10,T10=load(d[b['start']:b['end']]); TR=pos(vb10)[T10]
def topz(x,y):
    a,b_,c=TR[:,0],TR[:,1],TR[:,2]
    v0=b_[:,:2]-a[:,:2]; v1=c[:,:2]-a[:,:2]; v2=np.array([x,y])-a[:,:2]
    den=v0[:,0]*v1[:,1]-v1[:,0]*v0[:,1]; ok=np.abs(den)>1e-9
    s_=(v2[:,0]*v1[:,1]-v1[:,0]*v2[:,1])/np.where(ok,den,1); r=(v0[:,0]*v2[:,1]-v2[:,0]*v0[:,1])/np.where(ok,den,1)
    m=ok&(s_>=-1e-6)&(r>=-1e-6)&(s_+r<=1+1e-6)
    z=a[m,2]+s_[m]*(b_[m,2]-a[m,2])+r[m]*(c[m,2]-a[m,2])
    return z.max() if len(z) else 99
YS=np.arange(-200,201,2.0); HW=[]
for y in YS:
    w=-1.0
    for x in np.arange(0,80,0.5):
        if topz(x,y)>38.3: break
        w=x
    HW.append(w)
HW=np.minimum(np.array(HW),np.array(HW)[::-1])   # symmetric
INSET=4.0; ZB=38.8; ZT=40.3; GRID=10.0; GAP=0.5; YMAX=184.0
rng=np.random.default_rng(int(sys.argv[4]) if len(sys.argv)>4 else 1447)
V=[]; F=[]
def e8(x): return np.clip(np.round((np.asarray(x,float)+1)*127.5),0,255).astype(np.uint8)
def vert(p,uv,nrm,tan,bit):
    r=np.zeros(32,np.uint8)
    r[0:12]=np.frombuffer(np.asarray(p,'<f4').tobytes(),np.uint8); r[12:16]=np.frombuffer(np.float32(bit[0]).tobytes(),np.uint8)
    r[16:20]=np.frombuffer(np.asarray(uv,'<f2').tobytes(),np.uint8)
    r[20:23]=e8(nrm); r[23]=e8(bit[1]); r[24:27]=e8(tan); r[27]=e8(bit[2]); r[28:32]=255
    V.append(r); return len(V)-1
def quad(ps,uvs,nrm,tan,bit):
    ids=[vert(p,uv,nrm,tan,bit) for p,uv in zip(ps,uvs)]
    a,b_,c,dd=ids; pa,pb,pc=[np.array(p,float) for p in ps[:3]]
    if np.dot(np.cross(pb-pa,pc-pa),nrm)<0: F.extend([(a,c,b_),(a,dd,c)])
    else: F.extend([(a,b_,c),(a,c,dd)])
def box(x0,x1,y0,y1,z0,z1,top_uv=True):
    U=lambda x:0.5+x/200.0; Vv=lambda y:y/200.0
    quad([(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)],[(U(x0),Vv(y0)),(U(x1),Vv(y0)),(U(x1),Vv(y1)),(U(x0),Vv(y1))],(0,0,1),(0,1,0),(1,0,0))
    for sx,xx in ((-1,x0),(1,x1)):   # long sides: u from z, v along y
        quad([(xx,y0,z0),(xx,y1,z0),(xx,y1,z1),(xx,y0,z1)],[(U(xx)+ (z0-ZT)/200,Vv(y0)),(U(xx)+(z0-ZT)/200,Vv(y1)),(U(xx)+(z1-ZT)/200,Vv(y1)),(U(xx)+(z1-ZT)/200,Vv(y0))],(sx,0,0),(0,1,0),(0,0,1))
    for sy,yy in ((-1,y0),(1,y1)):   # ends: u along x, v from z
        quad([(x0,yy,z0),(x1,yy,z0),(x1,yy,z1),(x0,yy,z1)],[(U(x0),Vv(yy)+(z0-ZT)/200),(U(x1),Vv(yy)+(z0-ZT)/200),(U(x1),Vv(yy)+(z1-ZT)/200),(U(x0),Vv(yy)+(z1-ZT)/200)],(0,sy,0),(0,0,1),(1,0,0))
def prism(poly,z0,z1):
    U=lambda x:0.5+x/200.0; Vv=lambda y:y/200.0
    n=len(poly); ids=[vert((x,y,z1),(U(x),Vv(y)),(0,0,1),(0,1,0),(1,0,0)) for x,y in poly]
    for k in range(1,n-1):
        a,b_,c=ids[0],ids[k],ids[k+1]; pa,pb,pc=[np.array((*poly[j],0.0)) for j in (0,k,k+1)]
        F.append((a,b_,c) if np.cross(pb-pa,pc-pa)[2]>0 else (a,c,b_))
    cx=np.mean([p[0] for p in poly]); cy=np.mean([p[1] for p in poly])
    for k in range(n):
        (xa,ya),(xb,yb)=poly[k],poly[(k+1)%n]
        e=np.array([xb-xa,yb-ya]); L_=np.linalg.norm(e)
        if L_<1e-6: continue
        nn=np.array([e[1],-e[0]])/L_
        if nn@np.array([(xa+xb)/2-cx,(ya+yb)/2-cy])<0: nn=-nn
        t=e/L_; s0=(xa*t[0]+ya*t[1])
        quad([(xa,ya,z0),(xb,yb,z0),(xb,yb,z1),(xa,ya,z1)],
             [(0.5+s0/200,(z0-ZT)/200),(0.5+(s0+L_)/200,(z0-ZT)/200),(0.5+(s0+L_)/200,(z1-ZT)/200),(0.5+s0/200,(z1-ZT)/200)],
             (nn[0],nn[1],0),(0,0,1),(t[0],t[1],0))
def reach(x,sgn):   # farthest y (sign sgn) where the floor is wide enough at |x|
    ok=(HW-INSET>=abs(x))&(np.abs(YS)<=YMAX)
    idx=np.where(ok)[0]
    if len(idx)==0: return None
    return YS[idx[-1]] if sgn>0 else YS[idx[0]]
boards=[]
for k in range(-8,8):
    w=rng.uniform(8.3,9.4); xc=k*GRID+5+rng.uniform(-0.35,0.35)
    x0=xc-w/2; x1=xc+w/2
    ends={}
    bad=False
    for sgn in (1,-1):
        ri=reach(x0,sgn); ro=reach(x1,sgn)
        if ri is None or ro is None: bad=True; break
        # hand cut: aims at the hull's curve (the two edges' reach), misses by a few units, never square
        ya=ri-sgn*rng.uniform(0.5,4.5); yb=ro-sgn*rng.uniform(0.5,4.5)
        if abs(ya-yb)<1.5: yb-=sgn*rng.uniform(1.5,3.5)
        ends[sgn]=(ya,yb)
    if bad or ends[1][0]-ends[-1][0]<40: continue
    (ta,tb),(ba,bb)=ends[1],ends[-1]
    poly=[(x0,ba),(x1,bb),(x1,tb),(x0,ta)]
    # now and then a corner trimmed again (a second, rougher cut)
    base=[np.array(c_,float) for c_ in poly]
    for ci in range(4):
        if rng.uniform()<0.35:
            p=base[ci]; pn=base[(ci+1)%4]; pp=base[(ci-1)%4]
            a=rng.uniform(1.0,2.8); b2=rng.uniform(1.0,2.8)
            q1=p+(pp-p)/np.linalg.norm(pp-p)*a; q2=p+(pn-p)/np.linalg.norm(pn-p)*b2
            poly[ci]=[tuple(q1),tuple(q2)]
    flat=[]
    for c in poly:
        if isinstance(c,list): flat+=c
        else: flat.append(c)
    # laid by hand: a small turn about its middle, a little higher or lower
    ang=np.radians(rng.uniform(-0.6,0.6)); cy=(ta+ba)/2; ca,sa=np.cos(ang),np.sin(ang)
    flat=[(xc+(x-xc)*ca-(y-cy)*sa, cy+(x-xc)*sa+(y-cy)*ca) for x,y in flat]
    dz=rng.uniform(-0.2,0.2)
    prism(flat,ZB+dz,ZT+dz+rng.uniform(-0.15,0.15)); boards.append((x0,x1,min(ba,bb),max(ta,tb)))
# battens across, under the boards (between the frames), as wide as the boards above them
for yb in (-150.0,-52.0,52.0,150.0):
    over=[bd for bd in boards if bd[2]<=yb-3 and bd[3]>=yb+3]
    if not over: continue
    xa=min(bd[0] for bd in over)+2; xb=max(bd[1] for bd in over)-2
    box(xa,xb,yb-3,yb+3,38.0,ZB-0.05)
VB=np.array(V,np.uint8); FT=np.array(F,int)
log.append('floorboards: %d boards, %d verts, %d tris'%(len(boards),len(VB),len(FT)))
for bd in boards: log.append('  board x %.1f..%.1f y %.1f..%.1f'%bd)
# ---- new shape blocks from the OAS floor nif (BSTriShape 16, shader 17, texture set 18: same indices here)
o=open('oasfloor.nif','rb').read(); ho=parse(o)
so,_,_=load(o[ho['blocks'][16]['start']:ho['blocks'][16]['end']])
shape=bytearray(store(so,VB,FT)); struct.pack_into('<i',shape,0,len(h['names']))  # name -> the new string
shader=o[ho['blocks'][17]['start']:ho['blocks'][17]['end']]
def texset(paths):
    out=struct.pack('<I',len(paths))
    for p_ in paths: out+=struct.pack('<I',len(p_))+p_.encode()
    return out
traw=o[ho['blocks'][18]['start']:ho['blocks'][18]['end']]
n_,=struct.unpack_from('<I',traw,0); p=4; paths=[]
for _ in range(n_):
    l,=struct.unpack_from('<I',traw,p); p+=4; paths.append(traw[p:p+l].decode()); p+=l
paths=[q.replace('OarsAndSails','RowRim') for q in paths]
tex=texset(paths)
# ---- write: header with 3 more blocks, one more string; root gets child 16
nb=h['nb']; assert nb==16
i=d.index(b'\n')+1; p=i+4+1+4; nb_off=p; p+=4+4
for _ in range(3):
    l=d[p]; p+=1+l
nt,=struct.unpack_from('<H',d,p); p+=2; types=[]
for _ in range(nt):
    l,=struct.unpack_from('<I',d,p); types.append(d[p+4:p+4+l].decode()); p+=4+l
ti_off=p; sz_off=ti_off+2*nb; str_off=sz_off+4*nb
ns,ml=struct.unpack_from('<II',d,str_off); q=str_off+8
for _ in range(ns):
    l,=struct.unpack_from('<I',d,q); q+=4+l
grp=d[q:h['hdr_end']]
newname='RowRim_Floorboards'
blk=lambda k: nbk.get(k) or bytes(d[h['blocks'][k]['start']:h['blocks'][k]['end']])
root=bytearray(blk(0)); nch_off=4+4+4*1+4+4+12+36+4+4
nch,=struct.unpack_from('<I',root,nch_off); kids=list(struct.unpack_from('<%di'%nch,root,nch_off+4))+[16]
root=bytes(root[:nch_off])+struct.pack('<I',len(kids))+struct.pack('<%di'%len(kids),*kids)+bytes(root[nch_off+4+4*nch:])
blocks=[root]+[blk(k) for k in range(1,nb)]+[bytes(shape),shader,tex]
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
print('\n'.join(log)); print('textures',paths); print('written',OUT,len(out))
h2=parse(out); print([ (x['k'],x['type']) for x in h2['blocks'][14:]], h2['names'])
