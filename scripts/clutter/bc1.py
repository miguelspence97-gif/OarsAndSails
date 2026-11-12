# BC1 (DXT1) encoder with mips: per 4x4 block, endpoints along the colours' main axis, then two least-squares refits
import numpy as np, struct
def _565(c):
    c = np.clip(np.rint(c * [31, 63, 31]), 0, [31, 63, 31]).astype(np.uint16)
    return (c[..., 0] << 11) | (c[..., 1] << 5) | c[..., 2]
def _un565(v):
    return np.stack([(v >> 11) & 31, (v >> 5) & 63, v & 31], -1).astype(np.float32) / [31, 63, 31]
def encode_level(a):  # a: HxWx3 float 0..1, H, W multiples of 4 (or padded)
    H, W = a.shape[:2]
    hp, wp = max(4, (H + 3) // 4 * 4), max(4, (W + 3) // 4 * 4)
    if (hp, wp) != (H, W): a = np.pad(a, ((0, hp - H), (0, wp - W), (0, 0)), mode='edge')
    b = a.reshape(hp // 4, 4, wp // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
    mean = b.mean(1, keepdims=True); d = b - mean
    cov = np.einsum('nki,nkj->nij', d, d)
    axis = np.ones((len(b), 3), np.float32) / np.sqrt(3)
    for _ in range(8):
        axis = np.einsum('nij,nj->ni', cov, axis); axis /= np.linalg.norm(axis, axis=1, keepdims=True) + 1e-12
    t = np.einsum('nki,ni->nk', d, axis)
    lo, hi = t.min(1), t.max(1)
    e0 = mean[:, 0] + axis * hi[:, None]; e1 = mean[:, 0] + axis * lo[:, None]
    w = np.array([0, 1, 1 / 3, 2 / 3], np.float32)
    for _ in range(2):
        c0, c1 = _un565(_565(e0)), _un565(_565(e1))
        pal = c0[:, None] * (1 - w)[None, :, None] + c1[:, None] * w[None, :, None]
        idx = np.argmin(((b[:, :, None] - pal[:, None]) ** 2).sum(-1), 2)
        wt = w[idx]  # least squares for e0, e1 given weights
        A00 = ((1 - wt) ** 2).sum(1); A11 = (wt ** 2).sum(1); A01 = ((1 - wt) * wt).sum(1)
        det = A00 * A11 - A01 ** 2
        ok = np.abs(det) > 1e-6
        r0 = ((1 - wt)[..., None] * b).sum(1); r1 = (wt[..., None] * b).sum(1)
        n0 = (A11[:, None] * r0 - A01[:, None] * r1) / np.where(ok, det, 1)[:, None]
        n1 = (A00[:, None] * r1 - A01[:, None] * r0) / np.where(ok, det, 1)[:, None]
        e0 = np.where(ok[:, None], np.clip(n0, 0, 1), e0); e1 = np.where(ok[:, None], np.clip(n1, 0, 1), e1)
    v0, v1 = _565(e0), _565(e1)
    c0, c1 = _un565(v0), _un565(v1)
    swap = v0 < v1  # four-colour mode needs c0 > c1
    v0, v1 = np.where(swap, v1, v0), np.where(swap, v0, v1)
    c0, c1 = np.where(swap[:, None], c1, c0), np.where(swap[:, None], c0, c1)
    pal = c0[:, None] * (1 - w)[None, :, None] + c1[:, None] * w[None, :, None]
    idx = np.argmin(((b[:, :, None] - pal[:, None]) ** 2).sum(-1), 2)
    code = np.array([0, 1, 2, 3])[idx]  # palette order 0:c0 1:c1 2:2/3c0+1/3c1 3:1/3c0+2/3c1 (w above matches)
    eq = v0 == v1; code[eq] = 0
    bits = (code.astype(np.uint32) << (2 * np.arange(16, dtype=np.uint32))).sum(1).astype(np.uint32)
    out = np.zeros(len(b), dtype=[('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')])
    out['c0'], out['c1'], out['i'] = v0, v1, bits
    return out.tobytes()
def save_dds(path, img):  # img: PIL RGB, square power of two
    from PIL import Image
    w, h = img.size; levels = []; cur = img
    while True:
        levels.append(encode_level(np.asarray(cur, np.float32) / 255))
        if cur.size == (1, 1): break
        cur = cur.resize((max(1, cur.width // 2), max(1, cur.height // 2)), Image.BOX)
    hdr = struct.pack('<4sIIIIIII44sII4sIIIIIIIIII', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, h, w,
                      max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8, 0, len(levels), b'\0' * 44,
                      32, 0x4, b'DXT1', 0, 0, 0, 0, 0, 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    open(path, 'wb').write(hdr + b''.join(levels))
    return len(levels)
