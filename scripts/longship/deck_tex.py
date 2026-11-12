# v1.8.9h: the longship's deck (LS_Deck, texture set "bridge") from Miguel's own painting, "Weathered Wooden Boat Deck
# Texture.png" (E:\code\Viking Longboat\Textures, 1254 px, the deck island where the original bridge atlas has it):
# diffuse 2048 (BC1, mips); normal map made from its own light and dark (the seams and grain sink a little), green as
# Skyrim wants. usage: python deck_tex.py <png> <out dir>
import sys, numpy as np
from PIL import Image, ImageFilter
Image.MAX_IMAGE_PIXELS = None
SIZE = 4096  # v1.8.9h3: Miguel's Upscayl x5 (remacri) of the boards: diffuse at 4K like the other wood; normal 2048
import quicktex.dds as qdds
from quicktex.s3tc.bc1 import BC1Encoder
src, out = sys.argv[1], sys.argv[2]
ENC = BC1Encoder(18)
im = Image.open(src).convert('RGB').resize((SIZE, SIZE), Image.LANCZOS)
# v1.8.9h2: too bright in game: the boards brought down to the hull's own brightness (0.33 mean on the painted island;
# the flat grey around the island is left out of the measure)
LUM = 0.22  # v1.8.9i: darker (RULES: boat textures dark)
a = np.asarray(im, np.float32) / 255
bg = np.median(a[:64, :64].reshape(-1, 3), 0)
mask = np.abs(a - bg).sum(2) > 0.06
lum = float((a[mask] @ np.array([0.299, 0.587, 0.114])).mean())
a = a * (LUM / lum) if lum > LUM else a
print(f'deck boards: mean {lum:.2f} -> {min(lum, LUM):.2f}')
im = Image.fromarray((a.clip(0, 1) * 255 + 0.5).astype(np.uint8))
qdds.encode(im.convert('RGBA'), ENC, 'DXT1').save(f'{out}/bridge_d.dds')
h = np.asarray(im.resize((2048, 2048), Image.LANCZOS).convert('L').filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
gy, gx = np.gradient(h)
k = 6.0
n = np.dstack([-gx * k, gy * k, np.ones_like(h)])   # OpenGL style ...
n /= np.linalg.norm(n, axis=2, keepdims=True)
n[..., 1] *= -1                                     # ... flipped for Skyrim, as longship_tex.py
Image.fromarray(((n + 1) * 127.5).clip(0, 255).astype(np.uint8)).save('prev/deck_n.png')
qdds.encode(Image.open('prev/deck_n.png').convert('RGBA'), ENC, 'DXT1').save(f'{out}/bridge_n.dds')
print('ok')
