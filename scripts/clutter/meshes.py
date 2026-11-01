import numpy as np
def frames(P):
    P=np.asarray(P,float); n=len(P)
    T=np.gradient(P,axis=0); T/=np.linalg.norm(T,axis=1,keepdims=True)
    a=np.array([0,0,1.]) if abs(T[0][2])<0.9 else np.array([1.,0,0])
    N=[np.cross(T[0],a)/np.linalg.norm(np.cross(T[0],a))]
    for i in range(1,n):
        v=N[-1]-T[i]*np.dot(N[-1],T[i]); N.append(v/np.linalg.norm(v))
    N=np.array(N); B=np.cross(T,N); return T,N,B
def tube(P,R,m=10,vscale=20.0,closed=False,caps=False,ustretch=1.0):
    P=np.asarray(P,float); n=len(P); R=np.broadcast_to(np.asarray(R,float),(n,))
    T,N,B=frames(P)
    L=np.concatenate([[0],np.cumsum(np.linalg.norm(np.diff(P,axis=0),axis=1))])
    V=[];Nn=[];Tg=[];UV=[];F=[]
    for i in range(n):
        for j in range(m+1):
            a=2*np.pi*j/m; d=N[i]*np.cos(a)+B[i]*np.sin(a)
            V.append(P[i]+d*R[i]); Nn.append(d); Tg.append(T[i]); UV.append((j/m*ustretch, L[i]/vscale))
    for i in range(n-1):
        for j in range(m):
            a=i*(m+1)+j; b=a+1; c=a+m+1; d=c+1
            F+= [(a,c,b),(b,c,d)]
    if closed:
        for j in range(m):
            a=(n-1)*(m+1)+j; b=a+1; c=j; d=j+1; F+=[(a,c,b),(b,c,d)]
    if caps:
        for end,sg in ((0,-1),(n-1,1)):
            ci=len(V); V.append(P[end]); Nn.append(T[end]*sg); Tg.append(N[end]); UV.append((0.5,0.5))
            for j in range(m+1):
                a=2*np.pi*j/m; d=N[end]*np.cos(a)+B[end]*np.sin(a)
                V.append(P[end]+d*R[end]); Nn.append(T[end]*sg); Tg.append(N[end]); UV.append((0.5+0.5*np.cos(a),0.5+0.5*np.sin(a)))
            for j in range(m):
                f=(ci,ci+1+j,ci+2+j); F.append(f if sg>0 else (f[0],f[2],f[1]))
    return [np.array(V),np.array(Nn),np.array(Tg),np.array(UV),np.array(F)]
def merge(parts):
    V=[];N=[];T=[];U=[];F=[];o=0
    for p in parts:
        V.append(p[0]);N.append(p[1]);T.append(p[2]);U.append(p[3]);F.append(p[4]+o);o+=len(p[0])
    return [np.concatenate(V),np.concatenate(N),np.concatenate(T),np.concatenate(U),np.concatenate(F)]
def xform(p,R,t,s=1.0):
    return [p[0]@R.T*s+t,p[1]@R.T,p[2]@R.T,p[3],p[4]]
def ellipsoid(c,r,m=12,k=8):
    V=[];N=[];T=[];U=[];F=[]
    for i in range(k+1):
        th=np.pi*i/k
        for j in range(m+1):
            ph=2*np.pi*j/m
            n=np.array([np.sin(th)*np.cos(ph),np.sin(th)*np.sin(ph),np.cos(th)])
            V.append(np.asarray(c)+n*r); nn=n/np.asarray(r); N.append(nn/np.linalg.norm(nn))
            T.append(np.array([-np.sin(ph),np.cos(ph),0.0])+1e-6); U.append((j/m,i/k))
    for i in range(k):
        for j in range(m):
            a=i*(m+1)+j;b=a+1;cc=a+m+1;d=cc+1;F+=[(a,cc,b),(b,cc,d)]
    T=np.array(T); T/=np.linalg.norm(T,axis=1,keepdims=True)
    return [np.array(V),np.array(N),T,np.array(U),np.array(F)]
def orient(p):
    V,N,T,U,F=p; F=F.copy()
    fn=np.cross(V[F[:,1]]-V[F[:,0]],V[F[:,2]]-V[F[:,0]])
    vn=N[F[:,0]]+N[F[:,1]]+N[F[:,2]]
    bad=(fn*vn).sum(1)<0
    F[bad]=F[bad][:,[0,2,1]]
    return [V,N,T,U,F]
