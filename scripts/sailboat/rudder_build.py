import sys; sys.path.insert(0,'.')
from rudder_geo import *
def scroll_knob(end, fwd, side, up, r=2.3):
    """a carved curl at the tiller's end: the bar rolls up and over in a spiral"""
    pts=[];rad=[]
    for i in range(26):
        a=i/25*1.6*np.pi; R=7.0*(1-0.55*i/25)
        pts.append(end+fwd*(R*np.sin(a))+up*(R*(1-np.cos(a)))*0.9 + side*0.0); rad.append(r*(1-0.5*i/25))
    return sweep(pts,rad,10)
OCT=lambda a: 1.0/np.cos(((a+np.pi/8)%(np.pi/4))-np.pi/8)**0.35   # a softly octagonal section
def option_a(turn=0.0):
    """stern-hung rudder on the sternpost, the tiller forward through the stern to the helmsman's right"""
    H0=np.array([0,-227.5,18.0]); H1=np.array([0,-272.5,84.0]); ax=(H1-H0)/np.linalg.norm(H1-H0)
    aft=np.cross(ax,[1,0,0.]); aft/=np.linalg.norm(aft)  # perpendicular, pointing aft
    if aft[1]>0: aft=-aft
    # blade outline in (along hinge, aft): rounded heel, tapering up
    L=np.linalg.norm(H1-H0); outl=[]
    for t in np.linspace(0,1,14): outl.append((t*L, 2.0))
    top=[(L,2.0),(L,9.0)]
    back=[(t*L, 9.0+15.0*(1-t)**1.3) for t in np.linspace(1,0.12,10)]
    heel=[(0.12*L*(1-np.sin(a)) , 9.0+15.0*(0.88)**1.3*np.cos(a)+2.0*np.sin(a)) for a in np.linspace(0,np.pi/2,6)]
    o=np.array(outl+top[1:]+back+heel[1:])
    blade=slab(o,ax,aft,np.array([1.0,0,0]),H0-ax*3.0,3.2,0.9)
    parts=[blade]
    stock=sweep([H1-ax*6,H1+ax*4,H1+ax*7],[2.6,2.6,2.4],12); parts.append(stock)
    for t in (0.2,0.55,0.88):  # iron pintle straps round the leading edge
        c=H0+ax*(t*L-3.0)+aft*4.0
        parts.append(slab(np.array([[-1.2,-5.5],[1.2,-5.5],[1.2,5.5],[-1.2,5.5]]),ax,aft,np.array([1.0,0,0]),c,3.9,0.2))
    head=H1+ax*5.5
    fwd=np.array([0,1.0,0]); 
    path=[head, head+np.array([0,10,1.0]), np.array([4,-240,88.5]), np.array([14,-224,90.5]), np.array([22,-212,92.0])]
    pts=[]; 
    for i in range(len(path)-1):
        for s in np.linspace(0,1,8,endpoint=False): pts.append(path[i]*(1-s)+path[i+1]*s)
    pts.append(path[-1]); pts=np.array(pts)
    rad=np.linspace(2.4,1.7,len(pts))
    parts.append(sweep(pts,rad,10,prof=OCT))
    parts.append(sweep([head+np.array([0,1,0]),head+np.array([0,7,0.6])],[2.9,2.9],12))   # iron band at the head
    d=pts[-1]-pts[-2]; d/=np.linalg.norm(d)
    parts.append(scroll_knob(pts[-1],d,np.cross(d,[0,0,1.]),np.array([0,0,1.0])))
    V,F=merge(parts)
    V=rot_axis(V,H0,ax,turn)
    iron=np.zeros(len(F),bool); n0=len(blade[1])+len(stock[1]); iron[n0:n0+3*len(parts[2][1])]=True
    return V,F,iron
def option_b(turn=0.0):
    """a quarter rudder flush on the starboard side by the helm, its tiller in over the gunwale to the helmsman's right"""
    x0=66.0; A0=np.array([x0+3.0,-236.0,22.0]); A1=np.array([x0+7.0,-238.0,92.0]); ax=(A1-A0)/np.linalg.norm(A1-A0)
    aft=np.array([0,-1.0,0]); aft-=ax*np.dot(aft,ax); aft/=np.linalg.norm(aft)
    L=70.0
    o=np.array([(0,-3),(L*0.62,-3),(L*0.62,6),(L*0.3,10),(0.0,14),(-3,10),(-3,-3)])
    nrm=np.cross(ax,aft)
    blade=slab(o,ax,aft,nrm,A0,3.0,0.9)
    stock=sweep([A0+ax*40,A1+ax*2],[2.6,2.4],12)
    head=A1; pts=np.array([head+np.array([0,0,0])+(np.array([22,-212,92.0])-head)*s for s in np.linspace(0,1,14)])
    pts[:,2]+=4*np.sin(np.linspace(0,np.pi,14))
    tiller=sweep(pts,np.linspace(2.4,1.7,14),10,prof=OCT)
    d=pts[-1]-pts[-2]; d/=np.linalg.norm(d)
    V,F=merge([blade,stock,tiller,scroll_knob(pts[-1],d,np.cross(d,[0,0,1.]),np.array([0,0,1.0]))])
    return rot_axis(V,A0,ax,turn),F,np.zeros(len(F),bool)
def rudder_c(turn=0.0):
    """v2: the rudder alone on the sternpost (option A's), its stock capped in iron at the head"""
    H0=np.array([0,-227.5,18.0]); H1=np.array([0,-272.5,84.0]); ax=(H1-H0)/np.linalg.norm(H1-H0)
    aft=np.cross(ax,[1,0,0.]); aft/=np.linalg.norm(aft)
    if aft[1]>0: aft=-aft
    L=np.linalg.norm(H1-H0)
    outl=[(t*L,2.0) for t in np.linspace(0,1,14)]
    back=[(t*L, 9.0+15.0*(1-t)**1.3) for t in np.linspace(1,0.12,10)]
    heel=[(0.12*L*(1-np.sin(a)), 9.0+15.0*(0.88)**1.3*np.cos(a)+2.0*np.sin(a)) for a in np.linspace(0,np.pi/2,6)]
    o=np.array(outl+[(L,9.0)]+back+heel[1:])
    wood=[slab(o,ax,aft,np.array([1.0,0,0]),H0-ax*3.0,3.2,0.9), sweep([H1-ax*6,H1+ax*6],[2.6,2.5],12)]
    iron=[slab(np.array([[-1.2,-5.5],[1.2,-5.5],[1.2,5.5],[-1.2,5.5]]),ax,aft,np.array([1.0,0,0]),H0+ax*(t*L-3.0)+aft*4.0,3.9,0.2) for t in (0.2,0.55,0.88)]
    iron.append(sweep([H1+ax*5.0,H1+ax*7.5],[3.0,2.2],12))
    Vw,Fw=merge(wood); Vi,Fi=merge(iron)
    V,F=merge([(Vw,Fw),(Vi,Fi)]); isI=np.r_[np.zeros(len(Fw),bool),np.ones(len(Fi),bool)]
    return rot_axis(V,H0,ax,turn),F,isI
TILLER_POST=np.array([-27.0,-186.0])   # just forward of the bench, beside the helmsman's left knee   # through the floorboards on the helmsman's left
def tiller_c(turn=0.0, floor=48.0):
    """v2: an L-shaped tiller: a post up out of the floorboards on the helmsman's left, bending forward into a handle
    with a carved curl; it turns about the post"""
    x,y=TILLER_POST; top=floor+40.0; R=5.0
    pts=[np.array([x,y,floor-4.0]),np.array([x,y,top-R])]
    for a in np.linspace(0,np.pi/2,8)[1:]:   # the elbow
        pts.append(np.array([x, y-R*(1-np.cos(a)), top-R+R*np.sin(a)]))
    pts.append(np.array([x,y-R-20.0,top+0.5]))   # the handle back along his left side to his hand
    P=[]; 
    for i in range(len(pts)-1):
        n=max(2,int(np.linalg.norm(pts[i+1]-pts[i])/2.5))
        for s in np.linspace(0,1,n,endpoint=False): P.append(pts[i]*(1-s)+pts[i+1]*s)
    P.append(pts[-1]); P=np.array(P)
    rad=np.interp(np.arange(len(P)),[0,len(P)*0.55,len(P)-1],[2.6,2.4,1.8])
    wood=[sweep(P,rad,10,prof=OCT)]
    d=P[-1]-P[-2]; d/=np.linalg.norm(d); wood.append(scroll_knob(P[-1],d,np.cross(d,[0,0,1.]),np.array([0,0,1.0]),r=1.9))
    iron=[sweep([np.array([x,y,floor-0.2]),np.array([x,y,floor+1.0])],[5.2,4.6],16),   # the deck plate it turns in
          sweep([np.array([x,y,floor+1.0]),np.array([x,y,floor+4.0])],[3.1,3.0],12)]   # its collar
    Vw,Fw=merge(wood); Vi,Fi=merge(iron)
    V,F=merge([(Vw,Fw),(Vi,Fi)]); isI=np.r_[np.zeros(len(Fw),bool),np.ones(len(Fi),bool)]
    o=np.array([x,y,0.0]); return rot_axis(V,o,np.array([0,0,1.0]),-turn*0.8),F,isI
_S=None
def stern_y(z):
    global _S
    if _S is None: _S=shapes('skin_hull.nif')['SB_HullPlanks']
    t=ray(*_S,np.array([0.0,-400,z]),np.array([0,1.0,0])); return -400+t
def rudder_d(turn=0.0):
    """v3 (Miguel's sketch): a triangle hung on the sternpost: its head just under the stern box (bottom z 65.7), its
    forward edge along the post, its foot running aft along the bottom; rounded corners; three iron straps"""
    zs=np.linspace(17.0,61.0,12); fwd=[(stern_y(z)-1.6,z) for z in zs]            # forward edge, hugging the post
    head=(fwd[-1][0]-3.0,62.5)
    aftc=(-262.0,15.0)
    aft=[(head[0]+(aftc[0]-head[0])*s+1.5*np.sin(np.pi*s), head[1]+(aftc[1]-head[1])*s) for s in np.linspace(0,1,9)[1:-1]]
    corner=[(aftc[0]+3.0*(1-np.cos(a)), aftc[1]-3.0*np.sin(a)+3.0*np.sin(a)) for a in np.linspace(0,np.pi/2,4)]
    foot=[(-262.0+3.0+(fwd[0][0]+2.0-(-262.0+3.0))*s, 12.0+1.0*s) for s in np.linspace(0,1,8)]
    heel=[(fwd[0][0]+2.0*np.cos(a), 13.0+(fwd[0][1]-13.0)*np.sin(a)) for a in np.linspace(0,np.pi/2,4)]
    o=np.array(fwd+[head]+aft+[aftc]+foot+heel)
    o2=np.c_[o[:,0],o[:,1]]   # (y, z)
    ex=np.array([0,1.0,0]); ez=np.array([0,0,1.0])
    wood=[slab(o2,ex,ez,np.array([1.0,0,0]),np.zeros(3),3.2,0.9)]
    H0=np.array([0,fwd[0][0],fwd[0][1]]); H1=np.array([0,fwd[-1][0],fwd[-1][1]]); ax=(H1-H0)/np.linalg.norm(H1-H0)
    iron=[]
    for z in (22.0,40.0,56.0):   # straps round the forward edge, onto the post
        y=stern_y(z)-1.6
        iron.append(slab(np.array([[y+3.0,z-1.3],[y-8.0,z-1.3],[y-8.0,z+1.3],[y+3.0,z+1.3]]),ex,ez,np.array([1.0,0,0]),np.zeros(3),4.0,0.2))
    Vw,Fw=merge(wood); Vi,Fi=merge(iron)
    V,F=merge([(Vw,Fw),(Vi,Fi)]); isI=np.r_[np.zeros(len(Fw),bool),np.ones(len(Fi),bool)]
    global RUDDER_H0, RUDDER_AX; RUDDER_H0, RUDDER_AX = H0, ax
    return rot_axis(V,H0,ax,turn),F,isI
def tiller_d(turn=0.0, seat=70.8):
    """v3 (Miguel's sketch): out of the bench on the helmsman's other side, by the hull, an L: a short post up from an
    iron plate on the bench, bending into a handle reaching in toward his hand; it turns about the post"""
    base=np.array([44.0,-226.0,seat]); top=seat+17.0; R=4.0
    hand=np.array([20.0,-206.0,top+1.0]); d=hand-np.array([base[0],base[1],top]); d[2]=0; L=np.linalg.norm(d); d/=L
    pts=[base+np.array([0,0,-3.0]),np.array([base[0],base[1],top-R])]
    for a in np.linspace(0,np.pi/2,8)[1:]: pts.append(np.array([base[0],base[1],top-R])+d*R*(1-np.cos(a))+np.array([0,0,R*np.sin(a)]))
    pts.append(hand)
    P=[]
    for i in range(len(pts)-1):
        n=max(2,int(np.linalg.norm(pts[i+1]-pts[i])/2.5))
        for s in np.linspace(0,1,n,endpoint=False): P.append(pts[i]*(1-s)+pts[i+1]*s)
    P.append(pts[-1]); P=np.array(P)
    rad=np.interp(np.arange(len(P)),[0,len(P)*0.4,len(P)-1],[2.6,2.4,1.8])
    wood=[sweep(P,rad,10,prof=OCT)]
    dd=P[-1]-P[-2]; dd/=np.linalg.norm(dd); wood.append(scroll_knob(P[-1],dd,np.cross(dd,[0,0,1.]),np.array([0,0,1.0]),r=1.9))
    iron=[sweep([base+np.array([0,0,-0.2]),base+np.array([0,0,1.0])],[5.2,4.6],16),sweep([base+np.array([0,0,1.0]),base+np.array([0,0,4.0])],[3.1,3.0],12)]
    Vw,Fw=merge(wood); Vi,Fi=merge(iron)
    V,F=merge([(Vw,Fw),(Vi,Fi)]); isI=np.r_[np.zeros(len(Fw),bool),np.ones(len(Fi),bool)]
    return rot_axis(V,np.array([base[0],base[1],0]),np.array([0,0,1.0]),-turn*0.8),F,isI
SQ=lambda a: 1.0/max(abs(np.cos(a)),abs(np.sin(a)))**0.85   # a squared, softly chamfered section (Imperial joinery)
def tiller_e(turn=0.0, seat=71.2):
    """v4: Imperial: a squared, chamfered L in plain worked timber; iron bands at the elbow and the grip, a plain turned
    knob at the end, the post in an iron collar on its plate; no carving"""
    global TILLER_BASE
    base=np.array([44.0,-226.0,seat]); top=seat+17.0; R=3.2; TILLER_BASE=base
    hand=np.array([20.0,-206.0,top+0.8]); d=hand-np.array([base[0],base[1],top]); d[2]=0; d/=np.linalg.norm(d)
    pts=[base+np.array([0,0,-3.0]),np.array([base[0],base[1],top-R])]
    for a in np.linspace(0,np.pi/2,7)[1:]: pts.append(np.array([base[0],base[1],top-R])+d*R*(1-np.cos(a))+np.array([0,0,R*np.sin(a)]))
    pts.append(hand)
    P=[]
    for i in range(len(pts)-1):
        n=max(2,int(np.linalg.norm(pts[i+1]-pts[i])/2.5))
        for s in np.linspace(0,1,n,endpoint=False): P.append(pts[i]*(1-s)+pts[i+1]*s)
    P.append(pts[-1]); P=np.array(P)
    rad=np.interp(np.arange(len(P)),[0,len(P)*0.45,len(P)-1],[2.5,2.3,1.9])
    wood=[sweep(P,rad,16,prof=SQ,cap=False)]
    knob=[hand+d*s for s in (0,0.8,2.0,3.2,4.0,4.6)]
    wood.append(sweep(knob,[1.9,1.6,2.3,2.4,1.9,0.6],16))   # a plain turned end: a neck, a ball, a flat tip
    elbow=np.array([base[0],base[1],top-R])+d*R*0.3+np.array([0,0,R*0.7])
    iron=[sweep([base+np.array([0,0,-0.2]),base+np.array([0,0,0.9])],[5.0,4.8],16),
          sweep([base+np.array([0,0,0.9]),base+np.array([0,0,4.5])],[3.2,3.1],16),
          sweep([np.array([base[0],base[1],top-R-2.2]),np.array([base[0],base[1],top-R-0.6])],[2.62,2.62],16,prof=SQ),   # a band under the elbow
          sweep([hand-d*7.0,hand-d*5.6],[2.12,2.12],16,prof=SQ)]
    for k in range(4):   # four rivet heads on the plate
        a=np.pi/4+k*np.pi/2; iron.append(sweep([base+np.array([3.9*np.cos(a),3.9*np.sin(a),0.9]),base+np.array([3.9*np.cos(a),3.9*np.sin(a),1.5])],[0.6,0.4],6))
    Vw,Fw=merge(wood); Vi,Fi=merge(iron)
    V,F=merge([(Vw,Fw),(Vi,Fi)]); isI=np.r_[np.zeros(len(Fw),bool),np.ones(len(Fi),bool)]
    return rot_axis(V,np.array([base[0],base[1],0]),np.array([0,0,1.0]),-turn*0.8),F,isI
def tiller_f(turn=0.0, seat=71.1):
    """v5 (Miguel's picture): a black iron base plate on the bench, a round iron stem, and on it a flat wooden handle,
    round at the stem with an iron cap, tapering forward to a rounded tip; at rest the handle points at the bow.
    Returns (V, F, part) with part 0 the base, 1 the stem and cap (iron), 2 the handle (wood)."""
    global TILLER_BASE
    base=np.array([49.0,-226.0,seat]); TILLER_BASE=base; x,y=base[0],base[1]
    hz=seat+15.0
    parts=[]
    # the base: a low round plate with a bevelled rim
    ring=[(5.6,0.0),(5.6,0.8),(5.0,1.5),(2.6,1.7),(0.0,1.7)]
    pts=[np.array([x,y,seat-0.4+h]) for r,h in ring]; 
    def lathe(prof, cx, cy, z0, sides=24):
        V=[];F=[]
        for i,(r,h) in enumerate(prof):
            for j in range(sides):
                a=2*np.pi*j/sides; V.append([cx+r*np.cos(a),cy+r*np.sin(a),z0+h])
        for i in range(len(prof)-1):
            for j in range(sides):
                a=i*sides+j; b=i*sides+(j+1)%sides; F+=[[a,b,a+sides],[b,b+sides,a+sides]]
        return np.array(V,float),np.array(F)
    parts.append((lathe([(0.0,0.0),(5.6,0.0),(5.6,0.8),(5.0,1.5),(2.6,1.7),(0.0,1.7)],x,y,seat-0.4),0))
    # the stem, a slight collar at its foot and top
    parts.append((lathe([(0.0,1.6),(2.4,1.6),(2.4,2.6),(2.2,3.2),(2.2,hz-seat-0.6),(2.6,hz-seat-0.2),(2.6,hz-seat+0.1),(0.0,hz-seat+0.1)],x,y,seat),1))
    # the handle (v1.8.31c, as Miguel's drawing): a paddle: a broad rounded head over the stem (running on a little behind
    # it), easing into a long narrow grip, a small round end; a thick board, dipping slightly toward the end
    L=42.0; hw=lambda u: 4.4 if u<5.0 else (4.4+(1.5-4.4)*((u-5.0)/15.0)**0.9 if u<20.0 else 1.5-0.2*(u-20.0)/(L-21.4))
    us=np.r_[np.linspace(-4.6,20.0,26),np.linspace(20.0,L-1.4,10)[1:]]
    o=[]
    for a in np.linspace(np.pi/2,3*np.pi/2,11)[1:-1]: o.append((-4.6+4.4*np.cos(a)+4.4*0.0,4.4*np.sin(a)))  # the rounded back of the head
    o=[(-4.6+4.4*np.cos(a)+0.0,4.4*np.sin(a)) for a in np.linspace(np.pi/2,3*np.pi/2,11)]
    o+= [(u,-hw(u)) for u in us[1:]]
    tipw=hw(L-1.4); o+= [(L-1.4+tipw*np.cos(a),tipw*np.sin(a)) for a in np.linspace(-np.pi/2,np.pi/2,9)[1:-1]]
    o+= [(u,hw(u)) for u in us[::-1][:-1]]
    dip=np.radians(-4.0); fwd=np.array([0,np.cos(dip),np.sin(dip)]); side=np.array([1.0,0,0]); up=np.cross(side,fwd)
    parts.append((slab(np.array(o),fwd,side,-up,np.array([x,y,hz+1.6]),3.2,0.7),2))
    parts.append((lathe([(0.0,0.0),(2.6,0.0),(2.6,0.5),(2.0,1.1),(0.9,1.4),(0.0,1.45)],x,y,hz+3.15),1))   # the iron cap
    V=[];F=[];P=[];o0=0
    for (v,f),k in parts: V.append(v); F.append(f+o0); P.append(np.full(len(f),k)); o0+=len(v)
    V=np.vstack(V); F=np.vstack(F); P=np.concatenate(P)
    return rot_axis(V,np.array([x,y,0.0]),np.array([0,0,1.0]),turn*0.8),F,P
