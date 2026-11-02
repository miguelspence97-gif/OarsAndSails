# v1.7.49: the bed in the boat from Miguel's "camping bed" picture (a patchwork fur bag, shearling hood): a pillowed
# heightfield shaped by the picture's own outline, the hood's rolled shearling rim raised and its inside sunk in, the
# picture itself as the texture (it is a top-down shot of exactly this). Replaces RowRim_Clutter_SleepingBag's shape.
#   python campbed.py <hull in> <hull out> <camping bed.jpg> <out prefix for the textures>
import sys, os, io, struct, numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nifmerge import Nif, node_block, av_fix, e8, tex_set, root_add_kids
from nifp import tri_shape
from hull_v144_lib import load, store
SRC, OUT, PIC, TEX = sys.argv[1:5]
DARK, CONTRAST = 0.52, 0.85   # v1.7.50: darker again (0.74 too bright in game)
A = np.asarray(Image.open(PIC).convert('RGB')).astype(float) / 255
Hh, Ww = A.shape[:2]
bg = A[5:40, 5:40].reshape(-1, 3).mean(0)
mask = np.abs(A - bg).max(-1) > 0.08
mask = ndimage.binary_opening(mask, iterations=10)          # the drawstrings go
lab, nl = ndimage.label(mask); mask = lab == (np.bincount(lab.ravel())[1:].argmax() + 1)
mask = ndimage.binary_fill_holes(mask)
mask = ndimage.gaussian_filter(mask.astype(float), 6) > 0.5     # v1.7.49b: a smooth outline (no fur fuzz)
ys, xs = np.where(mask); x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
lum = A.mean(-1); sat = A.max(-1) - A.min(-1)
cream = mask & (lum > 0.6) & (sat < 0.22)
cream = ndimage.binary_closing(cream, iterations=6)
# heights in game units (s units per picture pixel: 170 units long). v1.7.49b: flatter (a bag lying flat, 8 high, the
# hood's rim 11), and a closed pillow: the side rolls round from the top to the outline at EDGE high and under again
# to the floor, a quarter circle each way (ROLL), so the edges and the underside are smooth
s = 170.0 / (x1 - x0)
dist = ndimage.distance_transform_edt(mask) * s                # units from the outline
EDGE, ROLL, TOP = 2.6, 4.5, 8.0
field = EDGE + (TOP - EDGE) * (1 - (1 - np.clip(dist / 22.0, 0, 1)) ** 2)
hood = cream & (np.arange(Ww)[None, :] < x0 + 0.30 * (x1 - x0))
yy, xx = np.mgrid[0:Hh, 0:Ww]
cx, cy = x0 + 0.155 * (x1 - x0), (y0 + y1) / 2
hollow = ((xx - cx) / (0.085 * (x1 - x0))) ** 2 + ((yy - cy) / (0.30 * (y1 - y0))) ** 2
hol = np.clip(1.15 - hollow, 0, 1) ** 0.6                    # 1 deep in the middle, 0 at the rim
field = field + 2.5 * hood * np.clip(dist / 5.0, 0, 1)        # the rolled shearling rim a little higher
field = EDGE + (field - EDGE) * (1 - 0.6 * hol)               # the hood's inside sunk in
# v1.7.49d: the collar, the shearling turned down over the hide like a cuff: a round roll across the bag (highest in
# its middle), shaded below as a fold (see the texture)
CA, CB = x0 + 0.221 * (x1 - x0), x0 + 0.292 * (x1 - x0)   # the cuff in the picture
cu = np.clip((xx - CA) / (CB - CA), 0, 1); incuff = (xx > CA) & (xx < CB) & mask
field = field + 2.8 * incuff * np.sin(np.pi * cu) * np.clip(dist / 8.0, 0, 1)
field = ndimage.gaussian_filter(field, 5)
roll = np.sqrt(np.clip(1 - (1 - np.clip(dist / ROLL, 0, 1)) ** 2, 0, 1))   # 0 at the outline, 1 from ROLL in (vertical there)
ztop = EDGE + (np.maximum(field, EDGE) - EDGE) * roll
zbot = EDGE * (1 - roll)
# grid (v1.7.49c): along the bag in columns, across each column from its outline on one side to the other, both
# bunched toward the ends and the edges (cosine spacing): the outline is a smooth curve, no stair steps; a top and an
# under side sharing the outline's vertices
from scipy.ndimage import map_coordinates
def bil(img, X, Y): return map_coordinates(img, [np.asarray(Y, float), np.asarray(X, float)], order=1, mode='nearest')
colsx = []
NC, NR = 140, 44
tt = (1 - np.cos(np.linspace(0, np.pi, NC))) / 2
for t in tt:
    colsx.append(x0 + 0.5 + t * (x1 - x0 - 1))
top_y = []; bot_y = []
for cx_ in colsx:
    col = mask[:, int(round(cx_))]; yy_ = np.where(col)[0]
    top_y.append(yy_.min() if len(yy_) else (y0 + y1) / 2); bot_y.append(yy_.max() if len(yy_) else (y0 + y1) / 2)
top_y = ndimage.gaussian_filter1d(np.array(top_y, float), 1.5); bot_y = ndimage.gaussian_filter1d(np.array(bot_y, float), 1.5)
uu = (1 - np.cos(np.linspace(0, np.pi, NR))) / 2
P = []; UV = []; idx = np.zeros((NC, NR), int); idb = np.zeros((NC, NR), int)
for i, cx_ in enumerate(colsx):
    for j, u in enumerate(uu):
        py = top_y[i] + u * (bot_y[i] - top_y[i]); px = cx_
        X, Y = (py - (y0 + y1) / 2) * s, -(px - (x0 + x1) / 2) * s
        uv = ((px - x0) / (x1 - x0), (py - y0) / (y1 - y0))
        edge = j == 0 or j == NR - 1 or i == 0 or i == NC - 1
        zt = EDGE if edge else float(bil(ztop, [px], [py])[0]); zb = EDGE if edge else float(bil(zbot, [px], [py])[0])
        idx[i, j] = len(P); P.append((X, Y, zt)); UV.append(uv)
        if edge: idb[i, j] = idx[i, j]
        else: idb[i, j] = len(P); P.append((X, Y, zb)); UV.append(uv)
Ft = []; Fb = []
for i in range(NC - 1):
    for j in range(NR - 1):
        a_, b_, c_, d_ = idx[i, j], idx[i, j + 1], idx[i + 1, j], idx[i + 1, j + 1]
        Ft += [(a_, b_, c_), (b_, d_, c_)]
        a_, b_, c_, d_ = idb[i, j], idb[i, j + 1], idb[i + 1, j], idb[i + 1, j + 1]
        Fb += [(a_, b_, c_), (b_, d_, c_)]
# degenerate triangles at the pinched ends out
P = np.array(P); UV = np.array(UV); Ft = np.array(Ft); Fb = np.array(Fb)
def nondeg(F):
    ar = np.linalg.norm(np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]]), axis=1); return F[ar > 1e-6]
Ft = nondeg(Ft); Fb = nondeg(Fb)
# the top's faces up, the under side's down
def orient(F, up):
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    flip = (fn[:, 2] < 0) if up else (fn[:, 2] > 0)
    F = F.copy(); F[flip] = F[flip][:, [0, 2, 1]]; return F
Ft = orient(Ft, True); Fb = orient(Fb, False)
F = np.concatenate([Ft, Fb])
used = np.unique(F); remap = -np.ones(len(P), int); remap[used] = np.arange(len(used)); P, UV, F = P[used], UV[used], remap[F]
fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
Nn = np.zeros_like(P)
for k in range(3): np.add.at(Nn, F[:, k], fn)
Nn /= np.maximum(np.linalg.norm(Nn, axis=1, keepdims=True), 1e-9)
Tg = np.tile((0, -1.0, 0), (len(P), 1)); Tg = Tg - Nn * (Tg * Nn).sum(1, keepdims=True); Tg /= np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-6)
B = np.cross(Nn, Tg)
print('camp bed: verts', len(P), 'tris', len(F), 'bounds', P.min(0).round(1), P.max(0).round(1))
# texture: the picture's bag, its background filled from the nearest bag pixel (no white at the edges), darker
crop = A[y0:y1 + 1, x0:x1 + 1]; cm = ndimage.binary_erosion(mask, iterations=4)[y0:y1 + 1, x0:x1 + 1]   # (the edge's white fringe out)
_, (iy, ix) = ndimage.distance_transform_edt(~cm, return_indices=True)
crop = crop[iy, ix]
g = crop.mean(-1, keepdims=True); m = crop.mean((0, 1))
crop = m + (crop - m) * CONTRAST
crop = crop * DARK
# the fold (v1.7.49e, where Miguel marked it): the cuff's inner side, where it folds over into the hood, rolls away
# into shadow, a deep crease at the fold line, and the cuff's shadow falls into the hood's hollow beside it; the
# body's side only a soft crease
cxs = np.arange(x0, x1 + 1)[None, :].astype(float)
cuc = (cxs - CA) / (CB - CA)
inside_c = (cuc > 0) & (cuc < 1)
roundsh = np.where(inside_c, 0.50 + 0.50 * np.clip(cuc / 0.55, 0, 1) ** 0.7, 1.0)          # dark at the hood side, lit on top
crease = 1 - 0.65 * np.exp(-(cuc / 0.07) ** 2) - 0.25 * np.exp(-((cuc - 1.0) / 0.05) ** 2)
fade = np.exp(np.minimum(cuc, 0) / 0.45) * (cuc < 0)                                         # into the hood, from the fold
shadef = (roundsh * crease).repeat(crop.shape[0], 0)
holc = hol[y0:y1 + 1, x0:x1 + 1]
shadef = shadef * (1 - 0.55 * fade.repeat(crop.shape[0], 0) * np.clip(holc * 3, 0, 1))
crop = crop * shadef[..., None]
# v1.7.50: the sides shaded: darker toward the outline, where the bag rolls round and under (the last 7 units)
dside = dist[y0:y1 + 1, x0:x1 + 1]
crop = crop * (0.40 + 0.60 * np.clip(dside / 7.0, 0, 1) ** 0.8)[..., None]
img = Image.fromarray((np.clip(crop, 0, 1) * 255).astype(np.uint8)).resize((1024, 512), Image.LANCZOS)
img.save(TEX + '_preview.png')
def dds(im, path):
    lv = []; w, h = im.size; cur = im
    while True:
        b = io.BytesIO(); cur.save(b, 'DDS', pixel_format='DXT1'); lv.append(b.getvalue())
        if w == 1 and h == 1: break
        w, h = max(1, w // 2), max(1, h // 2); cur = im.resize((w, h), Image.LANCZOS)
    hd = bytearray(lv[0][:128]); f, = struct.unpack_from('<I', hd, 8); struct.pack_into('<I', hd, 8, f | 0x20000)
    struct.pack_into('<I', hd, 28, len(lv)); c, = struct.unpack_from('<I', hd, 108); struct.pack_into('<I', hd, 108, c | 0x400008)
    open(path, 'wb').write(bytes(hd) + b''.join(x[128:] for x in lv))
dds(img, TEX + '_d.dds')
# normal map: fur / fleece detail from the picture's light and shade (fine), flat otherwise
L = np.asarray(img.convert('L').filter(ImageFilter.GaussianBlur(1.0))).astype(float) / 255
gyy, gxx = np.gradient(L); nm = np.dstack([-gxx * 3, gyy * 3, np.ones_like(L)]); nm /= np.linalg.norm(nm, axis=-1, keepdims=True)
dds(Image.fromarray(((nm * 0.5 + 0.5) * 255).astype(np.uint8)), TEX + '_n.dds')
# ---- into the hull: replace the sleeping bag node's shape, or add the node ----
d = open(SRC, 'rb').read(); n = Nif(d)
def tex_paths(k):
    raw = n.blocks[k]; c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p+4:p+4+l].decode('latin1').lower()); p += 4 + l
    return out
tpl = shd = None
for k, t in enumerate(n.ti):
    if n.types[t] != 'BSLightingShaderProperty': continue
    ts, = struct.unpack_from('<i', n.blocks[k], 40)
    if ts >= 0 and any('boat_floor_d' in q for q in tex_paths(ts)):
        for s2, t2 in enumerate(n.ti):
            if n.types[t2] == 'BSTriShape':
                ti = tri_shape(d, n.h, n.h['blocks'][s2])
                if ti['shader'] == k and ti['vsize'] == 32: tpl, shd = n.blocks[s2], n.blocks[k]; break
    if tpl: break
st, _, _ = load(tpl)
vb = np.zeros((len(P), 32), np.uint8)
vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
vb[:, 16:20] = np.ascontiguousarray(UV.astype('<f2')).view(np.uint8).reshape(-1, 4)
vb[:, 20:23] = e8(Nn); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2]); vb[:, 28:32] = 255
raw = bytearray(store(st, vb, F)); raw, p0 = av_fix(bytes(raw)); raw = bytearray(raw)
struct.pack_into('<3f', raw, p0 + 4, 0, 0, 0); struct.pack_into('<9f', raw, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
struct.pack_into('<f', raw, p0 + 52, 1.0); struct.pack_into('<i', raw, p0 + 56, -1)
t = n.add('BSShaderTextureSet', tex_set(['textures\\RowRim\\boat_campbed_d.dds', 'textures\\RowRim\\boat_campbed_n.dds']))
r2, _ = av_fix(shd, 4); r2 = bytearray(r2); struct.pack_into('<i', r2, 40, t)
f1, = struct.unpack_from('<I', r2, 16); struct.pack_into('<I', r2, 16, f1 & ~1)   # matte
sp = n.add('BSLightingShaderProperty', r2)
# an alpha property (blending off: opaque) the plugin turns on while the bed fades in / out (as the tent's)
ap = n.add('NiAlphaProperty', struct.pack('<iIiHB', -1, 0, -1, 0x00EC, 0))
struct.pack_into('<3i', raw, p0 + 60 + 16, -1, sp, ap)
shape = n.add('BSTriShape', raw)
if 'RowRim_Clutter_SleepingBag' in n.names:   # the old bag's node: its child swapped for the new shape (old blocks left unused)
    k = n.names.index('RowRim_Clutter_SleepingBag')
    for i, b in enumerate(n.blocks):
        if n.types[n.ti[i]] == 'NiNode' and struct.unpack_from('<i', b, 0)[0] == k:
            nb = bytes(node_block(k, kids=[shape])); nb = bytearray(nb); struct.pack_into('<I', nb, 12, 0xF); n.blocks[i] = bytes(nb)
else:
    nb = bytearray(node_block(n.name('RowRim_Clutter_SleepingBag'), kids=[shape])); struct.pack_into('<I', nb, 12, 0xF)
    root_add_kids(n, [n.add('NiNode', bytes(nb))])
open(OUT, 'wb').write(n.write())
print('->', OUT)
