# v1.7.50: the sleeping bag's own texture (Tactical_Beard, CC BY 4.0) recoloured: the outer shell (UV u < 0.5) to a
# brown hide, keeping its quilting and folds; the lining and flap (u > 0.5) to a warm fleece: the plaid and its brand
# tag blurred away, soft wool clumps instead. Normal map: the model's own, softened (the lining's weave gone).
#   python sleepingbag_tex.py <model textures folder> <out prefix>
import sys, io, struct, numpy as np
from PIL import Image, ImageFilter
T, OUT = sys.argv[1], sys.argv[2]
N = 1024
rng = np.random.default_rng(50)
def load(f, mode='RGB'): return np.asarray(Image.open(T + '/' + f).convert(mode).resize((N, N), Image.LANCZOS)).astype(float) / 255
def blur(a, r): return np.asarray(Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r))).astype(float) / 255
A = load('DefaultMaterial_albedo.jpg'); AO = load('DefaultMaterial_AO.jpg', 'L')
L = A.mean(-1)
U = np.tile((np.arange(N) + 0.5) / N, (N, 1)); lining = U > 0.485   # (the brand tag sat on the seam at 0.49)
def noise(scale, amp):
    s = max(1, N // scale); z = rng.random((s, s))
    return (np.asarray(Image.fromarray((z * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC)).astype(float) / 255 - 0.5) * amp
# hide: folds / quilting from the shell's own light and shade (relative to its local average), mottled browns
rel = np.clip(blur(L, 1.5) / np.maximum(blur(L, 30), 0.02), 0.5, 1.5)
mott = 1 + noise(12, 0.35) + noise(40, 0.18)
hide = np.array([0.27, 0.18, 0.11])[None, None, :] * (rel ** 0.8 * mott)[..., None]
hide = hide * (0.55 + 0.45 * AO)[..., None]
# fleece: warm cream, wool clumps, shaded by the model's AO only
# (the folds come from the AO alone: any of the plaid's light and dark left blotches)
clump = 1 + noise(60, 0.16) + noise(150, 0.14) + noise(320, 0.10)
wool = np.array([0.50, 0.40, 0.29])[None, None, :] * clump[..., None] * (0.45 + 0.55 * AO)[..., None]
C = np.where(lining[..., None], wool, hide)
img = Image.fromarray((np.clip(C, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
img.save(OUT + '_preview.png')
Nm = load('DefaultMaterial_normal.png')
Nm = np.where(lining[..., None], blur(Nm, 4), blur(Nm, 1.2))
n = Nm * 2 - 1; n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
nimg = Image.fromarray(((n * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8))
def dds(im, path):
    lv = []; w, h = im.size; cur = im
    while True:
        b = io.BytesIO(); cur.save(b, 'DDS', pixel_format='DXT1'); lv.append(b.getvalue())
        if w == 1 and h == 1: break
        w, h = max(1, w // 2), max(1, h // 2); cur = im.resize((w, h), Image.LANCZOS)
    hd = bytearray(lv[0][:128]); f, = struct.unpack_from('<I', hd, 8); struct.pack_into('<I', hd, 8, f | 0x20000)
    struct.pack_into('<I', hd, 28, len(lv)); c, = struct.unpack_from('<I', hd, 108); struct.pack_into('<I', hd, 108, c | 0x400008)
    open(path, 'wb').write(bytes(hd) + b''.join(x[128:] for x in lv))
dds(img, OUT + '_d.dds'); dds(nimg, OUT + '_n.dds')
print('mean hide', C[~lining].mean(0).round(3), 'fleece', C[lining].mean(0).round(3))
