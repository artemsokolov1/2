"""Imports the banner textures (SourceArt/Environment/Banners) and makes a material per banner."""
import glob, os
import unreal as u
SRC = os.path.join(u.Paths.project_dir(), "SourceArt", "Environment", "Banners")
DEST = "/Game/Environment/Banners"
tools = u.AssetToolsHelpers.get_asset_tools()
L = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary
for path in sorted(glob.glob(os.path.join(SRC, "T_Banner_*.png"))):
    name = os.path.splitext(os.path.basename(path))[0]
    t = u.AssetImportTask()
    for k, v in dict(filename=path, destination_path=DEST, destination_name=name, automated=True, save=True, replace_existing=True).items():
        t.set_editor_property(k, v)
    tools.import_asset_tasks([t])
    tex = L.load_asset(DEST + "/" + name)
    mname = "M_" + name[2:]
    mp = DEST + "/" + mname
    m = L.load_asset(mp) if L.does_asset_exist(mp) else tools.create_asset(mname, DEST, u.Material, u.MaterialFactoryNew())
    MEL.delete_all_material_expressions(m)
    ts = MEL.create_material_expression(m, u.MaterialExpressionTextureSample, -400, 0)
    ts.set_editor_property("texture", tex)
    MEL.connect_material_property(ts, "RGB", u.MaterialProperty.MP_BASE_COLOR)
    r = MEL.create_material_expression(m, u.MaterialExpressionConstant, -200, 200); r.set_editor_property("r", 0.75)
    MEL.connect_material_property(r, "", u.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m); L.save_asset(mp)
    u.log("BANNER " + mp)
