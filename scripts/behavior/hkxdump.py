import struct, sys
d = open(sys.argv[1], 'rb').read()
magic = struct.unpack_from('<II', d, 0); print('magic %x %x' % magic)
userTag, fileVersion = struct.unpack_from('<ii', d, 8)
layout = d[16:20]; numSections, contentsSection, contentsSectionOffset, contentsClassNameSection, contentsClassNameOffset = struct.unpack_from('<iiiii', d, 20)
ver = d[40:56]; flags, maxpredicate, predicateArraySizePlusPadding = struct.unpack_from('<ihh', d, 56)
print('fileVersion', fileVersion, 'layout', list(layout), 'sections', numSections, 'contents', contentsSection, contentsSectionOffset, contentsClassNameSection, contentsClassNameOffset, ver, flags, maxpredicate, predicateArraySizePlusPadding)
hdr = 64 + (predicateArraySizePlusPadding if fileVersion >= 11 else 0)
secs = []
for i in range(numSections):
    o = hdr + i * 64 if fileVersion >= 11 else hdr + i * 48
    tag = d[o:o + 20].split(b'\0')[0]
    absd, lf, gf, vf, exp, imp, end = struct.unpack_from('<7i', d, o + 20)
    secs.append((tag, absd, lf, gf, vf, exp, imp, end)); print(tag, absd, lf, gf, vf, exp, imp, end)
cn = secs[0]; cbase = cn[1]
def cname(off): return d[cbase + off:d.index(b'\0', cbase + off)].decode()
ds = secs[2]; base = ds[1]
lfix = [struct.unpack_from('<ii', d, base + o) for o in range(ds[2], ds[3] - 7, 8)]
gfix = [struct.unpack_from('<iii', d, base + o) for o in range(ds[3], ds[4] - 11, 12)]
vfix = [struct.unpack_from('<iii', d, base + o) for o in range(ds[4], ds[5] - 11, 12)]
lfix = [x for x in lfix if x[0] != -1]; gfix = [x for x in gfix if x[0] != -1]; vfix = [x for x in vfix if x[0] != -1]
objs = sorted((o, cname(c)) for o, s, c in vfix)
print('objects:')
for i, (o, c) in enumerate(objs):
    nxt = objs[i + 1][0] if i + 1 < len(objs) else ds[2]
    print('  %5d %-36s next %5d' % (o, c, nxt))
print('local fixups', len(lfix)); 
for s, t in lfix: print('   L %5d -> %5d' % (s, t), repr(d[base + t:base + t + 40].split(b'\0')[0][:40]))
print('global fixups', len(gfix))
for s, sec, t in gfix: print('   G %5d -> sec %d %5d' % (s, sec, t))
