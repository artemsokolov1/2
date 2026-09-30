"""Imports the night-zoo arena exported from Blender (SourceArt/Environment/Zoo/*.glb, built
in MF_ZooArena.blend) into /Game/Environment/Zoo with Nanite. The meshes are authored in
pitch space (origin = centre spot) and spawned at the origin by ASoccerGameMode::SpawnMatchCourt.
Headless: UnrealEditor-Cmd.exe MiniFootball.uproject -run=pythonscript -script=Scripts/import_zoo_arena.py"""
import glob, os
import unreal as u

SRC = os.path.join(u.Paths.project_dir(), "SourceArt", "Environment", "Zoo")
DEST = "/Game/Environment/Zoo"
tools = u.AssetToolsHelpers.get_asset_tools()
L = u.EditorAssetLibrary
import sys
# arena parts built in Blender, plus the Meshy props (Scripts/meshy_generate.py) under Zoo/Meshy
jobs = [(p, DEST) for p in sorted(glob.glob(os.path.join(SRC, "SM_Zoo_*.glb")))]
jobs += [(p, DEST + "/Meshy") for p in sorted(glob.glob(os.path.join(SRC, "Meshy", "*.glb")))]
ONLY = sys.argv[1:]
for path, root in jobs:
    name = os.path.splitext(os.path.basename(path))[0]
    if ONLY and name not in ONLY: continue
    folder = root + "/" + name
    t = u.AssetImportTask()
    for k, v in dict(filename=path, destination_path=folder, automated=True, save=True, replace_existing=True).items():
        t.set_editor_property(k, v)
    tools.import_asset_tasks([t])
    for p in L.list_assets(folder, recursive=True):
        if L.find_asset_data(p).asset_class_path.asset_name != "StaticMesh": continue
        m = L.load_asset(p)
        ns = m.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        m.set_editor_property("nanite_settings", ns)
        L.save_asset(p)
        b = m.get_bounding_box()
        u.log("ZOO_IMPORT %s -> %s min %s max %s" % (name, p, b.min, b.max))
