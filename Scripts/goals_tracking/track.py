import cv2, numpy as np, json, sys
from cam import *
from detect import detect
cap=cv2.VideoCapture(r'F:/MELLSTROY/0702(19).mp4'); fps=cap.get(cv2.CAP_PROP_FPS)
step=int(round(fps/10)); out=[]; prev=None; cam=np.zeros(2); i=0
win=None
while True:
    ok,im=cap.read()
    if not ok: break
    if i%step: i+=1; continue
    t=i/fps; i+=1
    td=cv2.cvtColor(topdown(im,res=0.1,x0=-25,x1=25,y0=28,y1=60),cv2.COLOR_BGR2GRAY).astype(np.float32)
    valid=(td>5).astype(np.float32)
    if win is None: win=cv2.createHanningWindow(td.shape[::-1],cv2.CV_32F)
    if prev is not None:
        (dx,dy),r=cv2.phaseCorrelate(prev*win,td*win)
        # image x -> ground X; image rows run toward -Y
        cam+=np.array([-dx*0.1, dy*0.1])
    prev=td
    p,b=detect(im)
    P=[]
    for x,y,hh,team in p:
        g=ground(x,y,HCAM); P.append([float(g[0]+cam[0]),float(g[1]+cam[1]),team])
    bs=[ (bx,by) for bx,by in b if not any(abs(bx-x)<25 and y-hh-10<by<y+5 for x,y,hh,tm in p)]
    B=[[float(v) for v in (ground(bx,by+6,HCAM)+cam)] for bx,by in bs]
    out.append(dict(t=round(t,2),cam=[float(cam[0]),float(cam[1])],p=P,b=B))
json.dump(out,open('track19.json','w'))
print(len(out),'frames; last cam',cam, 'players/frame %.1f'%np.mean([len(f['p']) for f in out]), 'ball found %d%%'%(100*np.mean([len(f['b'])>0 for f in out])))
