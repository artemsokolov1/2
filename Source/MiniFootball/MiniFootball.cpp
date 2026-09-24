// Основной модуль игры MiniFootball. Вся логика игры — в Soccer.h / Soccer.cpp / SoccerUI.cpp.
//
// Только в редакторе: при запуске модуль один раз импортирует 3D-модель футболиста и анимации
// из SourceArt/Footballer/*.fbx в Content/Characters/Footballer/<ИмяФайла>/ — вручную ничего
// импортировать не нужно. Если папка с импортом уже есть, файл пропускается
// (чтобы переимпортировать — удалите Content/Characters/Footballer/<ИмяФайла>).

#include "MiniFootball.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#endif

class FMiniFootballModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		if (GIsEditor && !IsRunningCommandlet())
		{
			// Ждём полного запуска движка и редактора, затем импортируем (один раз)
			FCoreDelegates::OnPostEngineInit.AddLambda([]()
			{
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					ImportFootballerFbx();
					return false;
				}), 1.f);
			});
		}
#endif
	}

private:
#if WITH_EDITOR
	static void ImportFootballerFbx()
	{
		const FString SourceDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/Footballer"));
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(SourceDir / TEXT("*.fbx")), true, false);

		TArray<UAssetImportTask*> Tasks;
		for (const FString& File : Files)
		{
			// Каждый FBX — в свою подпапку, чтобы материалы и текстуры не конфликтовали по именам
			const FString BaseName = FPaths::GetBaseFilename(File);
			TArray<FString> Existing;
			IFileManager::Get().FindFilesRecursive(Existing,
				*(FPaths::ProjectContentDir() / TEXT("Characters/Footballer") / BaseName), TEXT("*.uasset"), true, false);
			if (Existing.Num() > 0)
			{
				continue; // уже импортирован
			}

			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->AddToRoot();
			Task->Filename = SourceDir / File;
			Task->DestinationPath = TEXT("/Game/Characters/Footballer/") + BaseName;
			Task->bAutomated = true;       // без диалогов, настройки импорта по умолчанию
			Task->bReplaceExisting = true;
			Task->bSave = true;            // сразу сохранить .uasset на диск
			Tasks.Add(Task);
		}
		if (Tasks.Num() == 0)
		{
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("MiniFootball: импорт %d FBX из %s"), Tasks.Num(), *SourceDir);
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		AssetTools.ImportAssetTasks(Tasks);

		for (UAssetImportTask* Task : Tasks)
		{
			UE_LOG(LogTemp, Log, TEXT("MiniFootball: %s -> создано ассетов: %d"), *Task->Filename, Task->ImportedObjectPaths.Num());
			Task->RemoveFromRoot();
		}
	}
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMiniFootballModule, MiniFootball, "MiniFootball");
