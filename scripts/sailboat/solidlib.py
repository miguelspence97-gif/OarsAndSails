# shared: capping open ends, turning inside-out parts right way out (vertex arrays: P, N, F; extra per-vertex rows ride along)
import numpy as np
from collections import Counter
def weld(P, tol=1e-3):
    key = np.round(P / tol).astype(np.int64); _, w = np.unique(key, axis=0, return_inverse=True); return w.ravel()
def boundary(F, w):
    """directed boundary edges (original vertex ids) as their one triangle runs them"""
    und = {}
    for fa, fb, fc in F:
        for x, y in ((fa, fb), (fb, fc), (fc, fa)):
            k = (min(w[x], w[y]), max(w[x], w[y]))
            if k[0] == k[1]: continue
            und.setdefault(k, []).append((x, y))
    return [es[0] for es in und.values() if len(es) == 1]
def loops(bedges, w):
    """boundary loops in the cap's order (each edge run backwards): lists of original vertex ids"""
    nxt = {}
    for x, y in bedges: nxt.setdefault(w[y], []).append((y, x))
    used = set(); out = []
    for x0, y0 in bedges:
        if (x0, y0) in used: continue
        start = w[y0]; cur = start; L = []; ok = False
        for _ in range(len(bedges) + 1):
            cand = [e for e in nxt.get(cur, []) if (e[1], e[0]) not in used]
            if not cand: break
            y, x = cand[0]; used.add((x, y)); L.append(y); cur = w[x]
            if cur == start: ok = True; break
        if ok and len(L) >= 3: out.append(L)
    return out
def cap_loops(P, F, max_n=64, max_span=16.0):
    """returns new triangles (centre index given as -1-i for the i-th new centre) and the centres; caps share the ring's
    vertices (callers that want flat cap shading duplicate them)"""
    w = weld(P); L = loops(boundary(F, w), w); caps = []
    for ring in L:
        q = P[ring]
        if len(ring) > max_n or np.ptp(q, 0).max() >= max_span: continue
        caps.append(ring)
    return caps
def solid_volume_sign(P, F):
    T = P[F]; return np.einsum('ij,ij->i', T[:, 0], np.cross(T[:, 1], T[:, 2])).sum() / 6.0
def components_welded(F, w):
    parent = {}
    def find(a):
        while parent.setdefault(a, a) != a: parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for f in w[F]:
        a = find(f[0])
        for b in f[1:]:
            rb = find(b)
            if rb != a: parent[rb] = a
    roots = np.array([find(f[0]) for f in w[F]])
    return roots
def right_way_out(P, F):
    """flip the triangles of every closed part that is inside out; returns (F, mask of vertices whose part was flipped)"""
    w = weld(P); roots = components_welded(F, w); F = F.copy(); flipped = np.zeros(len(P), bool)
    Fw = w[F]
    for r in np.unique(roots):
        m = roots == r; sub = F[m]
        E = Counter()
        for a, b, c in Fw[m]: E[(min(a, b), max(a, b))] += 1; E[(min(b, c), max(b, c))] += 1; E[(min(c, a), max(c, a))] += 1
        if any(v == 1 for v in E.values()): continue  # open: no inside to speak of
        if solid_volume_sign(P, sub) < 0:
            F[m] = sub[:, [0, 2, 1]]; flipped[np.unique(sub)] = True
    return F, flipped
def face_normals_at(P, F):
    acc = np.zeros_like(P)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    for i in range(3): np.add.at(acc, F[:, i], fn)
    return acc
def consistent_outward(P, F, tol=1e-3):
    """v1.8.39: one closed part's triangles all wound the same way (spread from the first over shared edges, by welded
    position), then the whole turned outward by its volume's sign; returns F"""
    from collections import defaultdict, deque
    w = weld(P, tol); Fw = w[F]; F = F.copy()
    edges = defaultdict(list)
    for i, (a, b, c) in enumerate(Fw):
        for x, y in ((a, b), (b, c), (c, a)): edges[(min(x, y), max(x, y))].append(i)
    def runs(i, x, y):  # does triangle i run the edge x->y
        a, b, c = w[F[i]]
        return (a, b) == (x, y) or (b, c) == (x, y) or (c, a) == (x, y)
    seen = np.zeros(len(F), bool)
    for s0 in range(len(F)):
        if seen[s0]: continue
        seen[s0] = True; q = deque([s0])
        while q:
            i = q.popleft(); a, b, c = w[F[i]]
            for x, y in ((a, b), (b, c), (c, a)):
                for j in edges[(min(x, y), max(x, y))]:
                    if j == i or seen[j]: continue
                    if runs(j, x, y): F[j] = F[j][[0, 2, 1]]  # runs it the same way as i: flipped
                    seen[j] = True; q.append(j)
    if solid_volume_sign(P, F) < 0: F = F[:, [0, 2, 1]]
    return F
