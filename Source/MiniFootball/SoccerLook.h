// Внешность футболистов: одно общее тело Mixamo + «карикатурные» ползунки.
//
//  1) Морф-таргеты (MF_Nose, MF_Eyes, ...) — генерируются кодом в редакторе прямо на импортированной модели
//     (см. MiniFootball.cpp). Каждый морф — гладкое поле смещений вокруг ориентира лица/тела,
//     одинаковое для всех частей меша (тело, футболка, волосы, ресницы), поэтому ничего не рвётся.
//  2) Масштаб костей (голова, толщина рук и ног) — в своём AnimInstance поверх анимации.
//
// Всё это только визуал: коллизия игрока — прежняя капсула, физика и геймплей не меняются.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "SoccerLook.generated.h"

class USkeletalMeshComponent;
class UIKRetargeter;

// Рецепт внешности (хранится в карточке игрока и в сохранении). Значения ползунков 0..1.
USTRUCT()
struct FSoccerLook
{
	GENERATED_BODY()

	UPROPERTY() int32 Seed = 0;         // 0 — рецепт ещё не создан
	UPROPERTY() float Head = 0.3f;      // размер головы: 0 — обычная, 1 — огромная
	UPROPERTY() float Fat = 0.f;        // полнота: живот, шея, толстые руки и ноги
	UPROPERTY() float Nose = 0.f;       // нос-«картошка»
	UPROPERTY() float NoseLong = 0.f;   // длинный свисающий нос
	UPROPERTY() float Eyes = 0.f;       // выпученные глаза
	UPROPERTY() float Cheeks = 0.f;     // щёки и второй подбородок
	UPROPERTY() float Ears = 0.f;       // уши-лопухи

	bool IsSet() const { return Seed != 0; }
	// Случайная внешность. Полнота зависит от характеристик: крепкий и медленный — чаще толстяк.
	static FSoccerLook Random(int32 InSeed, int32 Pace, int32 Physical);
};

namespace SoccerLook
{
	// Имена морф-таргетов (совпадают с шейп-кеями прототипа в Blender)
	extern const FName MorphNames[7];
	// Метка версии формы морфов: поменяйте номер — и в редакторе морфы пересоздадутся
	extern const FName MorphVersionTag;

	// Смещение вершины (в пространстве меша, см) для морфа Index. FaceSign — куда смотрит лицо по Y (±1).
	FVector3f MorphDelta(int32 Index, const FVector3f& P, float FaceSign);

	// Применить внешность к скелетному мешу (морфы + масштаб костей). Вызывать после смены меша.
	void Apply(USkeletalMeshComponent* SkelMesh, const FSoccerLook& Look);
}

// Масштаб кости поверх анимации
struct FSoccerBoneScale
{
	FName Bone;
	FVector Scale = FVector::OneVector; // масштаб самой кости (в её осях)
	bool bSubtree = false;              // true — масштаб наследуют дочерние кости (голова целиком)
};

// Прокси: проигрывает анимацию как обычный одиночный узел и затем масштабирует кости.
// Дочерние кости получают обратный масштаб, поэтому толстая рука не удлиняет кисть.
struct FSoccerAnimInstanceProxy : public FAnimSingleNodeInstanceProxy
{
	FSoccerAnimInstanceProxy() {}
	FSoccerAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimSingleNodeInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual void CacheBones() override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Motion Matching: the pose of a hidden Epic mannequin, retargeted onto this skeleton.
	// The single-node clip then plays only as an action layer (kick, trap...) on top.
	FAnimNode_RetargetPoseFromMesh Retarget;
	bool bRetargetReady = false;
	float ActionWeight = 0.f;
	uint8 ActionFoot = 0;          // 0 = the clip takes the whole body; 1/2 = left/right kick
	TArray<uint8> BoneGroup;       // per compact bone: 0 torso, 1 hips, 2 left leg, 3 right leg

	TArray<FSoccerBoneScale> BoneScales;
	bool bLockRootMotionXY = false;
	// Previous clip, faded out over the first frames of the new one.
	const UAnimSequenceBase* BlendFrom = nullptr;
	float BlendFromTime = 0.f;
	float BlendWeight = 0.f; // weight of the previous clip, 1 -> 0
};

// AnimInstance игрока: как стандартный Single Node (PlayAnimation/SetPlayRate работают), плюс масштаб костей.
UCLASS(transient)
class USoccerAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	TArray<FSoccerBoneScale> BoneScales; // заполняет SoccerLook::Apply
	bool bLockRootMotionXY = false;

	// Switch clip with a crossfade from the current one. Locomotion loops keep their
	// normalized phase, so Run <-> Sprint does not restart the stride.
	void CrossfadeTo(UAnimSequenceBase* NewClip, bool bLoop, float BlendTime, bool bKeepPhase);

	// Locomotion comes from Source through Retargeter; see FSoccerAnimInstanceProxy::Retarget.
	void SetMotionSource(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter);
	bool HasMotionSource() const { return MotionSource.IsValid(); }
	float ActionWeight = 0.f; // 0 = pure motion matching, 1 = pure action clip
	uint8 ActionFoot = 0;     // kick layered on the run: 1 left / 2 right leg + torso only

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(Transient) TObjectPtr<const UAnimSequenceBase> BlendFrom;
	float BlendFromTime = 0.f;
	float BlendFromRate = 1.f;
	bool bBlendFromLoop = true;
	float BlendElapsed = 0.f;
	float BlendDuration = 0.f;

	TWeakObjectPtr<USkeletalMeshComponent> MotionSource;
	UPROPERTY(Transient) TObjectPtr<UIKRetargeter> MotionRetargeter;

	friend struct FSoccerAnimInstanceProxy;

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
