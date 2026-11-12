# v1.7.96: the sails skinned to a grid of bones (as armour is skinned to a skeleton): the mesh is static and the GPU
# bends it by the bones, which the plugin moves for the flapping (the reshaped-every-frame cloth flickered).
import numpy as np, struct
def sail_tu(P, jib):
    """t: 0 at the foot .. 1 at the head (jib: up the luff), u: 0 at the luff .. 1 at the leech (as the plugin did)"""
    a = P[:, 2] if jib else -P[:, 1]          # along: jib up its luff (z), main aft along the boom (-y)
    h = -P[:, 1] if jib else P[:, 2]          # across: jib out to the clew (-y), main up (z)
    K = 16
    def bands(v): lo, hi = v.min(), v.max(); return np.clip(((v - lo) / max(hi - lo, 1.0) * K).astype(int), 0, K - 1)
    ba, bh = bands(a), bands(h)
    t = np.zeros(len(P)); u = np.zeros(len(P))
    for b in range(K):
        m = ba == b
        if m.any():
            lo, hi = h[m].min(), h[m].max()
            if jib: u[m] = (h[m] - lo) / max(hi - lo, 1.0)
            else: t[m] = (h[m] - lo) / max(hi - lo, 1.0)
        m = bh == b
        if m.any() and not jib:
            lo, hi = a[m].min(), a[m].max(); u[m] = (a[m] - lo) / max(hi - lo, 1.0)
    if jib: t = (a - a.min()) / max(a.max() - a.min(), 1.0)
    return np.clip(t, 0, 1), np.clip(u, 0, 1)
def bone_grid(P, t, u, R, C):
    """bone bind positions on the cloth at grid points (r: t, c: u), and each vertex's 4 bones + bilinear weights"""
    binds = np.zeros((R * C, 3))
    for r in range(R):
        for c in range(C):
            tr, uc = r / (R - 1), c / (C - 1)
            w = np.exp(-((t - tr) ** 2 + (u - uc) ** 2) / 0.004)
            if w.sum() < 1e-6: w = np.exp(-((t - tr) ** 2 + (u - uc) ** 2) / 0.05)
            binds[r * C + c] = (P * w[:, None]).sum(0) / w.sum()
    ft = t * (R - 1); fu = u * (C - 1)
    r0 = np.clip(np.floor(ft).astype(int), 0, R - 2); c0 = np.clip(np.floor(fu).astype(int), 0, C - 2)
    at = ft - r0; au = fu - c0
    idx = np.stack([r0 * C + c0, r0 * C + c0 + 1, (r0 + 1) * C + c0, (r0 + 1) * C + c0 + 1], 1)
    wts = np.stack([(1 - at) * (1 - au), (1 - at) * au, at * (1 - au), at * au], 1)
    return binds, idx, wts
def skin_blocks(n, e8, P, N, Tg, U, F, idx, wts, binds, bone_ids, root_id):
    """NiSkinInstance + NiSkinData + NiSkinPartition (one partition, every vertex, global vertex buffer, 40-byte
    skinned vertices: position, bitangent x, uv, normal, tangent, 4 half weights, 4 bone indices) as the game's armour"""
    nv = len(P); nb = len(binds)
    B = np.cross(N, Tg)
    vb = np.zeros((nv, 40), np.uint8)
    vb[:, 0:12] = np.ascontiguousarray(P.astype('<f4')).view(np.uint8).reshape(-1, 12)
    vb[:, 12:16] = np.ascontiguousarray(B[:, 0:1].astype('<f4')).view(np.uint8).reshape(-1, 4)
    vb[:, 16:20] = np.ascontiguousarray(np.stack([U[:, 0], 1.0 - U[:, 1]], 1).astype('<f2')).view(np.uint8).reshape(-1, 4)
    vb[:, 20:23] = e8(N); vb[:, 23] = e8(B[:, 1]); vb[:, 24:27] = e8(Tg); vb[:, 27] = e8(B[:, 2])
    vb[:, 28:36] = np.ascontiguousarray(wts.astype('<f2')).view(np.uint8).reshape(-1, 8)
    vb[:, 36:40] = idx.astype(np.uint8)
    desc = 0x0005b0007065040a
    tris = np.asarray(F).astype('<u2')
    # NiSkinData: identity overall; per bone the bind inverse (bones unrotated: -bind), a sphere, the weights
    sd = struct.pack('<9f', 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack('<3f', 0, 0, 0) + struct.pack('<f', 1.0) + struct.pack('<I', nb) + b'\x01'
    for b in range(nb):
        m = (idx == b) & (wts > 1e-4)
        vi = np.where(m.any(1))[0]; ww = (wts * m).sum(1)[vi]
        loc = P[vi] - binds[b] if len(vi) else np.zeros((1, 3))
        cen = loc.mean(0); rad = float(np.linalg.norm(loc - cen, axis=1).max()) if len(vi) else 1.0
        sd += struct.pack('<9f', 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack('<3f', *(-binds[b])) + struct.pack('<f', 1.0)
        sd += struct.pack('<4f', *cen, rad) + struct.pack('<H', len(vi))
        sd += b''.join(struct.pack('<Hf', int(i), float(w)) for i, w in zip(vi, ww))
    # NiSkinPartition (SSE): partitions, data size, vertex size, desc, the vertices; then the one partition
    sp = struct.pack('<IIIQ', 1, nv * 40, 40, desc) + vb.tobytes()
    sp += struct.pack('<5H', nv, len(tris), nb, 0, 4) + struct.pack('<%dH' % nb, *range(nb))
    sp += b'\x01' + np.arange(nv, dtype='<u2').tobytes()
    sp += b'\x01' + wts.astype('<f4').tobytes()
    sp += b'\x01' + tris.tobytes()
    sp += b'\x01' + idx.astype(np.uint8).tobytes()
    sp += b'\x00\x00' + struct.pack('<Q', desc) + tris.tobytes()
    data_id = n.add('NiSkinData', sd)
    part_id = n.add('NiSkinPartition', sp)
    inst_id = n.add('NiSkinInstance', struct.pack('<iiiI', data_id, part_id, root_id, nb) + struct.pack('<%di' % nb, *bone_ids))
    return inst_id, desc
