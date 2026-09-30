#include "Soccer.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDogVisualIntegrationTest, "MiniFootball.Dogs.VisualIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDogVisualIntegrationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Dogs/SK_Dog_Cavapoo.SK_Dog_Cavapoo"));
	if (!TestNotNull(TEXT("Imported Cavapoo"), Mesh)) return false;
	AddInfo(FString::Printf(TEXT("Cavapoo root: %s, height: %.1f cm"),
		*Mesh->GetRefSkeleton().GetBoneName(0).ToString(), Mesh->GetBounds().BoxExtent.Z * 2.0));
	const UWorld::InitializationValues WorldOptions = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &WorldOptions);
	TArray<ASoccerPlayer*> Players;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ASoccerPlayer* Player = World->SpawnActor<ASoccerPlayer>(FVector(Index * 300.f, 0.f, 90.f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Player spawned"), Player)) continue;
		Players.Add(Player);
		const FLinearColor Color = Index % 2 ? FLinearColor::Blue : FLinearColor::Red;
		Player->Setup(Index % 2, Index >= 2, FVector(Index * 300.f, 0.f, 90.f), 0.f, FSoccerPlayerInfo(), Index, Color, FLinearColor::White);
		Player->ApplyCharacterModel(Mesh, nullptr, Mesh, nullptr);
		TestTrue(TEXT("Dog replaces placeholder"), Player->bDogModel && !Player->Body->IsVisible());
		TestEqual(TEXT("All clips imported on the shared skeleton"), Player->DogAnimations.Num(), 20);
		TestEqual(TEXT("Role-specific idle"), Player->CurrentAnim.Get(), Player->DogAnimations.FindRef(Index >= 2 ? TEXT("KeeperIdle") : TEXT("Idle")).Get());
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Player->GetMesh()->GetMaterial(0));
		if (TestNotNull(TEXT("Independent kit material"), Material))
			TestTrue(TEXT("Team color reaches shirt shader"), Material->K2_GetVectorParameterValue(TEXT("KitColor")).Equals(Color));
		Player->GetCharacterMovement()->Velocity = FVector(450.f, 0.f, 0.f);
		Player->UpdateDogAnimation();
		TestEqual(TEXT("Run selected"), Player->CurrentAnim.Get(), Player->DogAnimations.FindRef(TEXT("Run")).Get());
		Player->GetCharacterMovement()->Velocity = FVector(650.f, 0.f, 0.f);
		Player->UpdateDogAnimation();
		TestEqual(TEXT("Sprint selected"), Player->CurrentAnim.Get(), Player->DogAnimations.FindRef(TEXT("Sprint")).Get());
		for (const TCHAR* Action : {TEXT("Pass"), TEXT("Shot"), TEXT("Header"), TEXT("Slide"), TEXT("DiveLeft"), TEXT("DiveRight"), TEXT("CatchLow"), TEXT("CatchHigh")})
		{
			Player->PlayDogAction(Action, 0.6f);
			TestEqual(FString::Printf(TEXT("Action %s"), Action), Player->CurrentAnim.Get(), Player->DogAnimations.FindRef(Action).Get());
			TestFalse(TEXT("Action does not loop"), Player->GetMesh()->GetSingleNodeInstance()->IsLooping());
			Player->GetMesh()->SetPosition(Player->CurrentAnim->GetPlayLength() * 0.5f, false);
			Player->GetMesh()->TickAnimation(0.f, false);
			Player->GetMesh()->RefreshBoneTransforms();
			for (const FTransform& Bone : Player->GetMesh()->GetComponentSpaceTransforms())
				TestFalse(TEXT("Retargeted pose is finite"), Bone.ContainsNaN());
		}
		Player->DogActionEnd = -1.f;
		Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Player->UpdateDogAnimation();
		TestNull(TEXT("Completed action released"), Player->DogAction.Get());
		TestTrue(TEXT("Locomotion loops again"), Player->GetMesh()->GetSingleNodeInstance()->IsLooping());
		Player->PlayDogAction(TEXT("Slide"), 0.6f);
		Player->ResetToHome();
		Player->UpdateDogAnimation();
		TestNull(TEXT("Kickoff clears old action"), Player->DogAction.Get());
	}
	if (Players.Num() >= 2)
		TestTrue(TEXT("Team materials do not share mutable state"), Players[0]->GetMesh()->GetMaterial(0) != Players[1]->GetMesh()->GetMaterial(0));
	World->DestroyWorld(false);
	return true;
}
