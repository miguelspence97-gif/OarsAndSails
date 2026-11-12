# RowRim v1.8.39: meshes\RowRim\RowRim_WindStreak.nif (the swoosh quad's blocks, 4 wide x 16 long, its own texture) and
# textures\RowRim\wind_streak.dds (a soft white line along v, DXT1 with mips)
import sys, struct, numpy as np
sys.path.insert(0, '.')
from nifmerge import Nif
from hull_v144_lib import load, store, pos, setpos
from bc1 import save_dds
from PIL import Image
SRC, OUTN, OUTT = sys.argv[1:4]
d = open(SRC, 'rb').read(); n = Nif(d)
st, vb, F = load(n.blocks[1]); P = pos(vb).astype(float)
P[:, 0] = np.sign(P[:, 0]) * 2.0; P[:, 1] = np.sign(P[:, 1]) * 8.0
setpos(vb, P); n.blocks[1] = store(st, vb, F)
old = b'textures\\RowRim\\swoosh_streak.dds'; new = b'textures\\RowRim\\wind_streak.dds'
b = n.blocks[2]; i = b.find(old); assert i > 4
n.blocks[2] = b[:i - 4] + struct.pack('<I', len(new)) + new + b[i + len(old):]
n.names = ['RowRim_WindStreakPiece' if s == 'RowRim_SwooshPiece' else s for s in n.names]
out = n.write(); open(OUTN, 'wb').write(out); print(OUTN, len(out))
# the line: across u a soft core (gaussian), along v even (the pieces butt end to end)
W = 32; u = (np.arange(W) + 0.5) / W
prof = np.exp(-((u - 0.5) / 0.18) ** 2) * 0.9; prof[prof < 0.02] = 0
img = np.tile(prof[None, :, None], (W, 1, 3)) * np.array([0.92, 0.96, 1.0])
print('levels', save_dds(OUTT, Image.fromarray((img * 255).astype(np.uint8))))
