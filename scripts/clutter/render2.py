import numpy as np, sys, struct
from PIL import Image
from nifp import parse, tri_shape
NIF=sys.argv[1]; OUTP=sys.argv[2]; view=sys.argv[3]
d=open(NIF,'rb').read(); h=parse(d)
blocks=[bytes(d[b['start']:b['end']]) for b in h['blocks']]; types=[b['type'] for b in h['blocks']]
def L(p): return np.asarray(Image.open(p).convert('RGB'),np.float32)/255
def proc(kind):
    u,v=np.meshgrid(np.linspace(0,1,256),np.linspace(0,1,256))
    if kind=='bedroll':
        c=np.array([0.45,0.36,0.26])*(0.85+0.15*np.sin(u*60)*np.sin(v*50))[...,None]
        band=((np.mod(v*4,1)<0.12))[...,None]; c=np.where(band,np.array([0.30,0.22,0.15]),c)
        return c.astype(np.float32)
    if kind=='mead':
        st=0.8+0.2*np.sin(u*2*np.pi*12)
        c=np.array([0.42,0.28,0.17])*st[...,None]; hoop=((np.abs(v-0.15)<0.03)|(np.abs(v-0.85)<0.03))[...,None]
        return np.where(hoop,np.array([0.2,0.2,0.2]),c).astype(np.float32)
def texfor(path):
    p=path.lower()
    if 'boat_side' in p: return L('boat_side_blue_d.png')
    if 'boat_hull' in p or 'shipwood01' in p: return L('boat_hull_d.png')
    if 'boat_floor' in p: return L('boat_floor_d.png')
    if 'boat_rope' in p or 'shiprope' in p or 'riftenrope' in p: return L('boat_rope_d.png')
    if 'kwila' in p: return L('tex_boat_oar_kwila_d.png')
    if 'bucket01' in p: return L('tex_bucket01.png')
    if 'fishinggear01' in p: return L('tex_fishinggear01.png')
    if 'fishbones_d' in p: return 'SPRITE'
    if 'fishbone' in p: return L('fishbone_d.png')
    if 'bedroll' in p: return proc('bedroll')
    if 'meadbarrel' in p: return proc('mead')
    return None
TC={}
SPR=np.asarray(Image.open('fishbones_sprite.png').convert('RGBA'),np.float32)/255
def shape_tex(k):
    t=tri_shape(d,h,h['blocks'][k]); shd=t['shader']
    if shd<0 or types[shd]!='BSLightingShaderProperty': return None
    ts,=struct.unpack_from('<i',blocks[shd],40)
    if ts<0: return None
    raw=blocks[ts]; n,=struct.unpack_from('<I',raw,0); l,=struct.unpack_from('<I',raw,4); path=raw[8:8+l].decode()
    if path not in TC: TC[path]=texfor(path)
    return TC[path]
def xf(raw):
    ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4
    T=np.array(struct.unpack_from('<3f',raw,p)); R=np.array(struct.unpack_from('<9f',raw,p+12)).reshape(3,3); S=struct.unpack_from('<f',raw,p+48)[0]
    return R,T,S
shapes=[]
EXTRA=sys.argv[4] if len(sys.argv)>4 else None  # extra nif placed: path,x,y,z,yawdeg,scale
def walk(k,R,T,S,src_d=None):
    raw=blocks[k]; t=types[k]
    lR,lT,lS=xf(raw); R2=R@lR; T2=R@(lT*S)+T; S2=S*lS
    if t in('BSFadeNode','NiNode'):
        ne,=struct.unpack_from('<I',raw,4); p=4+4+4*ne+4+4+12+36+4+4
        nc,=struct.unpack_from('<I',raw,p)
        for c in struct.unpack_from('<%di'%nc,raw,p+4):
            if c>=0: walk(c,R2,T2,S2)
    elif t=='BSTriShape':
        ti=tri_shape(d,h,h['blocks'][k]); vs=ti['vsize']
        if ti['nv']==0: return
        V=np.frombuffer(raw,np.uint8,ti['nv']*vs,ti['raw_data_off']).reshape(-1,vs)
        P=V[:,:12].copy().view('<f4').reshape(-1,3).astype(float)
        uv=V[:,16:20].copy().view('<f2').reshape(-1,2).astype(np.float32)
        N=V[:,20:23].astype(np.float32)/127.5-1
        F=np.frombuffer(raw,'<u2',ti['ntri']*3,ti['raw_data_off']+ti['nv']*vs).reshape(-1,3).astype(int)
        shapes.append(((P*S2)@R2.T+T2,uv,N@R2.T,F,shape_tex(k)))
walk(0,np.eye(3),np.zeros(3),1.0)
if EXTRA:
    import importlib
    f,x,y,z,yaw,sc=EXTRA.split(','); ed=open(f,'rb').read(); eh=parse(ed)
    c,s=np.cos(np.radians(float(yaw))),np.sin(np.radians(float(yaw))); Rz=np.array([[c,-s,0],[s,c,0],[0,0,1]])
    for b in eh['blocks']:
        if b['type']!='BSTriShape': continue
        ti=tri_shape(ed,eh,b); vs=ti['vsize']; raw=ed[b['start']:b['end']]
        V=np.frombuffer(raw,np.uint8,ti['nv']*vs,ti['raw_data_off']).reshape(-1,vs)
        P=V[:,:12].copy().view('<f4').reshape(-1,3).astype(float); uv=V[:,16:20].copy().view('<f2').reshape(-1,2).astype(np.float32)
        N=V[:,20:23].astype(np.float32)/127.5-1
        F=np.frombuffer(raw,'<u2',ti['ntri']*3,ti['raw_data_off']+ti['nv']*vs).reshape(-1,3).astype(int)
        shapes.append(((P*float(sc))@Rz.T+np.array([float(x),float(y),float(z)]),uv,N@Rz.T,F,L('tex_fishinggear01.png')))
W,H=1400,900
V={'stern':([150.,-330.,190.],[0.,-170.,55.],42),'stern2':([-170.,-320.,180.],[0.,-160.,55.],42),
   'mid':([170.,10.,170.],[0.,0.,50.],48),'bow':([-150.,250.,160.],[0.,130.,50.],45),'wide':([300.,-420.,320.],[0.,-40.,50.],40),
   'under':([120.,40.,110.],[0.,100.,52.],38),'rod':([200.,-120.,170.],[80.,-250.,120.],50),'bones':([40.,150.,110.],[-15.,222.,45.],34),
   'keg':([120.,-20.,95.],[0.,-88.,50.],34),'mount':([-40.,-120.,110.],[60.,-178.,62.],40),'roll':([60.,50.,85.],[0.,113.,52.],40),'roll2':([-30.,175.,75.],[0.,113.,52.],44)}
eye,at,fov=V[view]; eye=np.array(eye); at=np.array(at)
fwd=at-eye; fwd/=np.linalg.norm(fwd); right=np.cross(fwd,[0,0,1.]); right/=np.linalg.norm(right); up=np.cross(right,fwd)
f=0.5*H/np.tan(np.radians(fov/2))
light=np.array([0.35,-0.45,0.82]); light/=np.linalg.norm(light)
img=np.zeros((H,W,3),np.float32); yy=np.linspace(0,1,H)[:,None]
img[:]=(np.array([0.55,0.68,0.80])*(1-yy*0.5)+np.array([0.80,0.84,0.86])*yy*0.5)[:, None,:].reshape(H,1,3)
zb=np.full((H,W),np.inf,np.float32)
# water plane z 32
for P,uv,N,F,tx in shapes:
    C=P-eye; z=C@fwd
    for t in F:
        if (z[t]<1).any(): continue
        Cs=C[t]; zs=z[t]
        xs=W/2+f*(Cs@right)/zs; ys=H/2-f*(Cs@up)/zs
        x0=max(int(np.floor(xs.min())),0); x1=min(int(np.ceil(xs.max())),W-1)
        y0=max(int(np.floor(ys.min())),0); y1=min(int(np.ceil(ys.max())),H-1)
        if x0>x1 or y0>y1: continue
        den=(ys[1]-ys[2])*(xs[0]-xs[2])+(xs[2]-xs[1])*(ys[0]-ys[2])
        if abs(den)<1e-9: continue
        px,py=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
        a=((ys[1]-ys[2])*(px-xs[2])+(xs[2]-xs[1])*(py-ys[2]))/den
        b=((ys[2]-ys[0])*(px-xs[2])+(xs[0]-xs[2])*(py-ys[2]))/den
        c=1-a-b; m=(a>=-1e-4)&(b>=-1e-4)&(c>=-1e-4)
        if not m.any(): continue
        iz=a/zs[0]+b/zs[1]+c/zs[2]; zz=1/iz
        sub=zb[y0:y1+1,x0:x1+1]; m&=zz<sub
        if not m.any(): continue
        wa=a/zs[0]*zz; wb=b/zs[1]*zz; wc=c/zs[2]*zz
        Ns=N[t]; n=wa[...,None]*Ns[0]+wb[...,None]*Ns[1]+wc[...,None]*Ns[2]
        n/=np.linalg.norm(n,axis=-1,keepdims=True)+1e-9
        vd=-(Cs.mean(0)); vd/=np.linalg.norm(vd)
        n=np.where((n@vd)[...,None]<0,-n,n)
        dif=np.clip(n@light,0,1)
        if isinstance(tx,str):
            uvs=uv[t]; u=wa*uvs[0,0]+wb*uvs[1,0]+wc*uvs[2,0]; v=wa*uvs[0,1]+wb*uvs[1,1]+wc*uvs[2,1]
            sp=SPR; th_,tw_=sp.shape[:2]; px_=sp[(np.clip(v,0,0.999)*th_).astype(int),(np.clip(u,0,0.999)*tw_).astype(int)]
            m&=px_[...,3]>0.43; col=px_[...,:3]
        elif tx is None: col=np.full(m.shape+(3,),0.30,np.float32)
        else:
            uvs=uv[t]; u=wa*uvs[0,0]+wb*uvs[1,0]+wc*uvs[2,0]; v=wa*uvs[0,1]+wb*uvs[1,1]+wc*uvs[2,1]
            th_,tw_=tx.shape[:2]
            col=tx[(np.mod(v,1)*th_).astype(int)%th_,(np.mod(u,1)*tw_).astype(int)%tw_]
        sh=col*(0.38+0.75*dif[...,None])
        ii=img[y0:y1+1,x0:x1+1]; ii[m]=sh[m]; sub[m]=zz[m]
Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8)).save(OUTP)
print('ok',OUTP)
