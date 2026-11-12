# RowRim v1.8.8: each boat its own collision. A collision-only nif for the hull ref (the carrier draws the boat; the ref is
# hidden and only collides): BSFadeNode + BSXFlags (havok) + bhkCollisionObject -> bhkRigidBody (the rowboat hull's own
# body block: layer STATIC, keyframed by the plugin) -> bhkListShape of bhkConvexVerticesShapes cut from the boat's model:
# per slice along the keel a floor slab (its top the deck / floorboards) and a wall each side (the hull above the floor),
# and the mast. Lowered kCollisionLift (10.5): the plugin lifts the hull ref's collision by that.
# usage: python hull_collision.py <visual nif> <donor nif with a bhkRigidBody (RowRim_Hull.nif)> <out nif> sail|longship [scale]
# v1.8.27: [scale] the boat drawn that much bigger in game (the sailing boat 1.25: TestHull::kSailScale); the sailing boat's
# well stands on its floorboards (z 48.6 since v1.8.27c, RowRim_Floorboards, sb_floor.py)
import sys, struct, numpy as np
from scipy.spatial import ConvexHull
from nifp import parse, tri_shape
from nifmerge import Nif
VIS, DONOR, OUT, KIND = sys.argv[1:5]
SCALE = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
HK = 69.99125; LIFT = 10.5
WOOD = 500811281   # SKY_HAV_MAT_WOOD
CFG = {
    'sail': dict(hull=['SB_HullPlanks', 'SB_HullFrame'], deck=None, deckZ=36.0, step=40.0, mast=(-2.0, 83.0, 6.0, 600.0)),
    'longship': dict(hull=['LS_Hull'], deck='LS_Deck', deckZ=None, step=50.0, mast=(2.0, -46.0, 10.0, 950.0)),
}[KIND]
d = open(VIS, 'rb').read(); h = parse(d)
# v1.8.8b: the deck and hull read as triangles and cut by planes across the keel (a slice's deck height and width from
# the mesh itself, not from its nearest vertices: the decimated deck has few, and slices without one were left out:
# the holes you fell through)
tri = {'hull': [], 'deck': []}
for b in h['blocks']:
    if b['type'] != 'BSTriShape': continue
    t = tri_shape(d, h, b)
    if t['name'] in CFG['hull'] or t['name'] == CFG['deck']:
        V = np.frombuffer(d, np.uint8, t['nv'] * 32, t['data_off']).reshape(-1, 32)[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
        F = np.frombuffer(d, '<u2', t['ntri'] * 3, t['data_off'] + t['nv'] * 32).reshape(-1, 3).astype(int)
        tri['deck' if t['name'] == CFG['deck'] else 'hull'].append(V[F])
HT = np.vstack(tri['hull']); DT = np.vstack(tri['deck']) if tri['deck'] else None
def section(T, Y):
    """the segments where the plane y = Y cuts the triangles T (n, 3, 3)"""
    out = []
    for a, b in ((0, 1), (1, 2), (2, 0)):
        pa, pb = T[:, a], T[:, b]
        ya, yb = pa[:, 1] - Y, pb[:, 1] - Y
        m = (ya * yb < 0)
        u = ya[m] / (ya[m] - yb[m])
        out.append(pa[m] + (pb[m] - pa[m]) * u[:, None])
    return np.vstack(out) if out else np.zeros((0, 3))
def deck_at(Y):
    if DT is None: return CFG['deckZ']
    p = section(DT, Y)
    if not len(p): return None
    c = p[np.abs(p[:, 0]) < 60]
    return float((c if len(c) else p)[:, 2].max())
def width_at(Y, z):
    src = DT if DT is not None else HT
    p = section(src, Y)
    if DT is None: p = p[np.abs(p[:, 2] - z) < 10]
    return float(np.abs(p[:, 0]).max()) if len(p) else None
H = HT.reshape(-1, 3)
pieces = []
y_lo, y_hi = H[:, 1].min(), H[:, 1].max()
ys = np.arange(y_lo + 2, y_hi - 2, CFG['step'])
ys = np.append(ys, y_hi - 2)
if KIND == 'sail':
    # v1.8.26: what you stand on is the boat's own: the raised foredeck (z ~70-80), the well aft (~31) and the stern
    # bench, read from the hull's up-facing surfaces (v1.8.8 had one flat floor at 36 the whole length, as the rowboat's)
    n_ = np.cross(HT[:, 1] - HT[:, 0], HT[:, 2] - HT[:, 0]); n_ /= np.linalg.norm(n_, axis=1, keepdims=True) + 1e-9
    U = HT[np.abs(n_[:, 2]) > 0.75]
    rg = np.random.default_rng(0); S = []
    for _ in range(60):
        u = rg.random(len(U)); v = rg.random(len(U)); m = u + v > 1; u[m] = 1 - u[m]; v[m] = 1 - v[m]
        S.append(U[:, 0] + (U[:, 1] - U[:, 0]) * u[:, None] + (U[:, 2] - U[:, 0]) * v[:, None])
    S = np.vstack(S); S = S[S[:, 2] < 92]
    G = 5.0; rows = np.arange(y_lo, y_hi + G, G); Zr = np.full(len(rows), np.nan)
    for i, y in enumerate(rows):
        p = S[np.abs(S[:, 1] - y) < G * 0.5]
        if not len(p): continue
        wmax = np.abs(p[:, 0]).max()
        c = p[np.abs(p[:, 0]) < wmax * 0.55]  # the middle, clear of the gunwales and the frames up the sides
        if len(c) > 20:
            # each 5 x 5 cell its highest surface, the row the median of those
            cells = {}
            for q in c:
                k = int(q[0] // G)
                cells[k] = max(cells.get(k, -1e9), q[2])
            Zr[i] = float(np.median(list(cells.values())))
    good = ~np.isnan(Zr)
    Zr = np.interp(rows, rows[good], Zr[good])
    Zr = np.maximum(Zr, 48.6)  # v1.8.27: the floorboards in the well (v1.8.27c: 8 higher, 48.6)
    # runs of one level (a jump over 8 between rows is a step: a slice edge exactly there)
    edges = [0] + [i for i in range(1, len(rows)) if abs(Zr[i] - Zr[i - 1]) > 8] + [len(rows) - 1]
    edges = [e for k, e in enumerate(edges) if k in (0, len(edges) - 1) or rows[e] - rows[edges[k - 1]] >= 20]  # short runs (the transom's curve up): one ramp
    ramp = {}  # runs that are ramps: their ends' levels
    for a, b in zip(edges[:-1], edges[1:]):
        if np.ptp(Zr[a:b + 1]) > 8: ramp[(a, b)] = True
    ys = []
    for a, b in zip(edges[:-1], edges[1:]):
        seg = np.arange(rows[a], rows[b] + 0.1, CFG['step'])
        ys += list(seg) + [rows[b]]
    ys = np.unique(np.clip(np.round(np.array(ys), 2), y_lo + 2, y_hi - 2))
    print('levels:', ' | '.join(f'y {rows[a]:.0f}..{rows[b]:.0f} z {np.median(Zr[a:b+1]):.0f}' for a, b in zip(edges[:-1], edges[1:])))
    def deck_at(Y, _Z=Zr, _r=rows, _e=edges):
        # the level of the run Y is in: its median (a ramp run: the level at Y)
        i = int(np.clip(np.searchsorted(_r, Y), 0, len(_r) - 1))
        for a, b in zip(_e[:-1], _e[1:]):
            if a <= i <= b:
                return float(_Z[i]) if (a, b) in ramp else float(np.median(_Z[a:b + 1]))
        return float(_Z[i])
    def width_at(Y, z):
        p = section(HT, Y)
        p = p[np.abs(p[:, 2] - z) < 10]
        return float(np.abs(p[:, 0]).max()) if len(p) else None
for y0, y1 in zip(ys[:-1], ys[1:]):
    if KIND == 'sail':
        za = zb = deck_at(y0 + 0.5 * (y1 - y0))
        if za < 60 and deck_at(y0) != deck_at(y1) and abs(deck_at(y0) - deck_at(y1)) < 20: za, zb = deck_at(y0), deck_at(y1)  # the ramp aft
    else:
        za, zb = deck_at(y0), deck_at(y1)
    if za is None or zb is None: continue
    wa, wb = width_at(y0, za), width_at(y1, zb)
    if not wa or not wb or min(wa, wb) < 15: continue
    wa -= 2.0; wb -= 2.0
    # the floor: a slab 60 deep, its top the deck at each end (the longship's deck rises toward the ends)
    P = []
    for yy, zz, ww in ((y0, za, wa), (y1, zb, wb)):
        for xx in (-ww, ww):
            P += [(xx, yy, zz), (xx, yy, zz - 60.0)]
    pieces.append(('floor', np.array(P)))
    # the walls: each side a slab 16 thick on the hull's inside, from below the deck up to the gunwale (the highest hull
    # point out at that side in this slice, at most 140 over the deck); continuous from slice to slice
    z = max(za, zb); w = min(wa, wb)
    m = (H[:, 1] >= y0 - 10) & (H[:, 1] <= y1 + 10) & (np.abs(H[:, 0]) > w * 0.7)
    if m.any():
        g = min(float(H[m, 2].max()), z + 140.0)
        if g - z >= 15:
            for sd in (-1, 1):
                Q = []
                for yy, zz, ww in ((y0, za, wa), (y1, zb, wb)):
                    for xx in (sd * (ww + 6.0), sd * (ww - 14.0)):
                        Q += [(xx, yy, zz - 20.0), (xx, yy, g)]
                pieces.append(('wall', np.array(Q)))
# v1.8.38: the stern seat and the stern box (SB_Spars, the bought model's) are solid too: you stood sunk in the seat and
# walked into the box (the floor there is the well's, 49). The seat: its top (z 66..73) down to the well, one piece; the
# box at the very stern: its own shape.
if KIND == 'sail':
    SP = None
    for b in h['blocks']:
        if b['type'] != 'BSTriShape': continue
        t = tri_shape(d, h, b)
        if t['name'] == 'SB_Spars' and t['nv']:
            SP = np.frombuffer(d, np.uint8, t['nv'] * t['vsize'], t['data_off']).reshape(-1, t['vsize'])[:, 0:12].copy().view('<f4').reshape(-1, 3).astype(float)
    if SP is not None:
        seat = SP[(SP[:, 1] > -259.5) & (SP[:, 1] < -186.0) & (SP[:, 2] > 64.0) & (SP[:, 2] < 74.5) & (np.abs(SP[:, 0]) < 76.0)]
        if len(seat) > 8:
            seat[:, 2] = np.minimum(seat[:, 2], 71.3)  # its top where you stand on it (the rim's bevel a hair higher)
            top = seat[:, 2].max()
            low = seat.copy(); low[:, 2] = 49.0 - 20.0  # down into the well's floor: nothing to sink into
            pieces.append(('seat', np.vstack([seat, low])))
            print(f'seat: y {seat[:,1].min():.0f}..{seat[:,1].max():.0f}, top z {top:.1f}')
        box = SP[(SP[:, 1] > -281.0) & (SP[:, 1] < -245.0) & (SP[:, 2] > 64.0) & (SP[:, 2] < 115.0) & (np.abs(SP[:, 0]) < 75.0)]
        if len(box) > 8:
            pieces.append(('box', box))
            print(f'stern box: y {box[:,1].min():.0f}..{box[:,1].max():.0f}, z {box[:,2].min():.0f}..{box[:,2].max():.0f}')
mx, my, mr, mh = CFG['mast']
zf = deck_at(my) or 36.0
pieces.append(('mast', np.array([[mx + a, my + b, zz] for a in (-mr, mr) for b in (-mr, mr) for zz in (zf, zf + mh)])))
# --- the nif ---
td = open(DONOR, 'rb').read(); dn = Nif(td)
rb = cb = bsx = None
for k, t in enumerate(dn.ti):
    ty = dn.types[t]
    if ty == 'bhkRigidBody': rb = dn.blocks[k]
    elif ty == 'bhkCollisionObject': cb = dn.blocks[k]
    elif ty == 'BSXFlags': bsx = dn.blocks[k]
assert rb and cb and bsx
n = Nif(td); n.blocks = []; n.ti = []; n.names = []
root = n.add('BSFadeNode', b'')
x = n.add('BSXFlags', struct.pack('<iI', n.name('BSX'), struct.unpack_from('<I', bsx, 4)[0]))
prop = struct.pack('<III', 0, 0, 0x80000000)
kids = []; ntri = 0
for kind, P in pieces:
    P = P.copy() * SCALE; P[:, 2] -= LIFT
    try: ch = ConvexHull(P)
    except Exception: continue
    V = P[ch.vertices] / HK
    eq = np.unique(np.round(ch.equations, 4), axis=0)
    N = eq.copy(); N[:, 3] = eq[:, 3] / HK
    raw = struct.pack('<If', WOOD, 0.01) + prop + prop + struct.pack('<I', len(V)) + np.hstack([V, np.zeros((len(V), 1))]).astype('<f4').tobytes()
    raw += struct.pack('<I', len(N)) + N.astype('<f4').tobytes()
    kids.append(n.add('bhkConvexVerticesShape', raw)); ntri += len(ch.simplices)
lst = n.add('bhkListShape', struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + struct.pack('<I', WOOD) + prop + prop + struct.pack('<I', 0))
r = bytearray(rb); struct.pack_into('<i', r, 0, lst); body = n.add('bhkRigidBody', r)
c = bytearray(cb); struct.pack_into('<i', c, 0, root); struct.pack_into('<i', c, 6, body); col = n.add('bhkCollisionObject', c)
nm = n.name('RowRim_%sHull' % ('SailBoat' if KIND == 'sail' else 'Longship'))
n.blocks[root] = (struct.pack('<iIi', nm, 1, x) + struct.pack('<i', -1) + struct.pack('<I', 14) + struct.pack('<3f', 0, 0, 0)
                  + struct.pack('<9f', 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack('<f', 1.0) + struct.pack('<i', col) + struct.pack('<II', 0, 0))
open(OUT, 'wb').write(n.write())
fl = [p for k, p in pieces if k == 'floor']
print(f'{OUT}: {len(kids)} convex pieces ({sum(1 for k, _ in pieces if k == "floor")} floor, {sum(1 for k, _ in pieces if k == "wall")} wall, mast), '
      f'scale {SCALE}, model units: floor y {min(p[:,1].min() for p in fl):.0f}..{max(p[:,1].max() for p in fl):.0f}, deck z {min(p[:,2].max() for p in fl):.0f}..{max(p[:,2].max() for p in fl):.0f} (stand), {len(open(OUT,"rb").read())} bytes')
