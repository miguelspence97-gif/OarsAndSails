import numpy as np
from nifp import *
def decode(d,s):
    vb=np.frombuffer(d,dtype=np.uint8,count=s['nv']*32,offset=s['data_off']).reshape(s['nv'],32)
    pos=vb[:,0:12].copy().view('<f4').reshape(-1,3)
    uv=vb[:,16:20].copy().view('<f2').reshape(-1,2).astype(np.float32)
    nrm=vb[:,20:23].astype(np.float32)/127.5-1
    tan=vb[:,24:27].astype(np.float32)/127.5-1
    col=vb[:,28:32]
    tris=np.frombuffer(d,dtype='<u2',count=s['ntri']*3,offset=s['data_off']+s['nv']*32).reshape(-1,3)
    return dict(vb=vb,pos=pos,uv=uv,nrm=nrm,tan=tan,col=col,tris=tris)
