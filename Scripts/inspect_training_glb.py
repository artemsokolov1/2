import bpy
from pathlib import Path

source = Path(__file__).resolve().parent.parent / "SourceArt" / "Training" / "football_court.glb"
bpy.ops.import_scene.gltf(filepath=str(source))
for obj in bpy.context.scene.objects:
    if obj.type != "MESH":
        continue
    materials = ",".join(slot.material.name if slot.material else "None" for slot in obj.material_slots)
    if not obj.data.vertices:
        continue
    zs = [(obj.matrix_world @ vertex.co).z for vertex in obj.data.vertices]
    print("COURT_PART", obj.name, "z_min_m", round(min(zs), 5), "z_max_m", round(max(zs), 5), "materials", materials)
