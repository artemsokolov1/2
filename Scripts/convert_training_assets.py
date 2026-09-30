import bpy
from pathlib import Path

root = Path(__file__).resolve().parent.parent
source_dir = root / 'SourceArt' / 'Training'
inputs = [
    (source_dir / 'football_court.glb', source_dir / 'football_court.fbx', 'Court'),
    (source_dir / 'soccer_ball.glb', source_dir / 'soccer_ball.fbx', 'Ball'),
]
for source, dest, label in inputs:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(source))
    objects = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    if not objects:
        raise RuntimeError(f'No mesh objects imported for {label}')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    merged = bpy.context.view_layer.objects.active
    merged.name = 'SM_Training' + label
    merged.data.name = merged.name
    # glTF dimensions are metres; bake the metre-to-centimetre conversion.
    merged.scale = (100.0, 100.0, 100.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    dest.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=str(dest), use_selection=True, object_types={'MESH'},
        global_scale=1.0, apply_unit_scale=False, apply_scale_options='FBX_SCALE_ALL',
        axis_forward='-Z', axis_up='Y', path_mode='COPY', embed_textures=True,
        bake_space_transform=False, add_leaf_bones=False, use_mesh_modifiers=True)
    print(f'EXPORTED {label}: {dest} bounds={tuple(merged.dimensions)} materials={len(merged.data.materials)}')
