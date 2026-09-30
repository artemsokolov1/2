import json, sys, numpy as np
d=json.load(open(sys.argv[1]))
def seg(p,a,b):
    a,b,p=map(np.array,(a,b,p)); ab=b-a; t=np.clip(np.dot(p-a,ab)/max(np.dot(ab,ab),1e-6),0,1); return np.linalg.norm(p-(a+ab*t))
mx={t:np.mean([p[0] for f in d if f['abs'] for p in f['p'] if p[2]==t]) for t in 'WD'}
att={'W':1 if mx['W']<mx['D'] else -1}; att['D']=-att['W']
poss=[]
for f in d:
    c=None
    if f['b'] and f['p']:
        b=np.array(f['b'][0]); best=min(f['p'],key=lambda p:np.hypot(p[0]-b[0],p[1]-b[1]))
        if np.hypot(best[0]-b[0],best[1]-b[1])<2.5: c=best
    poss.append(c)
R=[]
for f,c in zip(d,poss):
    if c is None or not f['abs']: continue
    team=c[2]; mates=[p for p in f['p'] if p[2]==team and p is not c]; opp=[p for p in f['p'] if p[2]!=team]
    o=a=0
    for m in mates:
        D=np.hypot(m[0]-c[0],m[1]-c[1])
        if 5<=D<=22 and min([seg(q[:2],c[:2],m[:2]) for q in opp] or [99])>=1.5:
            o+=1; a+= (m[0]-c[0])*att[team]>2
    R.append((o,a,min([np.hypot(q[0]-c[0],q[1]-c[1]) for q in opp] or [99]),min([np.hypot(m[0]-c[0],m[1]-c[1]) for m in mates] or [99])))
R=np.array(R)
print('possession frames',len(R))
print('open %.2f | none %d%% | 2+ %d%% | ahead>=1 %d%% | press median %.1f m, <3 m %d%% | nearest mate %.1f m'%(R[:,0].mean(),100*(R[:,0]==0).mean(),100*(R[:,0]>=2).mean(),100*(R[:,1]>=1).mean(),np.median(R[:,2]),100*(R[:,2]<3).mean(),np.median(R[:,3])))
# touches: runs of the same carrier (by position continuity) inside a segment
events=[]; cur=None
for f,c in zip(d,poss):
    if c is None: continue
    if cur and cur['seg']==f['seg'] and cur['team']==c[2] and np.hypot(cur['last'][0]-c[0],cur['last'][1]-c[1])<3.0 and f['t']-cur['t1']<0.8:
        cur['t1']=f['t']; cur['last']=c[:2]
    else:
        if cur: events.append(cur)
        cur=dict(seg=f['seg'],team=c[2],t0=f['t'],t1=f['t'],first=c[:2],last=c[:2])
if cur: events.append(cur)
hold=[e['t1']-e['t0'] for e in events if e['t1']>e['t0']]
passes=[];lost=0
for e1,e2 in zip(events,events[1:]):
    if e1['seg']!=e2['seg'] or e2['t0']-e1['t1']>3: continue
    dist=np.hypot(e2['first'][0]-e1['last'][0],e2['first'][1]-e1['last'][1])
    if dist<5: continue
    if e1['team']==e2['team']: passes.append((dist,e2['t0']-e1['t1']))
    else: lost+=1
P=np.array(passes)
print('time on the ball per touch-run: median %.1f s, p75 %.1f s'%(np.median(hold),np.percentile(hold,75)))
if len(P): print('passes seen %d, completed %d%%, length median %.1f m (p25 %.1f, p75 %.1f), flight %.2f s'%(len(P)+lost,100*len(P)//(len(P)+lost),np.median(P[:,0]),np.percentile(P[:,0],25),np.percentile(P[:,0],75),np.median(P[:,1])))
sp=[]
for a,b in zip(d,d[1:]):
    if a['seg']!=b['seg']: continue
    dt=b['t']-a['t']
    for p in b['p']:
        c=[q for q in a['p'] if q[2]==p[2]]
        if c:
            q=min(c,key=lambda q:np.hypot(q[0]-p[0],q[1]-p[1])); D=np.hypot(q[0]-p[0],q[1]-p[1])
            if D<1.2: sp.append(D/dt)
print('player speed m/s: median %.1f p75 %.1f p90 %.1f p97 %.1f'%tuple(np.percentile(sp,[50,75,90,97])))
