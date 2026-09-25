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
#include "SoccerLook.generated.h"

class USkeletalMeshComponent;

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
	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FSoccerBoneScale> BoneScales;
};

// AnimInstance игрока: как стандартный Single Node (PlayAnimation/SetPlayRate работают), плюс масштаб костей.
UCLASS(transient)
class USoccerAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	TArray<FSoccerBoneScale> BoneScales; // заполняет SoccerLook::Apply

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
