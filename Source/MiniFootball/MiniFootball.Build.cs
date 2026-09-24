// Модуль игры. Если проект называется иначе — поменяйте имя класса и файла на имя своего модуля.
using UnrealBuildTool;

public class MiniFootball : ModuleRules
{
	public MiniFootball(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// EnhancedInput — обязателен: ввод геймпада/клавиатуры создаётся через него
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
	}
}
