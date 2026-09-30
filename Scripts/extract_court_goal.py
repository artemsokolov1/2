"""Cuts one goal (frame + net) out of football_court.glb as its own mesh, origin at the middle
of the goal line, so the match can place correctly sized goals on a stretched court.
Run: blender --background --python Scripts/extract_court_goal.py"""
from pathlib import Path
import bmesh
import bpy

root = Path(__file__).resolve().parent.parent
src = root / 'SourceArt' / 'Training' / 'football_court.glb'
dest = root / 'SourceArt' / 'Training' / 'court_goal.fbx'
GOAL_LINE_X = 10.54  # metres, centre of the posts (measured, see CollideWithTrainingCourt)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(src))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

bm = bmesh.new()
bm.from_mesh(obj.data)
goal_slots = {i for i, m in enumerate(obj.data.materials) if m and m.name.startswith('kale')}
remove = [f for f in bm.faces if f.material_index not in goal_slots or f.calc_center_median().x < 0]
bmesh.ops.delete(bm, geom=remove, context='FACES')
for v in bm.verts:
    v.co.x -= GOAL_LINE_X
bm.to_mesh(obj.data)
bm.free()
obj.name = obj.data.name = 'SM_CourtGoal'
# Unreal's FBX import converts the metres itself (x100 here doubled it)
# Keep only the goal material slot in use
bpy.ops.export_scene.fbx(filepath=str(dest), use_selection=True, object_types={'MESH'},
    global_scale=1.0, apply_unit_scale=False, apply_scale_options='FBX_SCALE_ALL',
    axis_forward='-Z', axis_up='Y', path_mode='COPY', embed_textures=True,
    bake_space_transform=False, add_leaf_bones=False, use_mesh_modifiers=True)
print('GOAL_EXPORTED', dest, tuple(round(d, 1) for d in obj.dimensions), len(obj.data.polygons))
