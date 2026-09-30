import sys, math, statistics as st
rows=[];ev=[]
for line in open(sys.argv[1],encoding='utf-8',errors='ignore'):
    if 'MFTRACE' in line:
        f=line.split('MFTRACE ')[1].split()
        g=lambda k,n=2:[float(x) for x in f[f.index(k)+1:f.index(k)+1+n]]
        rows.append(dict(t=float(f[0]),V=g('V'),H=g('H',3),B=g('B',3),own=int(f[-1])))
    if 'MFRECV' in line:
        f=line.split('MFRECV ')[1].split(); ev.append(float(f[0]))
got=[];ratio=[];dist=[];miss=0
for T in ev:
    w=[r for r in rows if T<r['t']<T+2.5]
    g=next((r for r in w if r['own']),None)
    if not g: miss+=1; continue
    got.append(g['t']-T)
    v0=math.hypot(*g['V'])
    later=[r for r in w if r['t']>=g['t']+0.3]
    if later:
        r=later[0]; ratio.append(min(math.hypot(*r['V'])/max(v0,1),1.5))
        dist.append(math.hypot(r['B'][0]-r['H'][0],r['B'][1]-r['H'][1]))
print(f"passes {len(ev)}, controlled {len(got)}, missed {miss}; under control after {st.mean(got):.2f}s; speed 0.3s after control {100*st.median(ratio):.0f}% ; ball from hips then {st.mean(dist):.0f} cm")
