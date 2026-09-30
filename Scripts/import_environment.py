"""Imports the environment props exported from Blender (SourceArt/Environment/Export/*.glb)
into /Game/Environment/Props with Nanite. Run inside the editor."""
import glob, os
import unreal as u

SRC = os.path.join(u.Paths.project_dir(), "SourceArt", "Environment", "Export")
DEST = "/Game/Environment/Props"
tools = u.AssetToolsHelpers.get_asset_tools()
L = u.EditorAssetLibrary
import sys
ONLY = [a for a in sys.argv[1:]] if len(sys.argv) > 1 else None
for path in sorted(glob.glob(os.path.join(SRC, "*.glb"))):
    if ONLY and not any(o in path for o in ONLY): continue
    name = os.path.splitext(os.path.basename(path))[0]
    folder = DEST + "/" + name
    t = u.AssetImportTask()
    for k, v in dict(filename=path, destination_path=folder, automated=True, save=True, replace_existing=True).items():
        t.set_editor_property(k, v)
    tools.import_asset_tasks([t])
    meshes = [p for p in L.list_assets(folder, recursive=True) if L.find_asset_data(p).asset_class_path.asset_name == "StaticMesh"]
    for p in meshes:
        m = L.load_asset(p)
        # Foliage: Nanite (5.8) drops the Poly Haven leaves, so trees and shrubs use
        # classic LODs (decimated in Blender first); the rest stays on Nanite.
        foliage = "Tree" in name or "Shrub" in name
        ns = m.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", not foliage)
        m.set_editor_property("nanite_settings", ns)
        if foliage:
            m.set_editor_property("lod_group", "LargeProp")
        L.save_asset(p)
        b = m.get_bounding_box()
        u.log("ENV_IMPORT %s -> %s size %s" % (name, p, b.max - b.min))
