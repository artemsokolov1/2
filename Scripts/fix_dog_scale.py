"""Rescales the dog FBX files from metres to centimetres with the scale applied.

The source files store the dog in metres. Unreal compensates with a x100 scale on the
root bone (Hips), which breaks IK retargeting. Here the armature, mesh and bone
translation keys are multiplied by 100 and applied, and the scene unit is set to
centimetres, so Unreal imports a skeleton with unit bone scales.

Run: blender --background --python Scripts/fix_dog_scale.py -- <in.fbx> <out.fbx>
"""
import os
import sys
import bpy

args = sys.argv[sys.argv.index("--") + 1:]
src, dst = args[0], args[1]
os.makedirs(os.path.dirname(dst), exist_ok=True)
FACTOR = 100.0

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.01
bpy.ops.import_scene.fbx(filepath=src)

armature = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for obj in bpy.data.objects:
    obj.select_set(obj.parent is None)
bpy.context.view_layer.objects.active = armature
armature.scale = (FACTOR, FACTOR, FACTOR)
bpy.ops.object.select_all(action='SELECT')
# Rotation too: the armature object carries the Y-up to Z-up turn. Unreal bakes it into
# the mesh skeleton but drops it from animation-only files, so apply it to the bones.
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

for action in bpy.data.actions:
    # Blender 4.4+ layered actions keep curves in channelbags; older ones in action.fcurves.
    curves = list(getattr(action, "fcurves", []) or [])
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for bag in getattr(strip, "channelbags", []):
                curves += list(bag.fcurves)
    for fc in curves:
        if fc.data_path.endswith(".location"):
            for kp in fc.keyframe_points:
                kp.co[1] *= FACTOR
                kp.handle_left[1] *= FACTOR
                kp.handle_right[1] *= FACTOR

has_anim = any(True for _ in bpy.data.actions)
frames = [int(a.frame_range[1]) for a in bpy.data.actions]
if frames:
    scene.frame_start, scene.frame_end = 1, max(frames)
bpy.ops.export_scene.fbx(filepath=dst, use_selection=False, apply_unit_scale=True,
                         apply_scale_options='FBX_SCALE_NONE', global_scale=1.0,
                         add_leaf_bones=False, bake_anim=has_anim, bake_anim_use_all_actions=False,
                         bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0.0,
                         mesh_smooth_type='FACE', use_armature_deform_only=False)
print("FIXED", dst, "anim" if has_anim else "mesh")
