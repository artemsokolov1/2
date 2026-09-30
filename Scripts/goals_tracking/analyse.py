import json, numpy as np
d=json.load(open('track19a.json'))
def seg(p,a,b):
    a,b,p=map(np.array,(a,b,p)); ab=b-a; t=np.clip(np.dot(p-a,ab)/max(np.dot(ab,ab),1e-6),0,1); return np.linalg.norm(p-(a+ab*t))
# possession: nearest player within 1.5 m of the ball
poss=[]
for f in d:
    if not f['b'] or not f['p']: poss.append(None); continue
    b=np.array(f['b'][0]); best=min(f['p'],key=lambda p:np.hypot(p[0]-b[0],p[1]-b[1]))
    poss.append(best if np.hypot(best[0]-b[0],best[1]-b[1])<2.5 else None)
# which way each team attacks: team mean X over the clip vs the other (the team deeper on +X defends +X)
mx={t:np.mean([p[0] for f in d for p in f['p'] if p[2]==t]) for t in 'WD'}
rows=[]
for f,c in zip(d,poss):
    if c is None: continue
    team=c[2]; mates=[p for p in f['p'] if p[2]==team and p is not c]; opp=[p for p in f['p'] if p[2]!=team]
    att=1 if mx[team]<mx['D' if team=='W' else 'W'] else -1   # attacks toward the other team's side
    opens=0; ahead=0; dists=[]
    for m in mates:
        D=np.hypot(m[0]-c[0],m[1]-c[1]); dists.append(D)
        if not (5<=D<=22): continue
        lane=min([seg(o[:2],c[:2],m[:2]) for o in opp] or [99])
        if lane<1.5: continue
        opens+=1
        if (m[0]-c[0])*att>2: ahead+=1
    press=min([np.hypot(o[0]-c[0],o[1]-c[1]) for o in opp] or [99])
    pts=np.array([p[:2] for p in [c]+mates])
    rows.append(dict(team=team,open=opens,ahead=ahead,press=press,near=min(dists or [99]),
                     width=np.ptp(pts[:,1]) if len(pts)>1 else 0, depth=np.ptp(pts[:,0]) if len(pts)>1 else 0, n=len(pts)))
r=rows; n=len(r)
print('possession frames',n,'of',len(d))
print('open options avg %.2f | none %d%% | 2+ %d%% | ahead avg %.2f | ahead>=1 %d%%'%(np.mean([x['open'] for x in r]),100*np.mean([x['open']==0 for x in r]),100*np.mean([x['open']>=2 for x in r]),np.mean([x['ahead'] for x in r]),100*np.mean([x['ahead']>=1 for x in r])))
print('nearest team-mate to carrier: median %.1f m'%np.median([x['near'] for x in r]))
print('nearest opponent (pressure): median %.1f m, <3m %d%%'%(np.median([x['press'] for x in r]),100*np.mean([x['press']<3 for x in r])))
full=[x for x in r if x['n']>=4]
print('team shape (4+ visible): width median %.1f m, depth median %.1f m'%(np.median([x['width'] for x in full]),np.median([x['depth'] for x in full])))
# speeds: link detections frame to frame per team (nearest within 1.2 m)
sp=[]
for a,b in zip(d,d[1:]):
    dt=b['t']-a['t']
    for p in b['p']:
        c=[q for q in a['p'] if q[2]==p[2]]
        if not c: continue
        q=min(c,key=lambda q:np.hypot(q[0]-p[0],q[1]-p[1])); D=np.hypot(q[0]-p[0],q[1]-p[1])
        if D<1.2: sp.append(D/dt)
sp=np.array(sp); print('player speed m/s: median %.1f, p75 %.1f, p90 %.1f, p97 %.1f'%tuple(np.percentile(sp,[50,75,90,97])))
# ball speed when loose
bs=[]
for a,b,pa in zip(d,d[1:],poss):
    if a['b'] and b['b'] and pa is None:
        D=np.hypot(b['b'][0][0]-a['b'][0][0],b['b'][0][1]-a['b'][0][1]); v=D/(b['t']-a['t'])
        if 3<v<40: bs.append(v)
print('loose ball speed m/s: median %.1f, p90 %.1f (n=%d)'%(np.median(bs),np.percentile(bs,90),len(bs)))
