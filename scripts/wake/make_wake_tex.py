# RowRim wake textures (white, the code tints them).
# crest: two or three soft ripple bands along v (tiling along v), faint: the V arms are a gentle disturbance, not foam.
# wash: one round, soft-edged patch of churned water and foam streaks (no tiling: pieces are turned at random and overlap).
import numpy as np
from PIL import Image
rng=np.random.default_rng(11)
def tnoise(h,w,cv,cu,oct=4):
    out=np.zeros((h,w)); amp=1; tot=0
    for o in range(oct):
        a,b=cv*2**o,cu*2**o; g=rng.random((a,b))
        y=np.arange(h)/h*a; x=np.arange(w)/w*b
        y0=np.floor(y).astype(int); x0=np.floor(x).astype(int); fy=y-y0; fx=x-x0
        fy=fy*fy*(3-2*fy); fx=fx*fx*(3-2*fx); y1=(y0+1)%a; x1=(x0+1)%b; y0%=a; x0%=b
        v=(g[y0][:,x0]*(1-fx)+g[y0][:,x1]*fx)*(1-fy)[:,None]+(g[y1][:,x0]*(1-fx)+g[y1][:,x1]*fx)*fy[:,None]
        out+=v*amp; tot+=amp; amp*=0.5
    return out/tot
# crest 128 x 256
H,W=256,128; v=np.arange(H)/H; u=np.arange(W)/W
n=tnoise(H,W,3,2); n2=tnoise(H,W,6,3)
A=np.zeros((H,W))
for c,wid,s in ((0.38,0.07,0.55),(0.55,0.09,1.0),(0.70,0.06,0.45)):
    d=(u[None,:]-c-0.04*(n-0.5))/wid; A+=s*np.exp(-d*d)
A*=0.45+0.8*n2
A*=np.clip(np.minimum(u,1-u)/0.25,0,1)[None,:]
A=np.clip(A*0.6,0,1)
Image.fromarray(np.dstack([np.full((H,W,3),255,np.uint8),(A*255).astype(np.uint8)]),'RGBA').save('wake_crest.png')
# wash 256 x 256
H=W=256; yy,xx=np.mgrid[0:H,0:W]; r=np.hypot((xx-W/2)/(W/2),(yy-H/2)/(H/2))
fall=np.clip(1-r,0,1)**1.6
warp=tnoise(H,W,4,4,3)
fall=np.clip(fall*(0.7+0.6*warp),0,1)
streak=tnoise(H,W,3,20,3)            # streaks along v (the boat's travel)
fine=tnoise(H,W,24,24,3)
foam=np.clip((fine-0.45)/0.25,0,1)*np.clip((streak-0.35)/0.4,0,1)
sheen=0.35*tnoise(H,W,5,5,4)
A=np.clip((sheen+0.75*foam)*fall,0,1)
Image.fromarray(np.dstack([np.full((H,W,3),255,np.uint8),(A*255).astype(np.uint8)]),'RGBA').save('wake_wash.png')
