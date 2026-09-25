#include "SoccerLook.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"

// ============================================================================
//  Рецепт внешности
// ============================================================================

FSoccerLook FSoccerLook::Random(int32 InSeed, int32 Pace, int32 Physical)
{
	FRandomStream R(InSeed);
	FSoccerLook L;
	L.Seed = InSeed != 0 ? InSeed : 1;
	// «Сила» черты: чаще умеренная, иногда — во всю мощь (как на мемах)
	auto Trait = [&R](float Chance, float Min = 0.35f)
	{
		return R.FRand() < Chance ? FMath::Lerp(Min, 1.f, FMath::Sqrt(R.FRand())) : 0.f;
	};
	L.Head = FMath::Lerp(0.15f, 0.8f, R.FRand());
	// Полнота: крепкие (ФИЗ) и медленные (СКР) — толще. Плюс случайность.
	const float StatFat = FMath::Clamp((Physical - Pace + 15) / 45.f, 0.f, 1.f);
	L.Fat = FMath::Clamp(StatFat * 0.8f + (R.FRand() < 0.25f ? R.FRand() * 0.6f : 0.f), 0.f, 1.f);
	// Нос: либо «картошка», либо длинный (редко оба)
	if (R.FRand() < 0.5f) { L.Nose = Trait(0.9f, 0.4f); L.NoseLong = Trait(0.15f); }
	else { L.NoseLong = Trait(0.7f); L.Nose = Trait(0.3f); }
	L.Eyes = Trait(0.6f);
	L.Cheeks = FMath::Clamp(Trait(0.4f) + L.Fat * 0.6f, 0.f, 1.f);
	L.Ears = Trait(0.35f);
	return L;
}

// ============================================================================
//  Поля деформации для морф-таргетов
// ============================================================================
// Ориентиры сняты с модели Mixamo Ch38 в Blender (метры, лицо смотрит в −Y) и переведены в
// пространство меша Unreal (см, Y перевёрнут). Прототип форм — сцена MF_Caricature в Blender.

namespace SoccerLook
{
	const FName MorphNames[7] = {
		TEXT("MF_Nose"), TEXT("MF_NoseLong"), TEXT("MF_Eyes"), TEXT("MF_Cheeks"),
		TEXT("MF_Ears"), TEXT("MF_Belly"), TEXT("MF_Neck")
	};
	const FName MorphVersionTag = TEXT("MF_Version_3");

	// Мягкий купол: 1 в центре, 0 на радиусе Rad и дальше
	static float Falloff(float Dist, float Rad)
	{
		const float T = Dist / Rad;
		return T >= 1.f ? 0.f : FMath::Square(1.f - T * T);
	}

	FVector3f MorphDelta(int32 Index, const FVector3f& P, float FaceSign)
	{
		// Точка из Blender (метры, −Y вперёд) → меш Unreal (см, лицо в сторону FaceSign по Y)
		auto B = [FaceSign](float X, float Y, float Z) { return FVector3f(X * 100.f, -Y * 100.f * FaceSign, Z * 100.f); };
		const FVector3f Fwd(0.f, FaceSign, 0.f);
		const FVector3f Up(0.f, 0.f, 1.f);
		FVector3f D = FVector3f::ZeroVector;

		switch (Index)
		{
		case 0: // нос-«картошка»: раздуть кончик носа вперёд
		{
			const FVector3f Tip = B(0.f, -0.13f, 1.613f), Base = B(0.f, -0.10f, 1.603f);
			D = ((P - Base).GetSafeNormal() * 4.f + Fwd * 4.f - Up * 0.8f) * Falloff(FVector3f::Dist(P, Tip), 5.f);
			break;
		}
		case 1: // длинный свисающий нос
		{
			const FVector3f Tip = B(0.f, -0.14f, 1.61f), Axis = B(0.f, -0.115f, 1.61f);
			const float K = Falloff(FVector3f::Dist(P, Tip), 4.5f);
			const float Along = FMath::Clamp((P.Y * FaceSign - 10.f) / 4.5f, 0.f, 1.f); // только передняя часть носа
			D = (Fwd * 8.f - Up * 4.5f + (P - Axis).GetSafeNormal() * 1.2f) * K * Along;
			break;
		}
		case 2: // выпученные глаза
			for (float S : { 1.f, -1.f })
			{
				const FVector3f Eye = B(0.0337f * S, -0.109f, 1.649f);
				const FVector3f Back = Eye - Fwd * 3.5f;
				D += ((P - Back).GetSafeNormal() * 2.8f + Fwd * 1.6f) * Falloff(FVector3f::Dist(P, Eye), 3.6f);
			}
			break;
		case 3: // щёки и второй подбородок
		{
			for (float S : { 1.f, -1.f })
			{
				const FVector3f Cheek = B(0.05f * S, -0.075f, 1.575f), Origin = B(0.015f * S, -0.02f, 1.60f);
				D += (P - Origin).GetSafeNormal() * 4.f * Falloff(FVector3f::Dist(P, Cheek), 6.f);
			}
			const FVector3f Chin = B(0.f, -0.085f, 1.535f);
			D += (Fwd * 3.f - Up * 3.f) * Falloff(FVector3f::Dist(P, Chin), 6.f);
			break;
		}
		case 4: // уши-лопухи
			for (float S : { 1.f, -1.f })
			{
				const FVector3f Ear = B(0.092f * S, -0.008f, 1.645f), Root = B(0.07f * S, -0.005f, 1.645f);
				D += ((P - Root).GetSafeNormal() * 2.5f + FVector3f(S * 2.f, 0.f, 0.f)) * Falloff(FVector3f::Dist(P, Ear), 4.f);
			}
			break;
		case 5: // живот: раздуть торс вокруг оси позвоночника, спереди сильнее
		{
			if (P.Z < 78.f || P.Z > 150.f) break;
			const FVector3f H(P.X, P.Y, 0.f);
			const float Rad = H.Size();
			if (Rad > 26.f || Rad < 0.01f) break;
			const FVector3f Dir = H / Rad;
			const float Front = FMath::Max(0.f, FVector3f::DotProduct(Dir, Fwd));
			const float Height = FMath::Exp(-FMath::Square((P.Z - 108.f) / 22.f));
			// Бока — умеренно (руки висят рядом), вперёд — пузо
			D = Dir * Height * (8.f + 20.f * Front * Front) * FMath::Pow(Falloff(Rad, 27.f), 0.3f);
			break;
		}
		case 6: // толстая шея
		{
			if (P.Z < 140.f || P.Z > 160.f) break;
			const FVector3f H = FVector3f(P.X, P.Y, 0.f) - FVector3f(0.f, -FaceSign, 0.f);
			const float Rad = H.Size();
			if (Rad > 12.f || Rad < 0.01f) break;
			D = H / Rad * 3.5f * FMath::Exp(-FMath::Square((P.Z - 150.f) / 5.f));
			break;
		}
		default: break;
		}
		return D;
	}

	// Масштаб в поперечных осях кости (вдоль кости — ось Y у скелета Mixamo): толщина без удлинения
	static FVector Girth(float G) { return FVector(G, 1.f, G); }

	void Apply(USkeletalMeshComponent* SkelMesh, const FSoccerLook& Look)
	{
		if (!SkelMesh) return;

		SkelMesh->SetMorphTarget(MorphNames[0], Look.Nose);
		SkelMesh->SetMorphTarget(MorphNames[1], Look.NoseLong);
		SkelMesh->SetMorphTarget(MorphNames[2], Look.Eyes);
		SkelMesh->SetMorphTarget(MorphNames[3], Look.Cheeks);
		SkelMesh->SetMorphTarget(MorphNames[4], Look.Ears);
		SkelMesh->SetMorphTarget(MorphNames[5], Look.Fat);
		SkelMesh->SetMorphTarget(MorphNames[6], Look.Fat);

		if (USoccerAnimInstance* Anim = Cast<USoccerAnimInstance>(SkelMesh->GetAnimInstance()))
		{
			const float HeadScale = 1.f + 0.45f * Look.Head;   // стилизация: крупная голова (камера далеко)
			const float Arm = 1.f + 0.45f * Look.Fat;
			const float Leg = 1.f + 0.35f * Look.Fat;
			Anim->BoneScales.Reset();
			Anim->BoneScales.Add({ TEXT("Head"), FVector(HeadScale), true });
			for (const TCHAR* Side : { TEXT("Left"), TEXT("Right") })
			{
				Anim->BoneScales.Add({ FName(FString(Side) + TEXT("Arm")), Girth(Arm), false });
				Anim->BoneScales.Add({ FName(FString(Side) + TEXT("ForeArm")), Girth(FMath::Lerp(1.f, Arm, 0.7f)), false });
				Anim->BoneScales.Add({ FName(FString(Side) + TEXT("UpLeg")), Girth(Leg), false });
				Anim->BoneScales.Add({ FName(FString(Side) + TEXT("Leg")), Girth(FMath::Lerp(1.f, Leg, 0.6f)), false });
			}
		}
	}
}

// ============================================================================
//  AnimInstance с масштабом костей
// ============================================================================

FAnimInstanceProxy* USoccerAnimInstance::CreateAnimInstanceProxy()
{
	return new FSoccerAnimInstanceProxy(this);
}

void USoccerAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSoccerAnimInstanceProxy*>(InProxy);
}

void FSoccerAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	BoneScales = CastChecked<USoccerAnimInstance>(InAnimInstance)->BoneScales; // копия для потока анимации
}

bool FSoccerAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const bool bResult = FAnimSingleNodeInstanceProxy::Evaluate(Output);
	if (BoneScales.Num() == 0) return bResult;

	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	const int32 Num = Pose.GetNumBones();

	// Итоговый масштаб каждой кости (Own) и что наследуют её дети (Pass)
	TArray<FVector, TInlineAllocator<96>> Own, Pass;
	Own.Init(FVector::OneVector, Num);
	Pass.Init(FVector::OneVector, Num);
	TArray<const FSoccerBoneScale*, TInlineAllocator<96>> ByBone;
	ByBone.Init(nullptr, Num);
	for (const FSoccerBoneScale& S : BoneScales)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(S.Bone);
		if (MeshIndex == INDEX_NONE) continue;
		const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Compact != INDEX_NONE) ByBone[Compact.GetInt()] = &S;
	}

	// Родители в компактной позе всегда идут раньше детей
	for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const int32 I = Index.GetInt();
		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index);
		const FVector Inherited = Parent != INDEX_NONE ? Pass[Parent.GetInt()] : FVector::OneVector;
		const FSoccerBoneScale* S = ByBone[I];
		Own[I] = S ? S->Scale : Inherited;
		Pass[I] = S ? (S->bSubtree ? S->Scale : FVector::OneVector) : Inherited;

		// Движок перемножает масштабы по компонентам: локальный = нужный / накопленный у родителя
		const FVector ParentOwn = Parent != INDEX_NONE ? Own[Parent.GetInt()] : FVector::OneVector;
		const FVector Local = Own[I] / ParentOwn;
		if (!Local.Equals(FVector::OneVector, 1e-4))
		{
			Pose[Index].SetScale3D(Pose[Index].GetScale3D() * Local);
		}
	}
	return bResult;
}
