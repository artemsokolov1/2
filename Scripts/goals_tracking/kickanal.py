import sys, re, math
rows=[]; kicks=[]
for line in open(sys.argv[1], encoding='utf-8', errors='ignore'):
    if 'MFTRACE' in line:
        f=line.split('MFTRACE ')[1].split()
        t=float(f[0]); vx,vy=float(f[f.index('V')+1]),float(f[f.index('V')+2]); own=int(f[-1])
        rows.append((t,math.hypot(vx,vy),own))
    elif 'MFKICK' in line:
        f=line.split('MFKICK ')[1].split(); kicks.append((float(f[0]),f[1],float(f[3])))
out=[]
for T,kind,v0 in kicks:
    win=[r for r in rows if T<=r[0]<=T+0.7]
    if not win: continue
    vmin=min(r[1] for r in win)
    rel=[r[0] for r in win if r[2]==0]
    delay=(rel[0]-T) if rel else float('nan')
    out.append((kind,v0,vmin,delay))
for kind in ('pass','shot'):
    k=[o for o in out if o[0]==kind]
    if k: print(f"{kind}: n={len(k)} speed at press {sum(o[1] for o in k)/len(k):.0f} -> min {sum(o[2] for o in k)/len(k):.0f} (dip {100*(1-sum(o[2]/max(o[1],1) for o in k)/len(k)):.0f}%), ball leaves after {sum(o[3] for o in k if o[3]==o[3])/max(1,len([o for o in k if o[3]==o[3]])):.2f}s")
