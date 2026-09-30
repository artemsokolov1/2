"""The glTF import parents every material to /InterchangeAssets/gltf/M_Default (engine plugin
content, not used with instanced static meshes), so the instanced crowd and palms render with the
default grey material. This copies the parent chain into the project with the ISM usage on and
re-parents the instanced props' materials to it.
Headless: UnrealEditor-Cmd.exe MiniFootball.uproject -run=pythonscript -script=Scripts/fix_zoo_instancing.py"""
import unreal as u
L = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary
DST = "/Game/Environment/Zoo/Materials"
master = DST + "/M_Gltf_ISM"
inst = DST + "/MI_Gltf_Opaque_DS_ISM"
tools = u.AssetToolsHelpers.get_asset_tools()
def copy(src, dst):
    if L.does_asset_exist(dst): return L.load_asset(dst)
    s = u.load_asset(src)
    u.log("ISMFIX source %s -> %s" % (src, s))
    return tools.duplicate_asset(dst.rsplit("/", 1)[1], DST, s)
m = copy("/InterchangeAssets/gltf/M_Default.M_Default", master)
m.set_editor_property("used_with_instanced_static_meshes", True)
MEL.recompile_material(m)
L.save_asset(master, only_if_is_dirty=False)
mi = copy("/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS.MI_Default_Opaque_DS", inst)
MEL.set_material_instance_parent(mi, m)
L.save_asset(inst, only_if_is_dirty=False)
import os
names = [p.rstrip("/").split("/")[-1] for p in u.EditorAssetLibrary.list_sub_paths("/Game/Environment/Zoo/Meshy", recursive=False)] if hasattr(u.EditorAssetLibrary, "list_sub_paths") else []
if not names:
    names = sorted({os.path.splitext(f)[0] for f in os.listdir(os.path.join(u.Paths.project_dir(), "SourceArt", "Environment", "Zoo", "Meshy")) if f.endswith(".glb")})
for n in [n for n in names if n.startswith(("Fan", "Palm", "Bush"))]:  # the instanced props
    folder = "/Game/Environment/Zoo/Meshy/%s/%s/Materials" % (n, n)
    for p in L.list_assets(folder, recursive=False):
        a = L.load_asset(p)
        if isinstance(a, u.MaterialInstanceConstant):
            MEL.set_material_instance_parent(a, mi)
            a.modify(); L.save_asset(p, only_if_is_dirty=False)
            u.log("ISMFIX %s -> %s" % (p, a.get_base_material().get_path_name()))
