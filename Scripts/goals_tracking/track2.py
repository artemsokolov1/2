# Track players/ball through a long Goals recording at any resolution. Frames whose view
# is not the standard side camera (turned near the goals, corner view, menus) are skipped;
# the camera track restarts after each gap and is pinned to the halfway line when visible.
import cv2, numpy as np, json, sys
import cam as C
src, out_path = sys.argv[1], sys.argv[2]
cap=cv2.VideoCapture(src); fps=cap.get(cv2.CAP_PROP_FPS)
W=int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H=int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)); S=W/2560
C.W,C.H,C.CX,C.CY,C.F=W,H,W/2,H/2,3184.0*S
from detect import detect as detect_full
def detect(im):
    if S==1: return detect_full(im)
    big=cv2.resize(im,(2560,int(H/S)),interpolation=cv2.INTER_CUBIC)   # upscale: the detector is tuned at 2560
    p,b=detect_full(big)
    return [(x*S,y*S,hh*S,t) for x,y,hh,t in p],[(x*S,y*S) for x,y in b]
step=int(round(fps/10)); res=0.1
frames=[]; prev=None; camx=camy=0.0; seg=0; i=0; win=None
while True:
    ok,im=cap.read()
    if not ok: break
    if i%step: i+=1; continue
    t=i/fps; i+=1
    td=C.topdown(im,res=res,x0=-25,x1=25,y0=28,y1=60)
    hsv=cv2.cvtColor(td,cv2.COLOR_BGR2HSV)
    grass=((hsv[...,0]>=30)&(hsv[...,0]<=90)&(hsv[...,1]>60)).mean()
    wht=((hsv[...,2]>190)&(hsv[...,1]<60)).astype(np.uint8)*255
    lines=cv2.HoughLinesP(wht,1,np.pi/360,80,minLineLength=120,maxLineGap=15)
    ok_view = grass>0.55
    if ok_view and lines is not None:
        ang=[abs(np.degrees(np.arctan2(y2-y1,x2-x1)))%180 for x1,y1,x2,y2 in lines[:,0]]
        off=[min(a%90, 90-a%90) for a in ang]
        ok_view = np.median(off) < 3.0          # lines stay axis-aligned only in the standard view
    if not ok_view:
        prev=None; continue
    g=cv2.cvtColor(td,cv2.COLOR_BGR2GRAY).astype(np.float32)
    if win is None: win=cv2.createHanningWindow(g.shape[::-1],cv2.CV_32F)
    if prev is None:
        seg+=1; camx=camy=0.0
    else:
        (dx,dy),r=cv2.phaseCorrelate(prev*win,g*win)
        camx+=-dx*res; camy+=dy*res
    prev=g
    col=(wht>0).mean(0); c=np.maximum(col,np.maximum(np.roll(col,1),np.roll(col,-1))); j=int(np.argmax(c))
    halfway = float(-25+j*res+res/2) if c[j]>0.55 else None
    p,b=detect(im)
    P=[[float(v) for v in C.ground(x,y,C.HCAM)]+[tm] for x,y,hh,tm in p]
    bs=[(bx,by) for bx,by in b if not any(abs(bx-x)<25*S and y-hh-10*S<by<y+5*S for x,y,hh,tm in p)]
    B=[[float(v) for v in C.ground(bx,by+6*S,C.HCAM)] for bx,by in bs]
    frames.append(dict(t=round(t,2),seg=seg,cam=[camx,camy],hw=halfway,p=P,b=B))
# per segment: local -> world. X pinned to the halfway line (median over anchors), Y relative.
out=[]
for s in sorted(set(f['seg'] for f in frames)):
    fs=[f for f in frames if f['seg']==s]
    anc=[-(f['cam'][0]+f['hw']) for f in fs if f['hw'] is not None]
    ox=float(np.median(anc)) if anc else None
    for f in fs:
        for q in f['p']+f['b']: q[0]+=f['cam'][0]+(ox or 0); q[1]+=f['cam'][1]
        f['abs']=ox is not None; out.append(f)
json.dump(out,open(out_path,'w'))
segs=len(set(f['seg'] for f in out))
print('frames kept',len(out),'of',int(i/step),'| segments',segs,'| with absolute X',sum(f['abs'] for f in out),
      '| players/frame %.1f'%np.mean([len(f['p']) for f in out]),'| ball %d%%'%(100*np.mean([len(f['b'])>0 for f in out])))
