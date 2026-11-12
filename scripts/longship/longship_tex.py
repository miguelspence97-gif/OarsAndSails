# RowRim longship textures: the model's PBR set (Substance, by massive-graphisme, CC BY 4.0) -> Skyrim DDS.
# v1.8.8c (Miguel: "the textures look like shit in game", asked for 2K):
#  - 2048 for every set (the hull and carvings from their 4096 originals, the rest at their own 2048; normal maps from
#    1024 resized, renormalised); the bucket stays at its own 512
#  - a proper BC1 encoder (quicktex, rgbcx level 18) with mips: PIL's own DXT1 was blocky and banded
#  - grading measured on the painted (UV-covered) pixels only: the empty padding pulled the old brightness cap far too
#    low (everything darkened to mud); gentle now: AO light, contrast as painted, only very bright sets brought down
#  - normal maps: Sketchfab / Substance OpenGL (Y+) -> Skyrim (DirectX, Y-): green flipped
# usage: python longship_tex.py <model\textures dir> <dir with dec\*.obj> <out dir> [set ...]
import sys, os, numpy as np
from PIL import Image, ImageDraw, ImageFilter
import quicktex.dds as qdds
from quicktex.s3tc.bc1 import BC1Encoder
Image.MAX_IMAGE_PIXELS = None
SRC, DEC, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
SETS = {  # set: (diffuse, normal) size, the objs that use it (for the painted-pixel mask)
    'ldhull': (2048, 2048, ['ldhull']), 'quillandcarvings': (2048, 2048, ['quillandcarvings']), 'sail': (2048, 2048, ['sail', 'sail_spare']),
    'mastsandpaddles': (2048, 2048, ['spars', 'oars', 'yard']), 'ropes': (2048, 2048, ['ropes', 'yardropes']), 'logs': (2048, 2048, ['logs']),
    'bench': (2048, 2048, ['bench']), 'bench2': (2048, 2048, ['bench2']), 'bridge': (2048, 2048, ['bridge']),
    'shield1': (2048, 2048, ['shield1']), 'shield2': (2048, 2048, ['shield2']), 'shields3': (2048, 2048, ['shields3']), 'bucket': (512, 512, ['bucket'])}
LUM_MAX = 0.30     # v1.8.9i: RULES: boat textures dark (Miguel: too bright): painted pixels' mean brightness at most this
DARKEN = 0.80      # and every set 0.8x of its source on top
# v1.8.9e: more detail in the wood (same sizes): the grain brought out of the painted albedo (unsharp mask + a little local
# contrast) and into the normal map (fine bumps from the albedo's own grain, the painted bumps 1.5x deeper)
WOOD = set()  # v1.8.9f: reverted (Miguel: horrendous); was {'ldhull', 'quillandcarvings', 'bench', 'bench2', 'bridge', 'logs', 'mastsandpaddles'}
SHARP = (2.0, 90, 2)        # unsharp mask: radius, percent, threshold
LOCAL = 0.25                # local contrast: the detail above a 24 px blur, raised this much
NORMAL_DEEPER = 1.5         # the painted normal map's bumps
GRAIN = 2.2                 # bumps from the albedo's fine grain (height = its high-pass brightness)     # only sets brighter than this (on their painted pixels) are brought down to it
FLIP_GREEN = True
ENC = BC1Encoder(18)
# v1.8.9g: the wood at 4K diffuse: the hull and carvings from their own 4096 originals, the deck, chests, frames and spars
# from Miguel's Upscayl x3 albedos (high-fidelity-4x, 6144) brought to 4096; their normal maps stay 2048 (not AI'd)
UPS = os.environ.get('UPS', '')
D4K = {'ldhull', 'quillandcarvings', 'bench', 'bench2', 'bridge', 'logs', 'mastsandpaddles'}
def src(name, kind):
    if kind == 'albedo' and UPS and name in D4K and os.path.exists(f'{UPS}/{name}_albedo_1.png'): return f'{UPS}/{name}_albedo_1.png'
    p = f'{SRC}/{name}_{kind}_1.jpg'
    return p if os.path.exists(p) else None
def uv_mask(objs, size):
    m = Image.new('L', (size, size), 0); dr = ImageDraw.Draw(m)
    for o in objs:
        T = []; F = []
        for l in open(f'{DEC}/{o}.obj'):
            if l.startswith('vt '): T.append(l.split()[1:3])
            elif l.startswith('f '): F.append([int(w.split('/')[1]) - 1 for w in l.split()[1:4]])
        T = np.array(T, float)
        for f in F:
            uv = T[f]; dr.polygon([(u % 1.0 * size, (1 - v % 1.0) * size) for u, v in uv], fill=255)
    return np.asarray(m) > 0
def save(img, out):
    qdds.encode(img.convert('RGBA'), ENC, 'DXT1').save(out)
    return os.path.getsize(out)
ONLY = sys.argv[4:] or None; tot = 0
for name, (ds, ns, objs) in SETS.items():
    if ONLY and name not in ONLY: continue
    if UPS and name in D4K: ds, ns = 4096, 2048
    alb = Image.open(src(name, 'albedo')).convert('RGB'); assert ds <= alb.width
    a = np.asarray(alb.resize((ds, ds), Image.LANCZOS) if alb.width != ds else alb, np.float32) / 255
    if src(name, 'AO'):
        o = np.asarray(Image.open(src(name, 'AO')).convert('L').resize((ds, ds), Image.LANCZOS), np.float32) / 255
        a = a * (0.7 + 0.3 * o)[..., None]
    mask = uv_mask(objs, ds)
    lum = float((a[mask] @ np.array([0.299, 0.587, 0.114])).mean()) if mask.any() else 0.5
    k = (LUM_MAX / lum if lum > LUM_MAX else 1.0) * DARKEN
    a = a * k
    d = Image.fromarray((a.clip(0, 1) * 255 + 0.5).astype(np.uint8))
    grain = None
    if name in WOOD:
        d = d.filter(ImageFilter.UnsharpMask(*SHARP))
        a2 = np.asarray(d, np.float32) / 255
        blur = np.asarray(d.filter(ImageFilter.GaussianBlur(24)), np.float32) / 255
        a2 = (a2 + (a2 - blur) * LOCAL).clip(0, 1)
        d = Image.fromarray((a2 * 255 + 0.5).astype(np.uint8))
        gl = a2 @ np.array([0.299, 0.587, 0.114], np.float32)
        hp = gl - np.asarray(Image.fromarray((gl * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
        grain = hp
    d.save(f'prev/{name}_d.png')
    n = Image.open(src(name, 'normal')).convert('RGB')
    n = n.resize((ns, ns), Image.LANCZOS) if n.width != ns else n
    q = np.asarray(n, np.float32) / 127.5 - 1.0
    q /= np.linalg.norm(q, axis=2, keepdims=True) + 1e-6
    if grain is not None:
        q[..., :2] *= NORMAL_DEEPER
        g = np.asarray(Image.fromarray(((grain + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((ns, ns), Image.LANCZOS), np.float32) / 255 - 0.5
        gy, gx = np.gradient(g)
        q[..., 0] -= gx * GRAIN * 8; q[..., 1] += gy * GRAIN * 8   # OpenGL (Y+) like the source; flipped below
        q /= np.linalg.norm(q, axis=2, keepdims=True) + 1e-6
    if FLIP_GREEN: q[..., 1] *= -1
    n = Image.fromarray(((q + 1) * 127.5).clip(0, 255).astype(np.uint8))
    s1 = save(d, f'{OUT}/{name}_d.dds'); s2 = save(n, f'{OUT}/{name}_n.dds'); tot += s1 + s2
    print(f'{name}: d {ds} n {ns}, {s1 + s2:,} bytes, painted lum {lum:.2f}' + (f' -> {LUM_MAX:.2f}' if k < 1 else ' (kept)'))
print(f'total {tot:,}')
