"""Nanite cannot draw translucent materials: the glTF import marked the Poly Haven leaves
translucent, so they vanished. Make every leaves/shrub material masked and two-sided."""
import unreal as u
L = u.EditorAssetLibrary
for p in L.list_assets("/Game/Environment/Props", recursive=True):
    if L.find_asset_data(p).asset_class_path.asset_name != "MaterialInstanceConstant": continue
    name = p.split(".")[-1].lower()
    if not any(k in name for k in ("leaves", "shrub", "branches")): continue
    mi = L.load_asset(p)
    o = mi.get_editor_property("base_property_overrides")
    o.set_editor_property("override_blend_mode", True)
    o.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
    o.set_editor_property("override_two_sided", True)
    o.set_editor_property("two_sided", True)
    mi.set_editor_property("base_property_overrides", o)
    u.MaterialEditingLibrary.update_material_instance(mi)
    L.save_asset(p)
    u.log("FOLIAGE_FIX " + p)

# Nanite collapses thin leaves when it simplifies; UE 5.8's voxelized foliage keeps the canopy.
for p in L.list_assets("/Game/Environment/Props", recursive=True):
    if L.find_asset_data(p).asset_class_path.asset_name != "StaticMesh": continue
    if not any(k in p for k in ("Tree", "Shrub")): continue
    m = L.load_asset(p)
    ns = m.get_editor_property("nanite_settings")
    ns.set_editor_property("shape_preservation", u.NaniteShapePreservation.VOXELIZE if "Tree" in p else u.NaniteShapePreservation.PRESERVE_AREA)
    m.set_editor_property("nanite_settings", ns)
    L.save_asset(p)
    u.log("NANITE_AREA " + p)
