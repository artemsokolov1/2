// Мини-футбол 5×5 — геймплей: мяч, ворота, игроки и ИИ, управление, режим игры, сохранения.
// Интерфейс (меню, HUD матча, пауза) — в SoccerUI.cpp. Объявления — в Soccer.h.

#include "Soccer.h"

#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "Widgets/SWidget.h"

using namespace Soccer;

// ----------------------------------------------------------------------------
//  Вспомогательное
// ----------------------------------------------------------------------------

// Пути к простым мешам движка (есть в любом проекте, ничего импортировать не нужно)
#define MESH_CUBE     TEXT("/Engine/BasicShapes/Cube.Cube")
#define MESH_SPHERE   TEXT("/Engine/BasicShapes/Sphere.Sphere")
#define MESH_CYLINDER TEXT("/Engine/BasicShapes/Cylinder.Cylinder")
#define MESH_CONE     TEXT("/Engine/BasicShapes/Cone.Cone")

static const TCHAR* SaveSlot = TEXT("MiniFootball");

// Покрасить меш: динамический экземпляр стандартного материала BasicShapeMaterial,
// у которого есть векторный параметр "Color".
static void Paint(UStaticMeshComponent* Comp, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Comp || !Base) return;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Comp);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	Comp->SetMaterial(0, MID);
}

// Создать в конструкторе визуальный меш-компонент без коллизии.
static UStaticMeshComponent* MakeVisual(AActor* Owner, USceneComponent* Parent, FName Name, UStaticMesh* Mesh,
                                        const FVector& Loc, const FVector& Scale, const FRotator& Rot = FRotator::ZeroRotator)
{
	UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return C;
}

// Точка внутри поля (с отступом от линий)
static FVector ClampToField(FVector P, float Margin = 60.f)
{
	P.X = FMath::Clamp<double>(P.X, -HalfLength + Margin, HalfLength - Margin);
	P.Y = FMath::Clamp<double>(P.Y, -HalfWidth + Margin, HalfWidth - Margin);
	return P;
}

// ----------------------------------------------------------------------------
//  Справочники: формы, клубы, имена
// ----------------------------------------------------------------------------

const TArray<FSoccerKit>& GetSoccerKits()
{
	static const TArray<FSoccerKit> Kits = {
		{ TEXT("Красная"),       FLinearColor(0.75f, 0.03f, 0.03f),  FLinearColor(0.9f, 0.9f, 0.9f),    0    },
		{ TEXT("Белая"),         FLinearColor(0.85f, 0.85f, 0.85f),  FLinearColor(0.05f, 0.05f, 0.08f), 1000 },
		{ TEXT("Зелёная"),       FLinearColor(0.03f, 0.4f, 0.1f),    FLinearColor(0.9f, 0.9f, 0.9f),    1200 },
		{ TEXT("Чёрно-золотая"), FLinearColor(0.02f, 0.02f, 0.025f), FLinearColor(0.75f, 0.55f, 0.08f), 1500 },
		{ TEXT("Фиолетовая"),    FLinearColor(0.25f, 0.03f, 0.45f),  FLinearColor(0.05f, 0.05f, 0.05f), 2000 },
	};
	return Kits;
}

const TArray<FString>& GetClubNames()
{
	static const TArray<FString> Names = {
		TEXT("ФК Метеор"), TEXT("Спартак-Юг"), TEXT("Звезда"), TEXT("Торпедо"), TEXT("Легион"), TEXT("Вымпел")
	};
	return Names;
}

static const TArray<FString>& RivalClubNames()
{
	static const TArray<FString> Names = {
		TEXT("Буревестник"), TEXT("Ракета"), TEXT("Атлант"), TEXT("Факел"), TEXT("Прибой"), TEXT("Сокол")
	};
	return Names;
}

static const TArray<FString>& SurnamePool()
{
	static const TArray<FString> Names = {
		TEXT("Бойко"), TEXT("Мельник"), TEXT("Кравец"), TEXT("Зайцев"), TEXT("Титов"), TEXT("Панов"),
		TEXT("Белов"), TEXT("Шестаков"), TEXT("Фомин"), TEXT("Жуков"), TEXT("Родин"), TEXT("Карпов"),
		TEXT("Гусев"), TEXT("Носов"), TEXT("Яшин"), TEXT("Швецов"), TEXT("Демин"), TEXT("Соловьёв")
	};
	return Names;
}

static FSoccerPlayerInfo MakeInfo(const TCHAR* Name, const TCHAR* Pos,
                                  int32 Pac, int32 Sho, int32 Pas, int32 Dri, int32 Def, int32 Phy)
{
	FSoccerPlayerInfo I;
	I.Name = Name;
	I.Position = Pos;
	I.Pace = Pac; I.Shooting = Sho; I.Passing = Pas; I.Dribbling = Dri; I.Defending = Def; I.Physical = Phy;
	return I;
}

// Стартовый состав нового игрока
static TArray<FSoccerPlayerInfo> DefaultSquad()
{
	return {
		MakeInfo(TEXT("Громов"),  TEXT("ВР"), 60, 40, 58, 45, 78, 75),
		MakeInfo(TEXT("Ветров"),  TEXT("ЗЩ"), 72, 50, 64, 58, 76, 74),
		MakeInfo(TEXT("Лебедев"), TEXT("ЗЩ"), 70, 48, 62, 60, 74, 78),
		MakeInfo(TEXT("Орлов"),   TEXT("НП"), 84, 76, 70, 78, 40, 64),
		MakeInfo(TEXT("Корнеев"), TEXT("НП"), 80, 72, 74, 80, 38, 60),
	};
}

int32 FSoccerPlayerInfo::Rating() const
{
	if (Position == TEXT("ВР")) return (Defending * 3 + Physical * 2 + Pace + Passing) / 7;
	if (Position == TEXT("ЗЩ")) return (Pace + Passing + Defending * 3 + Physical * 2) / 7;
	return (Pace + Shooting * 2 + Passing + Dribbling * 2 + Physical) / 7;
}

int32& FSoccerPlayerInfo::Stat(int32 Index)
{
	switch (Index)
	{
	case 0:  return Pace;
	case 1:  return Shooting;
	case 2:  return Passing;
	case 3:  return Dribbling;
	case 4:  return Defending;
	default: return Physical;
	}
}

int32 FSoccerPlayerInfo::Stat(int32 Index) const
{
	return const_cast<FSoccerPlayerInfo*>(this)->Stat(Index);
}

const TCHAR* FSoccerPlayerInfo::StatLabel(int32 Index)
{
	static const TCHAR* Labels[6] = { TEXT("СКР"), TEXT("УДР"), TEXT("ПАС"), TEXT("ДРБ"), TEXT("ЗАЩ"), TEXT("ФИЗ") };
	return Labels[FMath::Clamp(Index, 0, 5)];
}

// ============================================================================
//  МЯЧ
// ============================================================================

ASoccerBall::ASoccerBall()
{
	PrimaryActorTick.bCanEverTick = true;

	// Сфера-коллизия нужна только для оверлапа с триггерами ворот.
	// Движение считаем сами (аркадная физика), физику движка не включаем.
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(BallRadius);
	Collision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Collision->SetGenerateOverlapEvents(true);
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(MESH_SPHERE);
	// Меш сферы движка — 100 см в диаметре
	Mesh = MakeVisual(this, Collision, TEXT("Mesh"), SphereMesh.Object, FVector::ZeroVector,
	                  FVector(BallRadius * 2.f / 100.f));
}

void ASoccerBall::BeginPlay()
{
	Super::BeginPlay();
	Paint(Mesh, FLinearColor::White);
}

void ASoccerBall::Kick(ASoccerPlayer* Kicker, const FVector& NewVelocity, const FVector& Curve, float CurveDuration)
{
	OwnerPlayer = nullptr;
	IntendedReceiver = nullptr;
	LastKicker = Kicker;
	LastKickTime = GetWorld()->GetTimeSeconds();
	Velocity = NewVelocity;
	CurveAccel = Curve;
	CurveTimeLeft = CurveDuration;
}

void ASoccerBall::SetOwnerPlayer(ASoccerPlayer* NewOwner)
{
	OwnerPlayer = NewOwner;
	if (NewOwner)
	{
		IntendedReceiver = nullptr;
		CurveTimeLeft = 0.f;
		Velocity = FVector::ZeroVector;
	}
	// при потере владельца мяч сохраняет текущую скорость и катится дальше
}

void ASoccerBall::ResetBall(const FVector& Location)
{
	SetActorLocation(Location);
	Velocity = FVector::ZeroVector;
	OwnerPlayer = nullptr;
	LastKicker = nullptr;
	IntendedReceiver = nullptr;
	CurveTimeLeft = 0.f;
}

float ASoccerBall::TimeSinceKick() const
{
	return GetWorld()->GetTimeSeconds() - LastKickTime;
}

void ASoccerBall::Tick(float Dt)
{
	Super::Tick(Dt);

	const FVector OldP = GetActorLocation();
	FVector P = OldP;

	if (OwnerPlayer)
	{
		// ДРИБЛИНГ: мяч «прилипает» к ногам владельца, чуть впереди.
		// При укрывании мяча корпусом он держится ближе к игроку.
		const float Dist = OwnerPlayer->IsShielding() ? 40.f : 62.f;
		FVector Target = OwnerPlayer->GetActorLocation() + OwnerPlayer->GetActorForwardVector() * Dist;
		Target.Z = BallRadius;
		P = FMath::VInterpTo(P, Target, Dt, 25.f);
		Velocity = OwnerPlayer->GetVelocity();
		Velocity.Z = 0.f;
		CollideWithWalls(P, OldP, Velocity);
	}
	else
	{
		Integrate(P, Velocity, CurveAccel, CurveTimeLeft, Dt);
		if (IntendedReceiver && TimeSinceKick() > 2.5f)
		{
			IntendedReceiver = nullptr; // пас «протух»
		}
	}

	// Телепорт без sweep: оверлапы (триггер гола) всё равно обновляются.
	SetActorLocation(P);
}

void ASoccerBall::Integrate(FVector& P, FVector& V, const FVector& Curve, float& CurveTime, float Dt) const
{
	const FVector OldP = P;

	// Закрутка (изящный удар): боковое ускорение в первые доли секунды полёта
	if (CurveTime > 0.f)
	{
		V += Curve * Dt;
		CurveTime -= Dt;
	}

	const bool bGrounded = P.Z <= BallRadius + 1.f && FMath::Abs(V.Z) < 1.f;
	if (!bGrounded)
	{
		V.Z -= Gravity * Dt;
	}

	// Трение: на газоне — качение, в воздухе — слабое сопротивление.
	// Экспоненциальное затухание: v(t) = v0 * e^(-k t) -> путь до остановки = v0 / k.
	const float Damp = FMath::Exp(-(bGrounded ? RollingFriction : AirDrag) * Dt);
	V.X *= Damp;
	V.Y *= Damp;
	if (bGrounded && V.Size2D() < 15.f)
	{
		V = FVector::ZeroVector; // мяч остановился
	}

	P += V * Dt;

	// Отскок от газона
	if (P.Z < BallRadius)
	{
		P.Z = BallRadius;
		if (V.Z < -150.f)
		{
			V.Z = -V.Z * Bounciness;
			V.X *= 0.9f;
			V.Y *= 0.9f;
		}
		else
		{
			V.Z = 0.f;
		}
	}

	CollideWithWalls(P, OldP, V);
}

void ASoccerBall::CollideWithWalls(FVector& P, const FVector& OldP, FVector& V) const
{
	const float R = BallRadius;
	const float WallY = HalfWidth + BoardGap; // борт стоит сразу за боковой линией

	// Боковые линии: мяч отскакивает от борта у самой линии
	if (FMath::Abs(P.Y) > WallY - R)
	{
		P.Y = FMath::Sign(P.Y) * (WallY - R);
		V.Y = -FMath::Sign(P.Y) * FMath::Abs(V.Y) * WallBounciness;
	}

	// Лицевые линии: за линию ворот мяч уходит только в створ,
	// в штангу/перекладину и мимо ворот — отскок от линии
	const bool bWasInGoal = FMath::Abs(OldP.X) > HalfLength;
	if (!bWasInGoal && FMath::Abs(P.X) > HalfLength - R)
	{
		const bool bInMouth = FMath::Abs(P.Y) < GoalHalfWidth - R && P.Z < GoalHeight - R;
		if (!bInMouth)
		{
			P.X = FMath::Sign(P.X) * (HalfLength - R);
			V.X = -FMath::Sign(P.X) * FMath::Abs(V.X) * WallBounciness;
		}
	}

	if (FMath::Abs(P.X) > HalfLength)
	{
		// Внутри ворот сетка гасит мяч
		const float Back = HalfLength + GoalDepth - R;
		if (FMath::Abs(P.X) > Back)
		{
			P.X = FMath::Sign(P.X) * Back;
			V.X *= -0.2f;
		}
		if (FMath::Abs(P.Y) > GoalHalfWidth - R)
		{
			P.Y = FMath::Sign(P.Y) * (GoalHalfWidth - R);
			V.Y *= -0.2f;
		}
		if (P.Z > GoalHeight - R)
		{
			P.Z = GoalHeight - R;
			V.Z = FMath::Min<double>(V.Z, 0.0);
		}
	}
}

void ASoccerBall::PredictPath(const FVector& Start, const FVector& StartVelocity, const FVector& Curve,
                              float CurveTime, TArray<FVector>& OutPoints) const
{
	// Прогоняем ту же физику вперёд на ~2.5 с, пока мяч не остановится или не пересечёт линию ворот
	OutPoints.Reset();
	FVector P = Start;
	FVector V = StartVelocity;
	float CT = CurveTime;
	const float Dt = 1.f / 30.f;

	OutPoints.Add(P);
	for (int32 Step = 1; Step <= 75; ++Step)
	{
		Integrate(P, V, Curve, CT, Dt);
		if (Step % 2 == 0)
		{
			OutPoints.Add(P);
		}
		if (FMath::Abs(P.X) > HalfLength || V.IsNearlyZero())
		{
			OutPoints.Add(P);
			break;
		}
	}
}

// ============================================================================
//  ВОРОТА
//  Локальные оси: створ в X = 0, сетка уходит в +X. Ворота на +X ставятся с yaw 0,
//  ворота на −X — с yaw 180.
// ============================================================================

ASoccerGoal::ASoccerGoal()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Триггер гола. Сфера мяча касается бокса, когда центр мяча заходит за линию
	// на BallRadius, т.е. мяч ПОЛНОСТЬЮ пересёк линию ворот — как по правилам.
	const float MinX = BallRadius * 2.f;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetBoxExtent(FVector((GoalDepth - MinX) * 0.5f, GoalHalfWidth - BallRadius, GoalHeight * 0.5f));
	Trigger->SetRelativeLocation(FVector(MinX + (GoalDepth - MinX) * 0.5f, 0.f, GoalHeight * 0.5f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(MESH_CYLINDER);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(MESH_CUBE);

	// Штанги и перекладина (цилиндр движка: диаметр 100, высота 100, пивот в центре)
	const float PostScale = 0.1f;
	Posts.Add(MakeVisual(this, Root, TEXT("PostL"), Cyl.Object, FVector(0.f, -GoalHalfWidth, GoalHeight * 0.5f),
	                     FVector(PostScale, PostScale, GoalHeight / 100.f)));
	Posts.Add(MakeVisual(this, Root, TEXT("PostR"), Cyl.Object, FVector(0.f, GoalHalfWidth, GoalHeight * 0.5f),
	                     FVector(PostScale, PostScale, GoalHeight / 100.f)));
	Posts.Add(MakeVisual(this, Root, TEXT("Crossbar"), Cyl.Object, FVector(0.f, 0.f, GoalHeight),
	                     FVector(PostScale, PostScale, GoalHalfWidth * 2.f / 100.f), FRotator(0.f, 0.f, 90.f)));

	// Сетка: задняя стенка и две боковые (крышу не делаем, чтобы камера сверху видела мяч)
	Nets.Add(MakeVisual(this, Root, TEXT("NetBack"), Cube.Object, FVector(GoalDepth, 0.f, GoalHeight * 0.5f),
	                    FVector(0.03f, GoalHalfWidth * 2.f / 100.f, GoalHeight / 100.f)));
	Nets.Add(MakeVisual(this, Root, TEXT("NetL"), Cube.Object, FVector(GoalDepth * 0.5f, -GoalHalfWidth, GoalHeight * 0.5f),
	                    FVector(GoalDepth / 100.f, 0.03f, GoalHeight / 100.f)));
	Nets.Add(MakeVisual(this, Root, TEXT("NetR"), Cube.Object, FVector(GoalDepth * 0.5f, GoalHalfWidth, GoalHeight * 0.5f),
	                    FVector(GoalDepth / 100.f, 0.03f, GoalHeight / 100.f)));

	// Игроки не должны проходить сквозь штанги и сетку (на мяч это не влияет — у него своя физика)
	for (UStaticMeshComponent* C : Posts) C->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	for (UStaticMeshComponent* C : Nets)  C->SetCollisionProfileName(TEXT("BlockAllDynamic"));
}

void ASoccerGoal::BeginPlay()
{
	Super::BeginPlay();
	for (UStaticMeshComponent* C : Posts) Paint(C, FLinearColor::White);
	for (UStaticMeshComponent* C : Nets)  Paint(C, FLinearColor(0.7f, 0.7f, 0.7f));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ASoccerGoal::OnTriggerOverlap);
}

void ASoccerGoal::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                   bool bFromSweep, const FHitResult& SweepResult)
{
	if (!Cast<ASoccerBall>(OtherActor)) return; // игроки в воротах нас не интересуют
	if (ASoccerGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoccerGameMode>())
	{
		GameMode->OnGoalScored(1 - DefendingTeam);
	}
}

// ============================================================================
//  ЛИНИЯ ПРИЦЕЛА — цепочка тонких белых отрезков вдоль прогноза полёта мяча
// ============================================================================

ASoccerAimLine::ASoccerAimLine()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(MESH_CUBE);
	for (int32 i = 0; i < MaxSegments; ++i)
	{
		UStaticMeshComponent* Seg = MakeVisual(this, Root, *FString::Printf(TEXT("Seg%d"), i), Cube.Object,
		                                       FVector::ZeroVector, FVector(0.01f));
		Seg->SetUsingAbsoluteLocation(true);
		Seg->SetUsingAbsoluteRotation(true);
		Seg->SetUsingAbsoluteScale(true);
		Seg->SetCastShadow(false);
		Seg->SetVisibility(false);
		Segments.Add(Seg);
	}
}

void ASoccerAimLine::BeginPlay()
{
	Super::BeginPlay();
	for (UStaticMeshComponent* Seg : Segments) Paint(Seg, FLinearColor::White);
}

void ASoccerAimLine::ShowPath(const TArray<FVector>& Points)
{
	int32 Used = 0;
	for (int32 i = 1; i < Points.Num() && Used < Segments.Num(); ++i)
	{
		// Линия идёт по газону (для навеса — по дуге полёта), чуть выше травы
		FVector A = Points[i - 1];
		FVector B = Points[i];
		A.Z = FMath::Max<double>(A.Z - BallRadius, 0.0) + 2.0;
		B.Z = FMath::Max<double>(B.Z - BallRadius, 0.0) + 2.0;
		const FVector D = B - A;
		const float Len = D.Size();
		if (Len < 1.f) continue;

		UStaticMeshComponent* Seg = Segments[Used++];
		Seg->SetWorldLocationAndRotation((A + B) * 0.5f, D.Rotation());
		Seg->SetWorldScale3D(FVector((Len + 2.f) / 100.f, 0.07f, 0.015f)); // ширина линии 7 см
		Seg->SetVisibility(true);
	}
	for (int32 i = Used; i < Segments.Num(); ++i)
	{
		Segments[i]->SetVisibility(false);
	}
}

void ASoccerAimLine::HidePath()
{
	for (UStaticMeshComponent* Seg : Segments) Seg->SetVisibility(false);
}

// ============================================================================
//  ИГРОК
// ============================================================================

ASoccerPlayer::ASoccerPlayer()
{
	PrimaryActorTick.bCanEverTick = true;

	// Капсула: диаметр 70 см, рост 180 см
	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	// Персонаж поворачивается по направлению движения, а не за контроллером
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 900.f, 0.f);
	Move->MaxWalkSpeed = RunSpeed;
	Move->MaxAcceleration = 4000.f;
	Move->BrakingDecelerationWalking = 2500.f;

	// Все боты получают стандартный AIController автоматически (логика ИИ — в Tick ниже)
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(MESH_CYLINDER);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(MESH_SPHERE);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(MESH_CONE);

	// Капсула «в форме»: футболка (цилиндр + плечи), шорты (нижняя полусфера), голова
	USceneComponent* Parent = GetCapsuleComponent();
	Body = MakeVisual(this, Parent, TEXT("Body"), Cyl.Object, FVector(0.f, 0.f, -5.f), FVector(0.62f, 0.62f, 1.0f));
	Top  = MakeVisual(this, Parent, TEXT("Top"),  Sph.Object, FVector(0.f, 0.f, 45.f), FVector(0.62f));
	Feet = MakeVisual(this, Parent, TEXT("Feet"), Sph.Object, FVector(0.f, 0.f, -55.f), FVector(0.62f));
	Head = MakeVisual(this, Parent, TEXT("Head"), Sph.Object, FVector(0.f, 0.f, 80.f), FVector(0.34f));
	// Маркер над головой — виден только у игрока, которым управляет человек
	Marker = MakeVisual(this, Parent, TEXT("Marker"), Cone.Object, FVector(0.f, 0.f, 140.f), FVector(0.3f),
	                    FRotator(180.f, 0.f, 0.f));
	Marker->SetCastShadow(false);
	Marker->SetVisibility(false);
	// Круг цвета команды под ногами — включается вместе с 3D-моделью
	Ring = MakeVisual(this, Parent, TEXT("Ring"), Cyl.Object, FVector(0.f, 0.f, -88.f), FVector(0.9f, 0.9f, 0.015f));
	Ring->SetCastShadow(false);
	Ring->SetVisibility(false);
}

void ASoccerPlayer::Setup(int32 InTeam, bool bInGoalkeeper, const FVector& InHome, float InHomeYaw,
                          const FSoccerPlayerInfo& InInfo, int32 InRosterIndex,
                          const FLinearColor& Shirt, const FLinearColor& Shorts)
{
	Team = InTeam;
	bGoalkeeper = bInGoalkeeper;
	Home = InHome;
	HomeYaw = InHomeYaw;
	Info = InInfo;
	RosterIndex = InRosterIndex;
	ShirtColor = Shirt;

	static const FLinearColor Skins[3] = {
		FLinearColor(0.8f, 0.55f, 0.4f), FLinearColor(0.55f, 0.35f, 0.22f), FLinearColor(0.9f, 0.7f, 0.55f)
	};
	Paint(Body, Shirt);
	Paint(Top, Shirt);
	Paint(Feet, Shorts);
	Paint(Head, Skins[GetTypeHash(Info.Name) % 3]);
	Paint(Marker, FLinearColor(0.35f, 1.f, 0.02f));
	Paint(Ring, Shirt);

	ResetToHome();
}

void ASoccerPlayer::ApplyCharacterModel(USkeletalMesh* InIdleMesh, UAnimSequence* InIdle,
                                        USkeletalMesh* InRunMesh, UAnimSequence* InRun)
{
	if (!InIdleMesh) return; // модели нет — остаёмся капсулой

	IdleMesh = InIdleMesh;
	RunMesh = InRunMesh ? InRunMesh : InIdleMesh;
	IdleAnim = InIdle;
	RunAnim = InRun;
	CurrentAnim = nullptr;

	// Встроенный в ACharacter скелетный меш: ноги на дне капсулы, лицом вперёд (+X)
	USkeletalMeshComponent* SkelMesh = GetMesh();
	SkelMesh->SetSkeletalMeshAsset(IdleMesh);
	SkelMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),
	                                         FRotator(0.f, MeshYawOffset, 0.f));
	SkelMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	// Капсулу прячем, вместо формы — круг цвета команды под ногами
	Body->SetVisibility(false);
	Top->SetVisibility(false);
	Feet->SetVisibility(false);
	Head->SetVisibility(false);
	Ring->SetVisibility(true);

	UpdateAnimation();
}

// Анимация по скорости: стоит — Idle, бежит — Running (скорость проигрывания под темп бега).
// Если у анимаций разные скелеты, вместе с анимацией меняется и модель (внешне они одинаковые).
void ASoccerPlayer::UpdateAnimation()
{
	if (!IdleAnim && !RunAnim) return;

	const float Speed = GetVelocity().Size2D();
	// Гистерезис, чтобы анимация не дёргалась на границе: бег с 80 см/с, обратно в Idle ниже 40
	const bool bRunning = RunAnim && Speed > (CurrentAnim == RunAnim ? 40.f : 80.f);
	UAnimSequence* Want = bRunning ? RunAnim.Get() : IdleAnim.Get();
	USkeletalMesh* WantMesh = bRunning ? RunMesh.Get() : IdleMesh.Get();

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (Want != CurrentAnim)
	{
		if (WantMesh && SkelMesh->GetSkeletalMeshAsset() != WantMesh)
		{
			SkelMesh->SetSkeletalMeshAsset(WantMesh);
		}
		if (Want)
		{
			SkelMesh->PlayAnimation(Want, true);
		}
		else
		{
			SkelMesh->Stop();
		}
		CurrentAnim = Want;
	}
	SkelMesh->SetPlayRate(bRunning ? FMath::Clamp(Speed / RunAnimSpeed, 0.8f, 2.f) : 1.f);
}

void ASoccerPlayer::ResetToHome()
{
	SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator(0.f, HomeYaw, 0.f));
	GetCharacterMovement()->StopMovementImmediately();
	StunTime = TackleCooldown = SkillCooldown = ProtectTime = DashTime = 0.f;
	AIDecisionTimer = 0.5f;
	bSliding = false;
	bShielding = false;
}

ASoccerGameMode* ASoccerPlayer::GM() const
{
	return GetWorld()->GetAuthGameMode<ASoccerGameMode>();
}

ASoccerPlayerController* ASoccerPlayer::HumanPC() const
{
	return Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController());
}

// Характеристика СКР: 50 -> 0.85, 99 -> 1.14 от базовой скорости
float ASoccerPlayer::SpeedFactor() const
{
	return FMath::Clamp(0.85f + (Info.Pace - 50) * 0.006f, 0.8f, 1.15f);
}

bool ASoccerPlayer::HasBall() const
{
	const ASoccerGameMode* G = GM();
	return G && G->Ball && G->Ball->OwnerPlayer.Get() == this;
}

bool ASoccerPlayer::TeamHasBall() const
{
	const ASoccerGameMode* G = GM();
	return G && G->Ball && G->Ball->OwnerPlayer && G->Ball->OwnerPlayer->Team == Team;
}

bool ASoccerPlayer::CanKickBall() const
{
	if (HasBall()) return true;
	const ASoccerGameMode* G = GM();
	if (!G || !G->Ball || G->Ball->OwnerPlayer) return false;
	// Свободный мяч рядом: удар в касание, с лёта или головой (до ~2.4 м высоты)
	const FVector B = G->Ball->GetActorLocation();
	return FVector::Dist2D(B, GetActorLocation()) < 110.f && B.Z < 240.f;
}

void ASoccerPlayer::GainBall()
{
	ASoccerGameMode* G = GM();
	if (!G || !G->Ball) return;
	G->Ball->SetOwnerPlayer(this);
	AIDecisionTimer = 0.4f; // ИИ «осматривается» перед решением

	// Как в FIFA: если мяч получил партнёр человека — управление переходит к нему
	if (Team == HumanTeam && !IsPlayerControlled())
	{
		if (ASoccerPlayerController* PC = HumanPC())
		{
			PC->PossessPlayer(this);
		}
	}
}

void ASoccerPlayer::Stun(float Seconds)
{
	StunTime = FMath::Max(StunTime, Seconds);
	DashTime = 0.f;
	bSliding = false;
	if (HasBall())
	{
		GM()->Ball->SetOwnerPlayer(nullptr); // мяч отскакивает и катится дальше
	}
}

// ---------------------------------------------------------------------------
//  Главный цикл игрока
// ---------------------------------------------------------------------------
void ASoccerPlayer::Tick(float Dt)
{
	Super::Tick(Dt);

	ASoccerGameMode* G = GM();
	if (!G || !G->Ball) return;

	Marker->SetVisibility(IsPlayerControlled());
	UpdateAnimation();

	StunTime       = FMath::Max(0.f, StunTime - Dt);
	TackleCooldown = FMath::Max(0.f, TackleCooldown - Dt);
	SkillCooldown  = FMath::Max(0.f, SkillCooldown - Dt);
	ProtectTime    = FMath::Max(0.f, ProtectTime - Dt);
	GKTackleCooldown = FMath::Max(0.f, GKTackleCooldown - Dt);
	AIDecisionTimer -= Dt;

	// В меню, после гола и после финального свистка все стоят
	if (!G->IsPlayActive() || StunTime > 0.f)
	{
		return;
	}

	if (TickDash(Dt)) return; // во время подката/финта управление заблокировано

	if (bGoalkeeper)
	{
		TickGoalkeeper(Dt);
		return;
	}

	TryControlBall();

	if (IsPlayerControlled())
	{
		TickHuman(Dt);
	}
	else
	{
		TickFieldAI(Dt);
	}
}

// Рывок в фиксированном направлении (подкат, финт, выпад при отборе)
void ASoccerPlayer::StartDash(const FVector& Dir, float Speed, float Time, bool bSlide)
{
	DashDir = Dir.GetSafeNormal2D();
	if (DashDir.IsNearlyZero()) DashDir = GetActorForwardVector();
	DashSpeed = Speed;
	DashTime = Time;
	bSliding = bSlide;
	SetActorRotation(DashDir.Rotation());
}

bool ASoccerPlayer::TickDash(float Dt)
{
	if (DashTime <= 0.f) return false;

	DashTime -= Dt;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->MaxWalkSpeed = DashSpeed;
	AddMovementInput(DashDir, 1.f);

	if (bSliding)
	{
		// Подкат: коснулись мяча — выбиваем его, владельца «сбиваем»
		ASoccerBall* Ball = GM()->Ball;
		const FVector B = Ball->GetActorLocation();
		if (FVector::Dist2D(B, GetActorLocation()) < 100.f && B.Z < 60.f && Ball->OwnerPlayer != this)
		{
			ASoccerPlayer* Carrier = Ball->OwnerPlayer;
			if (!Carrier || Carrier->Team != Team)
			{
				if (Carrier) Carrier->Stun(1.0f);
				Ball->Kick(this, DashDir * 750.f);
				DashTime = 0.f;
			}
		}
		if (DashTime <= 0.f)
		{
			bSliding = false;
			StunTime = 0.35f; // время подняться после подката
		}
	}
	return true;
}

// Подобрать свободный мяч, оказавшийся у ног
void ASoccerPlayer::TryControlBall()
{
	ASoccerBall* Ball = GM()->Ball;
	if (Ball->OwnerPlayer) return;
	if (Ball->LastKicker == this && Ball->TimeSinceKick() < 0.3f) return; // только что ударили сами

	const FVector B = Ball->GetActorLocation();
	const FVector ToBall = B - GetActorLocation();
	if (ToBall.Size2D() > 70.f || B.Z > 80.f) return;

	// Очень сильный удар соперника не принять — мяч отскакивает от корпуса
	if (Ball->Velocity.Size() > 1900.f && Ball->LastKicker && Ball->LastKicker->Team != Team)
	{
		Ball->Kick(this, Ball->Velocity.MirrorByVector(ToBall.GetSafeNormal2D()) * 0.35f);
		return;
	}
	GainBall();
}

void ASoccerPlayer::MoveTo(const FVector& Target, float Speed)
{
	FVector D = Target - GetActorLocation();
	D.Z = 0.f;
	const float Dist = D.Size();
	GetCharacterMovement()->MaxWalkSpeed = Speed * SpeedFactor();
	if (Dist > 30.f)
	{
		// у цели сбавляем ход, чтобы не «дёргаться» вокруг точки
		AddMovementInput(D / Dist, FMath::Clamp(Dist / 150.f, 0.2f, 1.f));
	}
}

void ASoccerPlayer::FaceTowards(const FVector& Target, float Dt)
{
	const FVector D = Target - GetActorLocation();
	if (D.SizeSquared2D() < 1.f) return;
	const FRotator Want(0.f, D.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Dt, 10.f));
}

// ---------------------------------------------------------------------------
//  Управление человеком (кнопки-действия приходят из контроллера напрямую,
//  здесь — движение, спринт, укрывание, жокей, сдерживание)
// ---------------------------------------------------------------------------
void ASoccerPlayer::TickHuman(float Dt)
{
	ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetController());
	if (!PC) return;

	const ASoccerBall* Ball = GM()->Ball;
	const FVector BallLoc = Ball->GetActorLocation();
	const bool bHas = HasBall();
	const bool bLT = PC->LTAxis > 0.3f;

	FVector Move = PC->StickToWorld(PC->MoveInput);
	float Speed = PC->SprintAxis > 0.3f ? SprintSpeed : RunSpeed;

	bShielding = bHas && bLT;              // LT с мячом — укрывание корпусом
	const bool bJockey = !bHas && bLT;     // LT без мяча — жокей (лицом к мячу)
	bool bFaceBall = bJockey;

	if (bShielding || bJockey)
	{
		Speed = SlowSpeed;
	}
	else if (bHas)
	{
		Speed *= 0.93f; // с мячом чуть медленнее
	}

	// A в обороне (удержание): сдерживание — встаём между мячом и своими воротами
	if (PC->bContainHeld && !TeamHasBall())
	{
		const FVector OwnGoal(-AttackSign() * HalfLength, 0.f, 0.f);
		const FVector ContainPoint = BallLoc + (OwnGoal - BallLoc).GetSafeNormal2D() * 150.f;
		FVector D = ContainPoint - GetActorLocation();
		D.Z = 0.f;
		Move = D.Size() > 30.f ? D.GetSafeNormal() : FVector::ZeroVector;
		bFaceBall = true;
	}

	GetCharacterMovement()->MaxWalkSpeed = Speed * SpeedFactor();
	GetCharacterMovement()->bOrientRotationToMovement = !bFaceBall;
	if (bFaceBall)
	{
		FaceTowards(BallLoc, Dt);
	}
	if (!Move.IsNearlyZero())
	{
		AddMovementInput(Move); // длина вектора = сила наклона стика (аналоговое управление)
	}
}

// ---------------------------------------------------------------------------
//  ИИ полевого игрока: бежать к мячу / держать позицию
// ---------------------------------------------------------------------------
bool ASoccerPlayer::ShouldChase() const
{
	const ASoccerGameMode* G = GM();
	ASoccerPlayer* Nearest = G->NearestToBall(Team, false);
	if (Nearest == this) return true;

	// Ближе всех — игрок человека: ИИ-партнёр прессингует, только пока зажата RB
	if (Nearest && Nearest->IsPlayerControlled() && G->NearestToBall(Team, true) == this)
	{
		const ASoccerPlayerController* PC = HumanPC();
		return PC && PC->bRBHeld && !TeamHasBall();
	}
	return false;
}

FVector ASoccerPlayer::FormationPoint() const
{
	// Расстановка «дышит» вслед за мячом, в атаке команда поднимается выше
	const FVector BallLoc = GM()->Ball->GetActorLocation();
	FVector T = Home;
	T.X += BallLoc.X * 0.45f;
	T.Y += BallLoc.Y * 0.25f;
	if (TeamHasBall())
	{
		T.X += AttackSign() * 300.f;
	}
	return ClampToField(T, 150.f);
}

void ASoccerPlayer::TickFieldAI(float Dt)
{
	bShielding = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	if (HasBall())
	{
		TickAIWithBall(Dt);
		return;
	}

	ASoccerBall* Ball = GM()->Ball;
	const FVector BallLoc = Ball->GetActorLocation();

	if (Ball->IntendedReceiver == this || ShouldChase())
	{
		// Бежим туда, где мяч будет через мгновение
		MoveTo(BallLoc + Ball->Velocity * 0.3f, SprintSpeed * 0.9f);

		// Мяч у соперника рядом — пробуем отобрать
		if (Ball->OwnerPlayer && Ball->OwnerPlayer->Team != Team &&
		    FVector::Dist2D(BallLoc, GetActorLocation()) < 120.f)
		{
			Tackle();
		}
	}
	else
	{
		MoveTo(FormationPoint(), RunSpeed);
	}
}

ASoccerPlayer* ASoccerPlayer::NearestOpponent(float& OutDist) const
{
	ASoccerPlayer* Best = nullptr;
	OutDist = TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (P->Team == Team) continue;
		const float D = FVector::Dist2D(P->GetActorLocation(), GetActorLocation());
		if (D < OutDist)
		{
			OutDist = D;
			Best = P;
		}
	}
	return Best;
}

void ASoccerPlayer::TickAIWithBall(float Dt)
{
	const FVector Me = GetActorLocation();
	const FVector OppGoal(AttackSign() * HalfLength, 0.f, 0.f);
	const float DistGoal = FVector::Dist2D(Me, OppGoal);
	const FVector ToGoal = (OppGoal - Me).GetSafeNormal2D();

	if (AIDecisionTimer <= 0.f)
	{
		// Близко к воротам — бьём в случайный угол
		if (DistGoal < 850.f)
		{
			const float AimY = FMath::FRandRange(-0.9f, 0.9f) * GoalHalfWidth;
			const FVector Aim = (FVector(OppGoal.X, AimY, 0.f) - Me).GetSafeNormal2D();
			Shoot(Aim, FMath::FRandRange(0.6f, 1.f), false);
			return;
		}

		// Соперник рядом — пытаемся отдать пас вперёд
		float PressDist = 0.f;
		NearestOpponent(PressDist);
		if (PressDist < 200.f)
		{
			AIDecisionTimer = 0.6f;
			if (FindPassTarget(ToGoal) && FMath::FRand() < 0.6f)
			{
				Pass(EPassKind::Ground, ToGoal, 0.5f);
				return;
			}
		}
	}

	// Ведём мяч к воротам, огибая ближайшего соперника
	float OppDist = 0.f;
	ASoccerPlayer* Opp = NearestOpponent(OppDist);
	FVector Dir = ToGoal;
	if (Opp && OppDist < 350.f)
	{
		const FVector Away = (Me - Opp->GetActorLocation()).GetSafeNormal2D();
		Dir = (Dir + Away * 0.7f).GetSafeNormal2D();
	}
	MoveTo(Me + Dir * 300.f, RunSpeed * 0.95f);
}

// ---------------------------------------------------------------------------
//  ИИ вратаря: стоит на линии ворот, смещается за мячом, отбивает
// ---------------------------------------------------------------------------
void ASoccerPlayer::TickGoalkeeper(float Dt)
{
	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const float Side = -AttackSign();          // с какой стороны наши ворота
	const float GoalX = Side * HalfLength;

	// Задержка реакции: у вратаря соперника зависит от сложности, у нашего — средняя
	const int32 Diff = FMath::Clamp(G->GetDifficulty(), 0, 2);
	static const float Reactions[3] = { 0.3f, 0.22f, 0.15f };
	const float Reaction = Team == HumanTeam ? 0.22f : Reactions[Diff];

	GKReactionTimer -= Dt;
	if (GKReactionTimer <= 0.f)
	{
		GKReactionTimer = Reaction;
		GKTargetY = BallLoc.Y * 0.5f;
		// Мяч летит в сторону ворот — встаём в точку, где он пересечёт линию
		if (Ball->Velocity.X * Side > 300.f)
		{
			const float T = (GoalX - BallLoc.X) / Ball->Velocity.X;
			if (T > 0.f && T < 1.5f)
			{
				GKTargetY = BallLoc.Y + Ball->Velocity.Y * T;
			}
		}
		// К самой штанге вратарь не прилипает — углы остаются открытыми
		GKTargetY = FMath::Clamp(GKTargetY, -GoalHalfWidth + 55.f, GoalHalfWidth - 55.f);
	}
	FVector Target(GoalX - Side * 70.f, GKTargetY, Me.Z);

	// Выход из ворот: удержание Y (для вратаря человека) или свободный медленный мяч в штрафной
	const bool bTeammateHasBall = TeamHasBall();
	const bool bLooseInBox = !Ball->OwnerPlayer &&
	                         FMath::Abs(BallLoc.X - GoalX) < PenaltyDepth - 100.f &&
	                         FMath::Abs(BallLoc.Y) < GoalHalfWidth + 350.f &&
	                         Ball->Velocity.Size() < 700.f;
	const ASoccerPlayerController* PC = HumanPC();
	const bool bRushButton = Team == HumanTeam && PC && PC->bGKRushHeld && !bTeammateHasBall;
	const bool bRush = bRushButton || bLooseInBox;
	if (bRush)
	{
		Target = BallLoc;
	}

	GetCharacterMovement()->bOrientRotationToMovement = false;
	MoveTo(Target, bRush ? KeeperSpeed * 1.3f : KeeperSpeed);
	FaceTowards(BallLoc, Dt);

	// Сейв: мяч в зоне досягаемости — вратарь пытается его отбить
	const FVector ToBall = BallLoc - Me;
	const bool bReach = ToBall.Size2D() < 95.f && BallLoc.Z < 220.f;
	const bool bJustKicked = Ball->LastKicker == this && Ball->TimeSinceKick() < 0.5f;
	if (!bReach || bTeammateHasBall || bJustKicked)
	{
		return;
	}

	bool bSave = false;
	if (Ball->OwnerPlayer)
	{
		// Нападающий с мячом рядом — бросок в ноги, не чаще раза в секунду
		if (GKTackleCooldown <= 0.f)
		{
			GKTackleCooldown = 1.f;
			bSave = FMath::FRand() < 0.45f;
		}
	}
	else if (Ball->Velocity.Size2D() < 600.f)
	{
		bSave = true; // медленный мяч вратарь просто забирает
	}
	else
	{
		// Удар по воротам: «возьму / не возьму» решается один раз на каждый удар.
		// Сильный удар, удар в угол и мяч далеко от корпуса снижают шанс сейва.
		if (GKDecisionKick != Ball->GetLastKickTime())
		{
			GKDecisionKick = Ball->GetLastKickTime();

			static const float BaseSave[3] = { 0.6f, 0.72f, 0.82f };
			float Chance = Team == HumanTeam ? 0.72f : BaseSave[Diff];
			Chance -= FMath::Max(0.f, (float)Ball->Velocity.Size2D() - 1200.f) / 5000.f;

			float CrossY = BallLoc.Y; // где мяч пересечёт линию ворот
			if (FMath::Abs(Ball->Velocity.X) > 1.f)
			{
				const float T = (GoalX - BallLoc.X) / Ball->Velocity.X;
				if (T > 0.f) CrossY = BallLoc.Y + Ball->Velocity.Y * T;
			}
			Chance -= 0.35f * FMath::Clamp(FMath::Abs(CrossY) / GoalHalfWidth, 0.f, 1.f);
			Chance -= FMath::Abs(BallLoc.Y - Me.Y) / 300.f;

			bGKWillSave = FMath::FRand() < FMath::Clamp(Chance, 0.08f, 0.9f);
		}
		bSave = bGKWillSave;
	}

	if (bSave)
	{
		if (Ball->OwnerPlayer) Ball->OwnerPlayer->Stun(0.5f); // забрал мяч у нападающего в ногах
		// выбиваем в поле и в сторону от центра ворот
		const FVector Dir = FVector(-Side, BallLoc.Y >= Me.Y ? 0.7f : -0.7f, 0.f).GetSafeNormal();
		Ball->Kick(this, Dir * 1500.f + FVector(0.f, 0.f, 450.f));
	}
}

// ---------------------------------------------------------------------------
//  Удары, пасы, отборы, финты
// ---------------------------------------------------------------------------
ASoccerPlayer* ASoccerPlayer::FindPassTarget(const FVector& AimDir) const
{
	// Лучший партнёр — тот, что ближе всего к направлению прицела и не слишком далеко
	ASoccerPlayer* Best = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (P == this || P->Team != Team || P->bGoalkeeper) continue;
		FVector ToP = P->GetActorLocation() - GetActorLocation();
		ToP.Z = 0.f;
		const float Dist = ToP.Size();
		if (Dist < 150.f) continue;
		const float Dot = FVector::DotProduct(ToP / Dist, AimDir);
		if (Dot < 0.35f) continue;
		const float Score = Dot * 2.f - Dist / 2500.f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = P;
		}
	}
	return Best;
}

FSoccerKick ASoccerPlayer::PlanPass(EPassKind Kind, const FVector& AimDir, float Power01) const
{
	FSoccerKick Plan;
	const ASoccerBall* Ball = GM()->Ball;
	const FVector From = Ball->GetActorLocation();
	const FVector Aim = AimDir.IsNearlyZero() ? GetActorForwardVector() : AimDir.GetSafeNormal2D();
	const float Power = FMath::Clamp(Power01, 0.f, 1.f);

	ASoccerPlayer* Mate = FindPassTarget(Aim);
	Plan.Receiver = Mate;

	FVector Target = From;
	if (Mate)
	{
		const FVector MateLoc = Mate->GetActorLocation();
		const FVector MateVel = Mate->GetVelocity();
		switch (Kind)
		{
		case EPassKind::Ground:  Target = MateLoc + MateVel * 0.4f; break;
		// В разрез: в свободную зону перед партнёром, по направлению атаки
		case EPassKind::Through: Target = MateLoc + FVector(AttackSign() * 400.f, 0.f, 0.f) + MateVel * 0.5f; break;
		case EPassKind::Lob:     Target = MateLoc + MateVel * 0.8f; break;
		}
	}
	else
	{
		// Партнёра по направлению нет — дальность паса задаёт сила замаха
		const float Range = Kind == EPassKind::Lob ? FMath::Lerp(700.f, 2600.f, Power) : FMath::Lerp(400.f, 1800.f, Power);
		Target = From + Aim * Range;
	}
	Target = ClampToField(Target, 100.f);

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	const FVector Dir = Flat / Dist;

	if (Kind == EPassKind::Lob)
	{
		// Навес: время полёта T = 2*Vz/g, горизонтальная скорость = дальность / T.
		// Сила замаха удлиняет или укорачивает навес относительно партнёра.
		const float Land = Mate ? Dist * FMath::Lerp(0.85f, 1.2f, Power) : Dist;
		const float Vz = FMath::Clamp(400.f + Land * 0.25f, 500.f, 950.f);
		const float T = 2.f * Vz / Ball->Gravity;
		Plan.Velocity = Dir * (Land / T) + FVector(0.f, 0.f, Vz);
	}
	else
	{
		// Мяч по газону: с экспоненциальным трением путь = (v0 - v1) / k,
		// значит v0 = путь * k + скорость_прихода. Сила замаха: 0.8x..1.35x от идеала.
		const float Arrive = Kind == EPassKind::Through ? 350.f : 450.f;
		const float Speed = Mate ? (Dist * Ball->RollingFriction + Arrive) * FMath::Lerp(0.8f, 1.35f, Power)
		                         : Dist * Ball->RollingFriction + 150.f;
		Plan.Velocity = Dir * FMath::Clamp(Speed, 500.f, 2600.f);
	}
	return Plan;
}

FSoccerKick ASoccerPlayer::PlanShot(const FVector& AimDir, float Power01, bool bFinesse, bool bWithError) const
{
	FSoccerKick Plan;
	const ASoccerBall* Ball = GM()->Ball;
	const FVector From = Ball->GetActorLocation();
	const float Power = FMath::Clamp(Power01, 0.f, 1.f);

	// Куда в створ: поперечная составляющая стика выбирает угол ворот
	float AimY = FMath::Clamp((float)AimDir.Y * 2.f, -1.f, 1.f) * GoalHalfWidth * 0.8f;
	if (bWithError && !bFinesse)
	{
		AimY += FMath::FRandRange(-1.f, 1.f) * 50.f * Power; // сильный удар — менее точный
	}
	const FVector Target(AttackSign() * HalfLength, AimY, 0.f);

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	FVector Dir = Flat / Dist;

	// Характеристика УДР: 50 -> 0.85, 99 -> 1.14 от базовой силы удара
	const float ShotFactor = FMath::Clamp(0.85f + (Info.Shooting - 50) * 0.006f, 0.8f, 1.15f);
	float Speed = FMath::Lerp(1300.f, 3000.f, Power) * ShotFactor;
	float Lift = Power * 250.f + (Dist > 1500.f ? 150.f : 0.f);

	if (bFinesse)
	{
		// ИЗЯЩНЫЙ УДАР (RB + B): медленнее, точнее и с закруткой.
		// Отклоняем старт на A градусов «наружу» и компенсируем боковым ускорением c:
		// −v·sin(A)·T + c·T²/2 = 0  =>  c = 2·v·sin(A) / T   (приближённо, без учёта трения)
		Speed *= 0.8f;
		Lift = 120.f;
		const float Angle = FMath::DegreesToRadians(12.f);
		const float T = Dist / Speed;
		FVector Perp = FVector::CrossProduct(FVector::UpVector, Dir);
		if (Perp.Y * AimY > 0.f) Perp = -Perp; // стартуем от центра ворот наружу
		Dir = (Dir * FMath::Cos(Angle) - Perp * FMath::Sin(Angle)).GetSafeNormal();
		Plan.Curve = Perp * (2.f * Speed * FMath::Sin(Angle) / T);
		Plan.CurveTime = T;
	}

	Plan.Velocity = Dir * Speed + FVector(0.f, 0.f, Lift);
	return Plan;
}

void ASoccerPlayer::ExecuteKick(const FSoccerKick& Plan)
{
	ASoccerBall* Ball = GM()->Ball;
	SetActorRotation(FRotator(0.f, Plan.Velocity.GetSafeNormal2D().Rotation().Yaw, 0.f));
	Ball->Kick(this, Plan.Velocity, Plan.Curve, Plan.CurveTime);
	Ball->IntendedReceiver = Plan.Receiver; // ИИ-партнёр побежит принимать
}

void ASoccerPlayer::Pass(EPassKind Kind, const FVector& AimDir, float Power01)
{
	if (!CanKickBall()) return;
	ExecuteKick(PlanPass(Kind, AimDir, Power01));
	if (IsPlayerControlled())
	{
		GM()->OnHumanPass();
	}
}

void ASoccerPlayer::Shoot(const FVector& AimDir, float Power01, bool bFinesse)
{
	if (!CanKickBall()) return;
	ExecuteKick(PlanShot(AimDir, Power01, bFinesse, true));
}

void ASoccerPlayer::Tackle()
{
	if (TackleCooldown > 0.f) return;
	TackleCooldown = 0.8f;

	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	ASoccerPlayer* Carrier = Ball->OwnerPlayer;
	if (!Carrier || Carrier->Team == Team) return;

	const FVector ToCarrier = Carrier->GetActorLocation() - GetActorLocation();
	if (ToCarrier.Size2D() > 140.f)
	{
		StartDash(ToCarrier, 800.f, 0.15f, false); // далеко — короткий выпад к сопернику
		return;
	}

	// Шанс отбора: человеку проще; у ИИ соперника зависит от сложности.
	// ЗАЩ отбирающего повышает шанс, укрывание мяча, финт и ФИЗ владельца — снижают.
	static const float AIChances[3] = { 0.25f, 0.35f, 0.45f };
	float Chance = IsPlayerControlled() ? 0.7f
	             : (Team == HumanTeam ? 0.35f : AIChances[FMath::Clamp(G->GetDifficulty(), 0, 2)]);
	Chance *= Info.Defending / 75.f;
	Chance *= FMath::Clamp(1.4f - Carrier->Info.Physical / 150.f, 0.6f, 1.2f);
	if (Carrier->IsShielding()) Chance *= 0.45f;
	if (Carrier->ProtectTime > 0.f) Chance *= 0.2f;

	// Толчок плечом
	Carrier->LaunchCharacter(ToCarrier.GetSafeNormal2D() * 350.f, true, false);

	if (FMath::FRand() < Chance)
	{
		Carrier->Stun(0.5f);
		GainBall();
	}
	else
	{
		Stun(0.25f); // промахнулись — теряем равновесие
	}
}

void ASoccerPlayer::SlideTackle(const FVector& Dir)
{
	if (bSliding || TackleCooldown > 0.f) return;
	TackleCooldown = 1.2f;
	StartDash(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, 1000.f, 0.45f, true);
}

void ASoccerPlayer::SkillMove(const FVector& Dir, bool bBig)
{
	if (!HasBall() || SkillCooldown > 0.f) return;
	// Финт: резкий уход с мячом в сторону щелчка правого стика.
	// С RB — более длинный финт и более долгая защита от отбора. ДРБ сокращает перезарядку.
	const float DribbleFactor = FMath::Clamp(1.2f - Info.Dribbling / 250.f, 0.8f, 1.f);
	SkillCooldown = (bBig ? 0.9f : 0.5f) * DribbleFactor;
	ProtectTime   = bBig ? 0.5f : 0.25f;
	StartDash(Dir, bBig ? 1000.f : 750.f, bBig ? 0.3f : 0.2f, false);
}

// ============================================================================
//  КОНТРОЛЛЕР
// ============================================================================

ASoccerPlayerController::ASoccerPlayerController()
{
	// Камерой управляет GameMode — не переключать вид на пешку при смене игрока
	bAutoManageActiveCameraTarget = false;
}

UInputAction* ASoccerPlayerController::NewAction(int32 Dimensions)
{
	UInputAction* A = NewObject<UInputAction>(this);
	A->ValueType = Dimensions == 2 ? EInputActionValueType::Axis2D
	             : Dimensions == 1 ? EInputActionValueType::Axis1D
	                               : EInputActionValueType::Boolean;
	Actions.Add(A);
	return A;
}

void ASoccerPlayerController::Map(UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate, bool bDeadZone)
{
	FEnhancedActionKeyMapping& M = Context->MapKey(Action, Key);
	if (bSwizzle)
	{
		// Клавиша даёт значение по X — переставляем в Y (для W/S и стрелок вверх/вниз)
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(this);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		M.Modifiers.Add(Swizzle);
	}
	if (bNegate)
	{
		M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
	if (bDeadZone)
	{
		M.Modifiers.Add(NewObject<UInputModifierDeadZone>(this)); // мёртвая зона стика
	}
}

// Input Actions и Mapping Context создаются прямо в коде — ассеты в редакторе не нужны.
void ASoccerPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	Context = NewObject<UInputMappingContext>(this);

	UInputAction* IA_Move   = NewAction(2);
	UInputAction* IA_Right  = NewAction(2);
	UInputAction* IA_Sprint = NewAction(1);
	UInputAction* IA_LT     = NewAction(1);
	UInputAction* IA_RB     = NewAction(0);
	UInputAction* IA_A      = NewAction(0);
	UInputAction* IA_B      = NewAction(0);
	UInputAction* IA_X      = NewAction(0);
	UInputAction* IA_Y      = NewAction(0);
	UInputAction* IA_LB     = NewAction(0);
	UInputAction* IA_Start  = NewAction(0);

	// --- Левый стик / WASD: движение ---
	Map(IA_Move, EKeys::Gamepad_Left2D, false, false, true);
	Map(IA_Move, EKeys::W, true);
	Map(IA_Move, EKeys::S, true, true);
	Map(IA_Move, EKeys::A, false, true);
	Map(IA_Move, EKeys::D);

	// --- Правый стик / стрелки: финты (атака), переключение по направлению (оборона) ---
	Map(IA_Right, EKeys::Gamepad_Right2D, false, false, true);
	Map(IA_Right, EKeys::Up, true);
	Map(IA_Right, EKeys::Down, true, true);
	Map(IA_Right, EKeys::Left, false, true);
	Map(IA_Right, EKeys::Right);

	// --- Курки и бамперы ---
	Map(IA_Sprint, EKeys::Gamepad_RightTriggerAxis); // RT / R2 — рывок
	Map(IA_Sprint, EKeys::LeftShift);
	Map(IA_LT, EKeys::Gamepad_LeftTriggerAxis);      // LT / L2 — укрывание / жокей
	Map(IA_LT, EKeys::LeftControl);
	Map(IA_RB, EKeys::Gamepad_RightShoulder);        // RB / R1 — модификатор / прессинг партнёром
	Map(IA_RB, EKeys::R);
	Map(IA_LB, EKeys::Gamepad_LeftShoulder);         // LB / L1 — смена игрока
	Map(IA_LB, EKeys::Tab);

	// --- Кнопки (Xbox A/B/X/Y = PlayStation ✖/⭕/⬛/▲). Зажать — сила, отпустить — удар/пас ---
	Map(IA_A, EKeys::Gamepad_FaceButton_Bottom);     // A ✖ — пас / сдерживание
	Map(IA_A, EKeys::SpaceBar);
	Map(IA_B, EKeys::Gamepad_FaceButton_Right);      // B ⭕ — удар / отбор
	Map(IA_B, EKeys::F);
	Map(IA_X, EKeys::Gamepad_FaceButton_Left);       // X ⬛ — навес / подкат
	Map(IA_X, EKeys::Q);
	Map(IA_Y, EKeys::Gamepad_FaceButton_Top);        // Y ▲ — пас в разрез / выход вратаря
	Map(IA_Y, EKeys::E);
	Map(IA_Start, EKeys::Gamepad_Special_Right);     // Start / Options — пауза
	Map(IA_Start, EKeys::P);
	Map(IA_Start, EKeys::Enter);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("Soccer: Default Input Component Class должен быть EnhancedInputComponent"));
		return;
	}

	EIC->BindAction(IA_Move,   ETriggerEvent::Triggered, this, &ASoccerPlayerController::OnMove);
	EIC->BindAction(IA_Move,   ETriggerEvent::Completed, this, &ASoccerPlayerController::OnMoveStop);
	EIC->BindAction(IA_Right,  ETriggerEvent::Triggered, this, &ASoccerPlayerController::OnRight);
	EIC->BindAction(IA_Right,  ETriggerEvent::Completed, this, &ASoccerPlayerController::OnRightStop);
	EIC->BindAction(IA_Sprint, ETriggerEvent::Triggered, this, &ASoccerPlayerController::OnSprint);
	EIC->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &ASoccerPlayerController::OnSprintStop);
	EIC->BindAction(IA_LT,     ETriggerEvent::Triggered, this, &ASoccerPlayerController::OnLT);
	EIC->BindAction(IA_LT,     ETriggerEvent::Completed, this, &ASoccerPlayerController::OnLTStop);
	EIC->BindAction(IA_RB,     ETriggerEvent::Started,   this, &ASoccerPlayerController::OnRB);
	EIC->BindAction(IA_RB,     ETriggerEvent::Completed, this, &ASoccerPlayerController::OnRBStop);
	EIC->BindAction(IA_A,      ETriggerEvent::Started,   this, &ASoccerPlayerController::OnA);
	EIC->BindAction(IA_A,      ETriggerEvent::Completed, this, &ASoccerPlayerController::OnAStop);
	EIC->BindAction(IA_B,      ETriggerEvent::Started,   this, &ASoccerPlayerController::OnB);
	EIC->BindAction(IA_B,      ETriggerEvent::Completed, this, &ASoccerPlayerController::OnBStop);
	EIC->BindAction(IA_X,      ETriggerEvent::Started,   this, &ASoccerPlayerController::OnX);
	EIC->BindAction(IA_X,      ETriggerEvent::Completed, this, &ASoccerPlayerController::OnXStop);
	EIC->BindAction(IA_Y,      ETriggerEvent::Started,   this, &ASoccerPlayerController::OnY);
	EIC->BindAction(IA_Y,      ETriggerEvent::Completed, this, &ASoccerPlayerController::OnYStop);
	EIC->BindAction(IA_LB,     ETriggerEvent::Started,   this, &ASoccerPlayerController::OnLB);
	EIC->BindAction(IA_Start,  ETriggerEvent::Started,   this, &ASoccerPlayerController::OnStart);
}

void ASoccerPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (UEnhancedInputLocalPlayerSubsystem* Sub =
	        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (Context)
		{
			Sub->AddMappingContext(Context, 0);
		}
	}
}

void ASoccerPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// Мяч потеряли во время замаха — удар/пас отменяется
	const ASoccerPlayer* P = Current();
	if (Charging != ECharge::None && (!P || !P->HasBall()))
	{
		Charging = ECharge::None;
	}
	UpdateAimPreview();
}

ASoccerGameMode* ASoccerPlayerController::GM() const
{
	return GetWorld()->GetAuthGameMode<ASoccerGameMode>();
}

ASoccerPlayer* ASoccerPlayerController::Current() const
{
	// Пока мяч не в игре (меню / гол / конец матча) — кнопки действий не работают
	const ASoccerGameMode* G = GM();
	if (!G || !G->IsPlayActive()) return nullptr;
	return Cast<ASoccerPlayer>(GetPawn());
}

void ASoccerPlayerController::ResetInputState()
{
	MoveInput = RightInput = FVector2D::ZeroVector;
	SprintAxis = LTAxis = 0.f;
	bRBHeld = bContainHeld = bGKRushHeld = false;
	Charging = ECharge::None;
	bRightStickArmed = true;
	if (ASoccerGameMode* G = GM())
	{
		if (G->AimLine) G->AimLine->HidePath();
	}
}

FVector ASoccerPlayerController::StickToWorld(const FVector2D& Stick) const
{
	// Вверх по стику = «от камеры», вправо = вправо по экрану
	const ASoccerGameMode* G = GM();
	const float Yaw = G ? G->GetCameraYaw() : 0.f;
	const FVector Forward = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Right = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
	return Forward * Stick.Y + Right * Stick.X;
}

FVector ASoccerPlayerController::AimDirection() const
{
	const FVector Dir = StickToWorld(MoveInput);
	if (Dir.Size() > 0.2f) return Dir.GetSafeNormal2D();
	const APawn* P = GetPawn();
	return P ? P->GetActorForwardVector() : FVector::ForwardVector;
}

float ASoccerPlayerController::GetCharge() const
{
	if (Charging == ECharge::None) return -1.f;
	// Полная сила — за 1 секунду удержания
	return FMath::Clamp<float>(GetWorld()->GetTimeSeconds() - ChargeStart, 0.f, 1.f);
}

void ASoccerPlayerController::BeginCharge(ECharge Kind)
{
	if (Charging != ECharge::None) return; // одновременно заряжается только одно действие
	Charging = Kind;
	ChargeStart = GetWorld()->GetTimeSeconds();
}

void ASoccerPlayerController::ReleaseCharge(ECharge Kind)
{
	if (Charging != Kind) return;
	const float Power = GetCharge();
	Charging = ECharge::None;

	ASoccerPlayer* P = Current();
	if (!P || !P->CanKickBall()) return;
	switch (Kind)
	{
	case ECharge::Pass:    P->Pass(EPassKind::Ground, AimDirection(), Power); break;
	case ECharge::Through: P->Pass(EPassKind::Through, AimDirection(), Power); break;
	case ECharge::Lob:     P->Pass(EPassKind::Lob, AimDirection(), Power); break;
	case ECharge::Shot:    P->Shoot(AimDirection(), FMath::Max(0.15f, Power), bRBHeld); break;
	default: break;
	}
}

// Белая линия: пока кнопка зажата, показываем, куда пойдёт мяч при текущих прицеле и силе
void ASoccerPlayerController::UpdateAimPreview()
{
	ASoccerGameMode* G = GM();
	if (!G || !G->AimLine || !G->Ball) return;

	const ASoccerPlayer* P = Current();
	if (Charging == ECharge::None || !P)
	{
		G->AimLine->HidePath();
		return;
	}

	const float Power = GetCharge();
	FSoccerKick Plan;
	switch (Charging)
	{
	case ECharge::Pass:    Plan = P->PlanPass(EPassKind::Ground, AimDirection(), Power); break;
	case ECharge::Through: Plan = P->PlanPass(EPassKind::Through, AimDirection(), Power); break;
	case ECharge::Lob:     Plan = P->PlanPass(EPassKind::Lob, AimDirection(), Power); break;
	default:               Plan = P->PlanShot(AimDirection(), FMath::Max(0.15f, Power), bRBHeld, false); break;
	}

	TArray<FVector> Path;
	G->Ball->PredictPath(G->Ball->GetActorLocation(), Plan.Velocity, Plan.Curve, Plan.CurveTime, Path);
	G->AimLine->ShowPath(Path);
}

void ASoccerPlayerController::PossessPlayer(ASoccerPlayer* NewPlayer)
{
	if (!NewPlayer || NewPlayer == GetPawn()) return;

	APawn* Old = GetPawn();
	// У нового игрока был AIController — убираем его
	if (AController* AI = NewPlayer->GetController())
	{
		AI->UnPossess();
		AI->Destroy();
	}
	Possess(NewPlayer);
	// Старого игрока отдаём ИИ
	if (Old)
	{
		Old->SpawnDefaultController();
	}
	Charging = ECharge::None;
	bContainHeld = false;
}

void ASoccerPlayerController::SwitchPlayer(const FVector& Dir)
{
	const ASoccerGameMode* G = GM();
	ASoccerPlayer* Cur = Current();
	if (!G || !G->Ball || !Cur) return;

	if (Dir.IsNearlyZero())
	{
		// LB: полевые игроки по удалённости от мяча. Первое нажатие — ближайший к мячу,
		// быстрые повторные нажатия перебирают всех остальных по кругу: дальше и дальше.
		TArray<ASoccerPlayer*> Order;
		for (ASoccerPlayer* P : G->Players)
		{
			if (P && P->Team == HumanTeam && !P->bGoalkeeper) Order.Add(P);
		}
		if (Order.Num() < 2) return;

		const FVector BallLoc = G->Ball->GetActorLocation();
		Order.Sort([&BallLoc](const ASoccerPlayer& A, const ASoccerPlayer& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), BallLoc) < FVector::DistSquared2D(B.GetActorLocation(), BallLoc);
		});

		const float Now = GetWorld()->GetTimeSeconds();
		const bool bCycling = Now - LastSwitchTime < 1.5f;
		LastSwitchTime = Now;

		const int32 CurIdx = Order.IndexOfByKey(Cur);
		const int32 NextIdx = bCycling ? (CurIdx + 1) % Order.Num()   // следующий по удалённости
		                               : (CurIdx == 0 ? 1 : 0);      // ближайший к мячу (если это вы — второй)
		PossessPlayer(Order[NextIdx]);
		return;
	}

	// Правый стик: партнёр в направлении щелчка
	ASoccerPlayer* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : G->Players)
	{
		if (P == Cur || P->Team != HumanTeam || P->bGoalkeeper) continue;
		FVector To = P->GetActorLocation() - Cur->GetActorLocation();
		To.Z = 0.f;
		const float Dot = FVector::DotProduct(To.GetSafeNormal(), Dir);
		if (Dot < 0.3f) continue;
		const float Score = To.Size() * (2.f - Dot);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = P;
		}
	}
	PossessPlayer(Best);
}

// --- Оси ---
void ASoccerPlayerController::OnMove(const FInputActionValue& V)   { MoveInput = V.Get<FVector2D>(); }
void ASoccerPlayerController::OnMoveStop()                         { MoveInput = FVector2D::ZeroVector; }
void ASoccerPlayerController::OnSprint(const FInputActionValue& V) { SprintAxis = V.Get<float>(); }
void ASoccerPlayerController::OnSprintStop()                       { SprintAxis = 0.f; }
void ASoccerPlayerController::OnLT(const FInputActionValue& V)     { LTAxis = V.Get<float>(); }
void ASoccerPlayerController::OnLTStop()                           { LTAxis = 0.f; }
void ASoccerPlayerController::OnRB()                               { bRBHeld = true; }
void ASoccerPlayerController::OnRBStop()                           { bRBHeld = false; }

void ASoccerPlayerController::OnRight(const FInputActionValue& V)
{
	RightInput = V.Get<FVector2D>();
	if (RightInput.Size() < 0.3f)
	{
		bRightStickArmed = true;
		return;
	}
	// «Щелчок» правым стиком срабатывает один раз, пока стик не вернётся в центр
	if (RightInput.Size() > 0.7f && bRightStickArmed)
	{
		bRightStickArmed = false;
		ASoccerPlayer* P = Current();
		if (!P) return;
		const FVector Dir = StickToWorld(RightInput).GetSafeNormal2D();
		if (P->HasBall())
		{
			P->SkillMove(Dir, bRBHeld); // атака: финт (с RB — «большой» финт)
		}
		else if (!P->TeamHasBall())
		{
			SwitchPlayer(Dir);          // оборона: переключение на игрока в направлении стика
		}
	}
}

void ASoccerPlayerController::OnRightStop()
{
	RightInput = FVector2D::ZeroVector;
	bRightStickArmed = true;
}

// --- A / ✖: пас низом (зажать — сила) | мяч рядом — пас в касание/головой | в обороне — сдерживание ---
void ASoccerPlayerController::OnA()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Pass);
	else if (P->CanKickBall()) P->Pass(EPassKind::Ground, AimDirection(), 0.5f);
	else                       bContainHeld = true;
}
void ASoccerPlayerController::OnAStop()
{
	bContainHeld = false;
	ReleaseCharge(ECharge::Pass);
}

// --- B / ⭕: удар (зажать — сила; с RB — изящный) | мяч рядом — с лёта/головой | в обороне — отбор ---
void ASoccerPlayerController::OnB()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Shot);
	else if (P->CanKickBall()) P->Shoot(AimDirection(), 0.8f, bRBHeld);
	else                       P->Tackle();
}
void ASoccerPlayerController::OnBStop() { ReleaseCharge(ECharge::Shot); }

// --- X / ⬛: навес / длинный пас (зажать — сила) | в обороне — подкат ---
void ASoccerPlayerController::OnX()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Lob);
	else if (P->CanKickBall()) P->Pass(EPassKind::Lob, AimDirection(), 0.5f);
	else                       P->SlideTackle(AimDirection());
}
void ASoccerPlayerController::OnXStop() { ReleaseCharge(ECharge::Lob); }

// --- Y / ▲: пас в разрез (зажать — сила) | в обороне удержание — выход вратаря ---
void ASoccerPlayerController::OnY()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Through);
	else if (P->CanKickBall()) P->Pass(EPassKind::Through, AimDirection(), 0.5f);
	else                       bGKRushHeld = true;
}
void ASoccerPlayerController::OnYStop()
{
	bGKRushHeld = false;
	ReleaseCharge(ECharge::Through);
}

// --- LB / L1: переключиться на игрока, ближайшего к мячу ---
void ASoccerPlayerController::OnLB() { SwitchPlayer(FVector::ZeroVector); }

// --- Start / Options: пауза ---
void ASoccerPlayerController::OnStart()
{
	if (ASoccerGameMode* G = GM())
	{
		G->TogglePause();
	}
}

// ============================================================================
//  HUD: шкала силы под игроком, пока зажата кнопка удара/паса
// ============================================================================

void ASoccerHUD::DrawHUD()
{
	Super::DrawHUD();

	const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas) return;
	const float Charge = PC->GetCharge();
	const APawn* P = PC->GetPawn();
	if (Charge < 0.f || !P) return;

	// Точка под ногами игрока на экране
	const FVector S = Project(P->GetActorLocation() - FVector(0.f, 0.f, 95.f));
	if (S.Z <= 0.f) return;

	const float K = Canvas->ClipY / 1080.f;
	const float W = 120.f * K;
	const float H = 12.f * K;
	const float X = S.X - W * 0.5f;
	const float Y = S.Y + 12.f * K;

	// Цвет заполнения: зелёный -> жёлтый -> красный
	const FLinearColor Green(0.3f, 1.f, 0.02f), Yellow(1.f, 0.85f, 0.f), Red(1.f, 0.1f, 0.02f);
	const FLinearColor Fill = Charge < 0.5f ? FMath::Lerp(Green, Yellow, Charge * 2.f)
	                                        : FMath::Lerp(Yellow, Red, (Charge - 0.5f) * 2.f);

	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), X - 2.f * K, Y - 2.f * K, W + 4.f * K, H + 4.f * K);
	DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * Charge, H);
}

// ============================================================================
//  РЕЖИМ ИГРЫ
// ============================================================================

ASoccerGameMode::ASoccerGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = ASoccerPlayerController::StaticClass();
	HUDClass = ASoccerHUD::StaticClass();
	DefaultPawnClass = nullptr; // пешек спавним сами

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(MESH_CUBE);
	CubeMesh = Cube.Object;
}

void ASoccerGameMode::BeginPlay()
{
	Super::BeginPlay();

	LoadProgress();
	LoadCharacterAssets();
	EnsureLighting();
	BuildField();

	Ball = GetWorld()->SpawnActor<ASoccerBall>(FVector(0.f, 0.f, BallRadius), FRotator::ZeroRotator);
	AimLine = GetWorld()->SpawnActor<ASoccerAimLine>(FVector::ZeroVector, FRotator::ZeroRotator);

	Camera = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;

	// Игра начинается с главного меню
	ReturnToMenu();
}

void ASoccerGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideWidget(PauseWidget);
	HideWidget(HudWidget);
	HideWidget(MenuWidget);
	Super::EndPlay(EndPlayReason);
}

// Вызывается движком для нового игрока. Пешку по умолчанию не спавним.
void ASoccerGameMode::RestartPlayer(AController* NewPlayer)
{
	if (bInMatch)
	{
		PossessHuman();
	}
}

// ---------------------------------------------------------------------------
//  Сохранения
// ---------------------------------------------------------------------------
void ASoccerGameMode::LoadProgress()
{
	if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
	{
		Save = Cast<USoccerSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	}
	if (!Save)
	{
		Save = Cast<USoccerSave>(UGameplayStatics::CreateSaveGameObject(USoccerSave::StaticClass()));
	}

	// Заполняем значения по умолчанию и чиним «битые» сохранения
	if (Save->Squad.Num() != 5)     Save->Squad = DefaultSquad();
	if (Save->ClubName.IsEmpty())   Save->ClubName = GetClubNames()[0];
	Save->OwnedKits.SetNum(GetSoccerKits().Num());
	Save->OwnedKits[0] = true;
	Save->PracticeDone.SetNum(NumPractice);
	Save->ClaimedChallenges.SetNum(8);
	Save->KitIndex = FMath::Clamp(Save->KitIndex, 0, GetSoccerKits().Num() - 1);
	if (!Save->OwnedKits[Save->KitIndex]) Save->KitIndex = 0;
	Save->StarterIndex = FMath::Clamp(Save->StarterIndex, 1, 4);
	Save->MatchMinutes = FMath::Clamp(Save->MatchMinutes, 1, 3);
	Save->Difficulty = FMath::Clamp(Save->Difficulty, 0, 2);
}

// 3D-модель футболиста из Content/Characters/Footballer (и подпапок — туда редактор сам
// импортирует FBX из SourceArt/Footballer, см. MiniFootball.cpp).
// Анимация Idle — ассет, в пути которого есть «Idle», бег — «Run» (например Running).
// Если в папке несколько дублей (Mixamo кладёт пустой «Take 001»), берём «mixamo.com».
// К каждой анимации подбираем модель с тем же скелетом.
void ASoccerGameMode::LoadCharacterAssets()
{
	const FString Folder = TEXT("/Game/Characters/Footballer");
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
#if WITH_EDITOR
	Registry.ScanPathsSynchronous({ Folder }, true);
#endif
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Folder), Assets, true);

	TArray<USkeletalMesh*> Meshes;
	UAnimSequence* Idle = nullptr;
	UAnimSequence* Run = nullptr;
	auto Prefer = [](const UAnimSequence* Current, const FString& CandidatePath)
	{
		return !Current || (CandidatePath.Contains(TEXT("mixamo")) && !Current->GetName().Contains(TEXT("mixamo")));
	};

	for (const FAssetData& Data : Assets)
	{
		// Путь внутри папки, например "/Running/Running_Anim_mixamo_com"
		const FString RelPath = Data.PackageName.ToString().RightChop(Folder.Len());
		UObject* Obj = Data.GetAsset();
		if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Obj))
		{
			Meshes.Add(SkelMesh);
		}
		else if (UAnimSequence* Anim = Cast<UAnimSequence>(Obj))
		{
			if (RelPath.Contains(TEXT("Idle")))
			{
				if (Prefer(Idle, RelPath)) Idle = Anim;
			}
			else if (RelPath.Contains(TEXT("Run")))
			{
				if (Prefer(Run, RelPath)) Run = Anim;
			}
		}
	}

	auto MeshFor = [&Meshes](const UAnimSequence* Anim) -> USkeletalMesh*
	{
		if (!Anim) return nullptr;
		for (USkeletalMesh* Candidate : Meshes)
		{
			if (Candidate->GetSkeleton() == Anim->GetSkeleton()) return Candidate;
		}
		return nullptr;
	};

	USkeletalMesh* IdleMeshAsset = MeshFor(Idle);
	USkeletalMesh* RunMeshAsset = MeshFor(Run);
	FootballerIdle = IdleMeshAsset ? Idle : nullptr;   // анимацию без подходящей модели не проиграть
	FootballerRun = RunMeshAsset ? Run : nullptr;
	FootballerRunMesh = RunMeshAsset;
	FootballerMesh = IdleMeshAsset ? IdleMeshAsset : (RunMeshAsset ? RunMeshAsset : (Meshes.Num() > 0 ? Meshes[0] : nullptr));

	// Подсказка в окне игры: что нашлось (видно при запуске из редактора)
	FString Report;
	if (!FootballerMesh)
	{
		Report = TEXT("3D-модель не найдена в Content/Characters/Footballer — игроки будут капсулами");
	}
	else
	{
		Report = FString::Printf(TEXT("Модель: %s | Idle: %s | Бег: %s"), *FootballerMesh->GetName(),
		                         FootballerIdle ? *FootballerIdle->GetName() : TEXT("нет"),
		                         FootballerRun ? *FootballerRun->GetName() : TEXT("нет"));
		if ((Idle && !FootballerIdle) || (Run && !FootballerRun))
		{
			Report += TEXT("\nВНИМАНИЕ: для анимации нет модели с таким же скелетом — при импорте выберите скелет персонажа");
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Soccer: %s"), *Report);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, Report);
	}
}

void ASoccerGameMode::SaveProgress()
{
	if (Save)
	{
		UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0);
	}
}

void ASoccerGameMode::ResetProgress()
{
	UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
	Save = nullptr;
	LoadProgress();
	SaveProgress();
	RefreshLineup();
}

FSoccerPlayerInfo ASoccerGameMode::MakeRandomPlayer(const FString& Position, int32 BaseRating)
{
	// Случайный футболист: база ± разброс, с уклоном под позицию
	auto Roll = [BaseRating](int32 Bias) { return FMath::Clamp(BaseRating + Bias + FMath::RandRange(-7, 7), 35, 99); };
	const TArray<FString>& Pool = SurnamePool();
	FSoccerPlayerInfo I;
	I.Name = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	I.Position = Position;
	if (Position == TEXT("ВР"))
	{
		I.Pace = Roll(-10); I.Shooting = Roll(-30); I.Passing = Roll(-10); I.Dribbling = Roll(-25); I.Defending = Roll(8); I.Physical = Roll(5);
	}
	else if (Position == TEXT("ЗЩ"))
	{
		I.Pace = Roll(0); I.Shooting = Roll(-15); I.Passing = Roll(-5); I.Dribbling = Roll(-10); I.Defending = Roll(8); I.Physical = Roll(6);
	}
	else
	{
		I.Pace = Roll(6); I.Shooting = Roll(5); I.Passing = Roll(0); I.Dribbling = Roll(5); I.Defending = Roll(-25); I.Physical = Roll(-5);
	}
	return I;
}

// ---------------------------------------------------------------------------
//  Интерфейс: показать/спрятать Slate-виджет, режимы ввода
// ---------------------------------------------------------------------------
void ASoccerGameMode::ShowWidget(TSharedPtr<SWidget>& Holder, const TSharedRef<SWidget>& Widget, int32 ZOrder)
{
	HideWidget(Holder);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->AddViewportWidgetContent(Widget, ZOrder);
		Holder = Widget;
	}
}

void ASoccerGameMode::HideWidget(TSharedPtr<SWidget>& Holder)
{
	if (!Holder.IsValid()) return;
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Holder.ToSharedRef());
		}
	}
	Holder.Reset();
}

void ASoccerGameMode::SetUIInput(const TSharedPtr<SWidget>& Focus)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	if (ASoccerPlayerController* SPC = Cast<ASoccerPlayerController>(PC)) SPC->ResetInputState();
	FInputModeUIOnly Mode;
	if (Focus.IsValid()) Mode.SetWidgetToFocus(Focus);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->bShowMouseCursor = true;
}

void ASoccerGameMode::SetGameInput()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	if (ASoccerPlayerController* SPC = Cast<ASoccerPlayerController>(PC)) SPC->ResetInputState();
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
}

// ---------------------------------------------------------------------------
//  Меню ↔ матч
// ---------------------------------------------------------------------------
void ASoccerGameMode::ReturnToMenu()
{
	GetWorldTimerManager().ClearTimer(ResetTimer);
	GetWorldTimerManager().ClearTimer(MenuTimer);
	if (UGameplayStatics::IsGamePaused(this))
	{
		UGameplayStatics::SetGamePaused(this, false);
	}
	HideWidget(PauseWidget);
	HideWidget(HudWidget);

	bInMatch = false;
	bPlayActive = false;
	bMatchOver = false;
	if (AimLine) AimLine->HidePath();

	SpawnLineup();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTarget(Camera);
	}
	UpdateCamera(0.f);

	TSharedPtr<SWidget> Focus;
	const TSharedRef<SWidget> Menu = SoccerUI::MakeMenu(this, Focus);
	ShowWidget(MenuWidget, Menu, 10);
	SetUIInput(Focus);
}

void ASoccerGameMode::StartMatch(ESoccerMode InMode, const FString& RivalName, int32 RivalDifficulty)
{
	GetWorldTimerManager().ClearTimer(ResetTimer);
	GetWorldTimerManager().ClearTimer(MenuTimer);
	if (UGameplayStatics::IsGamePaused(this))
	{
		UGameplayStatics::SetGamePaused(this, false);
	}
	HideWidget(MenuWidget);
	HideWidget(PauseWidget);

	MatchMode = InMode;
	MatchDifficulty = RivalDifficulty >= 0 ? FMath::Clamp(RivalDifficulty, 0, 2) : Save->Difficulty;
	if (IsPractice())
	{
		AwayName = TEXT("Спарринг");
	}
	else if (!RivalName.IsEmpty())
	{
		AwayName = RivalName;
	}
	else
	{
		const TArray<FString>& Rivals = RivalClubNames();
		AwayName = Rivals[FMath::RandRange(0, Rivals.Num() - 1)];
	}

	// Состав соперника: рейтинг зависит от сложности
	static const int32 BaseRating[3] = { 58, 68, 78 };
	const int32 Base = BaseRating[MatchDifficulty];
	AwaySquad = {
		MakeRandomPlayer(TEXT("ВР"), Base), MakeRandomPlayer(TEXT("ЗЩ"), Base), MakeRandomPlayer(TEXT("ЗЩ"), Base),
		MakeRandomPlayer(TEXT("НП"), Base), MakeRandomPlayer(TEXT("НП"), Base)
	};

	Score[0] = Score[1] = 0;
	TimeLeft = IsPractice() ? 60.f : Save->MatchMinutes * 60.f;
	GoalBanner = 0.f;
	ResultText = FText::GetEmpty();
	bMatchOver = false;
	bInMatch = true;

	SpawnTeams();
	ResetPositions();
	CamFocus = FVector::ZeroVector;

	ShowWidget(HudWidget, SoccerUI::MakeHud(this), 5);
	SetGameInput();
	PossessHuman();
}

void ASoccerGameMode::RestartMatch()
{
	StartMatch(MatchMode, AwayName, MatchDifficulty);
}

void ASoccerGameMode::TogglePause()
{
	if (!bInMatch || bMatchOver) return;

	const bool bPause = !UGameplayStatics::IsGamePaused(this);
	UGameplayStatics::SetGamePaused(this, bPause);
	if (bPause)
	{
		TSharedPtr<SWidget> Focus;
		const TSharedRef<SWidget> Pause = SoccerUI::MakePause(this, Focus);
		ShowWidget(PauseWidget, Pause, 20);
		SetUIInput(Focus);
	}
	else
	{
		HideWidget(PauseWidget);
		SetGameInput();
	}
}

void ASoccerGameMode::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
}

// ---------------------------------------------------------------------------
//  Спавн игроков
// ---------------------------------------------------------------------------
void ASoccerGameMode::DestroyPlayers()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->GetPawn())
	{
		PC->UnPossess();
	}
	for (ASoccerPlayer* P : Players)
	{
		if (P) P->Destroy();
	}
	Players.Reset();
}

ASoccerPlayer* ASoccerGameMode::SpawnPlayer(int32 InTeam, int32 RosterIdx, const FSoccerPlayerInfo& PlayerInfo,
                                            const FVector& Location, float Yaw)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASoccerPlayer* P = GetWorld()->SpawnActor<ASoccerPlayer>(Location, FRotator(0.f, Yaw, 0.f), Params);
	if (!P) return nullptr;

	// Форма: наша — из магазина/клуба, соперник — синий. Вратари выделяются цветом.
	const bool bGK = RosterIdx == 0;
	FLinearColor Shirt, Shorts;
	if (InTeam == HumanTeam)
	{
		const FSoccerKit& Kit = GetSoccerKits()[Save->KitIndex];
		Shirt = bGK ? FLinearColor(0.95f, 0.7f, 0.02f) : Kit.Shirt;
		Shorts = bGK ? FLinearColor(0.05f, 0.05f, 0.05f) : Kit.Shorts;
	}
	else
	{
		Shirt = bGK ? FLinearColor(0.02f, 0.6f, 0.5f) : FLinearColor(0.03f, 0.12f, 0.6f);
		Shorts = FLinearColor(0.9f, 0.9f, 0.9f);
	}
	P->Setup(InTeam, bGK, Location, Yaw, PlayerInfo, RosterIdx, Shirt, Shorts);
	P->ApplyCharacterModel(FootballerMesh, FootballerIdle, FootballerRunMesh, FootballerRun); // все футболисты — одна модель
	Players.Add(P);
	return P;
}

void ASoccerGameMode::SpawnTeams()
{
	DestroyPlayers();

	// Расстановка 1-2-2 для команды 0 (соперник — зеркально по X).
	// Индекс 0 — вратарь, 1–2 — защитники, 3–4 — нападающие.
	const FVector2D Layout[5] = {
		FVector2D(-HalfLength + 70.f, 0.f),
		FVector2D(-1150.f, -450.f),
		FVector2D(-1150.f,  450.f),
		FVector2D(-450.f,  -350.f),
		FVector2D(-450.f,   350.f),
	};
	const float SpawnZ = 92.f; // полувысота капсулы + зазор

	// Кто выходит на поле в каждом режиме
	TArray<int32> Home, Away;
	switch (MatchMode)
	{
	case ESoccerMode::PracticeShooting: Home = { Save->StarterIndex }; Away = { 0 };    break;
	case ESoccerMode::PracticeOneOnOne: Home = { Save->StarterIndex }; Away = { 0, 1 }; break;
	case ESoccerMode::PracticeAttack:   Home = { 0, 1, 2, 3, 4 };      Away = { 0, 1 }; break;
	default:                            Home = { 0, 1, 2, 3, 4 };      Away = { 0, 1, 2, 3, 4 }; break;
	}

	for (int32 Idx : Home)
	{
		SpawnPlayer(0, Idx, Save->Squad[Idx], FVector(Layout[Idx].X, Layout[Idx].Y, SpawnZ), 0.f);
	}
	for (int32 Idx : Away)
	{
		SpawnPlayer(1, Idx, AwaySquad[Idx], FVector(-Layout[Idx].X, Layout[Idx].Y, SpawnZ), 180.f);
	}
}

// Состав в меню: капитан в центре, остальные «клином» позади — как на командном фото
void ASoccerGameMode::SpawnLineup()
{
	DestroyPlayers();
	const int32 Starter = Save->StarterIndex;
	const FVector Spots[5] = {
		FVector(380.f, 0.f, 92.f), FVector(240.f, -260.f, 92.f), FVector(520.f, -260.f, 92.f),
		FVector(130.f, -480.f, 92.f), FVector(630.f, -480.f, 92.f)
	};
	int32 Spot = 1;
	for (int32 i = 0; i < 5; ++i)
	{
		const FVector Loc = i == Starter ? Spots[0] : Spots[Spot++];
		SpawnPlayer(0, i, Save->Squad[i], Loc, 90.f); // лицом к камере
	}
	if (Ball)
	{
		Ball->ResetBall(FVector(420.f, 90.f, BallRadius));
	}
}

void ASoccerGameMode::RefreshLineup()
{
	if (!bInMatch)
	{
		SpawnLineup();
	}
}

void ASoccerGameMode::PossessHuman()
{
	ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !Camera) return;

	// Стартовый игрок из состава, иначе — любой полевой нашей команды
	ASoccerPlayer* Target = nullptr;
	for (ASoccerPlayer* P : Players)
	{
		if (P->Team != HumanTeam || P->bGoalkeeper) continue;
		if (!Target || P->RosterIndex == Save->StarterIndex) Target = P;
	}
	PC->PossessPlayer(Target);
	PC->SetViewTarget(Camera);
}

// ---------------------------------------------------------------------------
//  Свет и стадион
// ---------------------------------------------------------------------------

// Если в уровне нет солнца (например, File → New Level → Empty Level) — ставим свет сами:
// основной источник с тенями и слабый заполняющий с противоположной стороны.
void ASoccerGameMode::EnsureLighting()
{
	if (TActorIterator<ADirectionalLight>(GetWorld()))
	{
		return; // в уровне уже есть свет (например, уровень Basic)
	}

	auto SpawnSun = [this](const FRotator& Rot, float Intensity, bool bShadows)
	{
		ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 1000.f), Rot);
		if (!Sun) return;
		ULightComponent* Light = Sun->GetLightComponent();
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(Intensity);
		Light->SetCastShadows(bShadows);
	};
	SpawnSun(FRotator(-50.f, -30.f, 0.f), 8.f, true);
	SpawnSun(FRotator(-35.f, 150.f, 0.f), 2.f, false);
}

AStaticMeshActor* ASoccerGameMode::SpawnBox(const FVector& Center, const FVector& Size, const FLinearColor& Color,
                                             bool bCollide, float Yaw)
{
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator(0.f, Yaw, 0.f));
	A->SetMobility(EComponentMobility::Movable); // иначе нельзя менять меш в рантайме
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetStaticMesh(CubeMesh);
	A->SetActorScale3D(Size / 100.f);            // куб движка — 100×100×100
	if (!bCollide)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
	}
	Paint(C, Color);
	return A;
}

void ASoccerGameMode::BuildField()
{
	const FLinearColor GrassA(0.07f, 0.33f, 0.06f);   // полосы газона, как после стрижки
	const FLinearColor GrassB(0.1f, 0.42f, 0.08f);
	const FLinearColor Apron(0.05f, 0.25f, 0.05f);    // газон за бортами
	const FLinearColor Line(0.95f, 0.95f, 0.95f);
	const float LineW = 10.f;  // ширина линий разметки
	const float LineZ = 1.5f;  // чуть выше газона, чтобы не мерцало
	const float WallY = HalfWidth + BoardGap;
	const float WallX = HalfLength + GoalDepth;

	// Основание (с коллизией — по нему бегают игроки). Верх — Z = 0.
	SpawnBox(FVector(0.f, 0.f, -10.f), FVector(2.f * WallX + 2400.f, 2.f * WallY + 3000.f, 20.f), Apron, true);

	// Полосатый газон: 10 поперечных полос по 4 м
	const int32 Stripes = 10;
	const float StripeW = 2.f * HalfLength / Stripes;
	for (int32 i = 0; i < Stripes; ++i)
	{
		const float X = -HalfLength + StripeW * (i + 0.5f);
		SpawnBox(FVector(X, 0.f, 0.5f), FVector(StripeW, 2.f * WallY, 1.f), i % 2 ? GrassA : GrassB, false);
	}
	// Газон за лицевыми линиями
	SpawnBox(FVector( HalfLength + GoalDepth * 0.5f, 0.f, 0.5f), FVector(GoalDepth, 2.f * WallY, 1.f), GrassA, false);
	SpawnBox(FVector(-HalfLength - GoalDepth * 0.5f, 0.f, 0.5f), FVector(GoalDepth, 2.f * WallY, 1.f), GrassA, false);

	// --- Разметка ---
	SpawnBox(FVector(0.f,  HalfWidth, LineZ), FVector(2.f * HalfLength, LineW, 1.f), Line, false);
	SpawnBox(FVector(0.f, -HalfWidth, LineZ), FVector(2.f * HalfLength, LineW, 1.f), Line, false);
	SpawnBox(FVector( HalfLength, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);
	SpawnBox(FVector(-HalfLength, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);
	SpawnBox(FVector(0.f, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);

	// Центральный круг (радиус 3 м) из коротких отрезков + центральная точка
	const int32 Segments = 48;
	const float CircleR = 300.f;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Angle = 2.f * PI * i / Segments;
		const FVector Pos(FMath::Cos(Angle) * CircleR, FMath::Sin(Angle) * CircleR, LineZ);
		const float SegLen = 2.f * PI * CircleR / Segments + 2.f;
		SpawnBox(Pos, FVector(LineW, SegLen, 1.f), Line, false, FMath::RadiansToDegrees(Angle));
	}
	SpawnBox(FVector(0.f, 0.f, LineZ), FVector(25.f, 25.f, 1.f), Line, false);

	// Штрафные площади (упрощённо — прямоугольники), точки пенальти, ворота
	const float BoxHalfW = GoalHalfWidth + 400.f;
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float FrontX = Side * (HalfLength - PenaltyDepth);
		SpawnBox(FVector(FrontX, 0.f, LineZ), FVector(LineW, 2.f * BoxHalfW, 1.f), Line, false);
		SpawnBox(FVector(Side * (HalfLength - PenaltyDepth * 0.5f),  BoxHalfW, LineZ), FVector(PenaltyDepth, LineW, 1.f), Line, false);
		SpawnBox(FVector(Side * (HalfLength - PenaltyDepth * 0.5f), -BoxHalfW, LineZ), FVector(PenaltyDepth, LineW, 1.f), Line, false);
		SpawnBox(FVector(FrontX, 0.f, LineZ), FVector(25.f, 25.f, 1.f), Line, false);

		// Ворота на +X защищает соперник (1), на −X — мы (0)
		ASoccerGoal* Goal = GetWorld()->SpawnActor<ASoccerGoal>(
			FVector(Side * HalfLength, 0.f, 0.f), FRotator(0.f, Side > 0 ? 0.f : 180.f, 0.f));
		Goal->DefendingTeam = Side > 0 ? 1 : 0;
	}

	// --- Рекламные борта вплотную к линиям поля (с коллизией: держат игроков;
	//     мяч отскакивает от линий в коде мяча) ---
	const FLinearColor BoardColors[4] = {
		FLinearColor(0.02f, 0.02f, 0.025f), FLinearColor(0.95f, 0.3f, 0.02f),
		FLinearColor(0.02f, 0.03f, 0.1f),   FLinearColor(0.7f, 0.04f, 0.03f)
	};
	const float BoardH = 90.f;
	const float PanelLen = 400.f;

	// Вдоль боковых линий
	const float SideLen = 2.f * (HalfLength + 30.f);
	const int32 SidePanels = FMath::CeilToInt(SideLen / PanelLen);
	const float SidePanel = SideLen / SidePanels;
	for (int32 i = 0; i < SidePanels; ++i)
	{
		const float X = -SideLen * 0.5f + SidePanel * (i + 0.5f);
		for (int32 S = -1; S <= 1; S += 2)
		{
			SpawnBox(FVector(X, S * (WallY + 10.f), BoardH * 0.5f), FVector(SidePanel, 20.f, BoardH),
			         BoardColors[(i + (S > 0 ? 1 : 0)) % 4], true);
		}
	}

	// Вдоль лицевых линий — по бокам от ворот (сами ворота закрыты сеткой) и за воротами
	const float EndFrom = GoalHalfWidth + 10.f;
	const float EndLen = WallY + 20.f - EndFrom;
	const int32 EndPanels = FMath::CeilToInt(EndLen / PanelLen);
	const float EndPanel = EndLen / EndPanels;
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 T = -1; T <= 1; T += 2)
		{
			for (int32 i = 0; i < EndPanels; ++i)
			{
				const float Y = T * (EndFrom + EndPanel * (i + 0.5f));
				SpawnBox(FVector(S * (HalfLength + 20.f), Y, BoardH * 0.5f), FVector(20.f, EndPanel, BoardH),
				         BoardColors[(i + 2) % 4], true);
			}
		}
		SpawnBox(FVector(S * (WallX + 10.f), 0.f, BoardH * 0.5f), FVector(20.f, 2.f * EndFrom + 40.f, BoardH),
		         BoardColors[1], true);
	}

	// --- Трибуна за дальним бортом: ступени с «сиденьями» и забор ---
	const FLinearColor StandA(0.16f, 0.16f, 0.18f), StandB(0.11f, 0.11f, 0.13f);
	const float StandLen = 2.f * WallX + 800.f;
	for (int32 Row = 0; Row < 7; ++Row)
	{
		const float Height = 80.f * (Row + 1);
		const float Y = -(WallY + 260.f + Row * 170.f);
		SpawnBox(FVector(0.f, Y, Height * 0.5f), FVector(StandLen, 170.f, Height), Row % 2 ? StandA : StandB, true);
		SpawnBox(FVector(0.f, Y + 40.f, Height + 8.f), FVector(StandLen, 40.f, 16.f),
		         Row % 2 ? FLinearColor(0.35f, 0.04f, 0.04f) : FLinearColor(0.05f, 0.06f, 0.2f), false);
	}
	const FLinearColor Fence(0.08f, 0.08f, 0.09f);
	for (float X = -StandLen * 0.5f; X <= StandLen * 0.5f; X += 300.f)
	{
		SpawnBox(FVector(X, -(WallY + 150.f), 160.f), FVector(8.f, 8.f, 320.f), Fence, false);
	}
	SpawnBox(FVector(0.f, -(WallY + 150.f), 320.f), FVector(StandLen, 6.f, 6.f), Fence, false);
}

// ---------------------------------------------------------------------------
//  Правила матча
// ---------------------------------------------------------------------------
int32 ASoccerGameMode::GetPracticeTarget() const
{
	switch (MatchMode)
	{
	case ESoccerMode::PracticeShooting: return 5;
	case ESoccerMode::PracticeOneOnOne: return 3;
	case ESoccerMode::PracticeAttack:   return 4;
	default:                            return 0;
	}
}

FString ASoccerGameMode::GetTeamName(int32 InTeam) const
{
	if (InTeam == HumanTeam) return Save ? Save->ClubName : FString();
	return AwayName;
}

void ASoccerGameMode::OnGoalScored(int32 ScoringTeam)
{
	if (!bInMatch || !bPlayActive) return; // мяч уже в сетке / матч окончен
	Score[ScoringTeam]++;
	bPlayActive = false;
	GoalBanner = 2.f;
	if (AimLine) AimLine->HidePath();
	// Через 2 секунды — расстановка заново и розыгрыш с центра
	GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::ResetPositions, 2.f, false);
}

void ASoccerGameMode::OnHumanPass()
{
	if (Save && bInMatch)
	{
		Save->PassesMade++;
	}
}

void ASoccerGameMode::ResetPositions()
{
	if (!bInMatch || bMatchOver) return;

	// Тренировка: цель выполнена — завершаем досрочно
	if (IsPractice() && Score[0] >= GetPracticeTarget())
	{
		EndMatch();
		return;
	}

	for (ASoccerPlayer* P : Players)
	{
		P->ResetToHome();
	}
	Ball->ResetBall(FVector(0.f, 0.f, BallRadius));
	bPlayActive = true;
}

void ASoccerGameMode::EndMatch()
{
	if (bMatchOver) return;
	bMatchOver = true;
	bPlayActive = false;
	if (AimLine) AimLine->HidePath();

	// Награды и статистика
	const int32 My = Score[0];
	const int32 Their = Score[1];
	Save->GoalsScored += My;
	int32 Reward = 0;
	FString Title;

	if (IsPractice())
	{
		const int32 Index = (int32)MatchMode - 1;
		const bool bDone = My >= GetPracticeTarget();
		Reward = bDone ? (Save->PracticeDone[Index] ? 100 : 500) : 50;
		if (bDone) Save->PracticeDone[Index] = true;
		Title = bDone ? TEXT("ТРЕНИРОВКА ПРОЙДЕНА!") : TEXT("НЕ ХВАТИЛО ГОЛОВ");
		ResultText = FText::FromString(FString::Printf(TEXT("%s   %d / %d   +%d монет"),
		                                               *Title, My, GetPracticeTarget(), Reward));
	}
	else
	{
		Save->MatchesPlayed++;
		Reward = 100 + 50 * My;
		if (My > Their)       { Save->Wins++; Reward += 400; Title = TEXT("ПОБЕДА!"); }
		else if (My == Their) { Reward += 150; Title = TEXT("НИЧЬЯ"); }
		else                  { Title = TEXT("ПОРАЖЕНИЕ"); }
		ResultText = FText::FromString(FString::Printf(TEXT("%s   %d : %d   +%d монет"), *Title, My, Their, Reward));
	}
	Save->Coins += Reward;
	SaveProgress();

	// Через 4 секунды — обратно в главное меню
	GetWorldTimerManager().SetTimer(MenuTimer, this, &ASoccerGameMode::ReturnToMenu, 4.f, false);
}

// ---------------------------------------------------------------------------
//  Данные для HUD и ИИ
// ---------------------------------------------------------------------------
ASoccerPlayer* ASoccerGameMode::GetTeamPlayer(int32 InTeam, int32 Index) const
{
	int32 N = 0;
	for (ASoccerPlayer* P : Players)
	{
		if (P && P->Team == InTeam)
		{
			if (N == Index) return P;
			++N;
		}
	}
	return nullptr;
}

ASoccerPlayer* ASoccerGameMode::GetHumanPlayer() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? Cast<ASoccerPlayer>(PC->GetPawn()) : nullptr;
}

// Соперник для правой карточки: владелец мяча или ближайший к мячу
ASoccerPlayer* ASoccerGameMode::GetFocusOpponent() const
{
	if (!Ball) return nullptr;
	if (Ball->OwnerPlayer && Ball->OwnerPlayer->Team != HumanTeam) return Ball->OwnerPlayer;
	ASoccerPlayer* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : Players)
	{
		if (!P || P->Team == HumanTeam) continue;
		const float D = FVector::Dist2D(P->GetActorLocation(), Ball->GetActorLocation());
		if (D < BestDist)
		{
			BestDist = D;
			Best = P;
		}
	}
	return Best;
}

ASoccerPlayer* ASoccerGameMode::NearestToBall(int32 InTeam, bool bOnlyAI) const
{
	ASoccerPlayer* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	const FVector BallLoc = Ball->GetActorLocation();
	for (ASoccerPlayer* P : Players)
	{
		if (P->Team != InTeam || P->bGoalkeeper) continue;
		if (bOnlyAI && P->IsPlayerControlled()) continue;
		const float D = FVector::Dist2D(P->GetActorLocation(), BallLoc);
		if (D < BestDist)
		{
			BestDist = D;
			Best = P;
		}
	}
	return Best;
}

// ---------------------------------------------------------------------------
//  Тик и камера
// ---------------------------------------------------------------------------
void ASoccerGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Ball) return;

	GoalBanner = FMath::Max(0.f, GoalBanner - DeltaSeconds);

	// Таймер идёт только во время игры (пауза после гола не считается)
	if (bInMatch && bPlayActive)
	{
		TimeLeft -= DeltaSeconds;
		if (TimeLeft <= 0.f)
		{
			TimeLeft = 0.f;
			EndMatch();
		}
	}

	UpdateCamera(DeltaSeconds);
}

void ASoccerGameMode::UpdateCamera(float Dt)
{
	if (!Camera || !Ball) return;

	if (!bInMatch)
	{
		// Меню: крупный план состава, камера медленно «дышит»
		MenuTime += Dt;
		const float Sway = FMath::Sin(MenuTime * 0.35f);
		Camera->GetCameraComponent()->SetFieldOfView(50.f);
		Camera->SetActorLocationAndRotation(FVector(380.f + Sway * 30.f, 720.f, 160.f),
		                                    FRotator(-5.f, -90.f + Sway * 1.5f, 0.f));
		return;
	}

	// Матч: «телевизионная» камера плавно следит за мячом, не уезжая за стадион
	FVector Target = Ball->GetActorLocation();
	Target.X = FMath::Clamp<double>(Target.X, -HalfLength + 1150.f, HalfLength - 1150.f);
	Target.Y = FMath::Clamp<double>(Target.Y - 200.0, -600.0, 600.0);
	Target.Z = 0.f;
	CamFocus = FMath::VInterpTo(CamFocus, Target, Dt, 2.5f);

	const FRotator Rot(CameraPitch, CameraYaw, 0.f);
	Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
	Camera->SetActorLocationAndRotation(CamFocus - Rot.Vector() * CameraDistance, Rot);
}
