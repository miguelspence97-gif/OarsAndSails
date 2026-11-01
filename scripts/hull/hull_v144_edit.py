# RowRim hull v1.4.4: the three main thwarts (y -99, 0, +99) moved PLANK_DY toward the bow (centred on the rowing oarlock);
# the fore and aft oarlock pairs replaced by copies of the middle pair (its taller fork, its seat on the gunwale), each centred
# on its moved thwart, turned to follow the gunwale (yaw only, as the vanilla pairs were).
from hull_v144_lib import *
import sys
SRC,OUT=sys.argv[1],sys.argv[2]
PLANK_DY=float(sys.argv[3]) if len(sys.argv)>3 else 6.5
d=open(SRC,'rb').read(); h=parse(d)
GY=np.array([-194,-147,-98,-49,0,49,98,147,194.0])
GZ=np.array([73.5,69.9,68.1,67.0,66.6,67.0,68.1,69.9,73.5])
GX=np.array([63.5,80.1,87.0,92.6,94.2,92.6,87.0,80.1,63.5])
gz=lambda y:float(np.interp(y,GY,GZ)); gx=lambda y:float(np.interp(y,GY,GX))
def slope(y,e=2.0): return (gx(y+e)-gx(y-e))/(2*e)
log=[]; nb={}
# ---- thwarts (shape 13)
b=h['blocks'][13]; s,vb,T=load(d[b['start']:b['end']]); P=pos(vb)
planks={}
for c in components(P,T):
    Q=P[c]; mn,mx=Q.min(0),Q.max(0)
    if len(c) in (48,52) and mx[0]-mn[0]>150 and mx[2]-mn[2]<6:
        yc=float((mn[1]+mx[1])/2)
        if abs(yc)<120:
            dz=gz(yc+PLANK_DY)-gz(yc)
            P[c,1]+=PLANK_DY; P[c,2]+=dz
            planks[round(yc)]=yc+PLANK_DY
            log.append('thwart y %.1f -> %.1f (dz %+.2f)'%(yc,yc+PLANK_DY,dz))
assert len(planks)==3, planks
setpos(vb,P); nb[13]=store(s,vb,T)
# ---- oarlocks (shape 7)
b=h['blocks'][7]; s,vb,T=load(d[b['start']:b['end']]); P=pos(vb)
parts={}
for c in components(P,T):
    if len(c) not in (20,64): continue
    side=1 if P[c][0,0]>0 else -1
    parts[(side,round(P[c][:,1].mean()),len(c))]=c
ymid=6.7
def rotz(a): ca,sa=np.cos(a),np.sin(a); return np.array([[ca,-sa,0],[sa,ca,0],[0,0,1.0]])
def dec8(x): return x.astype(np.float32)/127.5-1
def enc8(x): return np.clip(np.round((x+1)*127.5),0,255).astype(np.uint8)
for side in (1,-1):
    padM=parts[(side,7,20)]; forkM=parts[(side,7,64)]
    cM=P[padM].mean(0)
    offx=abs(cM[0])-gx(ymid); offz=cM[2]-gz(ymid)
    yawM=-side*np.arctan(slope(ymid))
    for old,ty in ((-127,planks[round(-99.4)] if round(-99.4) in planks else None),(72,None)):
        pass
    targets={-127:[v for k,v in planks.items() if k<-50][0], 72:[v for k,v in planks.items() if k>50][0]}
    for old,ty in targets.items():
        yaw=-side*np.arctan(slope(ty))
        Rm=rotz(yaw-yawM)
        ct=np.array([side*(gx(ty)+offx), ty, gz(ty)+offz])
        for src,dst in ((padM,parts[(side,old,20)]),(forkM,parts[(side,old,64)])):
            assert len(src)==len(dst)
            P[dst]=(P[src]-cM)@Rm.T+ct
            V=vb[src].copy()
            n=dec8(V[:,20:23]); t=dec8(V[:,24:27])
            bx=V[:,12:16].copy().view('<f4').reshape(-1); by=dec8(V[:,23]); bz=dec8(V[:,27])
            B=np.stack([bx,by,bz],1)
            n=n@Rm.T; t=t@Rm.T; B=B@Rm.T
            V[:,20:23]=enc8(n); V[:,24:27]=enc8(t)
            V[:,12:16]=np.ascontiguousarray(B[:,0].astype('<f4')).view(np.uint8).reshape(-1,4); V[:,23]=enc8(B[:,1]); V[:,27]=enc8(B[:,2])
            V[:,0:12]=np.ascontiguousarray(P[dst].astype('<f4')).view(np.uint8).reshape(-1,12)
            vb[dst]=V
        log.append('oarlock side %+d y %d -> %.1f (yaw %.1f deg, x %.1f z %.1f)'%(side,old,ty,np.degrees(yaw),ct[0],ct[2]))
setpos(vb,P); nb[7]=store(s,vb,T)
out=rebuild(d,h,nb)
open(OUT,'wb').write(out)
print('\n'.join(log)); print('written',OUT,len(out))
