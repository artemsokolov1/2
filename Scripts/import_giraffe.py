"""Imports the rigged giraffe (Scripts/build_giraffe.py) onto the dog's skeleton.

Sharing SK_Dog_Cavapoo's skeleton gives the giraffe every dog clip and mocap take;
each clip keeps the giraffe's own proportions (bone translation mode Skeleton). The kit
material is M_Dog_Kit with the giraffe textures: its green shirt is recoloured per team.
Motion matching gets its own IK rig and retargeter (the giraffe's reference pose is a
T-pose, the dog's an A-pose).

Run: UnrealEditor-Cmd MiniFootball.uproject -run=pythonscript -script=Scripts/import_giraffe.py
"""
from pathlib import Path
import unreal as u

SOURCE = Path(u.Paths.project_dir()).resolve() / 'SourceArt/Giraffe'
DEST = '/Game/Characters/Giraffe'
tools = u.AssetToolsHelpers.get_asset_tools()
assets = u.EditorAssetLibrary
lib = u.MaterialEditingLibrary
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')


def run_task(file, folder, name, options=None):
    task = u.AssetImportTask()
    for key, value in dict(filename=str(file), destination_path=folder, destination_name=name,
                           automated=True, save=True, replace_existing=True).items():
        task.set_editor_property(key, value)
    if options:
        task.options = options
        task.factory = u.FbxFactory()
    tools.import_asset_tasks([task])
    result = assets.load_asset(folder + '/' + name)
    if not result:
        raise RuntimeError('Import failed: ' + str(file))
    return result


dog = assets.load_asset('/Game/Characters/Dogs/SK_Dog_Cavapoo')
skeleton = dog.get_editor_property('skeleton')

opts = u.FbxImportUI()
opts.automated_import_should_detect_type = False
opts.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
opts.import_materials = False
opts.import_textures = False
opts.import_as_skeletal = True
opts.import_mesh = True
opts.import_animations = False
opts.create_physics_asset = False
opts.skeleton = skeleton
data = opts.skeletal_mesh_import_data
data.convert_scene = True
data.convert_scene_unit = True
data.force_front_x_axis = False
data.set_editor_property('import_morph_targets', False)
mesh = run_task(SOURCE / 'Fixed/Giraffe.fbx', DEST, 'SK_Giraffe', opts)
if mesh.get_editor_property('skeleton') != skeleton:
    raise RuntimeError('Giraffe did not land on the dog skeleton')

textures = {}
for kind in ['BaseColor', 'Normal']:
    tex = run_task(SOURCE / 'Textures' / ('T_Giraffe_' + kind + '.png'), DEST + '/Textures', 'T_Giraffe_' + kind)
    if kind == 'Normal':
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('flip_green_channel', True)  # glTF normals are OpenGL style
    tex.set_editor_property('max_texture_size', 2048)
    assets.save_loaded_asset(tex)
    textures[kind] = tex

path = DEST + '/MI_Giraffe_Kit'
kit = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
    'MI_Giraffe_Kit', DEST, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
lib.set_material_instance_parent(kit, assets.load_asset('/Game/Characters/Dogs/M_Dog_Kit'))
lib.set_material_instance_texture_parameter_value(kit, 'BaseTexture', textures['BaseColor'])
lib.set_material_instance_texture_parameter_value(kit, 'NormalTexture', textures['Normal'])
assets.save_loaded_asset(kit)
slots = mesh.get_editor_property('materials')
for slot in slots:
    slot.set_editor_property('material_interface', kit)
mesh.set_editor_property('materials', slots)
assets.save_loaded_asset(mesh)

# Motion matching: UEFN mannequin -> giraffe
rig_path = DEST + '/Retarget/IK_Giraffe'
if assets.does_asset_exist(rig_path):
    assets.delete_asset(rig_path)
rig = tools.create_asset('IK_Giraffe', DEST + '/Retarget', u.IKRigDefinition, u.IKRigDefinitionFactory())
rc = u.IKRigController.get_controller(rig)
rc.set_skeletal_mesh(mesh)
rc.apply_auto_generated_retarget_definition()
try:
    rc.apply_auto_fbik()
except Exception as e:
    u.log_warning('fbik ' + str(e))
for ch in rc.get_retarget_chains():
    n = ch.get_editor_property('chain_name')
    u.log('GIRAFFE_CHAIN %s %s -> %s' % (n, rc.get_retarget_chain_start_bone(n), rc.get_retarget_chain_end_bone(n)))
assets.save_loaded_asset(rig)

rtg_path = DEST + '/Retarget/RTG_UEFN_to_Giraffe'
if assets.does_asset_exist(rtg_path):
    assets.delete_asset(rtg_path)
rtg = tools.create_asset('RTG_UEFN_to_Giraffe', DEST + '/Retarget', u.IKRetargeter, u.IKRetargetFactory())
c = u.IKRetargeterController.get_controller(rtg)
src, tgt = u.RetargetSourceOrTarget.SOURCE, u.RetargetSourceOrTarget.TARGET
c.set_ik_rig(src, assets.load_asset('/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin'))
c.set_ik_rig(tgt, rig)
c.set_preview_mesh(src, assets.load_asset('/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin'))
c.set_preview_mesh(tgt, mesh)
c.add_default_ops()
c.assign_ik_rig_to_all_ops(src, assets.load_asset('/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin'))
c.assign_ik_rig_to_all_ops(tgt, rig)
c.auto_map_chains(u.AutoMapChainType.FUZZY, True)
c.auto_align_all_bones(tgt)
# Hips is the root (no ground bone): copying the mannequin's root would lift the pelvis.
c.set_retarget_op_enabled(c.get_index_of_op_by_name('Root Motion'), False)
for i in range(c.get_num_retarget_ops()):
    u.log('GIRAFFE_OP %s %s' % (c.get_op_name(i), c.get_retarget_op_enabled(i)))
assets.save_loaded_asset(rtg)
u.log('GIRAFFE_IMPORT_OK ' + mesh.get_path_name())
