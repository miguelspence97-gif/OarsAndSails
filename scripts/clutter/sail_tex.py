# RowRim sail texture: Miguel's "cloth sail.jpg" cropped inside its edges (no fringe, no backdrop), the photo's light
# fall-off evened out, a little darker (the game lights it), saved 1024 DXT1 with mips.
import numpy as np, io, struct, sys
from PIL import Image, ImageFilter
src=sys.argv[1]; out=sys.argv[2]
im=Image.open(src).convert('RGB').crop((34,40,976,982)).resize((1024,1024),Image.LANCZOS)
a=np.asarray(im).astype(np.float32)/255
lum=a.mean(2)
low=np.asarray(Image.fromarray((lum*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(160))).astype(np.float32)/255
a=a*(low.mean()/np.clip(low,0.05,1))[...,None]   # even out the studio light
a=np.clip(a*0.86,0,1)
img=Image.fromarray((a*255).astype(np.uint8)); img.save(out.replace('.dds','.png'))
mips=[]; m=img
while True:
    b=io.BytesIO(); m.save(b,'DDS',pixel_format='DXT1'); mips.append(b.getvalue())
    if m.width==1: break
    m=m.resize((max(1,m.width//2),max(1,m.height//2)),Image.LANCZOS)
hdr=bytearray(mips[0][:128]); body=b''.join(x[128:] for x in mips)
struct.pack_into('<I',hdr,8,struct.unpack_from('<I',hdr,8)[0]|0x20000); struct.pack_into('<I',hdr,28,len(mips))
struct.pack_into('<I',hdr,108,struct.unpack_from('<I',hdr,108)[0]|0x400008)
open(out,'wb').write(bytes(hdr)+body); print(out,len(hdr)+len(body),len(mips))
