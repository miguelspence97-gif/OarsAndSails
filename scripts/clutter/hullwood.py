# RowRim v1.7.82: the boat's brown wood (textures\RowRim\boat_hull_d.dds, 2048 DXT1: thwarts, gunwales, ribs, stem)
# swapped for Miguel's weathered marine wood. The old map's bottom-right corner is end grain (the cut ends of the planks
# are mapped there): kept, recoloured to the new wood. Same size and format as the file it replaces.
import numpy as np, io, struct, sys
from PIL import Image, ImageFilter
OLD, NEW, OUT = sys.argv[1:4]
old = np.asarray(Image.open(OLD).convert('RGB')).astype(np.float32) / 255
S = old.shape[0]
new = Image.open(NEW).convert('RGB').resize((S, S), Image.LANCZOS)
a = np.asarray(new).astype(np.float32) / 255
lum = a.mean(2)
low = np.asarray(Image.fromarray((lum * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(S / 6))).astype(np.float32) / 255
a = np.clip(a * (low.mean() / np.clip(low, 0.05, 1))[..., None] * 0.80, 0, 1)
c0 = int(S * 0.785)
corner = old[c0:, c0:]
# recolour the end grain: match its mean / spread to the new wood's, per channel
body = a[:c0, :c0].reshape(-1, 3)
cm, cs = corner.reshape(-1, 3).mean(0), corner.reshape(-1, 3).std(0) + 1e-4
bm, bs = body.mean(0), body.std(0)
rc = np.clip((corner - cm) / cs * bs + bm, 0, 1)
# blend its edge in over 24 px so no hard seam
m = np.ones(rc.shape[:2], np.float32); e = 24
ramp = np.clip(np.arange(rc.shape[0]) / e, 0, 1)
m *= ramp[:, None]; m *= ramp[None, :]
a[c0:, c0:] = a[c0:, c0:] * (1 - m[..., None]) + rc * m[..., None]
img = Image.fromarray((a * 255).astype(np.uint8)); img.resize((512, 512)).save(OUT.replace('.dds', '_preview.png'))
mips = []; mm = img
while True:
    b = io.BytesIO(); mm.save(b, 'DDS', pixel_format='DXT1'); mips.append(b.getvalue())
    if mm.width <= 4: break   # mips down to 4x4, as the file it replaces
    mm = mm.resize((max(1, mm.width // 2), max(1, mm.height // 2)), Image.LANCZOS)
hdr = bytearray(mips[0][:128]); body = b''.join(x[128:] for x in mips)
struct.pack_into('<I', hdr, 8, struct.unpack_from('<I', hdr, 8)[0] | 0x20000); struct.pack_into('<I', hdr, 28, len(mips))
struct.pack_into('<I', hdr, 108, struct.unpack_from('<I', hdr, 108)[0] | 0x400008)
open(OUT, 'wb').write(bytes(hdr) + body); print(OUT, len(hdr) + len(body))
