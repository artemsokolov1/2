import bpy
from pathlib import Path

root = Path(__file__).resolve().parent.parent
output = root / 'SourceArt' / 'Training' / 'Textures'
output.mkdir(parents=True, exist_ok=True)

for asset in ('football_court.glb', 'soccer_ball.glb'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(root / 'SourceArt' / 'Training' / asset))
    prefix = Path(asset).stem
    for image in bpy.data.images:
        if not image.packed_file:
            continue
        suffix = '.png'
        if image.file_format == 'JPEG':
            suffix = '.jpg'
        destination = output / f'{prefix}_{image.name}{suffix}'
        image.filepath_raw = str(destination)
        image.save()
        print('SAVED', destination)
