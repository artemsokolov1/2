import bpy
import mathutils
from pathlib import Path

root = Path(__file__).resolve().parent.parent
for filename in ('football_court.glb', 'soccer_ball.glb'):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(root / 'SourceArt' / 'Training' / filename))
    print('ASSET', filename)
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH':
            continue
        corners = [obj.matrix_world @ mathutils.Vector(corner) for corner in obj.bound_box]
        low = tuple(round(min(point[i] for point in corners), 3) for i in range(3))
        high = tuple(round(max(point[i] for point in corners), 3) for i in range(3))
        print('OBJECT', obj.name, 'bounds', low, high)
        for slot in obj.material_slots:
            mat = slot.material
            images = []
            if mat and mat.use_nodes:
                images = [node.image.name for node in mat.node_tree.nodes
                          if node.type == 'TEX_IMAGE' and node.image]
            print('  MATERIAL', mat.name if mat else 'None', 'images', images)
