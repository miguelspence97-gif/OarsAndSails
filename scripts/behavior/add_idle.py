# RowRim v1.8.40: a second basic idle in an FNIS behaviour (FNIS_<mod>_Behavior.hkx, hk_2010.2.0-r1, 64 bit), as GenerateFNIS
# would make it for a second "b" line: its own state, modifier generator, IsActive modifier and binding set, clip generator,
# the event added to the graph's events, and a wildcard transition on it to the new state. Pandora links this file into
# 0_Master as it is (it does not build it from the list), so the list's new line alone did nothing.
# usage: python add_idle.py <in.hkx> <out.hkx> <event> <animation file name>
import struct, sys
SRC, OUT, EVENT, ANIM = sys.argv[1:5]
d = bytearray(open(SRC, 'rb').read())
numSections = struct.unpack_from('<i', d, 20)[0]; assert numSections == 3 and struct.unpack_from('<i', d, 12)[0] == 8
HDR = 64
def sec(i):
    o = HDR + i * 48
    return list(struct.unpack_from('<7i', d, o + 20)), o
(cn, cno), (ty, tyo), (ds, dso) = sec(0), sec(1), sec(2)
cbase, base = cn[0], ds[0]
data = bytearray(d[base:base + ds[1]])
def rd(fmt, o, buf=None): return struct.unpack_from(fmt, data if buf is None else buf, o)
lfix = [rd('<ii', base_o, d) for base_o in range(base + ds[1], base + ds[2] - 7, 8)]; lfix = [x for x in lfix if x[0] != -1]
gfix = [rd('<iii', o, d) for o in range(base + ds[2], base + ds[3] - 11, 12)]; gfix = [x for x in gfix if x[0] != -1]
vfix = [rd('<iii', o, d) for o in range(base + ds[3], base + ds[4] - 11, 12)]; vfix = [x for x in vfix if x[0] != -1]
def cname(off): return bytes(d[cbase + off:d.index(b'\0', cbase + off)]).decode()
cls_off = {cname(c): c for _, _, c in vfix}
objs = sorted((o, cname(c)) for o, _, c in vfix)
nxt = {o: (objs[i + 1][0] if i + 1 < len(objs) else ds[1]) for i, (o, _) in enumerate(objs)}
kind = {o: c for o, c in objs}
lmap = {s: t for s, t in lfix}; gmap = {s: t for s, _, t in gfix}
def strat(o): return bytes(data[o:data.index(b'\0', o)]).decode()
# the parts of the one idle, found by their links
root_sm = gmap[96 + 128]  # hkbBehaviorGraph.rootGenerator (checked below)
assert kind[root_sm] == 'hkbStateMachine'
st0 = gmap[lmap[root_sm + 144]]; inner = gmap[st0 + 88]; assert kind[inner] == 'hkbStateMachine'
states_arr = lmap[inner + 144]; n_states = rd('<i', inner + 152)[0]; assert n_states >= 1  # the first idle is the pattern
idle_st = gmap[states_arr]; assert kind[idle_st] == 'hkbStateMachineStateInfo'
mg = gmap[idle_st + 88]; mod = gmap[mg + 72]; clip = gmap[mg + 80]; vbs = gmap[mod + 16]
assert (kind[mg], kind[mod], kind[clip], kind[vbs]) == ('hkbModifierGenerator', 'BSIsActiveModifier', 'hkbClipGenerator', 'hkbVariableBindingSet')
wild = gmap[inner + 160]; trans_arr = lmap[wild + 16]; n_trans = rd('<i', wild + 24)[0]; assert n_trans == n_states
graph_data = gmap[96 + 136]; assert kind[graph_data] == 'hkbBehaviorGraphData'
string_data = gmap[graph_data + 120]; assert kind[string_data] == 'hkbBehaviorGraphStringData'
ev_arr = lmap[string_data + 16]; n_ev = rd('<i', string_data + 24)[0]
evinfo_arr = lmap[graph_data + 64]; n_evinfo = rd('<i', graph_data + 72)[0]; assert n_ev == n_evinfo
names = [strat(lmap[ev_arr + 8 * k]) for k in range(n_ev)]
assert EVENT not in names, 'already there'
old_anim = strat(lmap[clip + 72]); old_clipname = strat(lmap[clip + 56]); old_state = strat(lmap[idle_st + 96])
print('idle', old_state, '->', old_clipname, old_anim, '| events', n_ev)
new_l, new_g, new_v = [], [], []
def align(n=16):
    while len(data) % n: data.append(0)
def put(b, n=16):
    align(n); o = len(data); data.extend(b); return o
def put_str(s): return put(s.encode() + b'\0', 2)
def copy_obj(o):
    """the object and everything up to the next object (its own strings and arrays), its fixups carried over"""
    end = nxt[o]; no = put(bytes(data[o:end])); sh = no - o
    for s, t in lfix:
        if o <= s < end: new_l.append([s + sh, t + sh if o <= t < end else t])
    for s, sc, t in gfix:
        if o <= s < end: new_g.append([s + sh, sc, t])
    new_v.append([no, 0, cls_off[kind[o]]])
    return no, sh
def set_l(src, dst):
    for x in new_l:
        if x[0] == src: x[1] = dst; return
    new_l.append([src, dst])
def set_g(src, dst):
    for x in new_g:
        if x[0] == src: x[2] = dst; return
    new_g.append([src, 2, dst])
# the copies
c_st, _ = copy_obj(idle_st); c_mg, _ = copy_obj(mg); c_mod, _ = copy_obj(mod); c_vbs, _ = copy_obj(vbs); c_clip, _ = copy_obj(clip)
set_g(c_st + 88, c_mg); set_g(c_mg + 72, c_mod); set_g(c_mg + 80, c_clip); set_g(c_mod + 16, c_vbs)
new_id = max(rd('<i', gmap[states_arr + 8 * k] + 104)[0] for k in range(n_states)) + 1
struct.pack_into('<i', data, c_st + 104, new_id)
mx = max(rd('<Q', o + 48)[0] for o in kind if kind[o] in ('hkbModifierGenerator', 'BSIsActiveModifier', 'hkbClipGenerator'))
struct.pack_into('<Q', data, c_mg + 48, mx + 1); struct.pack_into('<Q', data, c_mod + 48, mx + 2)
anim_new = old_anim.rsplit('\\', 1)[0] + '\\' + ANIM
clip_new = old_clipname.rsplit('_', 1)[0] if False else old_clipname[:old_clipname.find('_') + 1] + ANIM.rsplit('.', 1)[0]
for o, s in ((c_st + 96, EVENT), (c_mg + 56, EVENT + '_MG'), (c_mod + 56, EVENT + '_IsActive'), (c_clip + 56, clip_new), (c_clip + 72, anim_new)):
    set_l(o, put_str(s))
# the inner state machine's states: the old one and the new
arr = put(bytes(8 * (n_states + 1)))
for k in range(n_states): new_g.append([arr + 8 * k, 2, gmap[states_arr + 8 * k]])
new_g.append([arr + 8 * n_states, 2, c_st])
struct.pack_into('<ii', data, inner + 152, n_states + 1, (n_states + 1) | -0x80000000)
lmap_upd = {inner + 144: arr}
# the wildcard transitions: the old one, and the new event to the new state
arr2 = put(bytes(data[trans_arr:trans_arr + 72 * n_trans]) + bytes(data[trans_arr:trans_arr + 72]))
for k in range(n_trans + 1):
    src_k = trans_arr + 72 * min(k, 0 if k == n_trans else k)
    eff = gmap.get(src_k + 32)
    if eff is not None: new_g.append([arr2 + 72 * k + 32, 2, eff])
    cond = gmap.get(src_k + 40)
    if cond is not None: new_g.append([arr2 + 72 * k + 40, 2, cond])
struct.pack_into('<ii', data, arr2 + 72 * n_trans + 48, n_ev, new_id)
struct.pack_into('<ii', data, wild + 24, n_trans + 1, (n_trans + 1) | -0x80000000)
lmap_upd[wild + 16] = arr2
# the events: the old names and the new
arr3 = put(bytes(8 * (n_ev + 1)))
for k in range(n_ev): new_l.append([arr3 + 8 * k, lmap[ev_arr + 8 * k]])
new_l.append([arr3 + 8 * n_ev, put_str(EVENT)])
struct.pack_into('<ii', data, string_data + 24, n_ev + 1, (n_ev + 1) | -0x80000000)
lmap_upd[string_data + 16] = arr3
arr4 = put(bytes(data[evinfo_arr:evinfo_arr + 4 * n_ev]) + bytes(4))
struct.pack_into('<ii', data, graph_data + 72, n_ev + 1, (n_ev + 1) | -0x80000000)
lmap_upd[graph_data + 64] = arr4
align(16)
# the fixup tables, written after the data
L = [[s, lmap_upd.get(s, t)] for s, t in lfix] + new_l
G = [list(x) for x in gfix] + new_g
V = [list(x) for x in vfix] + new_v
out = bytearray(data)
lo = len(out)
for s, t in L: out += struct.pack('<ii', s, t)
while len(out) % 16: out += b'\xff'
go = len(out)
for s, sc, t in G: out += struct.pack('<iii', s, sc, t)
while len(out) % 16: out += b'\xff'
vo = len(out)
for o, sc, c in V: out += struct.pack('<iii', o, sc, c)
while len(out) % 16: out += b'\xff'
eo = len(out)
hdr = bytearray(d[:base])
struct.pack_into('<7i', hdr, dso + 20, base, lo, go, vo, eo, eo, eo)
res = bytes(hdr) + bytes(out)
open(OUT, 'wb').write(res)
print('wrote', OUT, len(res), 'bytes: states', n_states + 1, ', events', n_ev + 1, 'new state id', new_id, '|', clip_new, anim_new)
