using UnrealBuildTool;

// Цель сборки для редактора (Development Editor) — её собирает двойной клик по .uproject
public class MiniFootballEditorTarget : TargetRules
{
	public MiniFootballEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MiniFootball");
	}
}
