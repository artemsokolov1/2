from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
src = root / 'SourceArt' / 'Training'
dest = '/Game/Environment/Training'
assets = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()


def import_mesh(filename, name, materials):
    task = u.AssetImportTask()
    task.set_editor_property('filename', str(src / filename))
    task.set_editor_property('destination_path', dest)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    options = u.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('mesh_type_to_import', u.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('import_as_skeletal', False)
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_materials', materials)
    options.set_editor_property('import_textures', materials)
    options.set_editor_property('create_physics_asset', False)
    data = options.get_editor_property('static_mesh_import_data')
    data.set_editor_property('convert_scene', True)
    data.set_editor_property('convert_scene_unit', True)
    data.set_editor_property('force_front_x_axis', False)
    task.set_editor_property('options', options)
    task.set_editor_property('factory', u.FbxFactory())
    tools.import_asset_tasks([task])
    path = dest + '/' + name
    mesh = assets.load_asset(path)
    if not mesh:
        raise RuntimeError('Import failed: ' + str(src / filename))
    bounds = mesh.get_bounds()
    u.log("TRAINING_MESH {} extents={} materials={}".format(path, bounds.box_extent, len(mesh.get_editor_property("static_materials"))))
    assets.save_loaded_asset(mesh)
    return mesh


def import_texture(filename, name, normal=False):
    task = u.AssetImportTask()
    task.set_editor_property('filename', str(src / 'Textures' / filename))
    task.set_editor_property('destination_path', dest + '/Textures')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(dest + '/Textures/' + name)
    if not texture:
        raise RuntimeError('Texture import failed: ' + filename)
    if normal:
        texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('srgb', False)
    assets.save_loaded_asset(texture)
    return texture


def make_textured_material(name, base_texture, normal_texture=None, roughness=0.7):
    path = dest + '/' + name
    material = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, dest, u.Material, u.MaterialFactoryNew())
    lib = u.MaterialEditingLibrary
    lib.delete_all_material_expressions(material)
    base = lib.create_material_expression(material, u.MaterialExpressionTextureSample)
    base.set_editor_property('texture', base_texture)
    lib.connect_material_property(base, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
    if normal_texture:
        normal = lib.create_material_expression(material, u.MaterialExpressionTextureSample)
        normal.set_editor_property('texture', normal_texture)
        normal.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        lib.connect_material_property(normal, 'RGB', u.MaterialProperty.MP_NORMAL)
    rough = lib.create_material_expression(material, u.MaterialExpressionConstant)
    rough.set_editor_property('r', roughness)
    lib.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
    lib.layout_material_expressions(material)
    lib.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


def make_color_material(name, color, metallic=0.0, roughness=0.65):
    path = dest + '/' + name
    material = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, dest, u.Material, u.MaterialFactoryNew())
    lib = u.MaterialEditingLibrary
    lib.delete_all_material_expressions(material)
    base = lib.create_material_expression(material, u.MaterialExpressionConstant4Vector)
    base.set_editor_property('constant', u.LinearColor(*color, 1.0))
    lib.connect_material_property(base, '', u.MaterialProperty.MP_BASE_COLOR)
    metal = lib.create_material_expression(material, u.MaterialExpressionConstant)
    metal.set_editor_property('r', metallic)
    lib.connect_material_property(metal, '', u.MaterialProperty.MP_METALLIC)
    rough = lib.create_material_expression(material, u.MaterialExpressionConstant)
    rough.set_editor_property('r', roughness)
    lib.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


court = import_mesh('football_court.fbx', 'SM_TrainingCourt', False)
ball = import_mesh('soccer_ball.fbx', 'SM_TrainingBall', False)

court_base = import_texture('football_court_Image_0.png', 'T_Court_BaseColor')
black_base = import_texture('soccer_ball_Image_0.png', 'T_Ball_Black_BaseColor')
black_normal = import_texture('soccer_ball_Image_2.png', 'T_Ball_Black_Normal', True)
white_base = import_texture('soccer_ball_Image_3.png', 'T_Ball_White_BaseColor')
white_normal = import_texture('soccer_ball_Image_5.png', 'T_Ball_White_Normal', True)

court_materials = [
    make_textured_material('M_TrainingCourt', court_base, roughness=0.85),
    make_color_material('M_TrainingGoal', (0.92, 0.92, 0.92), metallic=0.25, roughness=0.35),
    make_color_material('M_TrainingMetal', (0.12, 0.14, 0.16), metallic=0.75, roughness=0.35),
    make_color_material('M_TrainingMetal', (0.12, 0.14, 0.16), metallic=0.75, roughness=0.35),
    make_color_material('M_TrainingLight', (1.0, 0.86, 0.55), roughness=0.25),
    make_color_material('M_TrainingStandBlack', (0.018, 0.022, 0.03), roughness=0.8),
    make_color_material('M_TrainingStandRed', (0.45, 0.018, 0.025), roughness=0.75),
]
for index, material in enumerate(court_materials):
    court.set_material(index, material)
assets.save_loaded_asset(court)

ball.set_material(0, make_textured_material('M_TrainingBallBlack', black_base, black_normal, 0.48))
ball.set_material(1, make_textured_material('M_TrainingBallWhite', white_base, white_normal, 0.48))
assets.save_loaded_asset(ball)
u.log('TRAINING_ASSETS_IMPORT_OK')
