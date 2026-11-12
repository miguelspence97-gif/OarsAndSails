# RowRim v1.7.82: the rig's own textures from Miguel's 4/11 pictures (blender\Weathered *.png, 1254 square):
#   boat_sail_d.dds      sailcloth, 1024 DXT1 (replaces the v1.7.73 one, same size)
#   boat_sailwood_d/n    the mast, yard, boom, partner and cleat, 512 DXT1 (new)
#   boat_sailrope_d/n    the halyard, sheet and ties, 512 DXT1 (new)
# Each: light fall-off evened out, a little darker (the game lights it), never bigger than its source. Normals from the
# picture's own light and shade (fine detail), matte.
import numpy as np, io, struct, sys
from PIL import Image, ImageFilter
SRC = sys.argv[1]; OUT = sys.argv[2]
def even(img, dark):
    a = np.asarray(img).astype(np.float32) / 255
    lum = a.mean(2)
    low = np.asarray(Image.fromarray((lum * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(img.width / 6))).astype(np.float32) / 255
    a = a * (low.mean() / np.clip(low, 0.05, 1))[..., None]
    return Image.fromarray((np.clip(a * dark, 0, 1) * 255).astype(np.uint8))
def dds(img, path):
    mips = []; m = img
    while True:
        b = io.BytesIO(); m.save(b, 'DDS', pixel_format='DXT1'); mips.append(b.getvalue())
        if m.width == 1: break
        m = m.resize((max(1, m.width // 2), max(1, m.height // 2)), Image.LANCZOS)
    hdr = bytearray(mips[0][:128]); body = b''.join(x[128:] for x in mips)
    struct.pack_into('<I', hdr, 8, struct.unpack_from('<I', hdr, 8)[0] | 0x20000); struct.pack_into('<I', hdr, 28, len(mips))
    struct.pack_into('<I', hdr, 108, struct.unpack_from('<I', hdr, 108)[0] | 0x400008)
    open(path, 'wb').write(bytes(hdr) + body)
def normal(img, strength, path):
    L = np.asarray(img.convert('L').filter(ImageFilter.GaussianBlur(1.0))).astype(float) / 255
    gy, gx = np.gradient(L); nm = np.dstack([-gx * strength, gy * strength, np.ones_like(L)]); nm /= np.linalg.norm(nm, axis=-1, keepdims=True)
    dds(Image.fromarray(((nm * 0.5 + 0.5) * 255).astype(np.uint8)), path)
for src, name, size, dark, nstr in (('Weathered Stitched Sailcloth Texture.png', 'boat_sail', 1024, 0.80, 0),
                                    ('Weathered Marine Wood Planks.png', 'boat_sailwood', 512, 0.80, 6.0),
                                    ('Weathered Nautical Rope Texture.png', 'boat_sailrope', 512, 0.82, 8.0)):
    im = Image.open(SRC + '/' + src).convert('RGB').resize((size, size), Image.LANCZOS)
    im = even(im, dark)
    im.save(OUT + '/' + name + '_d.png')
    dds(im, OUT + '/' + name + '_d.dds')
    if nstr: normal(im, nstr, OUT + '/' + name + '_n.dds')
    print(name, size)
