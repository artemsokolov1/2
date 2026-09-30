"""Rigs the generated giraffe footballer with the dog's Mixamo skeleton and exports it.

The giraffe gets the same bone names, hierarchy and bone axis conventions as
SK_Dog_Cavapoo, so it can share the dog's skeleton asset in Unreal: every dog clip,
mocap take and gameplay socket (Hips, LeftUpLeg, ...) then works on it unchanged.
Joint positions were measured on the mesh (Scripts/build_giraffe.py landmarks, metres,
model as generated: 1.18 m tall, facing -Y, its left on +X).

Run: blender --background --python Scripts/build_giraffe.py
Out: SourceArt/Giraffe/Fixed/Giraffe.fbx (centimetres) + Textures/*.png
"""
import os
import bpy
from mathutils import Vector

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC_GLB = os.path.join(ROOT, "SourceArt/Giraffe/c2438c5d501a4d118793003df98bcf7e.glb")
DOG_FBX = os.path.join(ROOT, "SourceArt/Dogs/Fixed/Dog_Cavapoo.fbx")
OUT_DIR = os.path.join(ROOT, "SourceArt/Giraffe/Fixed")
TEX_DIR = os.path.join(ROOT, "SourceArt/Giraffe/Textures")
CENTER_X = -0.01   # the model's symmetry plane
SCALE = 1.6        # 1.18 m model -> 1.88 m footballer

# Left-side joints in model metres (x, y, z); the right side is mirrored.
SIDE = {
    "Shoulder": ((0.02, 0.012, 0.80), (0.13, 0.012, 0.82)),
    "Arm": ((0.13, 0.012, 0.82), (0.245, 0.018, 0.82)),
    "ForeArm": ((0.245, 0.018, 0.82), (0.357, 0.012, 0.815)),
    "Hand": ((0.357, 0.012, 0.815), (0.405, 0.012, 0.813)),
    "HandIndex1": ((0.405, 0.012, 0.813), (0.425, 0.012, 0.812)),
    "HandIndex2": ((0.425, 0.012, 0.812), (0.445, 0.012, 0.811)),
    "HandIndex3": ((0.445, 0.012, 0.811), (0.46, 0.012, 0.81)),
    "HandIndex4": ((0.46, 0.012, 0.81), (0.472, 0.012, 0.81)),
    "HandThumb1": ((0.365, -0.012, 0.807), (0.385, -0.025, 0.803)),
    "HandThumb2": ((0.385, -0.025, 0.803), (0.402, -0.032, 0.801)),
    "HandThumb3": ((0.402, -0.032, 0.801), (0.416, -0.036, 0.80)),
    "HandThumb4": ((0.416, -0.036, 0.80), (0.428, -0.038, 0.80)),
    "UpLeg": ((0.065, 0.0, 0.50), (0.065, -0.004, 0.30)),
    "Leg": ((0.065, -0.004, 0.30), (0.065, 0.02, 0.075)),
    "Foot": ((0.065, 0.02, 0.075), (0.065, -0.06, 0.02)),
    "ToeBase": ((0.065, -0.06, 0.02), (0.065, -0.095, 0.02)),
    "Toe_End": ((0.065, -0.095, 0.02), (0.065, -0.115, 0.02)),
}
CENTER = {
    "Hips": ((0.0, 0.0, 0.52), (0.0, 0.0, 0.58)),
    "Spine": ((0.0, 0.0, 0.58), (0.0, 0.0, 0.66)),
    "Spine1": ((0.0, 0.0, 0.66), (0.0, 0.0, 0.74)),
    "Spine2": ((0.0, 0.0, 0.74), (0.0, 0.015, 0.86)),
    "Neck": ((0.0, 0.015, 0.86), (0.0, 0.012, 1.0)),
    "Head": ((0.0, 0.012, 1.0), (0.0, 0.0, 1.17)),
    "HeadTop_End": ((0.0, 0.0, 1.17), (0.0, 0.0, 1.25)),
}


def joints():
    out = {}
    for name, (h, t) in CENTER.items():
        out["mixamorig:" + name] = (Vector(h), Vector(t))
    for name, (h, t) in SIDE.items():
        out["mixamorig:Left" + name] = (Vector(h), Vector(t))
        out["mixamorig:Right" + name] = (Vector((-h[0], h[1], h[2])), Vector((-t[0], t[1], t[2])))
    return {k: (h * SCALE, t * SCALE) for k, (h, t) in out.items()}


bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# 1. The dog skeleton: hierarchy and bone axes to copy.
bpy.ops.import_scene.fbx(filepath=DOG_FBX)
dog = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
dog_bones = {}
for b in dog.data.bones:
    m = b.matrix_local.to_3x3()
    dog_bones[b.name] = (b.parent.name if b.parent else None, m.col[1].normalized(), m.col[2].normalized())
order = [b.name for b in dog.data.bones]
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

# 2. The giraffe mesh, centred and scaled to game size, one object, clean normals.
bpy.ops.import_scene.gltf(filepath=SRC_GLB)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in bpy.data.objects:
    o.select_set(o.type == 'MESH')
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
body.name = "Giraffe"
body.data.name = "Giraffe"
for o in list(bpy.data.objects):
    if o is not body:
        bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
body.data.transform(__import__("mathutils").Matrix.Translation((-CENTER_X, 0.0, 0.0)))
body.data.transform(__import__("mathutils").Matrix.Scale(SCALE, 4))
body.data.update()
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.0005)
bpy.ops.object.mode_set(mode='OBJECT')

# 3. The armature: giraffe joints, dog names/parents, dog axes turned onto each new bone.
J = joints()
arm_data = bpy.data.armatures.new("Armature")
arm = bpy.data.objects.new("Armature", arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
for name in order:
    if name not in J:
        raise RuntimeError("No giraffe joint for " + name)
    parent, dog_y, dog_z = dog_bones[name]
    eb = arm_data.edit_bones.new(name)
    eb.head, eb.tail = J[name]
    new_y = (eb.tail - eb.head).normalized()
    eb.align_roll(dog_y.rotation_difference(new_y) @ dog_z)
for name in order:
    parent = dog_bones[name][0]
    if parent:
        eb = arm_data.edit_bones[name]
        eb.parent = arm_data.edit_bones[parent]
        eb.use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')

# Finger tips, head top and toe ends carry no skin (as in Mixamo rigs).
for name in arm_data.bones.keys():
    if name.endswith(("4", "_End")):
        arm_data.bones[name].use_deform = False

# 4. Skin with bone-heat weights.
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.parent_set(type='ARMATURE_AUTO')
empty = [v.index for v in body.data.vertices if not v.groups]
print("GIRAFFE_UNWEIGHTED", len(empty), "of", len(body.data.vertices))

# 5. Textures out as PNG for the Unreal import.
os.makedirs(TEX_DIR, exist_ok=True)
images = {n.image for slot in body.material_slots if slot.material and slot.material.node_tree
          for n in slot.material.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image}
for img in images:
    kind = "Normal" if "normal" in img.name else ("MetalRough" if "roughness" in img.name else "BaseColor")
    img.filepath_raw = os.path.join(TEX_DIR, "T_Giraffe_" + kind + ".png")
    img.file_format = 'PNG'
    img.save()
    print("GIRAFFE_TEX", img.filepath_raw)

os.makedirs(OUT_DIR, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT_DIR, "Giraffe_rig.blend"))

# 6. Centimetres with the scale applied (see fix_dog_scale.py), then FBX.
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.01
body.parent = None
for o in (arm, body):
    o.scale = (100.0, 100.0, 100.0)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
body.parent = arm
body.matrix_parent_inverse.identity()
mod = next(m for m in body.modifiers if m.type == 'ARMATURE')
mod.object = arm
os.makedirs(OUT_DIR, exist_ok=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT_DIR, "Giraffe.fbx"), use_selection=False,
                         apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', global_scale=1.0,
                         add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE',
                         use_armature_deform_only=False, path_mode='STRIP')
print("GIRAFFE_OK", os.path.join(OUT_DIR, "Giraffe.fbx"))
