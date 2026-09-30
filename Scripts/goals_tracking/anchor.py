import cv2, numpy as np, json
from cam import *
d=json.load(open('track19.json'))
cap=cv2.VideoCapture(r'F:/MELLSTROY/0702(19).mp4'); fps=cap.get(cv2.CAP_PROP_FPS); step=int(round(fps/10))
anch={}; i=k=0
while True:
    ok,im=cap.read()
    if not ok: break
    if i%step: i+=1; continue
    i+=1
    td=topdown(im,res=0.1,x0=-25,x1=25,y0=30,y1=60)
    hsv=cv2.cvtColor(td,cv2.COLOR_BGR2HSV)
    wht=((hsv[...,2]>190)&(hsv[...,1]<60)).astype(np.float32)
    col=wht.mean(0)            # fraction of rows white per column (0.1 m)
    c=np.maximum(col, np.maximum(np.roll(col,1), np.roll(col,-1)))
    j=int(np.argmax(c))
    if c[j]>0.55:               # a line running most of the view's depth: the halfway line
        anch[k]=-25+j*0.1+0.05
    k+=1
idx=sorted(anch); print(len(idx),'anchor frames of',len(d))
# offset = where the halfway line should be (0) minus where the camera track puts it
off={f: -(d[f]['cam'][0]+anch[f]) for f in idx}
fr=np.arange(len(d)); corr=np.interp(fr, idx, [off[f] for f in idx])
for f in fr:
    for p in d[f]['p']: p[0]+=corr[f]
    for b in d[f]['b']: b[0]+=corr[f]
    d[f]['cam'][0]+=corr[f]
json.dump(d,open('track19a.json','w'))
print('correction range %.1f..%.1f m'%(corr.min(),corr.max()))
for f in fr[::40]: print(d[f]['t'],'cam %.1f'%d[f]['cam'][0])
