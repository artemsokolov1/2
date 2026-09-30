"""Downloads CC0 models from Poly Haven (glTF, 1k textures) into SourceArt/Environment/PolyHaven.
Usage: python Scripts/fetch_polyhaven.py id1 id2 ..."""
import json, os, sys, urllib.request

ROOT = os.path.join(os.path.dirname(__file__), "..", "SourceArt", "Environment", "PolyHaven")


def get(url, path, tries=5):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "MiniFootball-asset-fetch"})
            with urllib.request.urlopen(req, timeout=300) as r:
                data = r.read()
            with open(path + ".part", "wb") as f:
                f.write(data)
            os.replace(path + ".part", path)
            return
        except Exception as e:
            print("retry", attempt + 1, os.path.basename(path), e)
    raise RuntimeError("download failed: " + url)


for asset in sys.argv[1:]:
    req = urllib.request.Request("https://api.polyhaven.com/files/" + asset, headers={"User-Agent": "MiniFootball-asset-fetch"})
    files = json.load(urllib.request.urlopen(req, timeout=60))
    entry = files["gltf"]["1k"]["gltf"]
    folder = os.path.join(ROOT, asset)
    get(entry["url"], os.path.join(folder, os.path.basename(entry["url"])))
    for rel, inc in entry.get("include", {}).items():
        get(inc["url"], os.path.join(folder, rel))
    print("OK", asset, len(entry.get("include", {})), "files")
