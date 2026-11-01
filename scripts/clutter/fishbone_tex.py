import numpy as np
from PIL import Image
rng=np.random.default_rng(3)
H=W=64
y,x=np.mgrid[0:H,0:W]
base=np.array([0.80,0.76,0.66])
g=0.9+0.1*np.sin(x*0.9+rng.random()*6)*np.sin(y*0.31)
n=rng.random((H,W))*0.08
c=np.clip(base[None,None,:]*(g+n)[...,None]-np.array([0,0.02,0.05])*((y/H)[...,None]),0,1)
Image.fromarray((c*255).astype(np.uint8)).save('fishbone_d.png')
Image.fromarray(np.full((H,W,3),(128,128,255),np.uint8)).save('fishbone_n.png')
