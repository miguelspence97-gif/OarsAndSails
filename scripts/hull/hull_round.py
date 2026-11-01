# Rounds the hull: shapes 10 (hull skins) and 13 (gunwale, posts, ribs, keel, thwarts) get one level of Loop subdivision
# together (their shared seams welded), creases over CREASE deg kept sharp; the flat boards (thwarts, stern bench, end
# decks) are held rigid (only split, never moved or reshaded). Oarlocks (shape 7) untouched.
from hull_v144_lib import *
from hull_loop import loop
import sys
SRC,OUT=sys.argv[1],sys.argv[2]; CREASE=float(sys.argv[3]) if len(sys.argv)>3 else 55.0
d=open(SRC,'rb').read(); h=parse(d); nb={}
L={}
for k in (10,13):
    b=h['blocks'][k]; L[k]=load(d[b['start']:b['end']])
vb=np.concatenate([L[10][1],L[13][1]]); off=len(L[10][1])
T=np.concatenate([L[10][2],L[13][2]+off]); shp=np.array([10]*len(L[10][2])+[13]*len(L[13][2]))
P=pos(vb); rig=np.zeros(len(T),bool); nr=0
P13=pos(L[13][1])
for c in components(P13,L[13][2]):
    Q=P13[c]; mn,mx=Q.min(0),Q.max(0)
    if mx[2]-mn[2]<6.5 and (mx[0]-mn[0])>30:   # a flat board
        cs=set((c+off).tolist()); m=np.array([all(int(v) in cs for v in t) for t in T]); rig|=m; nr+=1
print('rigid boards',nr,'faces',rig.sum())
v2,t2=loop(vb,T,CREASE,rig)
s2=np.repeat(shp,4)
for k in (10,13):
    sel=t2[s2==k]; used=np.unique(sel); remap=-np.ones(len(v2),int); remap[used]=np.arange(len(used))
    vk=v2[used]; tk=remap[sel]; assert len(vk)<65535
    nb[k]=store(L[k][0],vk,tk); print('shape',k,len(L[k][1]),len(L[k][2]),'->',len(vk),len(tk))
out=rebuild(d,h,nb); open(OUT,'wb').write(out); print('written',OUT,len(out))
