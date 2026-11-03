# RowRim oar swoosh streak (v1.7.60): white, alpha = a soft band along v, tapered at both ends (the code tints it).
import numpy as np, struct, io
from PIL import Image
W,H=64,128
u=(np.arange(W)+0.5)/W*2-1; v=(np.arange(H)+0.5)/H*2-1
across=np.exp(-(u/0.42)**2*2.2)
core=np.exp(-(u/0.14)**2*2.0)
along=np.clip(1-np.abs(v),0,1)**0.9
A=np.clip((0.55*across[None,:]+0.6*core[None,:])*along[:,None],0,1)
img=Image.fromarray(np.dstack([np.full((H,W,3),255,np.uint8),(A*255).astype(np.uint8)]),'RGBA')
img.save('swoosh_streak.png')
mips=[]; m=img
while True:
    b=io.BytesIO(); m.save(b,'DDS',pixel_format='DXT5'); mips.append(b.getvalue())
    if m.width==1 and m.height==1: break
    m=m.resize((max(1,m.width//2),max(1,m.height//2)),Image.LANCZOS)
hdr=bytearray(mips[0][:128]); body=b''.join(x[128:] for x in mips)
fl=struct.unpack_from('<I',hdr,8)[0]|0x20000; struct.pack_into('<I',hdr,8,fl)
struct.pack_into('<I',hdr,28,len(mips))
caps=struct.unpack_from('<I',hdr,108)[0]|0x400008; struct.pack_into('<I',hdr,108,caps)
open('swoosh_streak.dds','wb').write(bytes(hdr)+body); print(len(mips), len(hdr)+len(body))
