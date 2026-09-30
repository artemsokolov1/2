import re, sys, math, statistics as st
log = sys.argv[1]
rows = []
for line in open(log, encoding="utf-8", errors="ignore"):
    if "MFTRACE" not in line: continue
    v = line.split("MFTRACE")[1].split()
    t, dt = float(v[0]), float(v[1])
    g = lambda k, n: [float(x) for x in v[v.index(k) + 1:v.index(k) + 1 + n]]
    rows.append(dict(t=t, dt=dt, C=g("C", 2), V=g("V", 2), H=g("H", 3), L=g("L", 3), R=g("R", 3), B=g("B", 3), own=int(v[v.index("own") + 1])))
print("frames", len(rows), "median dt %.1f ms" % (1000 * st.median(r["dt"] for r in rows)))
lat, fwd, pops, hipc = [], [], [], []
for i, r in enumerate(rows):
    vx, vy = r["V"]; sp = math.hypot(vx, vy)
    hx, hy = r["H"][0] - r["C"][0], r["H"][1] - r["C"][1]
    if i >= 2:
        a, b = rows[i - 2], rows[i - 1]
        rel = lambda q: (q["H"][0] - q["C"][0], q["H"][1] - q["C"][1], q["H"][2])
        ra, rb, rc = rel(a), rel(b), rel(r)
        pops.append((math.sqrt(sum((rc[k] - 2 * rb[k] + ra[k]) ** 2 for k in range(3))), r["t"], r["own"]))
    if sp < 300 or not r["own"]: continue
    ux, uy = vx / sp, vy / sp
    mx, my = (r["L"][0] + r["R"][0]) / 2, (r["L"][1] + r["R"][1]) / 2
    bx, by = r["B"][0] - mx, r["B"][1] - my
    lat.append(bx * -uy + by * ux); fwd.append(bx * ux + by * uy)
    hipc.append((hx * -uy + hy * ux, hx * ux + hy * uy))
if lat:
    print("ball vs feet midpoint while running with ball: lateral mean %+.1f sd %.1f | ahead mean %.1f min %.1f" % (st.mean(lat), st.pstdev(lat), st.mean(fwd), min(fwd)))
    print("hips vs capsule: lateral mean %+.1f, along %+.1f" % (st.mean(h[0] for h in hipc), st.mean(h[1] for h in hipc)))
big = [p for p in pops if p[0] > 6]
print("pose pops >6cm (2nd diff of pelvis rel capsule):", len(big), "of", len(pops), "| with ball:", sum(1 for p in big if p[2]), "without:", sum(1 for p in big if not p[2]))
for p in sorted(big, reverse=True)[:8]: print("   %.1f cm at t=%.2f own=%d" % p)
print("--- decomposition (running with ball) ---")
A = {"ball-hips": [], "feet-hips": [], "ball-capsule": []}
for r in rows:
    vx, vy = r["V"]; sp = math.hypot(vx, vy)
    if sp < 300 or not r["own"]: continue
    ux, uy = vx / sp, vy / sp
    def lf(p, q): 
        dx, dy = p[0] - q[0], p[1] - q[1]; return (dx * -uy + dy * ux, dx * ux + dy * uy)
    mid = ((r["L"][0] + r["R"][0]) / 2, (r["L"][1] + r["R"][1]) / 2)
    A["ball-hips"].append(lf(r["B"], r["H"])); A["feet-hips"].append(lf(mid, r["H"])); A["ball-capsule"].append(lf(r["B"], r["C"]))
for k, v in A.items():
    print("%-13s lateral %+6.1f  ahead %+6.1f" % (k, st.mean(x[0] for x in v), st.mean(x[1] for x in v)))
# which direction is the dog running when lateral is large
import collections
bins = collections.defaultdict(list)
for r in rows:
    vx, vy = r["V"]; sp = math.hypot(vx, vy)
    if sp < 300 or not r["own"]: continue
    ang = round(math.degrees(math.atan2(vy, vx)) / 90) * 90
    ux, uy = vx / sp, vy / sp
    bins[ang].append((r["B"][0] - r["H"][0]) * -uy + (r["B"][1] - r["H"][1]) * ux)
for a, v in sorted(bins.items()): print("heading %4d: ball-hips lateral %+6.1f (n=%d)" % (a, st.mean(v), len(v)))
