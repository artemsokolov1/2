"""Run with UnrealEditor-Cmd MiniFootball.uproject -run=pythonscript -script=..."""
from pathlib import Path
import unreal as u

SOURCE = Path(u.Paths.project_dir()).resolve() / 'SourceArt/Dogs'
DEST = '/Game/Characters/Dogs'
tools = u.AssetToolsHelpers.get_asset_tools()
assets = u.EditorAssetLibrary
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')


def import_asset(file, folder, name, options=None, replace=False):
    path = folder + '/' + name
    if assets.does_asset_exist(path) and not replace:
        return assets.load_asset(path)
    task = u.AssetImportTask()
    for key, value in dict(filename=str(file), destination_path=folder,
                           destination_name=name, automated=True, save=True,
                           replace_existing=replace).items():
        task.set_editor_property(key, value)
    if options:
        task.options = options
        task.factory = u.FbxFactory()
    tools.import_asset_tasks([task])
    result = assets.load_asset(path)
    if not result:
        raise RuntimeError('Import failed: ' + str(file) + ' -> ' + path)
    return result


def fbx_options(kind):
    opts = u.FbxImportUI()
    opts.automated_import_should_detect_type = False
    opts.mesh_type_to_import = kind
    opts.import_materials = False
    opts.import_textures = False
    opts.import_as_skeletal = True
    opts.import_mesh = kind == u.FBXImportType.FBXIT_SKELETAL_MESH
    opts.import_animations = not opts.import_mesh
    opts.create_physics_asset = False
    data = opts.skeletal_mesh_import_data if opts.import_mesh else opts.anim_sequence_import_data
    data.convert_scene = True
    data.convert_scene_unit = True
    data.force_front_x_axis = False
    return opts


# Fixed/: rescaled to centimetres by Scripts/fix_dog_scale.py (no x100 root bone scale)
FIXED = SOURCE / 'Fixed'
mesh = import_asset(FIXED / 'Dog_Cavapoo.fbx', DEST, 'SK_Dog_Cavapoo',
                    fbx_options(u.FBXImportType.FBXIT_SKELETAL_MESH))
skeleton = mesh.get_editor_property('skeleton')
if not skeleton:
    mesh = import_asset(FIXED / 'Dog_Cavapoo.fbx', DEST, 'SK_Dog_Cavapoo',
                        fbx_options(u.FBXImportType.FBXIT_SKELETAL_MESH), replace=True)
    skeleton = mesh.get_editor_property('skeleton')
if not skeleton:
    raise RuntimeError('Dog mesh has no skeleton')
assets.save_loaded_asset(skeleton)
clips = {
    'Idle': 'Dog_Idle.fbx', 'Run': 'Dog_Run.fbx', 'Sprint': 'Dog_Sprint.fbx',
    'Pass': 'Dog_Kick.fbx', 'Shot': 'SoccerPack/kick soccerball.fbx',
    'Header': 'SoccerPack/header soccerball.fbx', 'Tackle': 'SoccerPack/kick soccerball (2).fbx',
    'Slide': 'SoccerPack/soccer tackle (2).fbx', 'Receive': 'SoccerPack/receive soccerball.fbx',
    'KeeperIdle': 'SoccerPack/goalkeeper idle.fbx',
    'KeeperLeft': 'SoccerPack/goalkeeper sidestep.fbx',
    'KeeperRight': 'SoccerPack/goalkeeper sidestep (2).fbx',
    'DiveLeft': 'SoccerPack/goalkeeper diving save.fbx',
    'DiveRight': 'SoccerPack/goalkeeper diving save (2).fbx',
    'CatchLow': 'SoccerPack/goalkeeper scoop.fbx',
    'CatchMid': 'SoccerPack/goalkeeper catch.fbx',
    'CatchHigh': 'SoccerPack/goalkeeper catch (2).fbx',
    'KeeperPass': 'SoccerPack/goalkeeper pass.fbx',
    'KeeperThrow': 'SoccerPack/goalkeeper overhand throw.fbx',
    'KeeperKick': 'SoccerPack/goalkeeper drop kick.fbx',
    # the rest of the Mixamo soccer set, wired up 2026-09-30
    'KeeperPlace': 'SoccerPack/goalkeeper placing ball.fbx',
    'KeeperDirect': 'SoccerPack/goalkeeper directing.fbx',
    'KeeperMiss': 'SoccerPack/goalkeeper miss.fbx',
    'KeeperBlock': 'SoccerPack/goalkeeper body block.fbx',
    'KeeperBlock2': 'SoccerPack/goalkeeper body block (2).fbx',
    'Trap1': 'SoccerPack/stall soccerball.fbx',
    'Trap2': 'SoccerPack/stall soccerball (2).fbx',
    'Trap3': 'SoccerPack/stall soccerball (3).fbx',
    'Trap4': 'SoccerPack/stall soccerball (4).fbx',
    'StandUp': 'SoccerPack/standing up.fbx',
    'Volley': 'SoccerPack/scissor kick.fbx',
}
for name, file in clips.items():
    opts = fbx_options(u.FBXImportType.FBXIT_ANIMATION)
    opts.skeleton = skeleton
    opts.anim_sequence_import_data.set_editor_property('import_bone_tracks', True)
    opts.anim_sequence_import_data.set_editor_property('use_default_sample_rate', True)
    anim = import_asset(FIXED / 'Animations' / file, DEST + '/Animations', 'A_Dog_' + name, opts)
    # Runtime removes only horizontal root travel; pelvis rotation and height remain animated.
    anim.set_editor_property('force_root_lock', False)
    anim.set_editor_property('enable_root_motion', False)
    assets.save_loaded_asset(anim)
    u.log('DOG_CLIP ' + name + ' ' + str(anim.get_editor_property('sequence_length')))

textures = {}
for suffix in ['BaseColor', 'Normal', 'Kit_Home', 'Kit_Away']:
    tex = import_asset(SOURCE / 'Textures' / ('Dog_Cavapoo_' + suffix + '.png'),
                       DEST + '/Textures', 'T_Dog_' + suffix)
    if suffix == 'Normal':
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('flip_green_channel', True)
    assets.save_loaded_asset(tex)
    textures[suffix] = tex

lib = u.MaterialEditingLibrary
path = DEST + '/M_Dog_Kit'
material = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset('M_Dog_Kit', DEST, u.Material, u.MaterialFactoryNew())
lib.delete_all_material_expressions(material)
lib.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)


def node(cls, **props):
    obj = lib.create_material_expression(material, cls)
    for key, value in props.items():
        obj.set_editor_property(key, value)
    return obj


def wire(a, b, pin, output=''):
    if not lib.connect_material_expressions(a, output, b, pin):
        raise RuntimeError('Material connection failed: ' + pin)


base = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name='BaseTexture', texture=textures['BaseColor'])
normal = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name='NormalTexture', texture=textures['Normal'], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
color = node(u.MaterialExpressionVectorParameter, parameter_name='KitColor', default_value=u.LinearColor(0.8, 0.025, 0.02, 1))
maximum = node(u.MaterialExpressionMax)
wire(base, maximum, 'A', 'R'); wire(base, maximum, 'B', 'B')
diff = node(u.MaterialExpressionSubtract)
wire(base, diff, 'A', 'G'); wire(maximum, diff, 'B')
green = node(u.MaterialExpressionMax, const_b=0.001)
wire(base, green, 'A', 'G')
dominance = node(u.MaterialExpressionDivide)
wire(diff, dominance, 'A'); wire(green, dominance, 'B')
threshold = node(u.MaterialExpressionSubtract, const_b=0.25)
wire(dominance, threshold, 'A')
gain = node(u.MaterialExpressionMultiply, const_b=4.0)
wire(threshold, gain, 'A')
mask = node(u.MaterialExpressionClamp)
wire(gain, mask, '')
shade = node(u.MaterialExpressionMultiply)
wire(base, shade, 'A', 'G'); wire(color, shade, 'B')
lerp = node(u.MaterialExpressionLinearInterpolate)
wire(base, lerp, 'A', 'RGB'); wire(shade, lerp, 'B'); wire(mask, lerp, 'Alpha')
rough = node(u.MaterialExpressionConstant, r=0.8)
lib.connect_material_property(lerp, '', u.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(normal, 'RGB', u.MaterialProperty.MP_NORMAL)
lib.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
lib.layout_material_expressions(material)
lib.recompile_material(material)
assets.save_loaded_asset(material)

for side in ['Home', 'Away']:
    name = 'M_Dog_Cavapoo_' + side
    path = DEST + '/' + name
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, DEST, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(instance, material)
    lib.set_material_instance_texture_parameter_value(instance, 'BaseTexture', textures['Kit_' + side])
    assets.save_loaded_asset(instance)
slots = mesh.get_editor_property('materials')
for slot in slots:
    slot.set_editor_property('material_interface', material)
mesh.set_editor_property('materials', slots)
assets.save_loaded_asset(mesh)
assets.save_loaded_asset(skeleton)
u.log('DOG_IMPORT_OK ' + mesh.get_path_name())
