# RowRim hull v1: the vanilla ShipRowBoat01 meshes, edited in place (no added shapes):
#  - floor: the inner hull skin's bottom raised to model z FLOOR (vertices below it lifted onto it, normals up), so the
#    lake surface (model z 32 at rest) is under the boards
#  - ribs (the five frames): their parts under the new floor laid onto it (z FLOOR + 0.6), so they run on across the floor
#  - stern seat: a fourth plank, a copy of the aft thwart by the tiller (y -215), top 66.3, tapered to the hull
#  - thwarts (the three seat planks) raised flush with the gunwale top, widened into the gunwale, and squared off: the
#    vanilla planks have cut / worn corners; every vertex at a plank's ends goes to the full end line, so it is a full board
#  - oarlocks (forks + pads, all six) moved OARLOCK_DY along the boat (negative = toward the stern, the -Y end), following
#    the gunwale's curve and sheer
#  - hull skins (inner and outer, shape 10): smooth normals everywhere (the vanilla split-normal creases between the
#    planks removed), then LEVELS (2) rounds of curved subdivision, each followed by smoothing; before the floor is lifted
# Source: the OAS bilge hull (vanilla shapes byte-identical); its BilgeFloor shape (blocks 16-18) is dropped.
import sys, struct; sys.path.insert(0,'.')
import numpy as np
from nifp import *; from verts import *; from subdiv import subdivide, smooth_normals
SRC=sys.argv[1]; OUT=sys.argv[2]
FLOOR=float(sys.argv[3]) if len(sys.argv)>3 else 38.0
OARLOCK_DY=float(sys.argv[4]) if len(sys.argv)>4 else -30.0
RIB_LIFT=0.6
d=bytearray(open(SRC,'rb').read()); h=parse(bytes(d))
b8=lambda a:np.clip(np.round((np.asarray(a,dtype=np.float32)+1)*127.5),0,255).astype(np.uint8)
newblock={}
def load(k):
    raw=newblock.get(k) or bytes(d[h['blocks'][k]['start']:h['blocks'][k]['end']])
    p=4; ne,=struct.unpack_from('<I',raw,p); p+=4+4*ne; p+=4+4+12+36+4+4
    bs_off=p; p+=16+12+8; cnt_off=p; ntri,nv,dsz=struct.unpack_from('<HHI',raw,p); p+=8; data_off=p
    vb=np.frombuffer(raw,dtype=np.uint8,count=nv*32,offset=data_off).reshape(nv,32).copy()
    tris=np.frombuffer(raw,dtype='<u2',count=ntri*3,offset=data_off+nv*32).reshape(-1,3).astype(int)
    s=dict(raw=raw,bs_off=bs_off,cnt_off=cnt_off,data_off=data_off,tail=raw[data_off+dsz:])
    return s,vb,tris
def pos(vb): return vb[:,0:12].copy().view('<f4').reshape(-1,3)
def setpos(vb,P): vb[:,0:12]=np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1,12)
def store(k,s,vb,tris=None):
    if tris is None: tris=np.frombuffer(s['raw'],dtype='<u2',count=struct.unpack_from('<H',s['raw'],s['cnt_off'])[0]*3,offset=s['data_off']+struct.unpack_from('<H',s['raw'],s['cnt_off']+2)[0]*32).reshape(-1,3)
    assert len(vb)<65535
    data=vb.tobytes()+np.asarray(tris).astype('<u2').tobytes()
    hd=bytearray(s['raw'][:s['data_off']])
    P=pos(vb); cen=P.mean(0); rad=float(np.linalg.norm(P-cen,axis=1).max())
    struct.pack_into('<4f',hd,s['bs_off'],float(cen[0]),float(cen[1]),float(cen[2]),rad)
    struct.pack_into('<HHI',hd,s['cnt_off'],len(tris),len(vb),len(data))
    newblock[k]=bytes(hd)+data+s['tail']
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
# gunwale top (measured from the rim's top stations, symmetric): y -> top z, top centre x, inner face x
GY=np.array([-194,-147,-98,-49,0,49,98,147,194.0])
GZ=np.array([73.5,69.9,68.1,67.0,66.6,67.0,68.1,69.9,73.5])
GX=np.array([63.5,80.1,87.0,92.6,94.2,92.6,87.0,80.1,63.5])
GI=np.array([61.2,77.5,84.1,89.5,91.1,89.5,84.1,77.5,61.2])
gz=lambda y:float(np.interp(y,GY,GZ)); gx=lambda y:float(np.interp(y,GY,GX)); gi=lambda y:float(np.interp(y,GY,GI))
log=[]
# --- hull skins: smooth (no hard creases), then LEVELS rounds of curved subdivision, smoothing again after each
LEVELS=int(sys.argv[5]) if len(sys.argv)>5 else 1
s,vb,T=load(10); n0,t0=len(vb),len(T)
vb=smooth_normals(vb,T)
for _ in range(LEVELS):
    vb,T=subdivide(vb,T); vb=smooth_normals(vb,T)
store(10,s,vb,T); log.append('hull skins: smooth normals + %d subdivisions: %d verts / %d tris -> %d / %d'%(LEVELS,n0,t0,len(vb),len(T)))
# --- floor: shape 10, inner skin (the component whose lowest point is above 10)
s,vb,T=load(10); P=pos(vb)
inner=[c for c in components(P,T) if P[c][:,2].min()>10][0]
low=inner[P[inner][:,2]<FLOOR]
P[low,2]=FLOOR
vb[low,20:23]=b8([0,0,1])
setpos(vb,P); store(10,s,vb,T); log.append('floor: %d inner-skin vertices lifted to z %.1f'%(len(low),FLOOR))
# --- thwarts and ribs in shape 13 (thwart tops THWART_DROP below the gunwale top)
THWART_DROP=2.5
s,vb,T=load(13); P=pos(vb)
for c in components(P,T):
    Q=P[c]; mn,mx=Q.min(0),Q.max(0)
    if len(c)>=40 and len(c)<=60 and mx[2]-mn[2]<6 and mx[0]-mn[0]>150:   # a flat plank across the boat: a thwart
        yc=float((mn[1]+mx[1])/2); dz=gz(yc)-THWART_DROP-mx[2]; xe=gi(yc)+2.0
        Q=Q.copy(); Q[:,2]+=dz
        ends=np.abs(Q[:,0])>60.0                                    # the plank's end region: snapped to the full end line
        Q[ends,0]=np.sign(Q[ends,0])*xe
        Q[~ends,0]*=xe/max(abs(mn[0]),abs(mx[0]))
        P[c]=Q
        log.append('thwart at y %.0f: raised %.1f (top -> %.1f), squared off at +-%.1f (%d end vertices)'%(yc,dz,gz(yc),xe,int(ends.sum())))
    elif len(c)==210 and mx[0]-mn[0]>100 and mx[1]-mn[1]<30:      # a rib (frame) across the hull
        lowr=Q[:,2]<FLOOR+RIB_LIFT
        Q=Q.copy(); Q[lowr,2]=FLOOR+RIB_LIFT; P[c]=Q
        log.append('rib at y %.0f: %d vertices laid onto the floor (z %.1f)'%((mn[1]+mx[1])/2,int(lowr.sum()),FLOOR+RIB_LIFT))
# --- stern seat: a copy of the aft thwart (y -99) moved aft by the tiller (STERN_Y), top at STERN_TOP (a click below
#     where it first sat), each end following the hull's inner face (measured from the inner skin) as it narrows
STERN_Y=-215.0; STERN_TOP=66.3
s10,vb10,T10=load(10); P10=pos(vb10)
inn=set([c for c in components(P10,T10) if P10[c][:,2].min()>10][0].tolist())
def inner_half(Y,Z=62.0):
    best=0.0
    for t in T10:
        if t[0] not in inn: continue
        A=P10[t]; d=A[:,1]-Y; pts=[]
        for i in range(3):
            da,db=d[i],d[(i+1)%3]
            if da*db<0: a,b=A[i],A[(i+1)%3]; pts.append(a+(b-a)*da/(da-db))
        if len(pts)==2:
            p_,q_=pts
            if (p_[2]-Z)*(q_[2]-Z)<=0 and p_[2]!=q_[2]: best=max(best,abs(p_[0]+(q_[0]-p_[0])*(Z-p_[2])/(q_[2]-p_[2])))
    return best
src=[c for c in components(P,T) if 40<=len(c)<=60 and P[c][:,0].max()-P[c][:,0].min()>150 and P[c][:,1].mean()<-50][0]
Q=P[src].copy(); yc0=float((Q[:,1].min()+Q[:,1].max())/2); half=float(np.abs(Q[:,0]).max())
Q[:,1]+=STERN_Y-yc0; Q[:,2]+=STERN_TOP-Q[:,2].max()
hw={}
for i in range(len(Q)):
    yk=round(float(Q[i,1]),2)
    if yk not in hw: hw[yk]=inner_half(yk)+1.5
    xe=hw[yk]
    Q[i,0]=np.sign(Q[i,0])*xe if abs(Q[i,0])>half-1.0 else Q[i,0]*xe/half
remap=-np.ones(len(P),dtype=int); remap[src]=len(P)+np.arange(len(src))
nt=T[np.all(np.isin(T,src),axis=1)]
P=np.vstack([P,Q]); vb=np.vstack([vb,vb[src]]); T=np.vstack([T,remap[nt]])
log.append('stern seat: copy of the aft thwart (%d verts, %d tris) at y %.0f, top %.1f, ends +-%.1f..%.1f'%(len(src),len(nt),STERN_Y,STERN_TOP,min(hw.values()),max(hw.values())))
setpos(vb,P); store(13,s,vb,T)
# --- oarlocks: shape 7, every part on the gunwale sides (|x| > 70), each pair moved as a whole along y
s,vb,T=load(7); P=pos(vb)
for c in components(P,T):
    Q=P[c]; mn,mx=Q.min(0),Q.max(0)
    if min(abs(mn[0]),abs(mx[0]))<70: continue   # the fitting near the stern post, not an oarlock
    yc=float((mn[1]+mx[1])/2); ny=yc+OARLOCK_DY; side=1.0 if mn[0]>0 else -1.0
    dx=side*(gx(ny)-gx(yc)); dz=gz(ny)-gz(yc)
    Q=Q.copy(); Q[:,0]+=dx; Q[:,1]+=OARLOCK_DY; Q[:,2]+=dz; P[c]=Q
    log.append('oarlock part (%d verts) at y %.1f x %+.0f: -> y %.1f (dx %+.1f, dz %+.1f)'%(len(c),yc,side*abs((mn[0]+mx[0])/2),ny,dx,dz))
setpos(vb,P); store(7,s,vb,T)
# --- drop the BilgeFloor shape (blocks 16-18, the last three) and its root child entry
nb=h['nb']; assert h['blocks'][16]['type']=='BSTriShape' and nb==19
keep=16
rs=h['hdr_end']; raw0=bytearray(d[rs:rs+h['sizes'][0]])
nch_off=4+4+4*1+4+4+12+36+4+4
nch,=struct.unpack_from('<I',raw0,nch_off); kids=list(struct.unpack_from('<%di'%nch,raw0,nch_off+4))
kids=[k for k in kids if k<keep]
raw0=raw0[:nch_off]+struct.pack('<I',len(kids))+struct.pack('<%di'%len(kids),*kids)+raw0[nch_off+4+4*nch:]
i=d.index(b'\n')+1; p=i+4+1+4; nb_off=p; p+=4+4
for _ in range(3):
    l=d[p]; p+=1+l
nt_types,=struct.unpack_from('<H',d,p); p+=2
for _ in range(nt_types):
    l,=struct.unpack_from('<I',d,p); p+=4+l
ti_off=p
hdr=bytearray(d[:nb_off])+struct.pack('<I',keep)+d[nb_off+4:ti_off]
hdr+=struct.pack('<%dH'%keep,*h['ti'][:keep])
blk=lambda k: newblock.get(k) or bytes(d[h['blocks'][k]['start']:h['blocks'][k]['end']])
sz=[len(raw0)]+[len(blk(k)) for k in range(1,keep)]
hdr+=struct.pack('<%dI'%keep,*sz)
hdr+=d[ti_off+2*nb+4*nb:h['hdr_end']]   # strings + groups unchanged ('BilgeFloor' stays as an unused string)
body=bytearray(raw0)
for k in range(1,keep): body+=blk(k)
foot=bytes(d[h['hdr_end']+sum(h['sizes']):])
out=bytes(hdr)+bytes(body)+foot
open(OUT,'wb').write(out)
print('\n'.join(log)); print('written',OUT,len(out),'blocks',keep)
h2=parse(out)
for b in h2['blocks']:
    if b['type']=='BSTriShape':
        s2=tri_shape(out,h2,b); v2=decode(out,s2); print(' check',s2['name'],s2['nv'],s2['ntri'],'z %.1f..%.1f'%(v2['pos'][:,2].min(),v2['pos'][:,2].max()))
