"""Migrates the Motion Matching setup from Epic's Game Animation Sample (UE 5.8) into MiniFootball.
Run: UnrealEditor-Cmd "F:/Unreal Projects/GameAnimationSample/GameAnimationSample.uproject" -run=pythonscript -script=Scripts/migrate_gasp.py -unattended -nullrhi"""
import unreal
roots = ["/Game/Blueprints/SandboxCharacter_CMC_ABP", "/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget",
         "/Game/Blueprints/BPI_SandboxCharacter_Pawn", "/Game/Blueprints/BPI_SandboxCharacter_ABP"]
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for extra in ["/Game/Characters/UEFN_Mannequin/Rigs", "/Game/Characters/UEFN_Mannequin/Meshes"]:
    for a in reg.get_assets_by_path(extra, recursive=True):
        roots.append(str(a.package_name))
unreal.log("MIGRATE roots %d" % len(roots))
tools = unreal.AssetToolsHelpers.get_asset_tools()
opts = unreal.MigrationOptions()
opts.set_editor_property("prompt", False)
opts.set_editor_property("ignore_dependencies", False)
opts.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
tools.migrate_packages(roots, "F:/MiniFootball/Content", opts)
unreal.log("MIGRATE done")
