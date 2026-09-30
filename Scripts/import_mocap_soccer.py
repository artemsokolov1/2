"""Imports Anderson Rohr's free soccer mocap pack (UE5 skeleton version) and retargets
it onto the dog. Source files: SourceArt/MocapSoccer/mocap/UE5 (gitignored, not ours to
redistribute). Run inside the editor (Python remote execution or Tools > Execute Python)."""
from pathlib import Path
import unreal as u

SRC = Path(u.Paths.project_dir()).resolve() / 'SourceArt/MocapSoccer/mocap/UE5'
DEST = '/Game/Characters/Mocap/Soccer'
tools = u.AssetToolsHelpers.get_asset_tools()
L = u.EditorAssetLibrary
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')


def fbx_task(file, name, skeleton):
    o = u.FbxImportUI()
    o.automated_import_should_detect_type = False
    o.import_as_skeletal = True
    o.import_materials = False
    o.import_textures = False
    o.create_physics_asset = False
    if skeleton:
        o.mesh_type_to_import = u.FBXImportType.FBXIT_ANIMATION
        o.import_mesh = False
        o.skeleton = skeleton
    else:
        o.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
        o.import_mesh = True
    o.import_animations = True
    o.anim_sequence_import_data.set_editor_property('import_bone_tracks', True)
    t = u.AssetImportTask()
    for k, v in dict(filename=str(file), destination_path=DEST, destination_name=name,
                     automated=True, save=True, replace_existing=True).items():
        t.set_editor_property(k, v)
    t.options = o
    t.factory = u.FbxFactory()
    tools.import_asset_tasks([t])
    return t.get_editor_property('imported_object_paths')


files = sorted(SRC.glob('*.fbx'))
first = files[0]
paths = fbx_task(first, 'SK_MocapSoccer', None)
u.log('MOCAP first import: ' + str(paths))
mesh = next((L.load_asset(p) for p in paths if isinstance(L.load_asset(p), u.SkeletalMesh)), None)
skeleton = mesh.skeleton
for f in files:
    name = 'A_' + f.stem.replace('_ue5', '')
    fbx_task(f, name, skeleton)
u.log('MOCAP imported %d clips, skeleton %s' % (len(files), skeleton.get_path_name()))

# --- IK rig of the mocap skeleton and a retargeter onto the dog -------------------------
RIG = DEST + '/IK_MocapSoccer'
rig = L.load_asset(RIG) if L.does_asset_exist(RIG) else tools.create_asset('IK_MocapSoccer', DEST, u.IKRigDefinition, u.IKRigDefinitionFactory())
rc = u.IKRigController.get_controller(rig)
rc.set_skeletal_mesh(mesh)
rc.apply_auto_generated_retarget_definition()
rc.apply_auto_fbik()
L.save_asset(RIG)

RTG_DIR = '/Game/Characters/Dogs/Retarget'
RTG = RTG_DIR + '/RTG_Mocap_to_Dog'
if L.does_asset_exist(RTG):
    L.delete_asset(RTG)
rtg = tools.create_asset('RTG_Mocap_to_Dog', RTG_DIR, u.IKRetargeter, u.IKRetargetFactory())
c = u.IKRetargeterController.get_controller(rtg)
SRC_, TGT_ = u.RetargetSourceOrTarget.SOURCE, u.RetargetSourceOrTarget.TARGET
dog = L.load_asset('/Game/Characters/Dogs/SK_Dog_Cavapoo')
c.set_ik_rig(SRC_, rig)
c.set_ik_rig(TGT_, L.load_asset(RTG_DIR + '/IK_Dog'))
c.set_preview_mesh(SRC_, mesh)
c.set_preview_mesh(TGT_, dog)
c.add_default_ops()
c.auto_map_chains(u.AutoMapChainType.FUZZY, True)
c.auto_align_all_bones(TGT_)
# The dog has no separate root bone (Hips is the root), see RTG_UEFN_to_Dog.
c.set_retarget_op_enabled(c.get_index_of_op_by_name('Root Motion'), False)
L.save_asset(RTG)

clips = [a for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(DEST, recursive=False)
         if str(a.asset_class_path.asset_name) == 'AnimSequence' and str(a.asset_name).startswith('A_')]
out = u.IKRetargetBatchOperation.duplicate_and_retarget(clips, mesh, dog, rtg, 'A_', 'A_Dog_M', '', '',
                                                        '/Game/Characters/Dogs/Animations/Mocap', False, True)
for o in out:
    L.save_asset(str(o.package_name))
u.log('MOCAP retargeted %d clips onto the dog' % len(out))
