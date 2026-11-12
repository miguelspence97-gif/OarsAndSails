# check a built nif (RULES.md): indices in range, data sizes, nothing shared; then a textured render (views from args)
import sys, struct, numpy as np, collections
from PIL import Image
from nifp import parse, tri_shape
NIF, OUT = sys.argv[1], sys.argv[2]; PREV = sys.argv[3]
d = open(NIF, 'rb').read(); h = parse(d); B = h['blocks']
raw = [bytes(d[b['start']:b['end']]) for b in B]
def kids_of(k):
    r = raw[k]; ne, = struct.unpack_from('<I', r, 4); p = 4 + 4 + 4 * ne + 4 + 4
    T = np.array(struct.unpack_from('<3f', r, p + 4 - 4)); p2 = p + 12 + 36 + 4 + 4
    nc, = struct.unpack_from('<I', r, p2); return T, list(struct.unpack_from('<%di' % nc, r, p2 + 4))
off = {0: np.zeros(3)}; stack = [0]; parent_T = {}
while stack:
    k = stack.pop(); T, ks = kids_of(k)
    for c in ks:
        if B[c]['type'] in ('NiNode',): off[c] = off[k] + kids_of(c)[0]; stack.append(c)
        else: off[c] = off[k]
used = collections.Counter(); shapes = []
for b in B:
    if b['type'] != 'BSTriShape': continue
    t = tri_shape(d, h, b)
    if t['nv'] == 0: print('skinned (not drawn here):', t['name']); continue
    assert t['dsz'] == t['nv'] * t['vsize'] + t['ntri'] * 6, t['name']
    V = np.frombuffer(d, np.uint8, t['nv'] * 32, t['data_off']).reshape(-1, 32)
    F = np.frombuffer(d, '<u2', t['ntri'] * 3, t['data_off'] + t['nv'] * 32).reshape(-1, 3).astype(int)
    assert F.max() < t['nv'], t['name']
    used[t['shader']] += 1; used[t['alpha']] += 1
    sr = raw[t['shader']]; ts, = struct.unpack_from('<i', sr, 40); used[ts] += 1
    f1, f2 = struct.unpack_from('<II', sr, 16); assert not (f1 & 1), 'specular on ' + t['name']
    c, = struct.unpack_from('<I', raw[ts], 0); l, = struct.unpack_from('<I', raw[ts], 4); tex = raw[ts][8:8 + l].decode()
    P = V[:, 0:12].copy().view('<f4').reshape(-1, 3) + off[b['k']]
    UV = V[:, 16:20].copy().view('<f2').reshape(-1, 2).astype(float)
    shapes.append((t['name'], P, UV, F, tex))
assert all(v == 1 for v in used.values()), [k for k, v in used.items() if v > 1]
print('ok:', len(shapes), 'shapes, each its own shader/texture set/alpha, indices in range, sizes match,', len(d), 'bytes,', sum(len(s[3]) for s in shapes), 'tris')
texc = {}
def tex(path):
    nm = path.split('\\')[-1].replace('.dds', '')
    if nm not in texc: texc[nm] = np.asarray(Image.open(f'{PREV}/{nm}.png').convert('RGB').resize((512, 512)), np.float32) / 255
    return texc[nm]
def render(view, W=1400, H=800):
    if view == 'side': R = np.array([[0, 1, 0], [0, 0, 1], [-1, 0, 0]], float)       # from starboard, bow right
    elif view == 'front': R = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)    # from ahead
    elif view == 'top': R = np.array([[0, 1, 0], [-1, 0, 0], [0, 0, 1]], float)
    else:
        a, e = np.radians(35), np.radians(25)
        Rz = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
        R0 = np.array([[0, 1, 0], [0, 0, 1], [-1, 0, 0]], float) @ Rz
        Rx = np.array([[1, 0, 0], [0, np.cos(e), -np.sin(e)], [0, np.sin(e), np.cos(e)]]); R = Rx @ R0
    allP = np.vstack([s[1] for s in shapes]) @ R.T
    lo, hi = allP.min(0), allP.max(0); sc = min((W - 40) / (hi[0] - lo[0]), (H - 40) / (hi[1] - lo[1]))
    img = np.full((H, W, 3), 0.22, np.float32); zb = np.full((H, W), -1e9)
    L = np.array([0.4, 0.5, 0.75]); L /= np.linalg.norm(L)
    for name, P, UV, F, tp in shapes:
        Q = P @ R.T; X = (Q[:, 0] - lo[0]) * sc + 20; Y = H - ((Q[:, 1] - lo[1]) * sc + 20); Z = Q[:, 2]
        tx = tex(tp); th, tw = tx.shape[:2]
        for f in F:
            x, y, z = X[f], Y[f], Z[f]
            x0, x1 = int(max(x.min(), 0)), int(min(x.max(), W - 1)); y0, y1 = int(max(y.min(), 0)), int(min(y.max(), H - 1))
            if x1 < x0 or y1 < y0: continue
            den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
            w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
            w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den; w2 = 1 - w0 - w1
            m = (w0 >= -1e-3) & (w1 >= -1e-3) & (w2 >= -1e-3)
            if not m.any(): continue
            zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
            sub = zb[y0:y1 + 1, x0:x1 + 1]; m &= zz > sub
            if not m.any(): continue
            uv = w0[..., None] * UV[f[0]] + w1[..., None] * UV[f[1]] + w2[..., None] * UV[f[2]]
            tu = (np.mod(uv[..., 0], 1) * tw).astype(int).clip(0, tw - 1); tv = (np.mod(uv[..., 1], 1) * th).astype(int).clip(0, th - 1)
            fn = np.cross(P[f[1]] - P[f[0]], P[f[2]] - P[f[0]]); fn /= np.linalg.norm(fn) + 1e-12
            sh = 0.45 + 0.55 * abs(fn @ L) * 1.0
            col = tx[tv, tu] * sh * 1.6
            sub[m] = zz[m]; img[y0:y1 + 1, x0:x1 + 1][m] = col[m]
    Image.fromarray((img.clip(0, 1) * 255).astype(np.uint8)).save(f'{OUT}_{view}.png')
for v in sys.argv[4:]: render(v); print('render', v)
