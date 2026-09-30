import cv2, numpy as np
def detect(im):
    H,W=im.shape[:2]
    hsv=cv2.cvtColor(im,cv2.COLOR_BGR2HSV)
    h,s,v=hsv[...,0].astype(int),hsv[...,1].astype(int),hsv[...,2].astype(int)
    grass=(h>=30)&(h<=90)&(s>60)
    notg=(~grass).astype(np.uint8)*255
    notg[:int(H*0.075)]=0            # top HUD bar
    notg[int(H*0.86):]=0             # bottom cards
    body=cv2.morphologyEx(notg,cv2.MORPH_OPEN,cv2.getStructuringElement(cv2.MORPH_RECT,(9,13)))
    n,lab,st,cen=cv2.connectedComponentsWithStats(body)
    players=[]
    for i in range(1,n):
        x,y,w,hh,a=st[i]
        if a<120 or a>6000 or hh<22 or w>hh*1.3: continue
        m=lab[y:y+hh,x:x+w]==i
        pv=v[y:y+hh,x:x+w][m]; ps=s[y:y+hh,x:x+w][m]
        if w<14 or np.median(pv)>192: continue      # a thin, pure white line piece, not a player
        team='W' if np.median(pv)>150 and np.median(ps)<70 else 'D'
        players.append((x+w/2, y+hh, hh, team))
    # ball: small bright round blob away from lines
    white=((v>200)&(s<50)).astype(np.uint8)*255
    white[:int(H*0.075)]=0; white[int(H*0.86):]=0
    n2,lab2,st2,_=cv2.connectedComponentsWithStats(white)
    balls=[]
    for i in range(1,n2):
        x,y,w,hh,a=st2[i]
        if 12<=a<=160 and 0.6<w/max(hh,1)<1.6 and w<=16 and a>0.45*w*hh:
            balls.append((x+w/2,y+hh/2))
    # dashed lines come as chains of ball-like blobs: keep only isolated ones
    balls=[b for b in balls if sum(1 for c in balls if 0<(c[0]-b[0])**2+(c[1]-b[1])**2<45**2)==0]
    return players,balls
if __name__=='__main__':
    im=cv2.imread('t1.png'); p,b=detect(im)
    vis=im.copy()
    for x,y,hh,t in p: cv2.rectangle(vis,(int(x-10),int(y-hh)),(int(x+10),int(y)),(0,0,255) if t=='D' else (255,0,0),3)
    for x,y in b: cv2.circle(vis,(int(x),int(y)),10,(0,255,255),3)
    cv2.imwrite('t1_det.jpg',cv2.resize(vis,(1600,900)))
    print(len(p),'players',[ (int(x),int(y),int(hh),t) for x,y,hh,t in p]); print(len(b),'balls',b[:8])
