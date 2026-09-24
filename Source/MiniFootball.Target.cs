using UnrealBuildTool;

// Цель сборки игры (Development/Shipping)
public class MiniFootballTarget : TargetRules
{
	public MiniFootballTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MiniFootball");
	}
}
