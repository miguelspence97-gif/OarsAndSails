# RowRim rope texture: a three-strand laid hemp rope. u (x) runs along the rope = two lay lengths, v (y) once round it.
import numpy as np
from PIL import Image
W,H=512,256
rng=np.random.default_rng(3)
u=(np.arange(W)+0.5)/W; v=(np.arange(H)+0.5)/H
U,V=np.meshgrid(u,v)
# strand coordinate: strands wind round as you go along (2 lays across the tile, 3 strands)
s=(V - U*2.0)*3.0            # integer lines = strand boundaries (tiles in u and v)
f=s-np.floor(s)              # 0..1 across a strand
prof=np.sin(np.pi*f)**0.45   # rounded strand
groove=np.clip(np.sin(np.pi*f)*3.0,0,1)   # dark lay grooves
# yarns inside each strand: finer twist the other way
y_=(V*14.0 + U*9.0); fy=y_-np.floor(y_)
yarn=0.82+0.18*np.sin(np.pi*fy)**0.8
# fibres: streaks along the strand direction + noise
def noise(sc):
    n=rng.random((H//sc+2,W//sc+2)); n=np.kron(n,np.ones((sc,sc)))[:H,:W]
    return n
a=(U*2.0+V*1.0)                 # along the strand (roughly)
from scipy.ndimage import gaussian_filter
fn=gaussian_filter(rng.random((H,W)),0.8,mode='wrap')
fn=(fn-fn.min())/(fn.max()-fn.min())
streak=np.sin(2*np.pi*(s*16.0 + fn*1.5))*0.5+0.5          # fine fibres along each strand
fib=0.78+0.22*streak**1.5
hair=gaussian_filter(rng.random((H,W)),0.5,mode='wrap'); hair=(hair>0.62)*0.12
grime=0.85+0.15*noise(32)*noise(16)
height=prof*yarn*groove
base=np.array([0.40,0.33,0.23])           # darker, wet-weathered hemp           # weathered hemp
dark=np.array([0.07,0.055,0.04])
shade=np.clip(height*fib*grime+hair*prof,0,1)
col=dark[None,None,:]*(1-shade[...,None])+base[None,None,:]*shade[...,None]
col*= (0.92+0.08*noise(8))[...,None]
# very dark green moss: patches (tileable blotches), thickest down in the lay grooves, a little furry on the strand tops
mo=gaussian_filter(rng.random((H,W)),18,mode='wrap'); mo=(mo-mo.min())/(mo.max()-mo.min())
mo2=gaussian_filter(rng.random((H,W)),5,mode='wrap'); mo2=(mo2-mo2.min())/(mo2.max()-mo2.min())
patch=np.clip((mo*0.7+mo2*0.3-0.36)*3.0,0,1)
inGroove=1-prof
fuzz=gaussian_filter(rng.random((H,W)),0.7,mode='wrap'); fuzz=(fuzz-fuzz.min())/(fuzz.max()-fuzz.min())
moss=np.clip(patch*(0.5+0.6*inGroove)*(0.7+0.6*fuzz),0,1)
mosscol=np.array([0.045,0.095,0.03])*(0.7+0.6*fuzz)[...,None]
col=col*(1-moss[...,None]*0.9)+mosscol*moss[...,None]*0.9
Image.fromarray((np.clip(col,0,1)*255).astype(np.uint8)).save('boat_rope_d.png')
# normal map from the height (tangent space, Skyrim/DirectX green down)
hgt=height*fib*0.9
dx=(np.roll(hgt,-1,1)-np.roll(hgt,1,1))*W/2/48.0
dy=(np.roll(hgt,-1,0)-np.roll(hgt,1,0))*H/2/48.0
n=np.dstack([-dx,dy,np.ones_like(dx)]); n/=np.linalg.norm(n,axis=2,keepdims=True)
Image.fromarray(((n*0.5+0.5)*255).astype(np.uint8)).save('boat_rope_n.png')
print('ok')
