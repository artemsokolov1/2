import unreal

for path in (
    "/Game/Environment/Training/SM_TrainingCourt",
    "/Game/Environment/Training/SM_TrainingBall",
):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        raise RuntimeError("Missing training mesh: " + path)
    bounds = mesh.get_bounds()
    origin = bounds.origin
    extent = bounds.box_extent
    unreal.log("TRAINING_BOUNDS {} origin={} extent={} min_z={} max_z={}".format(
        path, origin, extent, origin.z - extent.z, origin.z + extent.z))
