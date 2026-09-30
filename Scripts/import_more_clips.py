"""Imports the rest of the Mixamo soccer set onto the dog/giraffe skeleton (clips only).
Run: UnrealEditor-Cmd MiniFootball.uproject -run=pythonscript -script=Scripts/import_more_clips.py"""
import runpy, unreal as u
# reuse the helpers and clip table of import_dogs.py without running its body
src = open(u.Paths.project_dir() + 'Scripts/import_dogs.py', encoding='utf-8').read()
head = src.split("# Fixed/:")[0]
ns = {}
exec(head, ns)
clips = eval(src.split("clips = ")[1].split("\n}\n")[0] + "\n}")
skeleton = u.load_asset('/Game/Characters/Dogs/SK_Dog_Cavapoo').get_editor_property('skeleton')
DEST = '/Game/Characters/Dogs'
for name, file in clips.items():
    if u.EditorAssetLibrary.does_asset_exist(DEST + '/Animations/A_Dog_' + name): continue
    opts = ns['fbx_options'](u.FBXImportType.FBXIT_ANIMATION)
    opts.skeleton = skeleton
    opts.anim_sequence_import_data.set_editor_property('import_bone_tracks', True)
    opts.anim_sequence_import_data.set_editor_property('use_default_sample_rate', True)
    anim = ns['import_asset'](ns['SOURCE'] / 'Fixed' / 'Animations' / file, DEST + '/Animations', 'A_Dog_' + name, opts)
    anim.set_editor_property('force_root_lock', False)
    anim.set_editor_property('enable_root_motion', False)
    u.EditorAssetLibrary.save_loaded_asset(anim)
    u.log('CLIP %s %.2f s' % (name, anim.get_editor_property('sequence_length')))
