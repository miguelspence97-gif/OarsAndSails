# v1.7.52/53: the six oarlocks out of L1_BoatDetails into their own shapes with their own texture (Miguel's forged iron
# sheet, textures\RowRim\boat_oarlock_d/_n.dds): the middle and aft forks each under a node at its pin
# (RowRim_Oarlock_MidR / MidL / AftR / AftL; the plugin turns them 90 deg while the oars are shipped), the bow pair
# (RowRim_Oarlock_FwdR / FwdL, still) and the six base blocks in one shape (RowRim_OarlockBases).
# New UVs by projection onto the sheet: a fork's two faces onto the two U plates (prongs up), its edges onto the side
# strips; the base blocks onto the riveted bars.
#   python oarlocks.py <hull in> <hull out> [<sheet.png> <texture out prefix>]
import sys, os, io, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nifmerge import Nif, node_block, av_fix, tex_set, root_add_kids
from nifp import tri_shape
from hull_v144_lib import load, store, components
SRC, OUT = sys.argv[1:3]
SHEET = sys.argv[3] if len(sys.argv) > 4 else None; TEXOUT = sys.argv[4] if len(sys.argv) > 4 else None
d = open(SRC, 'rb').read(); n = Nif(d)
assert 'RowRim_Oarlock_MidR' not in n.names, 'the oarlocks are already split in this hull'
k = n.names.index('L1_BoatDetails')
shape = next(i for i, b in enumerate(n.blocks) if n.types[n.ti[i]] == 'BSTriShape' and struct.unpack_from('<i', b, 0)[0] == k)
raw = n.blocks[shape]; st, vb, tris = load(raw)
P = vb[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
shd = tri_shape(d, n.h, n.h['blocks'][shape])['shader']
# sheet regions (fractions of the 1536 x 1024 sheet)
W, H = 1536.0, 1024.0
R = {'plateA': (95, 55, 750, 775), 'plateB': (1440, 55, 790, 775),   # B mirrored (u runs right to left)
     'stripA': (15, 25, 72, 1000), 'stripB': (1465, 25, 1522, 1000),
     'barA': (145, 810, 750, 975), 'barB': (790, 810, 1390, 975)}
def reg(name, a, b):  # a, b in 0..1 -> uv
    x0, y0, x1, y1 = R[name]; return ((x0 + a * (x1 - x0)) / W, (y0 + b * (y1 - y0)) / H)
names = {(1, 6.7): 'MidR', (-1, 6.7): 'MidL', (1, -93.0): 'AftR', (-1, -93.0): 'AftL', (1, 105.0): 'FwdR', (-1, 105.0): 'FwdL'}
forks, bases = {}, []
for c in components(P, tris):
    lo, hi = P[c].min(0), P[c].max(0); cen = (lo + hi) / 2
    if abs(abs(cen[0]) - 89) > 9: continue
    if len(c) == 64 and cen[2] > 70:
        for (sx, y), nm in names.items():
            if np.sign(cen[0]) == sx and abs(cen[1] - y) < 9: forks[nm] = c
    elif len(c) == 20 and cen[2] < 71 and any(abs(cen[1] - y) < 9 for (_, y) in names): bases.append(c)
assert len(forks) == 6 and len(bases) == 6, (forks.keys(), len(bases))
allv = set(np.concatenate(list(forks.values()) + bases).tolist())
keep = np.array([t[0] not in allv for t in tris])
n.blocks[shape] = store(st, vb, tris[keep])
TEXSET = ['textures\\RowRim\\boat_oarlock_d.dds', 'textures\\RowRim\\boat_oarlock_n.dds']
def shader():
    t = n.add('BSShaderTextureSet', tex_set(TEXSET))
    r, _ = av_fix(n.blocks[shd], 4); r = bytearray(r); struct.pack_into('<i', r, 40, t)
    f1, = struct.unpack_from('<I', r, 16); struct.pack_into('<I', r, 16, f1 & ~1)   # matte iron
    return n.add('BSLightingShaderProperty', r)
def faces_of(vs):
    s = set(vs.tolist()); return np.array([t for t in tris[~keep] if t[0] in s])
def split_by_normal(vs, ft):
    """a vertex per face corner (flat UV islands): returns positions, normals (from the old verts), uv, tris"""
    return ft
def build(vlist, ftris, uvfun, pin):
    # one vertex per (old vertex, island) so islands do not share UVs
    out_v = []; out_uv = []; out_f = []; key = {}
    Nold = vb[:, 20:23].astype(float) / 127.5 - 1
    for t in ftris:
        fn = np.cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]); fn /= np.linalg.norm(fn) + 1e-9
        isl = int(np.argmax(np.abs(fn))) * 2 + (1 if fn[np.argmax(np.abs(fn))] > 0 else 0)
        f = []
        for v in t:
            kk = (v, isl)
            if kk not in key:
                key[kk] = len(out_v); out_v.append(v); out_uv.append(uvfun(P[v], isl))
            f.append(key[kk])
        out_f.append(f)
    nv = vb[out_v].copy()
    p2 = P[out_v] - pin
    nv[:, 0:12] = np.ascontiguousarray(p2.astype('<f4')).view(np.uint8).reshape(-1, 12)
    nv[:, 16:20] = np.ascontiguousarray(np.array(out_uv, float).astype('<f2')).view(np.uint8).reshape(-1, 4)
    r = bytearray(store(st, nv, np.array(out_f)))
    r, p0 = av_fix(bytes(r)); r = bytearray(r)
    struct.pack_into('<3f', r, p0 + 4, 0, 0, 0); struct.pack_into('<9f', r, p0 + 16, 1, 0, 0, 0, 1, 0, 0, 0, 1)
    struct.pack_into('<f', r, p0 + 52, 1.0); struct.pack_into('<i', r, p0 + 56, -1)
    struct.pack_into('<3i', r, p0 + 60 + 16, -1, shader(), -1)
    return n.add('BSTriShape', r)
kids = []
for nm, c in forks.items():
    lo, hi = P[c].min(0), P[c].max(0)
    def uvf(p, isl, lo=lo, hi=hi):
        a = (p[1] - lo[1]) / (hi[1] - lo[1]); b = 1 - (p[2] - lo[2]) / (hi[2] - lo[2])   # across the prongs, top down
        if isl in (0, 1):   # the faces (normal across the boat)
            return reg('plateA' if isl == 1 else 'plateB', a, b)
        t = (p[1] - lo[1] + p[2] - lo[2]) / ((hi[1] - lo[1]) + (hi[2] - lo[2]))
        th = (p[0] - lo[0]) / max(hi[0] - lo[0], 1e-3)
        return reg('stripA' if isl % 2 else 'stripB', th, t)
    pin = np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]])
    s_id = build(c, faces_of(c), uvf, pin)
    kids.append(n.add('NiNode', node_block(n.name('RowRim_Oarlock_' + nm), np.eye(3), tuple(pin), 1.0, [s_id])))
    print(nm, 'pin', pin.round(2))
allb = np.concatenate(bases); ftb = faces_of(allb)
def uvb(p, isl):
    for c in bases:
        if np.any(np.all(np.isclose(P[c], p), axis=1)): lo, hi = P[c].min(0), P[c].max(0); break
    a = (p[1] - lo[1]) / max(hi[1] - lo[1], 1e-3)
    b = (p[0] - lo[0]) / max(hi[0] - lo[0], 1e-3) if isl in (4, 5) else (p[2] - lo[2]) / max(hi[2] - lo[2], 1e-3)
    return reg('barA' if isl % 2 else 'barB', a, 1 - b)
s_b = build(allb, ftb, uvb, np.zeros(3))
kids.append(n.add('NiNode', node_block(n.name('RowRim_OarlockBases'), np.eye(3), (0, 0, 0), 1.0, [s_b])))
root_add_kids(n, kids)
open(OUT, 'wb').write(n.write()); print('->', OUT)
if SHEET:
    from PIL import Image, ImageFilter
    from scipy import ndimage
    a = np.asarray(Image.open(SHEET).convert('RGB')).astype(float) / 255
    m = a.max(-1) > 0.06
    _, (iy, ix) = ndimage.distance_transform_edt(~ndimage.binary_erosion(m, iterations=3), return_indices=True)
    a = a[iy, ix]     # the black between the pieces filled from the nearest iron (no black seams)
    img = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).resize((512, 512), Image.LANCZOS)
    L = np.asarray(img.convert('L').filter(ImageFilter.GaussianBlur(0.8))).astype(float) / 255
    gy, gx = np.gradient(L); nm_ = np.dstack([-gx * 4, gy * 4, np.ones_like(L)]); nm_ /= np.linalg.norm(nm_, axis=-1, keepdims=True)
    nimg = Image.fromarray(((nm_ * 0.5 + 0.5) * 255).astype(np.uint8))
    def dds(im, path):
        lv = []; w, h = im.size; cur = im
        while True:
            b = io.BytesIO(); cur.save(b, 'DDS', pixel_format='DXT1'); lv.append(b.getvalue())
            if w == 1 and h == 1: break
            w, h = max(1, w // 2), max(1, h // 2); cur = im.resize((w, h), Image.LANCZOS)
        hd = bytearray(lv[0][:128]); f, = struct.unpack_from('<I', hd, 8); struct.pack_into('<I', hd, 8, f | 0x20000)
        struct.pack_into('<I', hd, 28, len(lv)); c, = struct.unpack_from('<I', hd, 108); struct.pack_into('<I', hd, 108, c | 0x400008)
        open(path, 'wb').write(bytes(hd) + b''.join(x[128:] for x in lv))
    dds(img, TEXOUT + '_d.dds'); dds(nimg, TEXOUT + '_n.dds'); img.save(TEXOUT + '_preview.png'); print('textures ->', TEXOUT)
