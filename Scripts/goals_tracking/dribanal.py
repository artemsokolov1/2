import sys, math, statistics as st
rows=[]
for line in open(sys.argv[1],encoding='utf-8',errors='ignore'):
    if 'MFTRACE' not in line: continue
    f=line.split('MFTRACE ')[1].split()
    g=lambda k,n=2:[float(x) for x in f[f.index(k)+1:f.index(k)+1+n]]
    rows.append(dict(t=float(f[0]),dt=float(f[1]),C=g('C'),V=g('V'),H=g('H',3),B=g('B',3),own=int(f[-1])))
ahead=[];lat=[];lost=0;prev=1
for r in rows:
    v=math.hypot(*r['V'])
    if r['own']==0 and prev==1: lost+=1
    prev=r['own']
    if r['own'] and v>250:
        d=(r['V'][0]/v,r['V'][1]/v); bx=r['B'][0]-r['H'][0]; by=r['B'][1]-r['H'][1]
        ahead.append(bx*d[0]+by*d[1]); lat.append(-bx*d[1]+by*d[0])
q=sorted(ahead)
print(f"ball ahead of hips: mean {st.mean(ahead):.0f} cm, range {q[len(q)//20]:.0f}..{q[-len(q)//20]:.0f} (5-95%), lateral sd {st.pstdev(lat):.1f} cm, ball lost {lost-0} times")
