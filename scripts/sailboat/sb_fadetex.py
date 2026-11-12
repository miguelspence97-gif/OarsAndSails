# RowRim v1.8.32d: the shapes the plugin fades get textures of their own. The game shares one material between shapes
# with the same textures, so fading the jib's rope wraps faded every rope (Rope02), the jib faded the main (BoatFabric02),
# the clew ring all the iron. Now: SB_SailJibS / SB_SailJibP -> BoatFabric02_jib_d.dds, SB_JibFurl ->
# BoatFabric02_jibfurl_d.dds (byte copies of BoatFabric02_d / BoatFabric02_furl_d); the wraps and the ring are not faded.
# usage: python sb_fadetex.py <in nif> <out nif>
import sys, struct
sys.path.insert(0, '.')
from nifp import parse, tri_shape
from nifmerge import Nif, av_fix
SRC, OUT = sys.argv[1:3]
d = open(SRC, 'rb').read(); h = parse(d); n = Nif(d)
T = 'textures\\RowRim\\SailBoat\\'
NEW = {'SB_SailJibS': 'BoatFabric02_jib_d.dds', 'SB_SailJibP': 'BoatFabric02_jib_d.dds', 'SB_JibFurl': 'BoatFabric02_jibfurl_d.dds'}
def set_diffuse(raw, path):
    c, = struct.unpack_from('<I', raw, 0); p = 4; out = []
    for _ in range(c):
        l, = struct.unpack_from('<I', raw, p); out.append(raw[p + 4:p + 4 + l]); p += 4 + l
    out[0] = path.encode()
    return struct.pack('<I', c) + b''.join(struct.pack('<I', len(q)) + q for q in out)
seen = {}
for b in h['blocks']:
    if b['type'] != 'BSTriShape': continue
    t = tri_shape(d, h, b)
    if t['name'] not in NEW: continue
    sr, sp = av_fix(n.blocks[t['shader']], 4); tex, = struct.unpack_from('<i', sr, sp + 4 + 4 + 8 + 8)
    assert tex not in seen, 'texture set shared'; seen[tex] = t['name']
    n.blocks[tex] = set_diffuse(n.blocks[tex], T + NEW[t['name']]); print(t['name'], '->', NEW[t['name']])
out = n.write(); open(OUT, 'wb').write(out); print(OUT, len(out))
