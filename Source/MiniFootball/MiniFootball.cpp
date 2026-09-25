// Основной модуль игры MiniFootball. Вся логика игры — в Soccer.h / Soccer.cpp / SoccerUI.cpp.
//
// Только в редакторе: при запуске модуль один раз импортирует 3D-модель футболиста и анимации
// из SourceArt/Footballer/*.fbx в Content/Characters/Footballer/<ИмяФайла>/ — вручную ничего
// импортировать не нужно. Если папка с импортом уже есть, файл пропускается
// (чтобы переимпортировать — удалите Content/Characters/Footballer/<ИмяФайла>).
//
// Затем на всех моделях футболиста создаются «карикатурные» морф-таргеты (нос, глаза, щёки, уши,
// живот, шея) — по формулам из SoccerLook.cpp. Повторно — только если сменилась версия форм.

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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "SoccerLook.h"
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
			FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					ImportFootballerFbx();
					BuildCaricatureMorphs();
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

	// Морф-таргеты карикатуры: пишем смещения вершин в MeshDescription и пересобираем меш
	static void BuildCaricatureMorphs()
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.ScanPathsSynchronous({ TEXT("/Game/Characters/Footballer") }, true);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(TEXT("/Game/Characters/Footballer"), Assets, true);

		TArray<UPackage*> ToSave;
		for (const FAssetData& Data : Assets)
		{
			USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Data.GetAsset());
			FMeshDescription* Desc = SkelMesh ? SkelMesh->GetMeshDescription(0) : nullptr;
			if (!Desc) continue;

			FSkeletalMeshAttributes Attributes(*Desc);
			if (Attributes.GetMorphTargetNames().Contains(SoccerLook::MorphVersionTag))
			{
				continue; // морфы этой версии уже есть
			}

			// Куда смотрит лицо: кончик носа — самая выступающая по Y точка на средней линии головы
			TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
			float NoseY = 0.f;
			for (const FVertexID Vertex : Desc->Vertices().GetElementIDs())
			{
				const FVector3f& P = Positions[Vertex];
				if (FMath::Abs(P.X) < 1.5f && P.Z > 155.f && P.Z < 167.f && FMath::Abs(P.Y) > FMath::Abs(NoseY))
				{
					NoseY = P.Y;
				}
			}
			if (FMath::Abs(NoseY) < 8.f)
			{
				UE_LOG(LogTemp, Warning, TEXT("MiniFootball: %s — не похоже на модель Ch38 (нос не найден), морфы не созданы"),
				       *SkelMesh->GetName());
				continue;
			}
			const float FaceSign = NoseY > 0.f ? 1.f : -1.f;

			for (const FName& Old : Attributes.GetMorphTargetNames())
			{
				if (Old.ToString().StartsWith(TEXT("MF_"))) Attributes.UnregisterMorphTargetAttribute(Old);
			}
			// Нормали: у модели они импортированы из FBX, поэтому движок берёт нормали морфа из атрибута.
			// Считаем их сами — поворачиваем исходную нормаль вершины так же, как повернулась сглаженная
			// нормаль поверхности после смещения (жёсткие рёбра сохраняются).
			TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
			const int32 VertexCount = Desc->Vertices().GetArraySize();
			TArray<FVector3f> Moved, BaseNormal, MovedNormal;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(SoccerLook::MorphNames); ++Index)
			{
				const FName MorphName = SoccerLook::MorphNames[Index];
				Attributes.RegisterMorphTargetAttribute(MorphName, true);
				TVertexAttributesRef<FVector3f> Deltas = Attributes.GetVertexMorphPositionDelta(MorphName);
				TVertexInstanceAttributesRef<FVector3f> NormalDeltas = Attributes.GetVertexInstanceMorphNormalDelta(MorphName);

				Moved.SetNumZeroed(VertexCount);
				for (const FVertexID Vertex : Desc->Vertices().GetElementIDs())
				{
					const FVector3f Delta = SoccerLook::MorphDelta(Index, Positions[Vertex], FaceSign);
					Deltas[Vertex] = Delta;
					Moved[Vertex.GetValue()] = Positions[Vertex] + Delta;
				}

				BaseNormal.SetNumZeroed(VertexCount);
				MovedNormal.SetNumZeroed(VertexCount);
				for (const FTriangleID Triangle : Desc->Triangles().GetElementIDs())
				{
					const TArrayView<const FVertexID> Corners = Desc->GetTriangleVertices(Triangle);
					const FVector3f& A = Positions[Corners[0]];
					const FVector3f FaceBase = FVector3f::CrossProduct(Positions[Corners[1]] - A, Positions[Corners[2]] - A);
					const FVector3f& MA = Moved[Corners[0].GetValue()];
					const FVector3f FaceMoved = FVector3f::CrossProduct(Moved[Corners[1].GetValue()] - MA, Moved[Corners[2].GetValue()] - MA);
					for (const FVertexID Corner : Corners)
					{
						BaseNormal[Corner.GetValue()] += FaceBase;
						MovedNormal[Corner.GetValue()] += FaceMoved;
					}
				}

				for (const FVertexInstanceID Instance : Desc->VertexInstances().GetElementIDs())
				{
					const FVertexID Vertex = Desc->GetVertexInstanceVertex(Instance);
					FVector3f NormalDelta = FVector3f::ZeroVector;
					if (!Deltas[Vertex].IsNearlyZero(1e-4f))
					{
						const FVector3f From = BaseNormal[Vertex.GetValue()].GetSafeNormal();
						const FVector3f To = MovedNormal[Vertex.GetValue()].GetSafeNormal();
						if (!From.IsZero() && !To.IsZero())
						{
							const FVector3f Rotated = FQuat4f::FindBetweenNormals(From, To).RotateVector(Normals[Instance]);
							NormalDelta = Rotated - Normals[Instance];
						}
					}
					NormalDeltas[Instance] = NormalDelta;
				}
			}
			Attributes.RegisterMorphTargetAttribute(SoccerLook::MorphVersionTag, true); // пустая метка версии

			SkelMesh->CommitMeshDescription(0);
			SkelMesh->Build();
			SkelMesh->PostEditChange();
			SkelMesh->MarkPackageDirty();
			ToSave.Add(SkelMesh->GetPackage());
			UE_LOG(LogTemp, Log, TEXT("MiniFootball: %s — созданы морфы карикатуры (лицо в сторону %sY)"),
			       *SkelMesh->GetName(), FaceSign > 0.f ? TEXT("+") : TEXT("-"));
		}
		if (ToSave.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, true);
		}
	}
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMiniFootballModule, MiniFootball, "MiniFootball");
