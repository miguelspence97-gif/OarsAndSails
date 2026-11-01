# Gemini rope image -> tiling RowRim rope texture (512x256), keeping its strands on our layout (45 deg, 6 strands across u).
# Seams fixed by blending with copies shifted along whole strands (x by 256, then x+y by 128), so grooves stay lined up.
import numpy as np, sys
from PIL import Image
from scipy.ndimage import gaussian_filter
src,out_d,out_n=sys.argv[1],sys.argv[2],sys.argv[3]
W,H=512,256
G=np.asarray(Image.open(src).convert('RGB').resize((W,H),Image.LANCZOS),np.float32)/255
def ramp(n,edge):
    i=np.arange(n)+0.5; d=np.minimum(i,n-i)/edge
    w=np.clip(d,0,1); return w*w*(3-2*w)
wx=ramp(W,72)[None,:,None]
A=G*wx+np.roll(G,W//2,axis=1)*(1-wx)
wy=ramp(H,56)[:,None,None]
B=A*wy+np.roll(np.roll(A,H//2,axis=0),H//2,axis=1)*(1-wy)
Image.fromarray((np.clip(B,0,1)*255).astype(np.uint8)).save(out_d)
# normal map from its own shading (dark grooves = low), strand-scale plus a little fibre
L=B.mean(2)
h=gaussian_filter(L,2.0,mode='wrap')*0.8+gaussian_filter(L,0.6,mode='wrap')*0.2
h=(h-h.min())/(h.max()-h.min())
dx=(np.roll(h,-1,1)-np.roll(h,1,1))*4.0; dy=(np.roll(h,-1,0)-np.roll(h,1,0))*4.0
n=np.dstack([-dx,dy,np.ones_like(dx)]); n/=np.linalg.norm(n,axis=2,keepdims=True)
Image.fromarray(((n*0.5+0.5)*255).astype(np.uint8)).save(out_n)
print('ok',B.shape)
