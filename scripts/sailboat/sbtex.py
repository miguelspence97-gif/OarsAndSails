# RowRim v1.7.88: the sailing boat's own textures (PolyCrate's 4K PBR set) -> Skyrim DDS: diffuse = base colour x a
# gentle ambient occlusion, normal = the OpenGL normal map, both DXT1 with mips; 2048 for the hull wood and the sails,
# 1024 for the rest (all smaller than the 4K source).
import sys, io, struct, numpy as np
from PIL import Image, ImageFilter
Image.MAX_IMAGE_PIXELS = None
SRC, OUT = sys.argv[1], sys.argv[2]
SETS = {  # name: (base, normal, ao, size)
 'BigWoodenBoat01': ('BigWoodenBoat01_Base_color', 'BigWoodenBoat01_Normal_OpenGL', 'BigWoodenBoat01_AO', 2048),
 'BigWoodenBoat02': ('BigWoodenBoat02_Base_color', 'BigWoodenBoat02_Normal_OpenGL', 'BigWoodenBoat02_AO', 2048),
 'BigWoodenBoat03': ('BigWoodenBoat03_Base_color', 'BigWoodenBoat03_Normal_OpenGL', 'BigWoodenBoat03_AO', 2048),
 'RustMetal01': ('RustMetal01_Base_color', 'RustMetal01_Normal_OpenGL', 'RustMetal01_AO', 1024),
 'Rope01': ('Rope01_Base_color', 'Rope01_Normal_OpenGL', 'Rope01_AO', 1024),
 'Rope02': ('Rope02_Base_color', 'Rope02_Normal_OpenGL', 'Rope02_AO', 1024),
 'RicePaper01': ('RicePaper01_Base_color', 'RicePaper01_Normal_OpenGL', 'RicePaper01_AO', 1024),
 'BoatFabric01': ('BoatFabric01_Base_color', 'BoatFabric_Normal_OpenGL', 'BoatFabric_AO', 1024),
 'BoatFabric02': ('BoatFabric02_Base_color', 'BoatFabric02_Normal_OpenGL', None, 2048),
}
def dds(img, out):
    mips = []; mm = img
    while True:
        b = io.BytesIO(); mm.save(b, 'DDS', pixel_format='DXT1'); mips.append(b.getvalue())
        if mm.width <= 4: break
        mm = mm.resize((max(1, mm.width // 2), max(1, mm.height // 2)), Image.LANCZOS)
    hdr = bytearray(mips[0][:128]); body = b''.join(x[128:] for x in mips)
    struct.pack_into('<I', hdr, 8, struct.unpack_from('<I', hdr, 8)[0] | 0x20000); struct.pack_into('<I', hdr, 28, len(mips))
    struct.pack_into('<I', hdr, 108, struct.unpack_from('<I', hdr, 108)[0] | 0x400008)
    open(out, 'wb').write(bytes(hdr) + body); return len(hdr) + len(body)
SOFT = {'BoatFabric02': 0.3}   # v1.7.91: 0.5 still shimmered as the cloth moved
ONLY = sys.argv[3:] or None
tot = 0
for name, (base, nrm, ao, size) in SETS.items():
    if ONLY and name not in ONLY: continue
    b = Image.open(f'{SRC}/{base}.jpg').convert('RGB').resize((size, size), Image.LANCZOS)
    a = np.asarray(b, np.float32) / 255
    if ao:
        o = np.asarray(Image.open(f'{SRC}/{ao}.jpg').convert('L').resize((size, size), Image.LANCZOS), np.float32) / 255
        a = a * (0.35 + 0.65 * o)[..., None] ** 0.8
    d = Image.fromarray((a.clip(0, 1) * 255).astype(np.uint8))
    if name in SOFT: d = d.filter(ImageFilter.GaussianBlur(0.8))   # v1.7.91: the fine weave crawled on the moving sail
    d.resize((256, 256)).save(f'{OUT}/{name}_preview.png')
    n = Image.open(f'{SRC}/{nrm}.jpg').convert('RGB').resize((size, size), Image.LANCZOS)
    if name in SOFT:  # v1.7.89: the sail cloth's fine weave read as grain in game: softened and half as deep
        q = np.asarray(n.filter(ImageFilter.GaussianBlur(2.0)), np.float32) / 127.5 - 1
        q[..., :2] *= SOFT[name]; q /= np.linalg.norm(q, axis=2, keepdims=True) + 1e-6
        n = Image.fromarray(((q + 1) * 127.5).clip(0, 255).astype(np.uint8))
    s1 = dds(d, f'{OUT}/{name}_d.dds'); s2 = dds(n, f'{OUT}/{name}_n.dds'); tot += s1 + s2
    print(name, size, s1, s2, 'mean', np.round(a.mean((0, 1)), 2))
print('total', tot)
