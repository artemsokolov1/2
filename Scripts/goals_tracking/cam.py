import numpy as np
W,H=2560,1440; CX,CY=W/2,H/2
F=3184.0; P=np.radians(26.9)     # Goals match camera, solved from the centre circle (0702(15))
def ray(u,v):
    # camera looks along +Y, pitched down P; returns direction in world (x right, y forward, z up)
    d=np.array([(u-CX)/F, 1.0, -(v-CY)/F])
    c,s=np.cos(P),np.sin(P)
    return np.array([d[0], d[1]*c - (-d[2])*s*0 + d[2]*s*0 + 0, 0])  # placeholder
def ground(u,v,h):
    x=(u-CX)/F; y=(v-CY)/F
    c,s=np.cos(P),np.sin(P)
    # camera frame -> world: forward f=(0,c,-s), up=(0,s,c), right=(1,0,0)
    dirw=np.array([x, c - y*s*(-1)*0 , 0])
    fwd=np.array([0,c,-s]); up=np.array([0,s,c]); right=np.array([1,0,0])
    d=fwd - y*up + x*right
    t=h/(-d[2]); return np.array([d[0]*t, d[1]*t])
def height_of(u,vb,vt,h):
    # person standing at the ground point of (u,vb): height where the top ray passes over it
    g=ground(u,vb,h); c,s=np.cos(P),np.sin(P)
    x=(u-CX)/F; y=(vt-CY)/F
    fwd=np.array([0,c,-s]); up=np.array([0,s,c]); right=np.array([1,0,0])
    d=fwd - y*up + x*right
    t=g[1]/d[1]; return h + d[2]*t

HCAM=22.7   # camera height (m): the centre circle then measures 18.3 m across
def project(X,Y,h=HCAM):
    # ground point (m, camera-ground frame) -> pixel; vectorised
    c,s=np.cos(P),np.sin(P)
    rx,ry,rz=X, Y, -h
    zc = ry*c + (-rz)*s*(-1)*-1 if False else ry*c - rz*s   # along fwd=(0,c,-s): ry*c + rz*(-s)
    yc = ry*s + rz*c                                         # along up=(0,s,c)
    u = CX + F*rx/zc; v = CY - F*yc/zc
    return u,v
def topdown(im, res=0.05, x0=-20, x1=20, y0=25, y1=75):
    import cv2
    xs=np.arange(x0,x1,res); ys=np.arange(y1,y0,-res)
    X,Y=np.meshgrid(xs,ys); u,v=project(X,Y)
    return cv2.remap(im,u.astype(np.float32),v.astype(np.float32),cv2.INTER_LINEAR)
