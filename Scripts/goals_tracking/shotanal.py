import sys, collections
R=collections.defaultdict(collections.Counter); tot=collections.Counter()
names={0:'centre 11m',1:'centre 16m',2:'angle L',3:'angle R',4:'wing L',5:'wing R'}
for l in open(sys.argv[1],encoding='utf-8',errors='ignore'):
    if 'MFSHOT' in l:
        f=l.split('MFSHOT ')[1].split(); sp=int(f[1]); aim=int(f[3]); r=f[5]
        R[sp][r]+=1; tot[r]+=1
n=sum(tot.values())
print(f"n={n} goals {tot['goal']} ({100*tot['goal']//max(n,1)}%), saves {tot['save']}, misses {tot['miss']}")
for sp in sorted(R): print(f"   {names[sp]}: goal {R[sp]['goal']} save {R[sp]['save']} miss {R[sp]['miss']}")
