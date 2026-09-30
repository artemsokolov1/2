#include "Soccer.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Retargeter/IKRetargeter.h"
#include "GameFramework/CharacterMovementComponent.h"

void ASoccerPlayer::ApplyDogModel(USkeletalMesh* DogMesh)
{
	if (!DogMesh || !DogMesh->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("Cavapoo has no skeleton. Run Scripts/import_dogs.py to complete the import."));
		return;
	}
	bDogModel = true;
	IdleMesh = RunMesh = DogMesh;
	USkeleton* Skeleton = DogMesh->GetSkeleton();
	// Keep the dog's proportions when playing clips authored on another Mixamo body.
	Skeleton->SetBoneTranslationRetargetingMode(0, EBoneTranslationRetargetingMode::Skeleton, true);
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		if (Ref.GetBoneName(Index).ToString().EndsWith(TEXT("Hips")))
			Skeleton->SetBoneTranslationRetargetingMode(Index, EBoneTranslationRetargetingMode::AnimationScaled);
	}
	for (const TCHAR* Name : {TEXT("Idle"), TEXT("Run"), TEXT("Sprint"), TEXT("Pass"), TEXT("Shot"),
		TEXT("Header"), TEXT("Tackle"), TEXT("Slide"), TEXT("Receive"), TEXT("KeeperIdle"),
		TEXT("KeeperLeft"), TEXT("KeeperRight"), TEXT("DiveLeft"), TEXT("DiveRight"),
		TEXT("CatchLow"), TEXT("CatchMid"), TEXT("CatchHigh"), TEXT("KeeperPass"),
		TEXT("KeeperThrow"), TEXT("KeeperKick"), TEXT("KeeperPlace"), TEXT("KeeperDirect"), TEXT("KeeperMiss"),
		TEXT("KeeperBlock"), TEXT("KeeperBlock2"), TEXT("StandUp"), TEXT("Volley")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Characters/Dogs/Animations/A_Dog_%s.A_Dog_%s"), Name, Name);
		UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
		if (Clip && Clip->GetSkeleton() == Skeleton) DogAnimations.Add(FName(Name), Clip);
		else UE_LOG(LogTemp, Warning, TEXT("Dog animation missing or incompatible: %s"), *Path);
	}
	// Actions played as a window of a longer take: [Start, End] seconds at Rate, with the
	// foot-ball contact Contact seconds into the window (0 = no ball contact).
	// Mocap: Anderson Rohr's free soccer pack (Scripts/import_mocap_soccer.py).
	// Foot: the kicking foot in the take (1 left, 2 right), measured on the clips.
	struct FWindowedAction { const TCHAR* Name; const TCHAR* Path; float Start; float End; float Contact; float Rate; uint8 Foot; };
	for (const FWindowedAction& W : {
		FWindowedAction{TEXT("Pass"), TEXT("Mocap/A_Dog_M08_Side_Foot_Kick"), 1.19f, 1.86f, 0.22f, 1.f, 2},
		FWindowedAction{TEXT("Shot"), TEXT("Mocap/A_Dog_M09_Power_Kick"), 2.33f, 3.11f, 0.28f, 1.f, 1},
		// Fake shot: the same wind-up, cut just before the foot reaches the ball.
		FWindowedAction{TEXT("FakeShot"), TEXT("Mocap/A_Dog_M09_Power_Kick"), 2.33f, 2.58f, 0.f, 1.f, 1},
		// Slide: run-in, 2 m slide on the grass, get up (Mixamo "soccer tackle (2)").
		FWindowedAction{TEXT("Slide"), TEXT("A_Dog_Slide"), 0.15f, 1.77f, 0.f, 1.5f, 0},
		// Brought down by a tackle: trip, fall and lie (Mixamo "soccer trip")
		FWindowedAction{TEXT("Fall"), TEXT("A_Dog_Fall"), 0.f, 1.57f, 0.f, 1.f, 0}})
	{
		const FString Asset = FPaths::GetBaseFilename(W.Path);
		const FString Path = FString::Printf(TEXT("/Game/Characters/Dogs/Animations/%s.%s"), W.Path, *Asset);
		UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Clip || Clip->GetSkeleton() != Skeleton) continue;
		DogAnimations.Add(FName(W.Name), Clip);
		DogClipWindows.Add(FName(W.Name), FVector2f(W.Start, W.End));
		DogClipRate.Add(FName(W.Name), W.Rate);
		if (W.Contact > 0.f) DogClipContact.Add(FName(W.Name), W.Contact);
		if (W.Foot) DogClipFoot.Add(FName(W.Name), W.Foot);
	}
	USkeletalMeshComponent* Visual = GetMesh();
	Visual->SetSkeletalMeshAsset(DogMesh);
	bGiraffeModel = DogMesh->GetPathName().StartsWith(TEXT("/Game/Characters/Giraffe/"));
	// The dog is normalized to the 180 cm gameplay capsule, with clearance for the paws in
	// the running pose. The giraffe is built at its game size (1.88 m to the ossicones).
	const FBoxSphereBounds Bounds = DogMesh->GetBounds();
	const float Scale = bGiraffeModel ? 1.f : 180.f / FMath::Max(1.f, float(Bounds.BoxExtent.Z * 2.f));
	const float Clearance = bGiraffeModel ? 0.f : 8.f;
	Visual->SetRelativeScale3D(FVector(Scale));
	Visual->SetRelativeLocationAndRotation(
		FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale + Clearance),
		FRotator(0.f, MeshYawOffset, 0.f));
	Visual->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Visual->SetAnimInstanceClass(USoccerAnimInstance::StaticClass());
	if (USoccerAnimInstance* Anim = Cast<USoccerAnimInstance>(Visual->GetAnimInstance()))
		Anim->bLockRootMotionXY = true;
	if (UMaterialInterface* Kit = LoadObject<UMaterialInterface>(nullptr, bGiraffeModel
		? TEXT("/Game/Characters/Giraffe/MI_Giraffe_Kit.MI_Giraffe_Kit") : TEXT("/Game/Characters/Dogs/M_Dog_Kit.M_Dog_Kit")))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Kit, this);
		Material->SetVectorParameterValue(TEXT("KitColor"), ShirtColor);
		for (int32 Slot = 0; Slot < Visual->GetNumMaterials(); ++Slot) Visual->SetMaterial(Slot, Material);
	}
	Body->SetVisibility(false);
	Top->SetVisibility(false);
	Feet->SetVisibility(false);
	Head->SetVisibility(false);
	Ring->SetVisibility(false); // no discs under the players: teams read from the kits
	if (GM() && GM()->IsFreeTraining())
	{
		// Goals-style control marker: a small dot above the head instead of the big cone.
		if (UStaticMesh* Dot = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		{
			Marker->SetStaticMesh(Dot);
			Marker->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 128.f), FRotator::ZeroRotator);
			Marker->SetRelativeScale3D(FVector(0.11f));
		}
	}
	CurrentAnim = nullptr;
	DogAction = nullptr;
	SetupMotionMatching();
	UpdateDogAnimation();
}

void ASoccerPlayer::SetupMotionMatching()
{
	// Needs the assets migrated from Epic's Game Animation Sample (Scripts/migrate_gasp.py)
	// and BP_SoccerDog, which implements the pawn interface the animation blueprint reads.
	UClass* PawnInterface = LoadClass<UInterface>(nullptr, TEXT("/Game/Blueprints/BPI_SandboxCharacter_Pawn.BPI_SandboxCharacter_Pawn_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	USkeletalMesh* Mannequin = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UClass* MotionAnim = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Blueprints/SandboxCharacter_CMC_ABP.SandboxCharacter_CMC_ABP_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, bGiraffeModel
		? TEXT("/Game/Characters/Giraffe/Retarget/RTG_UEFN_to_Giraffe.RTG_UEFN_to_Giraffe")
		: TEXT("/Game/Characters/Dogs/Retarget/RTG_UEFN_to_Dog.RTG_UEFN_to_Dog"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	USoccerAnimInstance* Anim = Cast<USoccerAnimInstance>(GetMesh()->GetAnimInstance());
	if (!PawnInterface || !GetClass()->ImplementsInterface(PawnInterface) || !Mannequin || !MotionAnim || !Retargeter || !Anim)
	{
		UE_LOG(LogTemp, Display, TEXT("Motion matching off for %s (interface=%d mannequin=%d abp=%d retargeter=%d)"), *GetName(),
			PawnInterface && GetClass()->ImplementsInterface(PawnInterface), Mannequin != nullptr, MotionAnim != nullptr, Retargeter != nullptr);
		return;
	}
	if (!MotionBody)
	{
		MotionBody = NewObject<USkeletalMeshComponent>(this, TEXT("MotionBody"));
		MotionBody->SetupAttachment(GetCapsuleComponent());
		MotionBody->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()), FRotator(0.f, -90.f, 0.f));
		MotionBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MotionBody->SetCastShadow(false);
		MotionBody->SetVisibility(false);
		// Hidden, but it must keep animating: the dog reads its pose every frame.
		MotionBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		MotionBody->SetSkeletalMeshAsset(Mannequin);
		MotionBody->SetAnimInstanceClass(MotionAnim);
		MotionBody->RegisterComponent();
	}
	GetMesh()->AddTickPrerequisiteComponent(MotionBody);
	Anim->SetMotionSource(MotionBody, Retargeter);
	UE_LOG(LogTemp, Display, TEXT("Motion matching on for %s"), *GetName());
}

FVector ASoccerPlayer::GetBodyCenter() const
{
	FVector Center = GetActorLocation();
	if (bDogModel && GetMesh())
	{
		const FVector Hips = GetMesh()->GetSocketLocation(TEXT("Hips"));
		Center.X = Hips.X;
		Center.Y = Hips.Y;
	}
	return Center;
}

void ASoccerPlayer::UpdateBodyYaw(float Dt)
{
	const USkeletalMeshComponent* Visual = GetMesh();
	if (!bDogModel || !Visual) return;
	// Hips and shoulders counter-rotate in the stride; their sum points where the torso goes.
	const FVector Hips = Visual->GetSocketLocation(TEXT("LeftUpLeg")) - Visual->GetSocketLocation(TEXT("RightUpLeg"));
	const FVector Shoulders = Visual->GetSocketLocation(TEXT("LeftShoulder")) - Visual->GetSocketLocation(TEXT("RightShoulder"));
	const FVector Left = (Hips.GetSafeNormal2D() + Shoulders.GetSafeNormal2D()).GetSafeNormal2D();
	if (Left.IsNearlyZero()) return;
	const float Target = FVector::CrossProduct(FVector::UpVector, Left).Rotation().Yaw; // up x left = forward
	if (!bBodyYawValid)
	{
		BodyYaw = Target;
		bBodyYawValid = true;
		return;
	}
	BodyYaw += FMath::FindDeltaAngleDegrees(BodyYaw, Target) * (1.f - FMath::Exp(-10.f * Dt));
}

FVector ASoccerPlayer::GetBodyForward() const
{
	return bBodyYawValid ? FRotator(0.f, BodyYaw, 0.f).Vector() : GetActorForwardVector().GetSafeNormal2D();
}

void ASoccerPlayer::KickAtContact(const FSoccerKick& Plan, FName Action)
{
	const float* Found = DogClipContact.Find(Action);
	float ContactTime = Found ? *Found / FMath::Max(0.1f, DogClipRate.FindRef(Action) > 0.f ? DogClipRate.FindRef(Action) : 1.f) : 0.f;
	// The kick variant may have shortened the wind-up (PlayDogAction).
	if (Found && Action == LastActionName && LastActionContact >= 0.f) ContactTime = LastActionContact;
	const float* Contact = Found ? &ContactTime : nullptr;
	if (!bDogModel || !Contact || *Contact <= 0.02f)
	{
		ExecuteKick(Plan);
		return;
	}
	// The ball stays at the paws through the short wind-up and leaves on contact (Tick).
	DelayedKick = Plan;
	DelayedKickTime = *Contact;
}

bool ASoccerPlayer::WantsSprintForAnimation() const
{
	return bSprinting || GetVelocity().Size2D() > RunSpeed * 1.08f;
}

void ASoccerPlayer::PlayDogAction(FName Action, float Duration)
{
	if (!bDogModel) return;
	const TObjectPtr<UAnimSequence>* Found = DogAnimations.Find(Action);
	if (!Found || !*Found) return;
	DogAction = *Found;
	LastActionName = Action;
	LastActionContact = -1.f;
	ActionFoot = 0;
	const bool bKick = Action == TEXT("Pass") || Action == TEXT("Shot") || Action == TEXT("FakeShot");
	const int32 Variant = SoccerVariants::Get(SoccerVariants::KickOnRun);
	if (const FVector2f* Window = DogClipWindows.Find(Action))
	{
		// Mocap window: real speed, starting just before the contact frame.
		DogActionStart = Window->X;
		DogActionRate = DogClipRate.FindRef(Action) > 0.f ? DogClipRate.FindRef(Action) : 1.f;
		const float* Contact = DogClipContact.Find(Action);
		if (bKick && Contact)
		{
			// Kick while running, variant A/B/C (SoccerVariants::KickOnRun)
			const float ContactClip = Window->X + *Contact;
			if (Variant == 1 && Action == TEXT("Shot"))
			{
				// B for a shot: the whole backswing (it starts 0.27 s before contact), played
				// faster. Cut to 0.1 s the leg barely went back and it read as a soft cross.
				DogActionStart = FMath::Max(Window->X, ContactClip - 0.24f);
				DogActionRate *= 1.5f; // whippy: the leg snaps through
			}
			else if (Variant == 1) DogActionStart = FMath::Max(Window->X, ContactClip - 0.1f); // B: short wind-up
			if (Variant == 2) DogActionStart = ContactClip;                              // C: follow-through only
			LastActionContact = (ContactClip - DogActionStart) / DogActionRate;
		}
		Duration = (Window->Y - DogActionStart) / DogActionRate;
	}
	else
	{
		DogActionStart = 0.f;
		const float Length = FMath::Max(0.05f, DogAction->GetPlayLength());
		if (Duration <= 0.f) Duration = Length;
		DogActionRate = Length / FMath::Max(0.05f, Duration);
	}
	if (bKick)
	{
		// A and C: only the kicking leg and the torso take the clip, the run goes on under
		// them. B: the whole body kicks, with a short dip in speed for the plant.
		if (Variant != 1) ActionFoot = DogClipFoot.FindRef(Action);
		else
		{
			KickSlowTime = 0.2f;
			KickSlowSpeed = FMath::Max(250.f, GetVelocity().Size2D() * 0.8f);
		}
	}
	DogActionEnd = GetWorld()->GetTimeSeconds() + Duration;
	CurrentAnim = nullptr; // Repeated kicks restart the clip, even when it is the same action.
	UpdateDogAnimation();
}

void ASoccerPlayer::UpdateDogAnimation()
{
	UpdateBodyYaw(GetWorld()->GetDeltaSeconds());
	if (MotionBody)
	{
		// Locomotion is motion matched; clips only play as actions faded over it.
		if (DogAction && GetWorld()->GetTimeSeconds() >= DogActionEnd) DogAction = nullptr;
		const float Dt = GetWorld()->GetDeltaSeconds();
		ActionWeight = FMath::FInterpConstantTo(ActionWeight, DogAction ? 1.f : 0.f, Dt, DogAction ? 10.f : 5.f);
		if (DogAction && DogAction != CurrentAnim)
		{
			GetMesh()->SetAnimation(DogAction);
			GetMesh()->Play(false);
			GetMesh()->SetPosition(DogActionStart, false);
			CurrentAnim = DogAction;
		}
		if (!DogAction && ActionWeight <= 0.f) CurrentAnim = nullptr;
		GetMesh()->SetPlayRate(DogActionRate);
		if (USoccerAnimInstance* Anim = Cast<USoccerAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			Anim->ActionWeight = ActionWeight;
			Anim->ActionFoot = DogAction ? ActionFoot : Anim->ActionFoot; // keep the mask while fading out
		}
		return;
	}

	auto Clip = [this](FName Name) -> UAnimSequence*
	{
		const TObjectPtr<UAnimSequence>* Found = DogAnimations.Find(Name);
		return Found ? Found->Get() : nullptr;
	};
	if (DogAction && GetWorld()->GetTimeSeconds() >= DogActionEnd) DogAction = nullptr;
	UAnimSequence* Want = DogAction;
	float Rate = DogActionRate;
	const bool bLoop = !DogAction;
	if (!Want)
	{
		const float Speed = GetVelocity().Size2D();
		// Wider hysteresis keeps a wobbly character velocity from rapidly
		// restarting the idle/run clips around one threshold.
		const bool bWasIdle = CurrentAnim == Clip(TEXT("Idle")) || CurrentAnim == Clip(TEXT("KeeperIdle"));
		const bool bMoving = Speed > (bWasIdle ? 115.f : 65.f);
		FName Name = bGoalkeeper ? TEXT("KeeperIdle") : TEXT("Idle");
		Rate = 1.f;
		if (bMoving && DivePoseTime <= 0.f && SlidePoseTime <= 0.f)
		{
			const bool bWasSprinting = CurrentAnim == Clip(TEXT("Sprint"));
			const bool bSprint = bWasSprinting ? Speed > 450.f : Speed > 560.f;
			Name = bSprint ? TEXT("Sprint") : TEXT("Run");
			float ReferenceSpeed = bSprint ? 520.f : 400.f;
			const FVector LocalVelocity = GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
			if (bGoalkeeper && FMath::Abs(LocalVelocity.Y) > FMath::Abs(LocalVelocity.X))
			{
				Name = LocalVelocity.Y < 0.f ? TEXT("KeeperLeft") : TEXT("KeeperRight");
				ReferenceSpeed = 220.f;
			}
			Rate = FMath::Clamp(Speed / ReferenceSpeed, 0.8f, 1.35f);
		}
		Want = Clip(Name);
		if (!Want) Want = Clip(TEXT("Idle"));
	}
	if (Want && Want != CurrentAnim)
	{
		USoccerAnimInstance* Anim = Cast<USoccerAnimInstance>(GetMesh()->GetAnimInstance());
		if (Anim && CurrentAnim)
		{
			// Kicks cut in quickly so the contact reads; everything else fades.
			const bool bLocomotion = bLoop && (Want == Clip(TEXT("Run")) || Want == Clip(TEXT("Sprint")));
			const bool bWasLocomotion = CurrentAnim == Clip(TEXT("Run")) || CurrentAnim == Clip(TEXT("Sprint"));
			Anim->CrossfadeTo(Want, bLoop, bLoop ? 0.2f : 0.08f, bLocomotion && bWasLocomotion);
		}
		else
		{
			GetMesh()->SetAnimation(Want);
			GetMesh()->Play(bLoop);
		}
		CurrentAnim = Want;
	}
	GetMesh()->SetPlayRate(Rate);
}
