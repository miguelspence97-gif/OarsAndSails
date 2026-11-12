# RowRim v1.8.8: OarsAndSails.esl gets the two boats' own hull activators (no name: no prompt, no activation): the hull
# refs of the sailing boat and the longship, each with its own collision model (RowRim_SailBoatHull.nif,
# RowRim_LongshipHull.nif). 0x804 OAS_SailBoatHull, 0x805 OAS_LongshipHull. Run once on release\OarsAndSails.esl.
import struct, sys
src, out = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
assert d[:4] == b'TES4'
tsz, = struct.unpack_from('<I', d, 4)
hedr = d.index(b'HEDR', 24)
ver, nrec, nxt = struct.unpack_from('<fiI', d, hedr + 6)
g = 24 + tsz
assert d[g:g+4] == b'GRUP' and d[g+8:g+12] == b'ACTI'
gsz, = struct.unpack_from('<I', d, g + 4)
def sub(t, data): return t + struct.pack('<H', len(data)) + data
def acti(fid, edid, bnd, model):
    body = sub(b'EDID', edid.encode() + b'\0') + sub(b'OBND', struct.pack('<6h', *bnd)) + sub(b'MODL', model.encode() + b'\0') + sub(b'FNAM', b'\0\0')
    return b'ACTI' + struct.pack('<IIIIHH', len(body), 0, fid, 0, 44, 0) + body
have = bytes(d[g:g+gsz])
new = b''
for fid, edid, bnd, model in [(0x804, 'OAS_SailBoatHull', (-103, -280, -4, 103, 420, 626), 'RowRim\\RowRim_SailBoatHull.nif'),
                              (0x805, 'OAS_LongshipHull', (-703, -1100, -110, 703, 1100, 985), 'RowRim\\RowRim_LongshipHull.nif')]:
    if edid.encode() in have: print(edid, 'already there'); continue
    new += acti(fid, edid, bnd, model)
d[g+gsz:g+gsz] = new
struct.pack_into('<I', d, g + 4, gsz + len(new))
add = new.count(b'ACTI')
struct.pack_into('<fiI', d, hedr + 6, ver, nrec + add, max(nxt, 0x806))
open(out, 'wb').write(d)
print(f'{out}: +{add} records, {len(d)} bytes, next id {max(nxt, 0x806):X}')
