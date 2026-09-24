// Модуль игры. Если проект называется иначе — поменяйте имя класса и файла на имя своего модуля.
using UnrealBuildTool;

public class MiniFootball : ModuleRules
{
	public MiniFootball(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// EnhancedInput — ввод геймпада/клавиатуры; Slate/SlateCore — меню и HUD (SoccerUI.cpp)
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
	}
}
