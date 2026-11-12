# RowRim v1.8.27e: an iron ring (cringle) in the jib's clew, the corner the jib sheet ties to. Under SB_JibRig (it swings
# with the jib), at the clew (SB_JibClew, 0 -172 122.3 in the rig) a little inside the corner, its hole across the sail.
# Its own shader, texture set and alpha: copies of SB_Ironwork's. The plugin runs the sheet from it to the mast.
# usage: python sb_clewring.py <sailboat nif> <out nif>
import sys, struct, numpy as np
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix, build_shape
SRC, OUT = sys.argv[1:3]
C = np.array([0.0, -170.6, 121.3])   # the ring's centre: just in from the clew corner (0 -172 122.3), half of it out past the cloth
RR, rr = 2.4, 0.9                   # ring radius, wire radius
NU, NV = 20, 8
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
iron = next(b for b in h['blocks'] if b['type'] == 'BSTriShape' and tri_shape(d, h, b)['name'] == 'SB_Ironwork')
jib = next(b['k'] for b in h['blocks'] if b['type'] == 'NiNode' and h['names'][struct.unpack_from('<i', d, b['start'])[0]] == 'SB_JibRig')
ti = tri_shape(d, h, iron)
# the ring in the sail's plane (y z), its axis x
P = []; N = []; Tg = []; UV = []
for i in range(NU + 1):
    a = 2 * np.pi * i / NU
    c0 = np.array([0.0, np.cos(a), np.sin(a)])
    t0 = np.array([0.0, -np.sin(a), np.cos(a)])
    for j in range(NV + 1):
        b = 2 * np.pi * j / NV
        nn = c0 * np.cos(b) + np.array([1.0, 0.0, 0.0]) * np.sin(b)
        P.append(C + c0 * RR + nn * rr); N.append(nn); Tg.append(t0); UV.append((0.40 + 0.2 * i / NU, 0.45 + 0.1 * j / NV))
T = []
for i in range(NU):
    for j in range(NV):
        a = i * (NV + 1) + j; b = a + NV + 1
        T += [(a, b, a + 1), (a + 1, b, b + 1)]
P = np.array(P); N = np.array(N); Tg = np.array(Tg)
# its own shader / texture set / alpha
raw = n.blocks[iron['k']]
_, ip = av_fix(raw)
shd, alp = struct.unpack_from('<2i', raw, ip + 60 + 16 + 4)
sr, sp = av_fix(n.blocks[shd], 4); sr = bytearray(sr)
tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
struct.pack_into('<i', sr, sp + 4 + 4 + 8 + 8, n.add('BSShaderTextureSet', n.blocks[tex]))
nshd = n.add('BSLightingShaderProperty', sr)
nalp = n.add('NiAlphaProperty', av_fix(n.blocks[alp])[0]) if alp >= 0 else -1
k = build_shape(n, raw, P, N, Tg, UV, T, nshd, nalp)
r = bytearray(n.blocks[k]); struct.pack_into('<i', r, 0, n.name('SB_JibClewRing'))
fl, = struct.unpack_from('<I', r, 12); struct.pack_into('<I', r, 12, fl | 1)  # hidden in the nif: the plugin shows it with the jib
n.blocks[k] = bytes(r)
nr = bytearray(n.blocks[jib]); ne, = struct.unpack_from('<I', nr, 4); q = 4 + 4 + 4 * ne + 4 + 4 + 12 + 36 + 4 + 4
nc, = struct.unpack_from('<I', nr, q); kids = list(struct.unpack_from('<%di' % nc, nr, q + 4)) + [k]
n.blocks[jib] = bytes(nr[:q]) + struct.pack('<I', len(kids)) + struct.pack('<%di' % len(kids), *kids) + bytes(nr[q + 4 + 4 * nc:])
out = n.write(); open(OUT, 'wb').write(out)
print(f'{OUT}: {len(d):,} -> {len(out):,} bytes (ring {k}: {len(P)} vertices, {len(T)} triangles, shader {nshd}, alpha {nalp})')
