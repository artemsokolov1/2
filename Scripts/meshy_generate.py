"""Generates arena props with the Meshy text-to-3D API: preview mesh, then PBR texturing,
then downloads the GLB to SourceArt/Environment/Zoo/Meshy/<name>.glb.
The key is read from the MESHY_API_KEY environment variable (never stored in the repo).
Usage: python Scripts/meshy_generate.py name1 name2 ...   (names from ASSETS below)"""
import json, os, sys, time, urllib.request, urllib.error

API = "https://api.meshy.ai/openapi/v2/text-to-3d"
KEY = os.environ.get("MESHY_API_KEY") or ""
OUT = os.path.join(os.path.dirname(__file__), "..", "SourceArt", "Environment", "Zoo", "Meshy")

STYLE = "stylized realistic game asset, night zoo football stadium theme, clean shapes, single object, no base plate"
ASSETS = {
    "LionStatue": "a majestic golden lion statue standing proudly on a rock, warm bronze-gold metal, full body, " + STYLE,
    "Palm": "a tall tropical coconut palm tree with a slightly curved trunk and lush green fronds, " + STYLE,
    "Lantern": "a square warm glowing lantern on a slim dark metal post, amber glass panes, park lamp, " + STYLE,
    "FanTiger": "a cartoon tiger football fan sitting on a stadium seat cheering with raised arms, green team scarf, " + STYLE,
    "GiraffeStatue": "a tall giraffe statue standing, realistic spotted pattern, full body, " + STYLE,
    "PolarBear": "a white polar bear standing on four legs on an icy rock, full body, " + STYLE,
    "Elephant": "an african elephant statue standing, full body, sandy stone color, " + STYLE,
    "Flamingo": "a pink flamingo standing on one leg, full body, " + STYLE,
    "ZooSign": "a big arched zoo entrance sign with the word ZOO in glowing golden letters and black silhouettes of an elephant, giraffe and rhino on a glowing amber arch, " + STYLE,
    "Floodlight": "a stadium floodlight tower, black steel lattice mast with a grid of bright lamps on top, " + STYLE,
    "FanPanda": "a cartoon panda football fan sitting on a stadium seat clapping, red team scarf, " + STYLE,
    "FanWolf": "a cartoon wolf football fan sitting on a stadium seat waving a flag, " + STYLE,
    "FanLion": "a cartoon lion football fan sitting on a stadium seat shouting with both paws raised, green team scarf, " + STYLE,
    "FanElephant": "a cartoon elephant football fan sitting on a stadium seat clapping, red team scarf, " + STYLE,
    "FanGiraffe": "a cartoon giraffe football fan sitting on a stadium seat cheering, green team shirt, " + STYLE,
    "FanMonkey": "a cartoon monkey football fan sitting on a stadium seat jumping with joy, red team scarf, " + STYLE,
    "FanFox": "a cartoon fox football fan sitting on a stadium seat waving a small flag, green team scarf, " + STYLE,
    "FanBear": "a cartoon brown bear football fan sitting on a stadium seat with a foam finger, red team shirt, " + STYLE,
    "Bush": "a lush tropical bush with big broad leaves, monstera and banana plant cluster, " + STYLE,
}
# a variant is the same prompt generated again: Name_2, Name_3 ...
ASSETS["ZooSign"] = ("a grand zoo entrance arch sign at night: a golden glowing arch plate with black silhouettes of an elephant, "
                     "giraffe and rhino on top, big warm glowing letters ZOO below, dark stone pillars with lanterns, " + STYLE)
def prompt(name):
    return ASSETS[name.rsplit("_", 1)[0] if name.rsplit("_", 1)[-1].isdigit() else name]

def call(url, body=None):
    for i in range(30):
        req = urllib.request.Request(url, data=json.dumps(body).encode() if body else None,
                                     headers={"Authorization": "Bearer " + KEY, "Content-Type": "application/json"},
                                     method="POST" if body else "GET")
        try:
            return json.loads(urllib.request.urlopen(req, timeout=60).read())
        except urllib.error.HTTPError as e:
            if e.code != 429 and e.code < 500: raise
            time.sleep(15)  # rate limit / queue full: wait and try again
    raise RuntimeError("Meshy busy: " + url)

def wait(task):
    while True:
        t = call(API + "/" + task)
        if t["status"] in ("SUCCEEDED", "FAILED", "CANCELED", "EXPIRED"): return t
        time.sleep(10)

def fetch(url, tries=4):
    for i in range(tries):
        try:
            return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "MiniFootball"}), timeout=180).read()
        except Exception as e:
            print("retry", i + 1, e, flush=True); time.sleep(5)
    raise RuntimeError("download failed: " + url)

def save(n, t):
    data = fetch(t["model_urls"]["glb"])
    open(os.path.join(OUT, n + ".glb"), "wb").write(data)
    if t.get("thumbnail_url"): open(os.path.join(OUT, n + ".png"), "wb").write(fetch(t["thumbnail_url"]))
    print(n, "saved", len(data) // 1024, "KB", flush=True)

def redownload(names):
    # finished refine tasks, newest first: match them to the asset prompts
    tasks = call(API + "?page_size=50&sort_by=-created_at")
    for n in names:
        for t in tasks:
            if t.get("mode") == "refine" and t["status"] == "SUCCEEDED" and prompt(n)[:60] in (t.get("prompt") or ""):
                save(n, t); break
        else: print(n, "no finished task found", flush=True)

def main(names):
    if not KEY: sys.exit("MESHY_API_KEY is not set")
    os.makedirs(OUT, exist_ok=True)
    previews = {n: call(API, {"mode": "preview", "prompt": prompt(n)[:600], "art_style": "realistic",
                              "topology": "triangle", "target_polycount": 30000, "should_remesh": True})["result"] for n in names}
    print("previews", previews, flush=True)
    refines = {}
    for n, task in previews.items():
        t = wait(task)
        print(n, "preview", t["status"], flush=True)
        if t["status"] == "SUCCEEDED":
            refines[n] = call(API, {"mode": "refine", "preview_task_id": task, "enable_pbr": True})["result"]
    for n, task in refines.items():
        t = wait(task)
        print(n, "refine", t["status"], flush=True)
        if t["status"] == "SUCCEEDED": save(n, t)

if __name__ == "__main__":
    if sys.argv[1:2] == ["--download"]: redownload(sys.argv[2:])
    else: main(sys.argv[1:] or ["LionStatue", "Palm", "Lantern", "FanTiger"])
