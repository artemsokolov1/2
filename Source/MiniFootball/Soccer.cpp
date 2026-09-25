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
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
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
	// Движение считаем сами, физику движка не включаем.
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

void ASoccerBall::Kick(ASoccerPlayer* Kicker, const FVector& NewVelocity, const FVector& NewSpin)
{
	OwnerPlayer = nullptr;
	IntendedReceiver = nullptr;
	LastKicker = Kicker;
	LastKickTime = GetWorld()->GetTimeSeconds();
	Velocity = NewVelocity;
	Spin = NewSpin;
	if (ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>())
	{
		G->PlaySfx(ESoccerSound::Kick, FMath::Clamp((float)NewVelocity.Size() / 2200.f, 0.2f, 1.f));
	}
}

void ASoccerBall::Touch(const FVector& NewVelocity)
{
	// Касание при ведении: мяч катится по газону (вращение — как у катящегося мяча)
	Velocity = FVector(NewVelocity.X, NewVelocity.Y, 0.f);
	Spin = FVector::CrossProduct(FVector::UpVector, Velocity) / BallRadius;
	if (ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>())
	{
		G->PlaySfx(ESoccerSound::Touch, FMath::Clamp((float)Velocity.Size() / 800.f, 0.25f, 1.f));
	}
}

void ASoccerBall::SetOwnerPlayer(ASoccerPlayer* NewOwner)
{
	OwnerPlayer = NewOwner;
	if (NewOwner)
	{
		IntendedReceiver = nullptr;
		Velocity = FVector::ZeroVector;
		Spin = FVector::ZeroVector;
	}
	// при потере владельца мяч сохраняет текущую скорость и катится дальше
}

void ASoccerBall::ResetBall(const FVector& Location)
{
	SetActorLocation(Location);
	Velocity = FVector::ZeroVector;
	Spin = FVector::ZeroVector;
	OwnerPlayer = nullptr;
	LastKicker = nullptr;
	IntendedReceiver = nullptr;
}

float ASoccerBall::TimeSinceKick() const
{
	return GetWorld()->GetTimeSeconds() - LastKickTime;
}

FVector ASoccerBall::PredictLocation(float T) const
{
	const FVector P = GetActorLocation();
	if (OwnerPlayer)
	{
		return P + OwnerPlayer->GetVelocity() * T; // мяч ведут — он движется вместе с игроком
	}
	// По газону скорость гаснет экспоненциально: путь = v·(1 − e^(−kT))/k. В воздухе — почти без потерь.
	const bool bGrounded = P.Z <= BallRadius + 5.f && FMath::Abs(Velocity.Z) < 50.f;
	const float K = bGrounded ? RollingFriction : 0.3f;
	const float Travel = (1.f - FMath::Exp(-K * T)) / K;
	FVector Result = P + FVector(Velocity.X, Velocity.Y, 0.f) * Travel;
	Result.X = FMath::Clamp<double>(Result.X, -HalfLength, HalfLength);
	Result.Y = FMath::Clamp<double>(Result.Y, -HalfWidth, HalfWidth);
	Result.Z = BallRadius;
	return Result;
}

void ASoccerBall::Tick(float Dt)
{
	Super::Tick(Dt);

	FVector P = GetActorLocation();

	if (OwnerPlayer && OwnerPlayer->bGoalkeeper)
	{
		// Вратарь держит мяч в руках на уровне груди
		FVector Target = OwnerPlayer->GetActorLocation() + OwnerPlayer->GetActorForwardVector() * 40.f;
		Target.Z = 110.f;
		P = FMath::VInterpTo(P, Target, Dt, 25.f);
		Velocity = FVector::ZeroVector;
		Spin = FVector::ZeroVector;
	}
	else if (OwnerPlayer)
	{
		// Ведение: мяч «прилип» к ногам — держится перед игроком и поворачивает вместе с ним.
		// Подтягивание с упреждением по скорости, чтобы на спринте мяч не отставал.
		const FVector OwnerVel(OwnerPlayer->GetVelocity().X, OwnerPlayer->GetVelocity().Y, 0.f);
		const float CarrySpeed = 18.f;
		P = FMath::VInterpTo(P, OwnerPlayer->GetDribbleSpot() + OwnerVel / CarrySpeed, Dt, CarrySpeed);
		Velocity = OwnerVel;
		Spin = FVector::CrossProduct(FVector::UpVector, Velocity) / BallRadius;
	}
	else
	{
		// Свободный мяч живёт по физике
		const float ImpactSpeed = Velocity.Size();
		const int32 Hits = Integrate(P, Velocity, Spin, Dt);
		CollideWithPlayers(P);
		if (IntendedReceiver && TimeSinceKick() > 2.5f)
		{
			IntendedReceiver = nullptr; // пас «протух»
		}
		if (Hits != 0)
		{
			if (ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>())
			{
				G->OnBallImpact(Hits, P, ImpactSpeed);
			}
		}
	}

	// Телепорт без sweep: оверлапы (триггер гола) всё равно обновляются.
	SetActorLocation(P);
}

int32 ASoccerBall::Integrate(FVector& P, FVector& V, FVector& W, float Dt) const
{
	const FVector OldP = P;
	const bool bGrounded = P.Z <= BallRadius + 1.f && FMath::Abs(V.Z) < 1.f;

	if (!bGrounded)
	{
		// Полёт: гравитация, квадратичное сопротивление воздуха и эффект Магнуса:
		// верхнее вращение прижимает мяч вниз («ныряет»), нижнее — поддерживает, боковое — закручивает
		V.Z -= Gravity * Dt;
		V -= V * (AirDragQuad * V.Size() * Dt);
		V += FVector::CrossProduct(W, V) * (MagnusCoeff * Dt);
		W *= FMath::Exp(-SpinDecayAir * Dt);
	}
	else
	{
		// Качение по газону: трение, вращение без проскальзывания
		const float Damp = FMath::Exp(-RollingFriction * Dt);
		V.X *= Damp;
		V.Y *= Damp;
		if (V.Size2D() < 15.f)
		{
			V = FVector::ZeroVector; // мяч остановился
		}
		W = FVector::CrossProduct(FVector::UpVector, V) / BallRadius;
	}

	P += V * Dt;

	// Отскок от газона. Вращение переходит в скорость: после удара с верхним вращением мяч
	// «прыгает» вперёд, с нижним — тормозит и «закапывается».
	if (P.Z < BallRadius)
	{
		P.Z = BallRadius;
		if (V.Z < -150.f)
		{
			V.Z = -V.Z * Bounciness;
			FVector SpinVel = FVector::CrossProduct(W, FVector::UpVector) * BallRadius;
			SpinVel.Z = 0.f;
			const FVector Horizontal = FMath::Lerp(FVector(V.X, V.Y, 0.f), SpinVel, 0.35f) * 0.92f;
			V.X = Horizontal.X;
			V.Y = Horizontal.Y;
			W = FMath::Lerp(W, FVector::CrossProduct(FVector::UpVector, Horizontal) / BallRadius, 0.5f);
		}
		else
		{
			V.Z = 0.f;
		}
	}

	return CollideWithWalls(P, OldP, V);
}

int32 ASoccerBall::CollideWithWalls(FVector& P, const FVector& OldP, FVector& V) const
{
	const float R = BallRadius;
	const float WallY = HalfWidth + BoardGap; // борт стоит сразу за боковой линией
	int32 Hits = 0;

	// Боковые линии: мяч отскакивает от борта у самой линии
	if (FMath::Abs(P.Y) > WallY - R)
	{
		P.Y = FMath::Sign(P.Y) * (WallY - R);
		if (FMath::Abs(V.Y) > 150.f) Hits |= HitBoard;
		V.Y = -FMath::Sign(P.Y) * FMath::Abs(V.Y) * WallBounciness;
	}

	// Лицевые линии: за линию ворот мяч уходит только в створ (центр мяча внутри рамки),
	// мимо ворот и над перекладиной — отскок от борта
	const bool bWasInGoal = FMath::Abs(OldP.X) > HalfLength;
	if (!bWasInGoal && FMath::Abs(P.X) > HalfLength - R)
	{
		const bool bInMouth = FMath::Abs(P.Y) < GoalHalfWidth && P.Z < GoalHeight;
		if (!bInMouth)
		{
			P.X = FMath::Sign(P.X) * (HalfLength - R);
			if (FMath::Abs(V.X) > 150.f) Hits |= HitEndBoard;
			V.X = -FMath::Sign(P.X) * FMath::Abs(V.X) * WallBounciness;
		}
	}

	// Штанги и перекладина — круглые: отскок по нормали к поверхности (удар в штангу, «в крестовину»)
	auto HitBar = [&P, &V, &Hits, R, this](const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float T = FMath::Clamp<float>(FVector::DotProduct(P - A, AB) / AB.SizeSquared(), 0.f, 1.f);
		const FVector Q = A + AB * T;
		const FVector D = P - Q;
		const float Dist = D.Size();
		const float MinDist = R + PostRadius;
		if (Dist >= MinDist || Dist < 0.01f) return;
		const FVector N = D / Dist;
		P = Q + N * MinDist;
		const float Vn = FVector::DotProduct(V, N);
		if (Vn < 0.f)
		{
			V -= N * (Vn * (1.f + PostBounciness));
			if (Vn < -200.f) Hits |= HitPost;
		}
	};
	for (int32 S = -1; S <= 1; S += 2)
	{
		const float X = S * HalfLength;
		HitBar(FVector(X, -GoalHalfWidth, 0.f), FVector(X, -GoalHalfWidth, GoalHeight));
		HitBar(FVector(X,  GoalHalfWidth, 0.f), FVector(X,  GoalHalfWidth, GoalHeight));
		HitBar(FVector(X, -GoalHalfWidth, GoalHeight), FVector(X, GoalHalfWidth, GoalHeight));
	}

	if (FMath::Abs(P.X) > HalfLength)
	{
		// Внутри ворот сетка гасит мяч
		const float Back = HalfLength + GoalDepth - R;
		if (FMath::Abs(P.X) > Back)
		{
			P.X = FMath::Sign(P.X) * Back;
			if (FMath::Abs(V.X) > 200.f) Hits |= HitNet;
			V.X *= -0.2f;
		}
		if (FMath::Abs(P.Y) > GoalHalfWidth - R)
		{
			P.Y = FMath::Sign(P.Y) * (GoalHalfWidth - R);
			if (FMath::Abs(V.Y) > 200.f) Hits |= HitNet;
			V.Y *= -0.2f;
		}
		if (P.Z > GoalHeight - R)
		{
			P.Z = GoalHeight - R;
			V.Z = FMath::Min<double>(V.Z, 0.0);
		}
	}
	return Hits;
}

void ASoccerBall::CollideWithPlayers(FVector& P)
{
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	if (!G) return;

	for (ASoccerPlayer* Pl : G->Players)
	{
		if (!Pl || Pl == OwnerPlayer) continue;                                   // свой мяч не мешает
		if (Pl == LastKicker && TimeSinceKick() < 0.2f) continue;                 // только что ударил сам
		const FVector C = Pl->GetActorLocation();
		if (P.Z > C.Z + 90.f + BallRadius) continue;                              // мяч над головой

		FVector D = P - C;
		D.Z = 0.f;
		const float Dist = D.Size();
		const float MinDist = PlayerRadius + BallRadius;
		if (Dist >= MinDist || Dist < 0.01f) continue;

		// Мяч попал в корпус или ноги: выталкиваем и отражаем с потерей скорости (блок удара, рикошет)
		const FVector N = D / Dist;
		P.X = C.X + N.X * MinDist;
		P.Y = C.Y + N.Y * MinDist;
		const float Vn = FVector::DotProduct(Velocity, N);
		if (Vn < 0.f)
		{
			Velocity -= N * (Vn * 1.4f);
			Velocity *= 0.7f;
			Spin *= 0.5f;
		}
		// Мяч, который вёл другой игрок, отскочил от соперника — владение потеряно
		if (OwnerPlayer)
		{
			SetOwnerPlayer(nullptr);
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
//  ЛИНИЯ ПРИЦЕЛА — белая стрелка направления паса/удара
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

void ASoccerAimLine::ShowArrow(const FVector& From, const FVector& To)
{
	// Стрелка лежит на газоне: древко от мяча к цели и два «пера» наконечника
	FVector A = From;
	FVector B = To;
	A.Z = B.Z = 2.0;
	const FVector Dir = (B - A).GetSafeNormal2D();
	if (Dir.IsNearlyZero() || Segments.Num() < 3)
	{
		HidePath();
		return;
	}

	auto Place = [](UStaticMeshComponent* Seg, const FVector& P0, const FVector& P1)
	{
		const FVector D = P1 - P0;
		Seg->SetWorldLocationAndRotation((P0 + P1) * 0.5f, D.Rotation());
		Seg->SetWorldScale3D(FVector(D.Size() / 100.f, 0.08f, 0.015f)); // ширина линии 8 см
		Seg->SetVisibility(true);
	};
	Place(Segments[0], A, B);
	Place(Segments[1], B, B + Dir.RotateAngleAxis(150.f, FVector::UpVector) * 70.f);
	Place(Segments[2], B, B + Dir.RotateAngleAxis(-150.f, FVector::UpVector) * 70.f);
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
	// Внешность: у своих игроков — из сохранения; у кого рецепта нет — стабильно по имени
	if (!Info.Look.IsSet())
	{
		Info.Look = FSoccerLook::Random(int32(GetTypeHash(Info.Name)) | 1, Info.Pace, Info.Physical);
	}

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
	// Свой AnimInstance: проигрывает анимацию как Single Node и масштабирует кости (голова, полнота)
	SkelMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	SkelMesh->SetAnimInstanceClass(USoccerAnimInstance::StaticClass());
	SoccerLook::Apply(SkelMesh, Info.Look);

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
	// В подкате и в броске вратаря тело лежит — ногами не «бежим».
	// Гистерезис, чтобы анимация не дёргалась на границе: бег с 80 см/с, обратно в Idle ниже 40
	const bool bLying = SlidePoseTime > 0.f || DivePoseTime > 0.f;
	const bool bRunning = !bLying && RunAnim && Speed > (CurrentAnim == RunAnim ? 40.f : 80.f);
	UAnimSequence* Want = bRunning ? RunAnim.Get() : IdleAnim.Get();
	USkeletalMesh* WantMesh = bRunning ? RunMesh.Get() : IdleMesh.Get();

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (Want != CurrentAnim)
	{
		if (WantMesh && SkelMesh->GetSkeletalMeshAsset() != WantMesh)
		{
			SkelMesh->SetSkeletalMeshAsset(WantMesh);
			SoccerLook::Apply(SkelMesh, Info.Look); // смена меша сбрасывает морфы и AnimInstance
		}
		if (Want)
		{
			// Не PlayAnimation: он переключил бы меш на стандартный Single Node без масштаба костей
			SkelMesh->SetAnimation(Want);
			SkelMesh->Play(true);
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
	TouchCooldown = ControlCooldown = GKHoldTime = SlidePoseTime = DivePoseTime = 0.f;
	AIDecisionTimer = 0.5f;
	bSliding = false;
	bShielding = false;
	bPendingKick = false;
	bSlideResolved = true;
	bGKDived = false;
	Stamina = FMath::Min(1.f, Stamina + 0.15f); // пауза после гола — немного отдышаться
	AIRole = ESoccerAIRole::Support;
	MarkTarget.Reset();
	SupportPoint = FVector::ZeroVector;
	SupportTimer = 0.f;
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

// Спринт с учётом усталости: при выносливости ниже 30% спринт постепенно превращается в обычный бег
float ASoccerPlayer::SprintSpeedNow() const
{
	return FMath::Lerp(RunSpeed, SprintSpeed, FMath::Clamp(Stamina / 0.3f, 0.f, 1.f));
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
	const ASoccerGameMode* G = GM();
	if (!G || !G->Ball) return false;
	if (bGoalkeeper && HasBall()) return true; // мяч в руках вратаря
	const ASoccerBall* Ball = G->Ball;
	if (Ball->OwnerPlayer && Ball->OwnerPlayer.Get() != this) return false; // мяч у другого — нужен отбор
	// Нога достаёт мяч у газона и до пояса (удар с лёта); выше — только головой
	const FVector B = Ball->GetActorLocation();
	return FVector::Dist2D(B, GetActorLocation()) < KickReach && B.Z < 110.f;
}

bool ASoccerPlayer::CanHeadBall() const
{
	const ASoccerGameMode* G = GM();
	if (!G || !G->Ball || G->Ball->OwnerPlayer) return false;
	const FVector B = G->Ball->GetActorLocation();
	return FVector::Dist2D(B, GetActorLocation()) < 130.f && B.Z >= 110.f && B.Z < 280.f;
}

void ASoccerPlayer::GainBall()
{
	ASoccerGameMode* G = GM();
	if (!G || !G->Ball) return;
	G->Ball->SetOwnerPlayer(this);
	G->OnBallGained(this);
	AIDecisionTimer = 0.4f; // ИИ «осматривается» перед решением
	TouchCooldown = 0.f;
	bPendingKick = false;

	// Как в FIFA: если мяч получил партнёр человека — управление переходит к нему
	if (Team == HumanTeam && !IsPlayerControlled() && !bGoalkeeper)
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
	bPendingKick = false;
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
	SlidePoseTime = FMath::Max(0.f, SlidePoseTime - Dt);
	DivePoseTime  = FMath::Max(0.f, DivePoseTime - Dt);
	UpdateAnimation();
	UpdateBodyPose(Dt);

	StunTime         = FMath::Max(0.f, StunTime - Dt);
	TackleCooldown   = FMath::Max(0.f, TackleCooldown - Dt);
	SkillCooldown    = FMath::Max(0.f, SkillCooldown - Dt);
	ProtectTime      = FMath::Max(0.f, ProtectTime - Dt);
	GKTackleCooldown = FMath::Max(0.f, GKTackleCooldown - Dt);
	TouchCooldown    = FMath::Max(0.f, TouchCooldown - Dt);
	ControlCooldown  = FMath::Max(0.f, ControlCooldown - Dt);
	AIDecisionTimer -= Dt;
	bSprinting = false; // выставят TickHuman / ИИ

	// В меню, после гола и после финального свистка все стоят
	if (!G->IsPlayActive() || StunTime > 0.f)
	{
		UpdateLocomotion(Dt);
		return;
	}

	// Во время подката/финта/броска управление заблокировано (вратарь в броске ловит мяч)
	if (TickDash(Dt))
	{
		if (bGoalkeeper) TryKeeperSave();
		return;
	}

	if (bGoalkeeper)
	{
		TickGoalkeeper(Dt);
		UpdateLocomotion(Dt);
		return;
	}

	TryControlBall();
	if (HasBall())
	{
		TickDribble(Dt);
	}

	if (!TickPendingKick(Dt))
	{
		if (IsPlayerControlled())
		{
			TickHuman(Dt);
		}
		else
		{
			TickFieldAI(Dt);
		}
	}
	UpdateLocomotion(Dt);
}

// Инерция: разгон зависит от СКР, низкое трение не даёт мгновенно развернуть скорость,
// на бегу корпус поворачивается медленнее (поворот дугой). Спринт тратит выносливость.
void ASoccerPlayer::UpdateLocomotion(float Dt)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();

	if (bSprinting && Speed > RunSpeed * 0.8f)
	{
		Stamina -= Dt * FMath::Lerp(0.16f, 0.08f, Info.Physical / 100.f); // ФИЗ — устаёт медленнее
	}
	else
	{
		Stamina += Dt * 0.07f;
	}
	Stamina = FMath::Clamp(Stamina, 0.f, 1.f);

	const float PaceAlpha = FMath::Clamp((Info.Pace - 40) / 59.f, 0.f, 1.f);
	const float Accel = bGoalkeeper ? 2600.f : FMath::Lerp(1100.f, 1900.f, PaceAlpha);
	Move->MaxAcceleration = Accel * (HasBall() ? 0.85f : 1.f); // с мячом разгоняемся медленнее
	Move->BrakingDecelerationWalking = bGoalkeeper ? 2500.f : 1500.f;
	Move->GroundFriction = bGoalkeeper ? 8.f : 3.5f;
	const float SpeedRatio = FMath::Clamp(Speed / 700.f, 0.f, 1.f);
	Move->RotationRate = FRotator(0.f, FMath::Lerp(720.f, 300.f, SpeedRatio), 0.f);
}

bool ASoccerPlayer::TickDash(float Dt)
{
	if (DashTime <= 0.f) return false;

	DashTime -= Dt;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = !bGoalkeeper; // вратарь прыгает боком
	Move->MaxWalkSpeed = DashSpeed;
	Move->MaxAcceleration = 5000.f;
	Move->GroundFriction = 8.f;
	AddMovementInput(DashDir, 1.f);

	if (bSliding && !bSlideResolved)
	{
		ASoccerGameMode* G = GM();
		ASoccerBall* Ball = G->Ball;
		const FVector Foot = GetActorLocation() + DashDir * 70.f; // в подкате ноги впереди
		const FVector B = Ball->GetActorLocation();
		ASoccerPlayer* Carrier = Ball->OwnerPlayer;
		const bool bCanPlayBall = Carrier != this && (!Carrier || (Carrier->Team != Team && !Carrier->bGoalkeeper));

		if (bCanPlayBall && FVector::Dist2D(B, Foot) < 85.f && B.Z < 60.f)
		{
			// Чистый подкат: ноги первыми достали мяч — мяч выбит, соперник падает
			bSlideResolved = true;
			if (Carrier) Carrier->Stun(0.8f);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, DashDir) * FMath::FRandRange(-0.4f, 0.4f);
			Ball->Kick(this, (DashDir + Side).GetSafeNormal() * 750.f);
		}
		else
		{
			// Ноги соперника оказались раньше мяча — фол
			for (ASoccerPlayer* Other : G->Players)
			{
				if (!Other || Other->Team == Team || Other->StunTime > 0.f) continue;
				if (FVector::Dist2D(Other->GetActorLocation(), Foot) < 60.f)
				{
					bSlideResolved = true;
					G->OnFoul(this, Other);
					break;
				}
			}
		}
	}

	if (DashTime <= 0.f && bSliding)
	{
		bSliding = false;
		StunTime = 0.45f; // время подняться после подката
	}
	return true;
}

// ПРИЁМ МЯЧА (первое касание): мяч, пришедший к игроку, «прилипает» к ногам.
// Не справиться с приёмом можно только с очень сильным или неудобным мячом.
void ASoccerPlayer::TryControlBall()
{
	ASoccerBall* Ball = GM()->Ball;
	if (ControlCooldown > 0.f || HasBall() || GM()->MustKeepDistance(this)) return;
	if (Ball->OwnerPlayer) return; // мяч у другого игрока — отнять можно только отбором
	if (Ball->LastKicker == this && Ball->TimeSinceKick() < 0.3f) return; // только что ударили сами

	const FVector B = Ball->GetActorLocation();
	FVector ToBall = B - GetActorLocation();
	ToBall.Z = 0.f;
	const float Dist = ToBall.Size();

	// Зона приёма: игрок человека дотягивается дальше (помощь при приёме, как в FIFA)
	const float Reach = IsPlayerControlled() ? 105.f : 85.f;
	if (Dist > Reach || B.Z > 150.f) return; // выше полутора метров — только головой

	// Качество приёма: навык (ДРБ и ПАС) против сложности мяча — скорость, высота,
	// положение корпуса (спиной к мячу принимать сложнее) и приём на спринте
	const float Speed = Ball->Velocity.Size();
	const float Skill = (Info.Dribbling * 0.6f + Info.Passing * 0.4f) / 100.f;
	float Difficulty = Speed / 2200.f;
	if (B.Z > 60.f) Difficulty += 0.25f; // грудью или бедром
	const float Facing = FVector::DotProduct(GetActorForwardVector(), ToBall.GetSafeNormal());
	Difficulty += (1.f - Facing) * 0.15f;
	if (GetVelocity().Size2D() > RunSpeed * 1.1f) Difficulty += 0.15f;
	const float Quality = Skill - Difficulty * 0.6f + FMath::FRandRange(-0.15f, 0.15f);

	if (Quality > -0.1f)
	{
		// Приём: мяч под контролем и сразу подтягивается к ногам
		GainBall();
		GM()->PlaySfx(ESoccerSound::Touch, 0.6f);
	}
	else
	{
		// Не справился (очень сильный мяч): мяч отскакивает от ноги
		ControlCooldown = 0.4f;
		FVector Bounce = Ball->Velocity.MirrorByVector(ToBall.GetSafeNormal()) * 0.45f;
		Bounce = Bounce.RotateAngleAxis(FMath::FRandRange(-30.f, 30.f), FVector::UpVector);
		Ball->Kick(this, Bounce);
	}
}

// Где держится мяч при ведении: у ног перед игроком. Стоя и с LT — ближе, на спринте — чуть дальше.
// В ритме «касаний» мяч немного отходит от ноги и возвращается.
FVector ASoccerPlayer::GetDribbleSpot() const
{
	const float Speed = GetVelocity().Size2D();
	float Ahead = 50.f;
	if (bCloseControl)
	{
		Ahead = 42.f;
	}
	else if (Speed > RunSpeed * 1.05f)
	{
		Ahead = 58.f;
	}
	if (Speed > 80.f)
	{
		Ahead += (bCloseControl ? 4.f : 8.f) * (1.f - FMath::Cos(DribblePhase));
	}
	FVector Spot = GetActorLocation() + GetActorForwardVector() * Ahead;
	Spot.Z = BallRadius;

	// Мяч не проходит сквозь борта; в ворота — только через створ
	const bool bMouth = FMath::Abs(Spot.Y) < GoalHalfWidth - BallRadius;
	const double MaxX = bMouth ? HalfLength + GoalDepth - BallRadius : HalfLength - BallRadius;
	const double MaxY = HalfWidth + BoardGap - BallRadius;
	Spot.X = FMath::Clamp<double>(Spot.X, -MaxX, MaxX);
	Spot.Y = FMath::Clamp<double>(Spot.Y, -MaxY, MaxY);
	return Spot;
}

// ВЕДЕНИЕ: мяч «прилип» к ногам (его держит ASoccerBall::Tick в точке GetDribbleSpot).
// Здесь — только ритм касаний: мяч чуть отходит от ноги, слышно касание.
void ASoccerPlayer::TickDribble(float Dt)
{
	ASoccerBall* Ball = GM()->Ball;
	// Мяч оказался далеко от игрока (например, после телепорта) — контроль потерян
	if (FVector::Dist2D(Ball->GetActorLocation(), GetActorLocation()) > 300.f)
	{
		Ball->SetOwnerPlayer(nullptr);
		return;
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < 80.f)
	{
		DribblePhase = 0.f;
		return;
	}
	// Касание — каждые ~1.3 м бега (на спринте реже, с LT — чаще)
	const float Stride = bCloseControl ? 90.f : (bSprinting ? 170.f : 130.f);
	const float Before = DribblePhase;
	DribblePhase += Speed * Dt / Stride * 2.f * PI;
	if (FMath::FloorToInt(Before / (2.f * PI)) != FMath::FloorToInt(DribblePhase / (2.f * PI)))
	{
		GM()->PlaySfx(ESoccerSound::Touch, FMath::Clamp(Speed / 900.f, 0.2f, 0.7f));
	}
	if (DribblePhase > 1000.f)
	{
		DribblePhase = FMath::Fmod(DribblePhase, 2.f * PI);
	}
}

// Отложенный удар: бежим к мячу и бьём, как только нога его достаёт.
// Возвращает true, пока сама управляет движением игрока.
bool ASoccerPlayer::TickPendingKick(float Dt)
{
	if (!bPendingKick) return false;

	PendingTime -= Dt;
	if (PendingTime <= 0.f || !HasBall())
	{
		bPendingKick = false;
		return false;
	}
	if (CanKickBall())
	{
		ExecuteQueued();
		return true;
	}
	GetCharacterMovement()->bOrientRotationToMovement = true;
	MoveTo(GM()->Ball->GetActorLocation(), RunSpeed * 1.1f);
	return true;
}

void ASoccerPlayer::QueueKick(ECharge Kind, const FVector& AimDir, float Power01, bool bFinesse, bool bChip)
{
	bPendingKick = true;
	PendingKind = Kind;
	PendingAim = AimDir;
	PendingPower = Power01;
	bPendingFinesse = bFinesse;
	bPendingChip = bChip;
	PendingTime = 0.8f;
}

void ASoccerPlayer::ExecuteQueued()
{
	bPendingKick = false;
	switch (PendingKind)
	{
	case ECharge::Pass:    Pass(EPassKind::Ground, PendingAim, PendingPower); break;
	case ECharge::Through: Pass(EPassKind::Through, PendingAim, PendingPower); break;
	case ECharge::Lob:     Pass(EPassKind::Lob, PendingAim, PendingPower); break;
	case ECharge::Shot:    Shoot(PendingAim, PendingPower, bPendingFinesse, bPendingChip); break;
	default: break;
	}
}

// Рывок в фиксированном направлении (подкат, финт, выпад при отборе)
void ASoccerPlayer::StartDash(const FVector& Dir, float Speed, float Time, bool bSlide, bool bTurn)
{
	DashDir = Dir.GetSafeNormal2D();
	if (DashDir.IsNearlyZero()) DashDir = GetActorForwardVector();
	DashSpeed = Speed;
	DashTime = Time;
	bSliding = bSlide;
	if (bTurn)
	{
		SetActorRotation(DashDir.Rotation());
	}
}

// Поза тела без отдельных анимаций: в подкате модель ложится назад ногами вперёд,
// в броске вратаря — падает вбок. Поворот вокруг ступней, плавный вход и выход.
void ASoccerPlayer::UpdateBodyPose(float Dt)
{
	SlideAlpha = FMath::FInterpTo(SlideAlpha, SlidePoseTime > 0.f ? 1.f : 0.f, Dt, 14.f);
	DiveAlpha  = FMath::FInterpTo(DiveAlpha,  DivePoseTime  > 0.f ? 1.f : 0.f, Dt, 16.f);
	if (!IdleMesh) return; // капсулы не наклоняем

	const bool bPosed = SlideAlpha > 0.001f || DiveAlpha > 0.001f;
	if (!bPosed && !bPoseDirty) return;
	bPoseDirty = bPosed;

	const FQuat Base = FRotator(0.f, MeshYawOffset, 0.f).Quaternion();
	const FQuat SlideTilt(FVector(0.f, 1.f, 0.f), FMath::DegreesToRadians(-65.f * SlideAlpha));          // голова назад
	const FQuat DiveTilt(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(-75.f * DiveAlpha * DiveSign)); // голова вбок
	const FVector Loc(55.f * SlideAlpha, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
	GetMesh()->SetRelativeLocationAndRotation(Loc, DiveTilt * SlideTilt * Base);
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
	const bool bWantSprint = PC->SprintAxis > 0.3f;
	float Speed = bWantSprint ? SprintSpeedNow() : RunSpeed;
	bSprinting = bWantSprint && !Move.IsNearlyZero();

	bShielding = bHas && bLT;              // LT с мячом — укрывание корпусом и короткие касания
	bCloseControl = bShielding;
	const bool bJockey = !bHas && bLT;     // LT без мяча — жокей (лицом к мячу)
	bool bFaceBall = bJockey;

	if (bShielding || bJockey)
	{
		Speed = SlowSpeed;
		bSprinting = false;
	}
	else if (bHas)
	{
		Speed *= 0.93f; // с мячом чуть медленнее
	}

	// Помощь при приёме: пас летит мне, а стик отпущен — сам иду навстречу мячу
	if (!bHas && Move.IsNearlyZero() && !Ball->OwnerPlayer && Ball->IntendedReceiver == this)
	{
		FVector Meet;
		InterceptTime(Meet);
		FVector ToMeet = Meet - GetActorLocation();
		ToMeet.Z = 0.f;
		if (ToMeet.Size() > 40.f)
		{
			Move = ToMeet.GetSafeNormal();
		}
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

	// Стандарт у соперника: ближе положенного к мячу не подойти
	const ASoccerGameMode* G = GM();
	if (G->MustKeepDistance(this))
	{
		FVector Away = GetActorLocation() - BallLoc;
		Away.Z = 0.f;
		const float AwayDist = Away.Size();
		const FVector AwayDir = Away.GetSafeNormal();
		const float Radius = G->GetRestartRadius();
		if (AwayDist < Radius)
		{
			Move = AwayDir.IsNearlyZero() ? FVector(-AttackSign(), 0.f, 0.f) : AwayDir;
		}
		else if (AwayDist < Radius + 80.f)
		{
			const float Toward = -FVector::DotProduct(Move, AwayDir);
			if (Toward > 0.f) Move += AwayDir * Toward; // убираем движение к мячу
		}
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
//  ИИ полевого игрока. Роли раздаёт режим игры (ASoccerGameMode::UpdateTeamAI):
//  в обороне — прессинг, страховка, персональная опека; в атаке — открывания под пас.
// ---------------------------------------------------------------------------

// Ближайшая к P точка отрезка AB (на плоскости поля)
static FVector ClosestOnSegment2D(const FVector& P, const FVector& A, const FVector& B)
{
	FVector AB = B - A;
	AB.Z = 0.f;
	const double Len2 = AB.SizeSquared();
	const double T = Len2 > 1.0 ? FMath::Clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / Len2, 0.0, 1.0) : 0.0;
	return FVector(A.X + AB.X * T, A.Y + AB.Y * T, 0.0);
}

static float DistToSegment2D(const FVector& P, const FVector& A, const FVector& B)
{
	return FVector::Dist2D(P, ClosestOnSegment2D(P, A, B));
}

void ASoccerPlayer::SetAIRole(ESoccerAIRole InRole, ASoccerPlayer* InMark)
{
	AIRole = InRole;
	MarkTarget = InMark;
}

void ASoccerPlayer::PrepareRestart(float Delay)
{
	AIDecisionTimer = Delay;
	bPendingKick = false;
}

// Партнёры человека играют на «нормальном» уровне, соперник — на выбранной сложности
int32 ASoccerPlayer::AILevel() const
{
	const ASoccerGameMode* G = GM();
	return (Team == HumanTeam || !G) ? 1 : FMath::Clamp(G->GetDifficulty(), 0, 2);
}

float ASoccerPlayer::InterceptTime(FVector& OutPoint) const
{
	const ASoccerBall* Ball = GM()->Ball;
	const FVector Me = GetActorLocation();
	const float Speed = SprintSpeedNow() * SpeedFactor();
	for (float T = 0.f; T <= 2.5f; T += 0.1f)
	{
		const FVector B = Ball->PredictLocation(T);
		if (FVector::Dist2D(B, Me) - 60.f <= Speed * T)
		{
			OutPoint = B;
			return T;
		}
	}
	OutPoint = Ball->PredictLocation(2.5f);
	return 99.f;
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
	bCloseControl = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	if (HasBall())
	{
		TickAIWithBall(Dt);
		return;
	}

	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const FVector OwnGoal(-AttackSign() * HalfLength, 0.f, 0.f);

	// Стандарт у соперника: стоим в стороне, пока мяч не введён в игру
	if (G->MustKeepDistance(this))
	{
		TickRestartHold(Dt);
		return;
	}

	// Мяч в воздухе рядом — играем головой: у чужих ворот — удар, иначе — скидка вперёд
	if (CanHeadBall() && Ball->TimeSinceKick() > 0.15f)
	{
		const FVector OppGoal(AttackSign() * HalfLength, 0.f, 0.f);
		const bool bNearGoal = FVector::Dist2D(Me, OppGoal) < 900.f;
		Header(bNearGoal, bNearGoal ? (OppGoal - Me).GetSafeNormal2D() : FVector(AttackSign(), 0.f, 0.f));
		return;
	}

	// Мяч в руках вратаря соперника — не толпимся у него, а занимаем позиции
	if (Ball->OwnerPlayer && Ball->OwnerPlayer->bGoalkeeper && Ball->OwnerPlayer->Team != Team)
	{
		MoveTo(FormationPoint(), RunSpeed);
		return;
	}

	// Пас адресован мне — бегу навстречу мячу
	if (Ball->IntendedReceiver == this && !Ball->OwnerPlayer)
	{
		FVector Meet;
		InterceptTime(Meet);
		bSprinting = true;
		MoveTo(Meet, SprintSpeedNow());
		return;
	}

	switch (AIRole)
	{
	case ESoccerAIRole::Chase:
	{
		// На перехват: туда, где встречу мяч
		FVector Meet;
		InterceptTime(Meet);
		bSprinting = true;
		MoveTo(Meet, SprintSpeedNow() * 0.97f);
		break;
	}
	case ESoccerAIRole::Press:
		TickPress(Dt);
		break;
	case ESoccerAIRole::Cover:
	{
		// Страховка: между мячом и своими воротами, в 4 м от мяча
		const FVector Point = ClampToField(BallLoc + (OwnGoal - BallLoc).GetSafeNormal2D() * 400.f, 100.f);
		const float D = FVector::Dist2D(Point, Me);
		bSprinting = D > 500.f && Stamina > 0.3f;
		MoveTo(Point, bSprinting ? SprintSpeedNow() : RunSpeed);
		if (D < 200.f)
		{
			GetCharacterMovement()->bOrientRotationToMovement = false;
			FaceTowards(BallLoc, Dt);
		}
		break;
	}
	case ESoccerAIRole::Mark:
	{
		const ASoccerPlayer* Opp = MarkTarget.Get();
		if (!Opp)
		{
			MoveTo(FormationPoint(), RunSpeed);
			break;
		}
		// Опека: со стороны своих ворот и чуть ближе к мячу — чтобы успеть на перехват паса
		const FVector OppLoc = Opp->GetActorLocation();
		const FVector Point = ClampToField(OppLoc + (OwnGoal - OppLoc).GetSafeNormal2D() * 120.f
		                                          + (BallLoc - OppLoc).GetSafeNormal2D() * 60.f, 80.f);
		const float D = FVector::Dist2D(Point, Me);
		bSprinting = D > 400.f && Stamina > 0.3f;
		MoveTo(Point, bSprinting ? SprintSpeedNow() : RunSpeed);
		if (D < 150.f)
		{
			GetCharacterMovement()->bOrientRotationToMovement = false;
			FaceTowards(BallLoc, Dt);
		}
		break;
	}
	default:
	{
		// Своя команда с мячом — открываемся под пас, иначе держим позицию
		if (TeamHasBall())
		{
			SupportTimer -= Dt;
			if (SupportTimer <= 0.f || SupportPoint.IsZero())
			{
				SupportTimer = FMath::FRandRange(0.8f, 1.3f);
				SupportPoint = ComputeSupportPoint();
			}
			const float D = FVector::Dist2D(SupportPoint, Me);
			bSprinting = D > 450.f && Stamina > 0.35f;
			MoveTo(SupportPoint, bSprinting ? SprintSpeedNow() : RunSpeed);
		}
		else
		{
			SupportPoint = FVector::ZeroVector;
			MoveTo(FormationPoint(), RunSpeed);
		}
		break;
	}
	}
}

// Прессинг владельца мяча: сблизиться со стороны своих ворот, держать дистанцию («сдерживание»)
// и идти в отбор в удачный момент — например, когда мяч отскочил от ноги между касаниями.
void ASoccerPlayer::TickPress(float Dt)
{
	ASoccerBall* Ball = GM()->Ball;
	ASoccerPlayer* Carrier = Ball->OwnerPlayer;
	const FVector Me = GetActorLocation();
	if (!Carrier || Carrier->Team == Team || Carrier->bGoalkeeper)
	{
		FVector Meet;
		InterceptTime(Meet);
		bSprinting = true;
		MoveTo(Meet, SprintSpeedNow());
		return;
	}

	const int32 Level = AILevel();
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector OwnGoal(-AttackSign() * HalfLength, 0.f, 0.f);
	const FVector ToGoal = (OwnGoal - BallLoc).GetSafeNormal2D();
	const float Dist = FVector::Dist2D(Me, BallLoc);

	// Точка сдерживания: между мячом и воротами, с упреждением по ходу соперника
	const FVector Contain = BallLoc + ToGoal * 95.f + Carrier->GetVelocity() * 0.25f;
	if (Dist > 380.f)
	{
		bSprinting = true;
		MoveTo(Contain, SprintSpeedNow());
	}
	else
	{
		MoveTo(Contain, RunSpeed);
		GetCharacterMovement()->bOrientRotationToMovement = false;
		FaceTowards(BallLoc, Dt);
	}

	if (TackleCooldown > 0.f || AIDecisionTimer > 0.f) return;

	if (Dist < 125.f)
	{
		// Отбор: реакция и агрессивность зависят от сложности
		static const float Reaction[3] = { 0.45f, 0.3f, 0.2f };
		static const float Aggression[3] = { 0.2f, 0.3f, 0.4f };
		AIDecisionTimer = Reaction[Level];
		float Chance = Aggression[Level];
		if (FVector::Dist2D(Carrier->GetActorLocation(), BallLoc) > 55.f) Chance += 0.35f; // мяч отскочил от ноги
		if (Carrier->IsShielding()) Chance -= 0.15f;
		if (FMath::FRand() < Chance)
		{
			Tackle();
		}
	}
	else if (Dist < 260.f)
	{
		// Подкат, если соперник убегает к нашим воротам и догнать его уже не получается
		AIDecisionTimer = 0.3f;
		const FVector CarrierVel = Carrier->GetVelocity();
		const bool bBreaking = FVector::DotProduct(CarrierVel.GetSafeNormal2D(), ToGoal) > 0.6f && CarrierVel.Size2D() > 450.f;
		const bool bDanger = FVector::Dist2D(BallLoc, OwnGoal) < 1400.f;
		static const float SlideChance[3] = { 0.05f, 0.1f, 0.15f };
		if (bBreaking && bDanger && FMath::FRand() < SlideChance[Level])
		{
			SlideTackle((Ball->PredictLocation(0.35f) - Me).GetSafeNormal2D());
		}
	}
}

// Стандарт у соперника: не ближе положенного радиуса от мяча, лицом к мячу
void ASoccerPlayer::TickRestartHold(float Dt)
{
	const ASoccerGameMode* G = GM();
	const FVector BallLoc = G->Ball->GetActorLocation();
	FVector Away = GetActorLocation() - BallLoc;
	Away.Z = 0.f;
	const float Radius = G->GetRestartRadius();
	if (Away.Size() < Radius + 20.f)
	{
		const FVector Dir = Away.IsNearlyZero() ? FVector(-AttackSign(), 0.f, 0.f) : Away.GetSafeNormal();
		MoveTo(ClampToField(BallLoc + Dir * (Radius + 60.f), 60.f), RunSpeed);
	}
	GetCharacterMovement()->bOrientRotationToMovement = false;
	FaceTowards(BallLoc, Dt);
}

// Открывание: точка, куда партнёру удобно отдать пас. Нападающие ищут свободную зону впереди мяча
// в своём «коридоре», защитники страхуют сзади. Кандидаты оцениваются по свободному месту вокруг,
// открытости линии паса, продвижению к воротам и тому, не толпятся ли там партнёры.
FVector ASoccerPlayer::ComputeSupportPoint() const
{
	const ASoccerGameMode* G = GM();
	const FVector BallLoc = G->Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const float S = AttackSign();
	const bool bForward = RosterIndex >= 3;
	const float Side = Home.Y >= 0.f ? 1.f : -1.f;

	FVector Base;
	if (bForward)
	{
		Base = FVector(BallLoc.X + S * FMath::FRandRange(400.f, 800.f), Side * FMath::FRandRange(300.f, 750.f), 0.f);
	}
	else
	{
		Base = FVector(BallLoc.X - S * FMath::FRandRange(350.f, 600.f), Side * FMath::FRandRange(250.f, 600.f), 0.f);
	}
	Base.X = FMath::Clamp<double>(Base.X, -HalfLength + 250.0, HalfLength - 250.0);
	Base = ClampToField(Base, 150.f);

	FVector Best = Base;
	float BestScore = -1000.f;
	for (int32 i = 0; i < 10; ++i)
	{
		const FVector C = i == 0 ? Base
			: ClampToField(Base + FVector(FMath::FRandRange(-350.f, 350.f), FMath::FRandRange(-350.f, 350.f), 0.f), 150.f);
		float Space = 450.f;
		float LaneOpen = 250.f;
		float Crowd = 0.f;
		for (ASoccerPlayer* P : G->Players)
		{
			if (!P || P == this) continue;
			const float D = FVector::Dist2D(P->GetActorLocation(), C);
			if (P->Team != Team)
			{
				Space = FMath::Min(Space, D);
				LaneOpen = FMath::Min(LaneOpen, DistToSegment2D(P->GetActorLocation(), BallLoc, C));
			}
			else if (D < 350.f)
			{
				Crowd += (350.f - D) / 350.f;
			}
		}
		const float Score = Space / 450.f + LaneOpen / 250.f * 1.2f + (float)(C.X * S) / HalfLength * (bForward ? 0.5f : 0.2f)
		                  - (float)FVector::Dist2D(C, Me) / 2500.f - Crowd;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = C;
		}
	}
	return Best;
}

ASoccerPlayer* ASoccerPlayer::NearestOpponent(float& OutDist) const
{
	ASoccerPlayer* Best = nullptr;
	OutDist = TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (!P || P->Team == Team) continue;
		const float D = FVector::Dist2D(P->GetActorLocation(), GetActorLocation());
		if (D < OutDist)
		{
			OutDist = D;
			Best = P;
		}
	}
	return Best;
}

// Свободное место впереди (в сторону чужих ворот): расстояние до ближайшего соперника в «конусе»
float ASoccerPlayer::SpaceAhead() const
{
	const FVector Me = GetActorLocation();
	const FVector Dir = (FVector(AttackSign() * HalfLength, 0.f, 0.f) - Me).GetSafeNormal2D();
	float Space = 1000.f;
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (!P || P->Team == Team) continue;
		FVector ToOpp = P->GetActorLocation() - Me;
		ToOpp.Z = 0.f;
		const float D = ToOpp.Size();
		if (D > 1.f && FVector::DotProduct(ToOpp / D, Dir) > 0.35f)
		{
			Space = FMath::Min(Space, D);
		}
	}
	return Space;
}

// Направление ведения: к воротам, огибая соперников впереди и не прижимаясь к бортам
FVector ASoccerPlayer::DribbleDirection() const
{
	const FVector Me = GetActorLocation();
	const FVector Dir = (FVector(AttackSign() * HalfLength, 0.f, 0.f) - Me).GetSafeNormal2D();
	FVector Steer = Dir;
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (!P || P->Team == Team) continue;
		FVector ToOpp = P->GetActorLocation() - Me;
		ToOpp.Z = 0.f;
		const float D = ToOpp.Size();
		if (D > 450.f || D < 1.f) continue;
		if (FVector::DotProduct(ToOpp / D, Dir) < -0.2f) continue; // соперник сзади не мешает
		Steer -= (ToOpp / D) * ((450.f - D) / 450.f) * 1.3f;
	}
	if (FMath::Abs(Me.Y) > HalfWidth - 250.f)
	{
		Steer.Y -= FMath::Sign(Me.Y) * 0.8f;
	}
	Steer.Z = 0.f;
	return Steer.IsNearlyZero() ? Dir : Steer.GetSafeNormal();
}

// Направление «стика» для удара в точку створа: PlanShot берёт угол ворот из поперечной
// составляющей (Y·2), поэтому строим вектор с Y = AimFrac / 2.
FVector ASoccerPlayer::ShotAimDir(float AimFrac) const
{
	const float Y = FMath::Clamp(AimFrac, -1.f, 1.f) * 0.5f;
	return FVector(AttackSign() * FMath::Sqrt(1.f - Y * Y), Y, 0.f);
}

// Оценка удара: расстояние, видимый угол ворот и соперники на линии удара.
// Целимся в угол подальше от вратаря.
float ASoccerPlayer::EvaluateShot(float& OutAimFrac, float& OutPower, bool& bOutFinesse) const
{
	const ASoccerGameMode* G = GM();
	const FVector Me = GetActorLocation();
	const FVector OppGoal(AttackSign() * HalfLength, 0.f, 0.f);
	const float DX = FMath::Abs((float)(OppGoal.X - Me.X));
	const float DistGoal = FVector::Dist2D(Me, OppGoal);
	OutAimFrac = 0.f;
	OutPower = 0.7f;
	bOutFinesse = false;
	if (DistGoal > 1500.f || DX < 30.f) return 0.f;

	// Видимый угол ворот (рад): по центру с 6 м ≈ 0.5, с острого угла — почти 0
	const float MeY = Me.Y;
	const float Angle = FMath::Abs(FMath::Atan2(GoalHalfWidth - MeY, DX) - FMath::Atan2(-GoalHalfWidth - MeY, DX));
	float Score = FMath::Clamp(Angle / 0.35f, 0.f, 1.f) * FMath::Clamp(1.3f - DistGoal / 1300.f, 0.f, 1.f);

	float KeeperY = 0.f;
	if (const ASoccerPlayer* GK = G->GetGoalkeeper(1 - Team))
	{
		KeeperY = GK->GetActorLocation().Y;
	}
	OutAimFrac = (KeeperY > 0.f ? -1.f : 1.f) * FMath::FRandRange(0.55f, 0.95f);
	const FVector AimPoint(OppGoal.X, OutAimFrac * GoalHalfWidth * 0.8f, 0.f);

	// Соперники на линии удара — мяч скорее всего попадёт в них
	for (ASoccerPlayer* P : G->Players)
	{
		if (!P || P->Team == Team || P->bGoalkeeper) continue;
		if (DistToSegment2D(P->GetActorLocation(), Me, AimPoint) < 70.f)
		{
			Score *= 0.45f;
		}
	}
	OutPower = FMath::Lerp(0.55f, 0.95f, FMath::Clamp(DistGoal / 1400.f, 0.f, 1.f));
	bOutFinesse = DistGoal < 1000.f && FMath::FRand() < 0.35f;
	return Score;
}

// Оценка паса партнёру: безопасность линии (успеет ли соперник на перехват раньше мяча),
// свободное место у точки приёма и продвижение к воротам. Меньше нуля — пас опасный.
float ASoccerPlayer::EvaluatePass(const ASoccerPlayer* Mate, EPassKind Kind, FVector& OutTarget) const
{
	const ASoccerGameMode* G = GM();
	const FVector From = G->Ball->GetActorLocation();
	const FVector MateLoc = Mate->GetActorLocation();
	const float S = AttackSign();

	FVector Target = Kind == EPassKind::Through
		? MateLoc + FVector(S * 400.f, 0.f, 0.f) + Mate->GetVelocity() * 0.5f
		: MateLoc + Mate->GetVelocity() * 0.4f;
	Target = ClampToField(Target, 120.f);
	OutTarget = Target;

	const float Dist = FVector::Dist2D(From, Target);
	if (Dist < 250.f || Dist > 2400.f) return -1.f;

	const float BallSpeed = FMath::Clamp(Dist * 0.8f + 700.f, 900.f, 2400.f);
	float Safety = 1.f;  // запас времени (с): насколько мяч опережает самого быстрого перехватчика
	float Open = 500.f;  // свободное место у точки приёма
	for (ASoccerPlayer* P : G->Players)
	{
		if (!P || P->Team == Team) continue;
		const FVector OppLoc = P->GetActorLocation();
		Open = FMath::Min(Open, (float)FVector::Dist2D(OppLoc, Target));
		if (Kind == EPassKind::Lob) continue; // навес летит над соперниками
		const FVector Q = ClosestOnSegment2D(OppLoc, From, Target);
		const float TBall = FVector::Dist2D(From, Q) / BallSpeed;
		const float TOpp = FMath::Max(0.f, (float)FVector::Dist2D(OppLoc, Q) - (P->bGoalkeeper ? 120.f : 70.f)) / 600.f;
		Safety = FMath::Min(Safety, TOpp - TBall);
	}
	if (Kind == EPassKind::Lob)
	{
		Safety = (Open - 150.f) / 400.f; // навес опасен, только если у точки приземления соперник
	}
	if (Kind == EPassKind::Through)
	{
		// Пас на ход: партнёр должен успеть к мячу раньше соперников
		const float TMate = FVector::Dist2D(MateLoc, Target) / 650.f;
		const float TOpp = FMath::Max(0.f, Open - 60.f) / 600.f;
		if (TMate > TOpp) Safety = FMath::Min(Safety, TOpp - TMate);
	}

	const float Progress = FMath::Clamp((float)(Target.X - From.X) * S / 1000.f, -1.f, 1.f);
	return FMath::Clamp(Safety, -1.f, 0.6f) * 1.6f + Open / 500.f * 0.5f + Progress * 0.7f;
}

bool ASoccerPlayer::FindBestPass(EPassKind& OutKind, FVector& OutTarget, float& OutScore) const
{
	const ASoccerGameMode* G = GM();
	const FVector Me = GetActorLocation();
	const float S = AttackSign();
	const float OppGoalX = S * HalfLength;
	const bool bWide = FMath::Abs(Me.Y) > 500.f && FMath::Abs(OppGoalX - Me.X) < 1000.f;
	OutScore = -1000.f;
	bool bFound = false;

	for (ASoccerPlayer* Mate : G->Players)
	{
		if (!Mate || Mate == this || Mate->Team != Team || Mate->bGoalkeeper) continue;
		const FVector MateLoc = Mate->GetActorLocation();
		auto Consider = [&](EPassKind Kind, float Bonus)
		{
			FVector Target;
			const float Score = EvaluatePass(Mate, Kind, Target) + Bonus;
			if (Score > OutScore)
			{
				OutScore = Score;
				OutKind = Kind;
				OutTarget = Target;
				bFound = true;
			}
		};
		Consider(EPassKind::Ground, 0.f);
		// На ход — партнёру, который впереди и может убежать к воротам
		if ((MateLoc.X - Me.X) * S > 100.f)
		{
			Consider(EPassKind::Through, 0.1f);
		}
		// Навес в штрафную с фланга
		if (bWide && FMath::Abs(OppGoalX - MateLoc.X) < PenaltyDepth + 100.f && FMath::Abs(MateLoc.Y) < 450.f)
		{
			Consider(EPassKind::Lob, 0.35f);
		}
	}
	return bFound;
}

// Решение с мячом: удар, пас или продолжить ведение. Чем выше сложность, тем меньше «шума» в оценках.
bool ASoccerPlayer::DecideWithBall(int32 Level)
{
	static const float Noise[3] = { 0.3f, 0.18f, 0.08f };
	const float N = Noise[Level];
	float OppDist = 0.f;
	NearestOpponent(OppDist);

	float AimFrac = 0.f, Power = 0.7f;
	bool bFinesse = false;
	const float ShotScore = EvaluateShot(AimFrac, Power, bFinesse) + FMath::FRandRange(-N, N);

	EPassKind Kind = EPassKind::Ground;
	FVector Target = FVector::ZeroVector;
	float PassScore = -1000.f;
	FindBestPass(Kind, Target, PassScore);
	PassScore += FMath::FRandRange(-N, N);

	// Вести мяч выгодно, когда впереди свободно; под прессингом лучше отдать
	float DribbleScore = FMath::Clamp(SpaceAhead() / 600.f, 0.f, 1.f) * 0.9f;
	if (OppDist < 160.f) DribbleScore -= 0.35f;

	if (ShotScore > 0.45f && ShotScore >= PassScore - 0.1f)
	{
		Shoot(ShotAimDir(AimFrac), Power, bFinesse);
		return true;
	}
	if (PassScore > 0.25f && PassScore > DribbleScore)
	{
		const float PassPower = Kind == EPassKind::Lob ? 0.5f : FMath::FRandRange(0.25f, 0.5f);
		Pass(Kind, (Target - GM()->Ball->GetActorLocation()).GetSafeNormal2D(), PassPower);
		return true;
	}
	return false;
}

// ИИ разыгрывает стандарт: пенальти — в угол, штрафной у ворот — закрученный удар,
// иначе (и разводка с центра) — лучший пас
void ASoccerPlayer::TakeRestart()
{
	ASoccerGameMode* G = GM();
	const ESoccerRestart Kind = G->GetRestart();
	AIDecisionTimer = 1.f;
	float AimFrac = 0.f, Power = 0.7f;
	bool bFinesse = false;

	if (Kind == ESoccerRestart::Penalty)
	{
		AimFrac = (FMath::RandBool() ? 1.f : -1.f) * FMath::FRandRange(0.4f, 0.95f);
		Shoot(ShotAimDir(AimFrac), FMath::FRandRange(0.65f, 0.9f), FMath::FRand() < 0.3f);
		return;
	}
	if (Kind == ESoccerRestart::FreeKick && EvaluateShot(AimFrac, Power, bFinesse) > 0.2f)
	{
		Shoot(ShotAimDir(AimFrac), FMath::Max(Power, 0.7f), FMath::FRand() < 0.65f);
		return;
	}
	EPassKind PassKind = EPassKind::Ground;
	FVector Target = FVector::ZeroVector;
	float Score = 0.f;
	if (FindBestPass(PassKind, Target, Score))
	{
		Pass(PassKind, (Target - G->Ball->GetActorLocation()).GetSafeNormal2D(), 0.35f);
	}
	else
	{
		Pass(EPassKind::Ground, FVector(AttackSign(), 0.f, 0.f), 0.4f);
	}
}

void ASoccerPlayer::TickAIWithBall(float Dt)
{
	ASoccerGameMode* G = GM();
	const FVector Me = GetActorLocation();
	const FVector OppGoal(AttackSign() * HalfLength, 0.f, 0.f);
	const FVector ToGoal = (OppGoal - Me).GetSafeNormal2D();
	const float DistGoal = FVector::Dist2D(Me, OppGoal);
	float OppDist = 0.f;
	ASoccerPlayer* Opp = NearestOpponent(OppDist);
	bCloseControl = OppDist < 250.f; // соперник рядом — короткие касания

	// Исполнитель стандарта: стоит у мяча и после паузы разыгрывает
	if (G->IsRestartTaker(this))
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
		FaceTowards(OppGoal, Dt);
		if (AIDecisionTimer <= 0.f)
		{
			TakeRestart();
		}
		return;
	}

	const int32 Level = AILevel();
	if (AIDecisionTimer <= 0.f)
	{
		static const float Think[3] = { 0.55f, 0.4f, 0.28f };
		AIDecisionTimer = Think[Level] * FMath::FRandRange(0.8f, 1.2f);
		if (DecideWithBall(Level)) return;

		// Соперник вплотную — финт в сторону от него (чем выше ДРБ, тем чаще)
		if (Opp && OppDist < 180.f && SkillCooldown <= 0.f && FMath::FRand() < Info.Dribbling / 200.f)
		{
			FVector Side = FVector::CrossProduct(FVector::UpVector, ToGoal);
			if (FVector::DotProduct(Side, Opp->GetActorLocation() - Me) > 0.f) Side = -Side;
			SkillMove((Side + ToGoal * 0.5f).GetSafeNormal2D(), FMath::FRand() < 0.4f);
			return;
		}
	}

	const FVector Dir = DribbleDirection();
	bSprinting = !bCloseControl && DistGoal > 900.f && Stamina > 0.35f && SpaceAhead() > 450.f;
	MoveTo(Me + Dir * 300.f, bSprinting ? SprintSpeedNow() : RunSpeed * 0.95f);
}

// ---------------------------------------------------------------------------
//  ИИ вратаря: стоит на линии ворот, смещается за мячом, прыгает, ловит
// ---------------------------------------------------------------------------
void ASoccerPlayer::TickGoalkeeper(float Dt)
{
	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const float Side = -AttackSign();          // с какой стороны наши ворота
	const float GoalX = Side * HalfLength;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	if (IsPlayerControlled())
	{
		// Ваш вратарь с мячом — вы сами выбираете, куда отдать пас
		if (HasBall())
		{
			TickHumanKeeper(Dt);
			return;
		}
		// Мяча у вратаря больше нет — управление переходит к полевому игроку, вратарь снова под ИИ
		HandOverFromKeeper();
	}

	// Мяч в руках: осмотреться и через секунду ввести в игру
	if (HasBall())
	{
		FaceTowards(Me + FVector(AttackSign() * 100.f, 0.f, 0.f), Dt);
		GKHoldTime += Dt;
		if (GKHoldTime > 1.2f)
		{
			KeeperDistribute();
		}
		return;
	}

	// После броска вратарь ещё лежит — только добирает мяч рядом
	if (DivePoseTime > 0.f)
	{
		TryKeeperSave();
		return;
	}

	// Задержка реакции при выборе позиции: у вратаря соперника зависит от сложности
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

	MoveTo(Target, bRush ? KeeperSpeed * 1.3f : KeeperSpeed);
	FaceTowards(BallLoc, Dt);

	// Бросок: удар летит в створ, а мяч пройдёт в стороне от вратаря — прыгаем в угол
	if (!Ball->OwnerPlayer && Ball->Velocity.X * Side > 700.f)
	{
		DecideShot();
		const float TMe = (Me.X - BallLoc.X) / Ball->Velocity.X;      // когда мяч будет на уровне вратаря
		const float TLine = (GoalX - BallLoc.X) / Ball->Velocity.X;   // когда — на линии ворот
		const bool bOnTarget = FMath::Abs(BallLoc.Y + Ball->Velocity.Y * TLine) < GoalHalfWidth + 20.f;
		if (!bGKDived && bOnTarget && TMe > 0.f && TMe < 0.6f)
		{
			const float Lateral = BallLoc.Y + Ball->Velocity.Y * TMe - Me.Y;
			if (FMath::Abs(Lateral) > 45.f && FMath::Abs(Lateral) < 320.f)
			{
				bGKDived = true;
				const FVector DiveDir(0.f, FMath::Sign(Lateral), 0.f);
				// Если удар «берётся» — прыжок точный, если нет — вратарь чуть не дотягивается
				StartDash(DiveDir, bGKWillSave ? 950.f : 520.f, 0.35f, false, false);
				DivePoseTime = 0.9f;
				DiveSign = FVector::DotProduct(DiveDir, GetActorRightVector()) >= 0.f ? 1.f : -1.f;
				return;
			}
		}
	}

	TryKeeperSave();
}


// Ваш вратарь с мячом в руках: можно пройти с мячом по своей штрафной и выбрать направление паса.
// A — пас низом, Y — пас на ход, X или B — выбить далеко (зажать — сила). Через 6 с вратарь отдаёт пас сам.
void ASoccerPlayer::TickHumanKeeper(float Dt)
{
	const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetController());
	if (!PC) return;
	GKHoldTime += Dt;

	const FVector Me = GetActorLocation();
	const float GoalX = -AttackSign() * HalfLength;
	FVector Move = PC->StickToWorld(PC->MoveInput);
	Move.Z = 0.f;
	if (DivePoseTime > 0.f)
	{
		Move = FVector::ZeroVector; // ещё поднимается после броска
	}
	// Из своей штрафной с мячом в руках выходить нельзя
	const FVector Next = Me + Move.GetSafeNormal() * 60.f;
	if (FMath::Abs(Next.X - GoalX) > PenaltyDepth - 40.f || FMath::Abs(Next.Y) > GoalHalfWidth + 360.f ||
	    FMath::Abs(Next.X) > HalfLength - 40.f)
	{
		Move = FVector::ZeroVector;
	}
	GetCharacterMovement()->MaxWalkSpeed = KeeperSpeed * 0.6f;
	if (!Move.IsNearlyZero())
	{
		AddMovementInput(Move);
	}

	// Вратарь смотрит туда, куда полетит пас
	FaceTowards(Me + PC->AimDirection() * 100.f, Dt);

	if (GKHoldTime > 6.f)
	{
		KeeperDistribute();
	}
}

// Вратарь вводит мяч сам: открытому партнёру, а если все закрыты — длинным навесом вперёд
void ASoccerPlayer::KeeperDistribute()
{
	const FVector BallLoc = GM()->Ball->GetActorLocation();
	EPassKind Kind = EPassKind::Ground;
	FVector Target = FVector::ZeroVector;
	float Score = 0.f;
	if (FindBestPass(Kind, Target, Score) && Score > 0.1f)
	{
		Pass(Kind, (Target - BallLoc).GetSafeNormal2D(), 0.4f);
	}
	else
	{
		Pass(EPassKind::Lob, FVector(AttackSign(), 0.f, 0.f), 0.7f);
	}
}

// Управление — полевому игроку: адресату паса, иначе ближайшему к мячу
void ASoccerPlayer::HandOverFromKeeper()
{
	ASoccerPlayerController* PC = HumanPC();
	ASoccerGameMode* G = GM();
	if (!PC || !G || !G->Ball) return;

	ASoccerPlayer* Next = G->Ball->IntendedReceiver;
	if (!Next || Next->Team != Team || Next->bGoalkeeper)
	{
		Next = nullptr;
		const FVector Spot = G->Ball->PredictLocation(1.f);
		double BestDist = TNumericLimits<double>::Max();
		for (ASoccerPlayer* P : G->Players)
		{
			if (!P || P->Team != Team || P->bGoalkeeper) continue;
			const double D = FVector::DistSquared2D(P->GetActorLocation(), Spot);
			if (D < BestDist)
			{
				BestDist = D;
				Next = P;
			}
		}
	}
	if (Next)
	{
		PC->PossessPlayer(Next);
	}
}

// Вратарь решает один раз на каждый удар, возьмёт ли он мяч.
// Базовый шанс по сложности; сильный удар, удар в угол и удар в упор его снижают.
void ASoccerPlayer::DecideShot()
{
	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	if (GKDecisionKick == Ball->GetLastKickTime()) return;
	GKDecisionKick = Ball->GetLastKickTime();
	bGKDived = false;

	const FVector BallLoc = Ball->GetActorLocation();
	const float GoalX = -AttackSign() * HalfLength;
	const int32 Diff = FMath::Clamp(G->GetDifficulty(), 0, 2);

	static const float BaseSave[3] = { 0.68f, 0.8f, 0.9f };
	float Chance = Team == HumanTeam ? 0.8f : BaseSave[Diff];
	Chance -= FMath::Max(0.f, (float)Ball->Velocity.Size2D() - 1500.f) / 6000.f;

	float CrossY = BallLoc.Y; // где мяч пересечёт линию ворот
	if (FMath::Abs(Ball->Velocity.X) > 1.f)
	{
		const float T = (GoalX - BallLoc.X) / Ball->Velocity.X;
		if (T > 0.f) CrossY = BallLoc.Y + Ball->Velocity.Y * T;
	}
	Chance -= 0.3f * FMath::Clamp(FMath::Abs(CrossY) / GoalHalfWidth, 0.f, 1.f);
	if (FMath::Abs(BallLoc.X - GoalX) < 500.f) Chance -= 0.1f; // удар в упор

	bGKWillSave = FMath::FRand() < FMath::Clamp(Chance, 0.1f, 0.92f);
}

// Мяч в зоне досягаемости: поймать в руки, отбить сильный удар или забрать мяч у нападающего
void ASoccerPlayer::TryKeeperSave()
{
	ASoccerBall* Ball = GM()->Ball;
	if (HasBall() || TeamHasBall()) return;

	const FVector BallLoc = Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const float Reach = DivePoseTime > 0.f ? 140.f : 95.f; // в броске дотягивается дальше
	if (FVector::Dist2D(BallLoc, Me) > Reach || BallLoc.Z > 230.f) return;
	if (Ball->LastKicker == this && Ball->TimeSinceKick() < 0.5f) return;

	if (ASoccerPlayer* Carrier = Ball->OwnerPlayer)
	{
		// Нападающий с мячом рядом — бросок в ноги, не чаще раза в секунду
		if (GKTackleCooldown > 0.f) return;
		GKTackleCooldown = 1.f;
		if (FMath::FRand() < 0.45f)
		{
			Carrier->Stun(0.6f);
			KeeperCatch();
		}
		return;
	}

	const float Speed = Ball->Velocity.Size2D();
	if (Speed >= 700.f)
	{
		DecideShot();
		if (!bGKWillSave) return; // этот удар вратарь не берёт
	}

	if (Speed < 2600.f)
	{
		KeeperCatch();
	}
	else
	{
		// Слишком сильный удар — отбивает кулаками в сторону от ворот
		const FVector Dir = FVector(AttackSign(), BallLoc.Y >= Me.Y ? 0.8f : -0.8f, 0.f).GetSafeNormal();
		GM()->OnKeeperSave(this);
		Ball->Kick(this, Dir * 1400.f + FVector(0.f, 0.f, 400.f));
	}
}

void ASoccerPlayer::KeeperCatch()
{
	ASoccerGameMode* G = GM();
	G->Ball->SetOwnerPlayer(this); // мяч в руках (см. ASoccerBall::Tick)
	G->OnKeeperSave(this);
	G->OnBallGained(this);
	GKHoldTime = 0.f;
	DashTime = 0.f;

	// Мяч поймал ваш вратарь — управление переходит к нему: вы сами выбираете, куда отдать пас
	if (Team == HumanTeam && !IsPlayerControlled())
	{
		if (ASoccerPlayerController* PC = HumanPC())
		{
			PC->PossessPlayer(this);
		}
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

// Разброс направления удара/паса в градусах: навык, сила, угол корпуса к цели,
// прессинг соперника, удар в одно касание и удар на спринте
float ASoccerPlayer::KickErrorDegrees(int32 Skill, float Power01, const FVector& Dir, bool bFirstTime) const
{
	float Err = FMath::Lerp(9.f, 1.5f, FMath::Clamp((Skill - 40) / 59.f, 0.f, 1.f));  // 40 — 9°, 99 — 1.5°
	Err *= FMath::Lerp(0.7f, 1.4f, FMath::Clamp(Power01, 0.f, 1.f));                   // сильнее — неточнее
	const float Facing = FVector::DotProduct(GetActorForwardVector(), Dir.GetSafeNormal2D());
	Err *= FMath::Lerp(2.f, 1.f, FMath::Clamp((Facing + 0.2f) / 1.2f, 0.f, 1.f));    // бьёт не туда, куда смотрит
	float OppDist = 0.f;
	NearestOpponent(OppDist);
	if (OppDist < 150.f) Err *= 1.35f;                                                  // прессинг
	if (bFirstTime) Err *= 1.25f;                                                       // в одно касание
	if (GetVelocity().Size2D() > RunSpeed * 1.1f) Err *= 1.2f;                          // на спринте
	return Err;
}

FSoccerKick ASoccerPlayer::PlanPass(EPassKind Kind, const FVector& AimDir, float Power01, bool bWithError) const
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
		const float Range = Kind == EPassKind::Lob ? FMath::Lerp(800.f, 2600.f, Power) : FMath::Lerp(700.f, 2000.f, Power);
		Target = From + Aim * Range;
	}
	Target = ClampToField(Target, 100.f);
	Plan.Target = Target;

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	FVector Dir = Flat / Dist;
	if (bWithError)
	{
		const float Err = KickErrorDegrees(Info.Passing, Power, Dir, !HasBall());
		Dir = Dir.RotateAngleAxis((FMath::FRand() - FMath::FRand()) * Err, FVector::UpVector);
	}

	if (Kind == EPassKind::Lob)
	{
		// Навес с нижним вращением: мяч «парит» и гасится при приземлении.
		// Время полёта T = 2·Vz/g, где g уменьшена подъёмной силой от вращения; +5% на сопротивление воздуха.
		const float BackSpin = 10.f;
		const float Land = Mate ? Dist * FMath::Lerp(1.f, 1.2f, Power) : Dist;
		const float Vz = FMath::Clamp(400.f + Land * 0.25f, 500.f, 950.f);
		const float Estimate = Land / (2.f * Vz / Ball->Gravity);
		const float GEff = FMath::Max(600.f, Ball->Gravity - Ball->MagnusCoeff * BackSpin * Estimate);
		const float Horizontal = Land / (2.f * Vz / GEff) * 1.05f;
		Plan.Velocity = Dir * Horizontal + FVector(0.f, 0.f, Vz);
		Plan.Spin = -FVector::CrossProduct(FVector::UpVector, Dir) * BackSpin;
	}
	else
	{
		// Мяч по газону: с экспоненциальным трением путь = (v0 - v1) / k,
		// значит v0 = путь * k + скорость_прихода. Даже самый слабый пас приходит к партнёру
		// с запасом скорости (6 м/с), сила замаха делает пас резче — до 1.4x.
		const float Arrive = Kind == EPassKind::Through ? 500.f : 600.f;
		const float Speed = Mate ? (Dist * Ball->RollingFriction + Arrive) * FMath::Lerp(1.f, 1.4f, Power)
		                         : Dist * Ball->RollingFriction + 250.f;
		Plan.Velocity = Dir * FMath::Clamp(Speed, 700.f, 2800.f);
		Plan.Spin = FVector::CrossProduct(FVector::UpVector, Plan.Velocity) / BallRadius; // катится
	}
	return Plan;
}

FSoccerKick ASoccerPlayer::PlanShot(const FVector& AimDir, float Power01, bool bFinesse, bool bChip, bool bWithError) const
{
	FSoccerKick Plan;
	const ASoccerBall* Ball = GM()->Ball;
	const FVector From = Ball->GetActorLocation();
	const float Power = FMath::Clamp(Power01, 0.f, 1.f);

	// Куда в створ: поперечная составляющая стика выбирает угол ворот
	const float AimY = FMath::Clamp((float)AimDir.Y * 2.f, -1.f, 1.f) * GoalHalfWidth * 0.8f;
	const FVector Target(AttackSign() * HalfLength, AimY, 0.f);
	Plan.Target = Target;

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	FVector Dir = Flat / Dist;

	float Err = 0.f;
	if (bWithError)
	{
		Err = KickErrorDegrees(Info.Shooting, Power, Dir, !HasBall()) * (bFinesse ? 0.7f : 1.f);
		Dir = Dir.RotateAngleAxis((FMath::FRand() - FMath::FRand()) * Err, FVector::UpVector);
	}

	// Характеристика УДР: 50 -> 0.85, 99 -> 1.14 от базовой силы удара
	const float ShotFactor = FMath::Clamp(0.85f + (Info.Shooting - 50) * 0.006f, 0.8f, 1.15f);
	const FVector Topspin = FVector::CrossProduct(FVector::UpVector, Dir); // ось верхнего вращения
	float Speed;
	float TargetZ;     // на какой высоте мяч должен пересечь линию ворот
	float SpinGravity; // добавка к гравитации от вращения (верхнее — вниз, нижнее — вверх)

	if (bChip)
	{
		// «Парашют» (LB + B): мягкий высокий удар с нижним вращением — через вышедшего вратаря
		Speed = FMath::Lerp(900.f, 1300.f, Power);
		TargetZ = 170.f;
		Plan.Spin = -Topspin * 12.f;
		SpinGravity = -Ball->MagnusCoeff * 12.f * Speed;
	}
	else if (bFinesse)
	{
		// Изящный удар (RB + B): медленнее, точнее, с боковым вращением — заворачивает в угол
		Speed = FMath::Lerp(1300.f, 3000.f, Power) * ShotFactor * 0.8f;
		TargetZ = 110.f;
		SpinGravity = 0.f;
	}
	else
	{
		// Обычный удар с верхним вращением: мяч «ныряет»; чем сильнее — тем выше целимся
		Speed = FMath::Lerp(1300.f, 3000.f, Power) * ShotFactor;
		TargetZ = FMath::Lerp(40.f, 150.f, Power);
		Plan.Spin = Topspin * 15.f;
		SpinGravity = Ball->MagnusCoeff * 15.f * Speed;
	}
	if (bWithError)
	{
		TargetZ += (FMath::FRand() - FMath::FRand()) * Err * 12.f; // разброс по высоте
	}

	// Подбираем вертикальную скорость, чтобы на линии ворот мяч был на высоте TargetZ.
	// Время полёта с учётом сопротивления воздуха (~7%).
	const float T = Dist / (Speed * 0.93f);
	const float GEff = Ball->Gravity + SpinGravity;
	const float Vz = (TargetZ - From.Z + 0.5f * GEff * T * T) / T;

	if (bFinesse)
	{
		// Отклоняем старт на A градусов наружу и закручиваем боковым вращением обратно в створ:
		// −v·sin(A)·T + a·T²/2 = 0  =>  a = 2·v·sin(A)/T,  a = k·ω·v  =>  ω = 2·sin(A)/(k·T)
		const float Angle = FMath::DegreesToRadians(12.f);
		FVector Perp = FVector::CrossProduct(FVector::UpVector, Dir);
		float SpinSign = 1.f;
		if (Perp.Y * AimY > 0.f)
		{
			Perp = -Perp; // стартуем от центра ворот наружу
			SpinSign = -1.f;
		}
		Dir = (Dir * FMath::Cos(Angle) - Perp * FMath::Sin(Angle)).GetSafeNormal();
		Plan.Spin = FVector(0.f, 0.f, SpinSign * 2.f * FMath::Sin(Angle) / (Ball->MagnusCoeff * T));
	}

	Plan.Velocity = Dir * Speed + FVector(0.f, 0.f, Vz);
	return Plan;
}

void ASoccerPlayer::ExecuteKick(const FSoccerKick& Plan)
{
	ASoccerBall* Ball = GM()->Ball;
	bPendingKick = false;
	SetActorRotation(FRotator(0.f, Plan.Velocity.GetSafeNormal2D().Rotation().Yaw, 0.f));
	Ball->Kick(this, Plan.Velocity, Plan.Spin);
	Ball->IntendedReceiver = Plan.Receiver; // ИИ-партнёр побежит принимать
}

void ASoccerPlayer::Pass(EPassKind Kind, const FVector& AimDir, float Power01)
{
	if (!CanKickBall())
	{
		// Мяч наш, но укатился вперёд — добегаем и отдаём пас
		if (HasBall())
		{
			const ECharge Kick = Kind == EPassKind::Ground ? ECharge::Pass : (Kind == EPassKind::Through ? ECharge::Through : ECharge::Lob);
			QueueKick(Kick, AimDir, Power01, false, false);
		}
		return;
	}
	ExecuteKick(PlanPass(Kind, AimDir, Power01, true));
	GM()->OnPassMade(this);
	if (IsPlayerControlled())
	{
		GM()->OnHumanPass();
	}
}

void ASoccerPlayer::Shoot(const FVector& AimDir, float Power01, bool bFinesse, bool bChip)
{
	if (!CanKickBall())
	{
		if (HasBall())
		{
			QueueKick(ECharge::Shot, AimDir, Power01, bFinesse, bChip);
		}
		return;
	}
	ExecuteKick(PlanShot(AimDir, Power01, bFinesse, bChip, true));
	GM()->OnShot(this);
}

// Игра головой: прыжок и удар по мячу в воздухе — слабее и менее точно, чем ногой.
// Удар по воротам — сверху вниз, скидка — к партнёру по направлению.
void ASoccerPlayer::Header(bool bShot, const FVector& AimDir)
{
	if (!CanHeadBall()) return;
	ASoccerBall* Ball = GM()->Ball;
	LaunchCharacter(FVector(0.f, 0.f, 380.f), false, true);

	FSoccerKick Plan;
	FVector Dir = AimDir.IsNearlyZero() ? GetActorForwardVector() : AimDir.GetSafeNormal2D();
	float Speed = 1100.f;
	if (bShot)
	{
		const FVector Goal(AttackSign() * HalfLength, FMath::Clamp((float)AimDir.Y * 2.f, -1.f, 1.f) * GoalHalfWidth * 0.7f, 0.f);
		Dir = (Goal - Ball->GetActorLocation()).GetSafeNormal2D();
		Speed = 1600.f * FMath::Clamp(0.85f + (Info.Shooting - 50) * 0.006f, 0.8f, 1.15f);
	}
	else if (ASoccerPlayer* Mate = FindPassTarget(Dir))
	{
		Dir = (Mate->GetActorLocation() - Ball->GetActorLocation()).GetSafeNormal2D();
		Plan.Receiver = Mate;
	}
	const float Err = KickErrorDegrees(bShot ? Info.Shooting : Info.Passing, 0.6f, Dir, true) * 1.5f;
	Dir = Dir.RotateAngleAxis((FMath::FRand() - FMath::FRand()) * Err, FVector::UpVector);

	Plan.Velocity = Dir * Speed + FVector(0.f, 0.f, bShot ? -150.f : 150.f);
	ExecuteKick(Plan);
	if (bShot) GM()->OnShot(this);
	else       GM()->OnPassMade(this);
}

// Отбор ногой: игрок выставляет ногу к мячу. Достал мяч — чистый отбор (кто сильнее, тот и забрал,
// иначе мяч отскакивает свободным). Не достал мяч, но попал в ноги (особенно сзади) — фол.
void ASoccerPlayer::Tackle()
{
	if (TackleCooldown > 0.f || GM()->MustKeepDistance(this)) return;
	TackleCooldown = 0.8f;

	ASoccerGameMode* G = GM();
	ASoccerBall* Ball = G->Ball;
	ASoccerPlayer* Carrier = Ball->OwnerPlayer;
	if (!Carrier || Carrier->Team == Team || Carrier->bGoalkeeper) return; // мяч в руках вратаря не отнять

	const FVector Me = GetActorLocation();
	const FVector CarrierLoc = Carrier->GetActorLocation();
	const FVector BallLoc = Ball->GetActorLocation();
	FVector ToBall = BallLoc - Me;
	ToBall.Z = 0.f;
	if (FVector::Dist2D(Me, CarrierLoc) > 170.f)
	{
		StartDash(ToBall, 800.f, 0.15f, false); // далеко — короткий выпад к сопернику
		return;
	}

	SetActorRotation(FRotator(0.f, ToBall.Rotation().Yaw, 0.f));
	const FVector Foot = Me + ToBall.GetSafeNormal() * 60.f;
	float Reach = 70.f + (Info.Defending - 60) * 0.6f;
	if (Carrier->IsShielding()) Reach -= 25.f;     // мяч прикрыт корпусом
	if (Carrier->ProtectTime > 0.f) Reach -= 30.f; // соперник только что сделал финт

	if (FVector::Dist2D(Foot, BallLoc) < Reach)
	{
		Ball->SetOwnerPlayer(nullptr);
		Carrier->Stun(0.3f);
		const float Strength = (Info.Physical + Info.Defending) * 0.5f - Carrier->Info.Physical;
		if (FMath::FRand() < 0.5f + Strength / 100.f)
		{
			GainBall();
			Ball->Touch(ToBall.GetSafeNormal() * 200.f);
		}
		else
		{
			const FVector Loose = ToBall.GetSafeNormal().RotateAngleAxis(FMath::FRandRange(-60.f, 60.f), FVector::UpVector);
			Ball->Kick(this, Loose * FMath::FRandRange(350.f, 650.f));
		}
		return;
	}

	// Мимо мяча. Сзади или в упор по ногам — фол, иначе просто провалились в отборе
	FVector ToCarrier = CarrierLoc - Me;
	ToCarrier.Z = 0.f;
	const bool bFromBehind = FVector::DotProduct(Carrier->GetActorForwardVector(), ToCarrier.GetSafeNormal()) > 0.5f;
	if (ToCarrier.Size() < 90.f && (bFromBehind || FMath::FRand() < 0.35f))
	{
		G->OnFoul(this, Carrier);
		return;
	}
	Stun(0.3f);
}

void ASoccerPlayer::SlideTackle(const FVector& Dir)
{
	if (bSliding || TackleCooldown > 0.f || GM()->MustKeepDistance(this)) return;
	TackleCooldown = 1.2f;
	bSlideResolved = false;
	StartDash(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, 1100.f, 0.5f, true);
	SlidePoseTime = 0.5f + 0.45f; // скольжение + время подняться
}

void ASoccerPlayer::SkillMove(const FVector& Dir, bool bBig)
{
	if (!HasBall() || SkillCooldown > 0.f || bGoalkeeper) return;
	// Финт: рывок с мячом в сторону щелчка правого стика (мяч остаётся у ног).
	// С RB — длиннее и дольше защищает от отбора. ДРБ сокращает перезарядку.
	const float DribbleFactor = FMath::Clamp(1.2f - Info.Dribbling / 250.f, 0.8f, 1.f);
	SkillCooldown = (bBig ? 0.9f : 0.5f) * DribbleFactor;
	ProtectTime   = bBig ? 0.5f : 0.25f;
	GM()->PlaySfx(ESoccerSound::Touch, 0.7f);
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
	Map(IA_LB, EKeys::Gamepad_LeftShoulder);         // LB / L1 — смена игрока; LB + B — удар «парашютом»
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
	EIC->BindAction(IA_LB,     ETriggerEvent::Completed, this, &ASoccerPlayerController::OnLBStop);
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
	bRBHeld = bLBHeld = bContainHeld = bGKRushHeld = false;
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

	// Мяч наш, но между касаниями откатился — игрок добежит и ударит (см. ASoccerPlayer::QueueKick)
	ASoccerPlayer* P = Current();
	if (!P || (!P->HasBall() && !P->CanKickBall())) return;
	if (P->bGoalkeeper && Kind == ECharge::Shot)
	{
		P->Pass(EPassKind::Lob, AimDirection(), FMath::Max(0.7f, Power)); // вратарь выбивает мяч далеко
		return;
	}
	switch (Kind)
	{
	case ECharge::Pass:    P->Pass(EPassKind::Ground, AimDirection(), Power); break;
	case ECharge::Through: P->Pass(EPassKind::Through, AimDirection(), Power); break;
	case ECharge::Lob:     P->Pass(EPassKind::Lob, AimDirection(), Power); break;
	case ECharge::Shot:    P->Shoot(AimDirection(), FMath::Max(0.15f, Power), bRBHeld, bLBHeld); break;
	default: break;
	}
}

// Белая стрелка: пока кнопка зажата, показываем направление паса/удара.
// Длина растёт с силой замаха, но не дальше адресата паса.
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
	const ECharge Preview = P->bGoalkeeper && Charging == ECharge::Shot ? ECharge::Lob : Charging;
	switch (Preview)
	{
	// Стрелка показывает замысел — без случайного разброса по точности
	case ECharge::Pass:    Plan = P->PlanPass(EPassKind::Ground, AimDirection(), Power, false); break;
	case ECharge::Through: Plan = P->PlanPass(EPassKind::Through, AimDirection(), Power, false); break;
	case ECharge::Lob:     Plan = P->PlanPass(EPassKind::Lob, AimDirection(), Power, false); break;
	default:               Plan = P->PlanShot(AimDirection(), FMath::Max(0.15f, Power), bRBHeld, bLBHeld, false); break;
	}

	const FVector From = G->Ball->GetActorLocation();
	FVector ToTarget = Plan.Target - From;
	ToTarget.Z = 0.f;
	const float Len = FMath::Min((float)ToTarget.Size(), FMath::Lerp(300.f, 900.f, Power));
	if (Len < 50.f)
	{
		G->AimLine->HidePath();
		return;
	}
	G->AimLine->ShowArrow(From, From + ToTarget.GetSafeNormal() * Len);
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
	else if (P->CanHeadBall()) P->Header(false, AimDirection());
	else if (P->CanKickBall()) P->Pass(EPassKind::Ground, AimDirection(), 0.5f);
	else                       bContainHeld = true;
}
void ASoccerPlayerController::OnAStop()
{
	bContainHeld = false;
	ReleaseCharge(ECharge::Pass);
}

// --- B / ⭕: удар (зажать — сила; с RB — изящный, с LB — «парашют») | мяч рядом — с лёта/головой | в обороне — отбор ---
void ASoccerPlayerController::OnB()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Shot);
	else if (P->CanHeadBall()) P->Header(true, AimDirection());
	else if (P->CanKickBall()) P->Shoot(AimDirection(), 0.8f, bRBHeld, bLBHeld);
	else                       P->Tackle();
}
void ASoccerPlayerController::OnBStop() { ReleaseCharge(ECharge::Shot); }

// --- X / ⬛: навес / длинный пас (зажать — сила) | в обороне — подкат ---
void ASoccerPlayerController::OnX()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())          BeginCharge(ECharge::Lob);
	else if (P->CanHeadBall()) P->Header(false, AimDirection());
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
	else if (P->CanHeadBall()) P->Header(false, AimDirection());
	else if (P->CanKickBall()) P->Pass(EPassKind::Through, AimDirection(), 0.5f);
	else                       bGKRushHeld = true;
}
void ASoccerPlayerController::OnYStop()
{
	bGKRushHeld = false;
	ReleaseCharge(ECharge::Through);
}

// --- LB / L1: переключиться на игрока, ближайшего к мячу. С мячом — модификатор (LB + B — «парашют») ---
void ASoccerPlayerController::OnLB()
{
	bLBHeld = true;
	const ASoccerPlayer* P = Current();
	if (P && !P->HasBall() && !P->CanKickBall())
	{
		SwitchPlayer(FVector::ZeroVector);
	}
}
void ASoccerPlayerController::OnLBStop() { bLBHeld = false; }

// --- Start / Options: пауза ---
void ASoccerPlayerController::OnStart()
{
	if (ASoccerGameMode* G = GM())
	{
		G->TogglePause();
	}
}

// ============================================================================
//  HUD: выносливость и шкала силы под игроком
// ============================================================================

void ASoccerHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas) return;
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	if (G && G->Ball && G->IsInMatch() && !G->IsMatchOver())
	{
		DrawRadar(G);
	}

	const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetOwningPlayerController());
	if (!PC) return;
	const ASoccerPlayer* P = Cast<ASoccerPlayer>(PC->GetPawn());
	if (!P) return;

	// Точка под ногами игрока на экране
	const FVector S = Project(P->GetActorLocation() - FVector(0.f, 0.f, 95.f));
	if (S.Z <= 0.f) return;

	const float K = Canvas->ClipY / 1080.f;
	const float W = 120.f * K;
	const float H = 12.f * K;
	const float X = S.X - W * 0.5f;
	const float Y = S.Y + 12.f * K;

	// Выносливость: тонкая полоска, видна, пока игрок не восстановился полностью
	const float Stamina = P->GetStamina();
	if (Stamina < 0.99f)
	{
		const float SW = 70.f * K;
		const float SH = 4.f * K;
		const float SX = S.X - SW * 0.5f;
		const float SY = S.Y + 30.f * K;
		const FLinearColor StaminaColor = Stamina > 0.3f ? FLinearColor(0.2f, 0.85f, 1.f) : FLinearColor(1.f, 0.35f, 0.1f);
		DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.7f), SX, SY, SW, SH);
		DrawRect(StaminaColor, SX, SY, SW * Stamina, SH);
	}

	const float Charge = PC->GetCharge();
	if (Charge < 0.f) return;

	// Цвет заполнения: зелёный -> жёлтый -> красный
	const FLinearColor Green(0.3f, 1.f, 0.02f), Yellow(1.f, 0.85f, 0.f), Red(1.f, 0.1f, 0.02f);
	const FLinearColor Fill = Charge < 0.5f ? FMath::Lerp(Green, Yellow, Charge * 2.f)
	                                        : FMath::Lerp(Yellow, Red, (Charge - 0.5f) * 2.f);

	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), X - 2.f * K, Y - 2.f * K, W + 4.f * K, H + 4.f * K);
	DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * Charge, H);
}

void ASoccerHUD::DrawRadar(const ASoccerGameMode* G)
{
	// Поле в масштабе: вид «с трибуны», как у камеры (+X — вправо, +Y — вниз)
	const float K = Canvas->ClipY / 1080.f;
	const float W = 270.f * K;
	const float H = W * HalfWidth / HalfLength;
	const float X0 = (Canvas->ClipX - W) * 0.5f;
	const float Y0 = Canvas->ClipY - H - 46.f * K;
	const float T = FMath::Max(1.f, 1.5f * K); // толщина линий
	const FLinearColor Line(1.f, 1.f, 1.f, 0.5f);

	DrawRect(FLinearColor(0.02f, 0.12f, 0.03f, 0.6f), X0, Y0, W, H);
	DrawRect(Line, X0, Y0, W, T);
	DrawRect(Line, X0, Y0 + H - T, W, T);
	DrawRect(Line, X0, Y0, T, H);
	DrawRect(Line, X0 + W - T, Y0, T, H);
	DrawRect(Line, X0 + (W - T) * 0.5f, Y0, T, H);
	const float BoxW = W * PenaltyDepth / (2.f * HalfLength);
	const float BoxH = H * (GoalHalfWidth + 400.f) / HalfWidth;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float BX = Side == 0 ? X0 : X0 + W - BoxW;
		DrawRect(Line, BX, Y0 + (H - BoxH) * 0.5f, BoxW, T);
		DrawRect(Line, BX, Y0 + (H + BoxH) * 0.5f - T, BoxW, T);
		DrawRect(Line, Side == 0 ? BX + BoxW - T : BX, Y0 + (H - BoxH) * 0.5f, T, BoxH);
	}
	const float GoalH = H * GoalHalfWidth / HalfWidth;
	DrawRect(FLinearColor::White, X0 - 3.f * K, Y0 + (H - GoalH) * 0.5f, 3.f * K, GoalH);
	DrawRect(FLinearColor::White, X0 + W, Y0 + (H - GoalH) * 0.5f, 3.f * K, GoalH);

	auto ToRadar = [X0, Y0, W, H](const FVector& L)
	{
		const float RX = FMath::Clamp((float)(L.X + HalfLength) / (2.f * HalfLength), 0.f, 1.f);
		const float RY = FMath::Clamp((float)(L.Y + HalfWidth) / (2.f * HalfWidth), 0.f, 1.f);
		return FVector2D(X0 + RX * W, Y0 + RY * H);
	};

	const APawn* Human = GetOwningPawn();
	for (const ASoccerPlayer* P : G->Players)
	{
		if (!P) continue;
		const FVector2D R = ToRadar(P->GetActorLocation());
		const bool bHuman = P == Human;
		const float Size = (bHuman ? 10.f : 8.f) * K;
		const float Border = (bHuman ? 2.5f : 1.2f) * K;
		const FLinearColor Outline = bHuman ? FLinearColor(0.64f, 0.9f, 0.09f) : FLinearColor(1.f, 1.f, 1.f, 0.8f);
		DrawRect(Outline, R.X - Size * 0.5f - Border, R.Y - Size * 0.5f - Border, Size + 2.f * Border, Size + 2.f * Border);
		DrawRect(P->ShirtColor, R.X - Size * 0.5f, R.Y - Size * 0.5f, Size, Size);
	}
	const FVector2D B = ToRadar(G->Ball->GetActorLocation());
	DrawRect(FLinearColor::Black, B.X - 3.5f * K, B.Y - 3.5f * K, 7.f * K, 7.f * K);
	DrawRect(FLinearColor::White, B.X - 2.5f * K, B.Y - 2.5f * K, 5.f * K, 5.f * K);
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
	InitAudio();

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
	if (SoundComp)
	{
		SoundComp->Stop();
	}
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
	Save->SoundVolume = FMath::Clamp(Save->SoundVolume, 0, 10);
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
	I.Look = FSoccerLook::Random(FMath::Rand() | 1, I.Pace, I.Physical);
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
	EventBanner = 0.f;
	SetPieceTaker.Reset();
	Restart = ESoccerRestart::None;
	RestartTaker.Reset();
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
	EventBanner = 0.f;
	SetPieceTaker.Reset();
	Restart = ESoccerRestart::None;
	RestartTaker.Reset();
	KickoffTeam = HumanTeam; // первыми разводят с центра хозяева — вы
	Stats[0] = Stats[1] = FSoccerMatchStats();
	PossessionTeam = -1;
	PendingPasser.Reset();
	bShotPending = false;
	ResultText = FText::GetEmpty();
	bMatchOver = false;
	bInMatch = true;

	SpawnTeams();
	CamFocus = FVector::ZeroVector;

	ShowWidget(HudWidget, SoccerUI::MakeHud(this), 5);
	SetGameInput();
	PossessHuman();
	ResetPositions(); // разводка с центра: управление перейдёт к разводящему
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
	EventText = FText::FromString(TEXT("ГОЛ!"));
	EventBanner = 2.f;
	if (AimLine) AimLine->HidePath();

	// Статистика: гол — это удар в створ (даже если мяч заскочил после рикошета)
	if (!bShotPending || PendingShotTeam != ScoringTeam)
	{
		Stats[ScoringTeam].Shots++;
	}
	Stats[ScoringTeam].ShotsOnTarget++;
	bShotPending = false;
	PendingPasser.Reset();
	Restart = ESoccerRestart::None;
	KickoffTeam = 1 - ScoringTeam; // с центра разводит пропустившая команда
	Audio.Play(ESoccerSound::Roar, ScoringTeam == HumanTeam ? 1.f : 0.5f);
	// Через 2 секунды — расстановка заново и розыгрыш с центра
	GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::ResetPositions, 2.f, false);
}

// Фол: игра останавливается, через 1.5 с — штрафной с места нарушения
// или пенальти, если сфолили в штрафной площади защищающейся команды.
void ASoccerGameMode::OnFoul(ASoccerPlayer* Offender, ASoccerPlayer* Victim)
{
	if (!bInMatch || !bPlayActive || bMatchOver || !Victim || !Ball) return;

	bPlayActive = false;
	Victim->Stun(1.f);
	if (Offender)
	{
		Offender->Stun(0.f); // прервать подкат/рывок нарушителя
		Stats[Offender->Team].Fouls++;
	}
	if (AimLine) AimLine->HidePath();
	Restart = ESoccerRestart::None;
	PendingPasser.Reset();
	bShotPending = false;

	const FVector Spot = Victim->GetActorLocation();
	const float Sign = Victim->AttackSign();
	const float AttackX = Sign * HalfLength; // ворота, которые атакует пострадавший
	bPenalty = FMath::Abs(AttackX - Spot.X) < PenaltyDepth && FMath::Abs(Spot.Y) < GoalHalfWidth + 400.f;

	if (bPenalty)
	{
		SetPieceSpot = FVector(AttackX - Sign * PenaltyDepth, 0.f, BallRadius); // точка пенальти
	}
	else
	{
		SetPieceSpot = FVector(FMath::Clamp<double>(Spot.X, -HalfLength + 80.0, HalfLength - 80.0),
		                       FMath::Clamp<double>(Spot.Y, -HalfWidth + 80.0, HalfWidth - 80.0), BallRadius);
	}
	SetPieceTaker = Victim;

	EventText = FText::FromString(bPenalty ? TEXT("ПЕНАЛЬТИ!") : TEXT("ФОЛ!  Штрафной"));
	EventBanner = 2.f;
	PlaySfx(bPenalty ? ESoccerSound::WhistleLong : ESoccerSound::Whistle);
	GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::StartSetPiece, 1.5f, false);
}

void ASoccerGameMode::StartSetPiece()
{
	if (!bInMatch || bMatchOver) return;
	ASoccerPlayer* Taker = SetPieceTaker.Get();
	if (!Taker)
	{
		ResetPositions();
		return;
	}

	const float Sign = Taker->AttackSign();
	const FVector GoalCenter(Sign * HalfLength, 0.f, 0.f);
	const FVector ToGoal = (GoalCenter - SetPieceSpot).GetSafeNormal2D();

	auto Place = [](ASoccerPlayer* P, const FVector& Loc, const FVector& LookAt)
	{
		const FVector Pos(Loc.X, Loc.Y, P->GetActorLocation().Z);
		P->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetActorRotation(FRotator(0.f, (LookAt - Pos).Rotation().Yaw, 0.f));
		P->GetCharacterMovement()->StopMovementImmediately();
	};

	// Все прерывают рывки/подкаты/отложенные удары, мяч — на точку
	for (ASoccerPlayer* P : Players)
	{
		if (P) P->Stun(0.f);
	}
	Ball->ResetBall(SetPieceSpot);

	int32 Slot = 0;
	for (ASoccerPlayer* P : Players)
	{
		if (!P || P == Taker) continue;
		const FVector Loc = P->GetActorLocation();

		if (bPenalty)
		{
			if (P->bGoalkeeper && P->Team != Taker->Team)
			{
				Place(P, FVector(GoalCenter.X - Sign * 40.f, 0.f, 0.f), SetPieceSpot); // вратарь — на линии ворот
			}
			else if (!P->bGoalkeeper)
			{
				// Остальные — за пределами штрафной, позади точки пенальти
				const float Y = (Slot % 2 ? 1.f : -1.f) * (250.f + 180.f * (Slot / 2));
				Place(P, FVector(SetPieceSpot.X - Sign * 500.f, Y, 0.f), GoalCenter);
				++Slot;
			}
		}
		else if (P->Team != Taker->Team)
		{
			// Штрафной: соперники — не ближе 5 м от мяча
			FVector Away = Loc - SetPieceSpot;
			Away.Z = 0.f;
			if (Away.Size() < 500.f)
			{
				if (Away.IsNearlyZero()) Away = FVector(Sign, 0.f, 0.f);
				FVector NewLoc = SetPieceSpot + Away.GetSafeNormal() * 500.f;
				NewLoc.X = FMath::Clamp<double>(NewLoc.X, -HalfLength + 60.0, HalfLength - 60.0);
				NewLoc.Y = FMath::Clamp<double>(NewLoc.Y, -HalfWidth + 60.0, HalfWidth - 60.0);
				Place(P, NewLoc, SetPieceSpot);
			}
		}
	}

	// Стенка: штрафной недалеко от ворот — двое ближайших защитников встают в 5 м от мяча на линии удара
	if (!bPenalty && FVector::Dist2D(SetPieceSpot, GoalCenter) < 1400.f)
	{
		TArray<ASoccerPlayer*> Wall;
		for (ASoccerPlayer* P : Players)
		{
			if (P && P->Team != Taker->Team && !P->bGoalkeeper) Wall.Add(P);
		}
		const FVector Spot = SetPieceSpot;
		Wall.Sort([Spot](const ASoccerPlayer& A, const ASoccerPlayer& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), Spot) < FVector::DistSquared2D(B.GetActorLocation(), Spot);
		});
		const FVector Across = FVector::CrossProduct(FVector::UpVector, ToGoal);
		for (int32 i = 0; i < FMath::Min(2, Wall.Num()); ++i)
		{
			Place(Wall[i], ClampToField(Spot + ToGoal * 530.f + Across * (i == 0 ? -40.f : 40.f), 60.f), Spot);
		}
	}

	// Исполнитель — пострадавший: у мяча, лицом к воротам соперника
	Place(Taker, SetPieceSpot - ToGoal * 70.f, GoalCenter);
	Taker->GainBall();
	Taker->ProtectTime = 1.f; // соперник не отбирает мяч в первую секунду
	BeginRestart(bPenalty ? ESoccerRestart::Penalty : ESoccerRestart::FreeKick, Taker, SetPieceSpot);

	SetPieceTaker.Reset();
	bPlayActive = true;
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
	SetPieceTaker.Reset();
	PendingPasser.Reset();
	bShotPending = false;
	bPlayActive = true;

	// Разводка с центра: разводит самый передний игрок команды, пропустившей гол
	// (в начале матча и на тренировке — ваша команда). Соперники ждут за центральным кругом.
	const int32 KickTeam = IsPractice() ? HumanTeam : KickoffTeam;
	ASoccerPlayer* Kicker = nullptr;
	for (ASoccerPlayer* P : Players)
	{
		if (!P || P->Team != KickTeam || P->bGoalkeeper) continue;
		if (!Kicker || P->Home.X * P->AttackSign() > Kicker->Home.X * Kicker->AttackSign()) Kicker = P;
	}
	if (Kicker)
	{
		const float S = Kicker->AttackSign();
		Kicker->SetActorLocation(FVector(-S * 45.f, 0.f, Kicker->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		Kicker->SetActorRotation(FRotator(0.f, S > 0.f ? 0.f : 180.f, 0.f));
		Kicker->GainBall();
		BeginRestart(ESoccerRestart::Kickoff, Kicker, FVector(0.f, 0.f, BallRadius));
	}
	PlaySfx(ESoccerSound::Whistle);
}

void ASoccerGameMode::EndMatch()
{
	if (bMatchOver) return;
	bMatchOver = true;
	bPlayActive = false;
	Restart = ESoccerRestart::None;
	if (AimLine) AimLine->HidePath();
	PlaySfx(ESoccerSound::WhistleEnd);

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

	// Через 8 секунд (успеть посмотреть статистику) — обратно в главное меню
	GetWorldTimerManager().SetTimer(MenuTimer, this, &ASoccerGameMode::ReturnToMenu, 8.f, false);
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

ASoccerPlayer* ASoccerGameMode::GetGoalkeeper(int32 InTeam) const
{
	for (ASoccerPlayer* P : Players)
	{
		if (P && P->Team == InTeam && P->bGoalkeeper) return P;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
//  Роли ИИ команд (несколько раз в секунду). Игрок человека в распределении не участвует.
//  Своя команда с мячом — все открываются. Мяч свободен — на него бежит тот, кто успеет первым.
//  Мяч у соперника — один прессингует, второй страхует, остальные опекают самых опасных
//  (ближе к нашим воротам) соперников. Партнёр человека прессингует, только пока зажата RB.
// ---------------------------------------------------------------------------
void ASoccerGameMode::UpdateTeamAI()
{
	const ASoccerPlayer* BallOwner = Ball->OwnerPlayer;
	const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController());
	const ASoccerPlayer* Human = GetHumanPlayer();
	const FVector BallLoc = Ball->GetActorLocation();

	for (int32 T = 0; T < 2; ++T)
	{
		TArray<ASoccerPlayer*> Free;    // наши полевые под управлением ИИ
		TArray<ASoccerPlayer*> Rivals;  // полевые соперники без мяча
		for (ASoccerPlayer* P : Players)
		{
			if (!P || P->bGoalkeeper) continue;
			if (P->Team == T)
			{
				if (P != Human) Free.Add(P);
			}
			else if (P != BallOwner)
			{
				Rivals.Add(P);
			}
		}
		for (ASoccerPlayer* P : Free)
		{
			P->SetAIRole(ESoccerAIRole::Support);
		}
		if (Free.Num() == 0 || (BallOwner && BallOwner->Team == T)) continue;

		// Кто раньше всех успеет к мячу (к владельцу — просто ближайший)
		TArray<TPair<float, ASoccerPlayer*>> ByTime;
		for (ASoccerPlayer* P : Free)
		{
			FVector Meet;
			const float Time = BallOwner ? (float)FVector::Dist2D(P->GetActorLocation(), BallLoc) / 600.f : P->InterceptTime(Meet);
			ByTime.Emplace(Time, P);
		}
		ByTime.Sort([](const TPair<float, ASoccerPlayer*>& A, const TPair<float, ASoccerPlayer*>& B) { return A.Key < B.Key; });
		Free.Reset();
		for (const TPair<float, ASoccerPlayer*>& Entry : ByTime)
		{
			Free.Add(Entry.Value);
		}

		bool bAssignFirst = true;
		if (Human && Human->Team == T)
		{
			if (BallOwner)
			{
				bAssignFirst = PC && PC->bRBHeld;
			}
			else
			{
				FVector Meet;
				bAssignFirst = ByTime[0].Key + 0.3f < Human->InterceptTime(Meet);
			}
		}
		if (bAssignFirst)
		{
			Free[0]->SetAIRole(BallOwner ? ESoccerAIRole::Press : ESoccerAIRole::Chase);
			Free.RemoveAt(0);
		}
		if (!BallOwner) continue; // мяч свободен: остальные держат позиции

		if (Free.Num() > 0)
		{
			Free[0]->SetAIRole(ESoccerAIRole::Cover);
			Free.RemoveAt(0);
		}

		const float OwnGoalX = T == 0 ? -HalfLength : HalfLength;
		Rivals.Sort([OwnGoalX](const ASoccerPlayer& A, const ASoccerPlayer& B)
		{
			return FMath::Abs(A.GetActorLocation().X - OwnGoalX) < FMath::Abs(B.GetActorLocation().X - OwnGoalX);
		});
		for (ASoccerPlayer* Rival : Rivals)
		{
			if (Free.Num() == 0) break;
			int32 BestIdx = 0;
			double BestDist = TNumericLimits<double>::Max();
			for (int32 i = 0; i < Free.Num(); ++i)
			{
				const double D = FVector::Dist2D(Free[i]->GetActorLocation(), Rival->GetActorLocation());
				if (D < BestDist)
				{
					BestDist = D;
					BestIdx = i;
				}
			}
			Free[BestIdx]->SetAIRole(ESoccerAIRole::Mark, Rival);
			Free.RemoveAt(BestIdx);
		}
	}
}

// ---------------------------------------------------------------------------
//  Стандарты: пока мяч не введён, соперники исполнителя держат дистанцию
// ---------------------------------------------------------------------------
void ASoccerGameMode::BeginRestart(ESoccerRestart Kind, ASoccerPlayer* Taker, const FVector& Spot)
{
	Restart = Kind;
	RestartTaker = Taker;
	SetPieceSpot = Spot;
	RestartStartTime = GetWorld()->GetTimeSeconds();
	if (Taker)
	{
		Taker->PrepareRestart(Kind == ESoccerRestart::Kickoff ? 0.8f : 1.3f);
	}
}

bool ASoccerGameMode::IsRestartTaker(const ASoccerPlayer* P) const
{
	return Restart != ESoccerRestart::None && P && RestartTaker.Get() == P;
}

bool ASoccerGameMode::MustKeepDistance(const ASoccerPlayer* P) const
{
	const ASoccerPlayer* Taker = RestartTaker.Get();
	if (Restart == ESoccerRestart::None || !P || !Taker || P == Taker) return false;
	if (Restart == ESoccerRestart::Penalty) return !P->bGoalkeeper; // на пенальти ждут все, кроме вратарей
	return P->Team != Taker->Team;
}

float ASoccerGameMode::GetRestartRadius() const
{
	return Restart == ESoccerRestart::Kickoff ? 320.f : 520.f; // центральный круг / 5 м
}

// ---------------------------------------------------------------------------
//  Статистика матча
// ---------------------------------------------------------------------------
void ASoccerGameMode::OnShot(ASoccerPlayer* Shooter)
{
	if (!bInMatch || !Shooter) return;
	Stats[Shooter->Team].Shots++;
	bShotPending = true;
	PendingShotTeam = Shooter->Team;
	PendingShotTime = GetWorld()->GetTimeSeconds();
}

void ASoccerGameMode::OnPassMade(ASoccerPlayer* Passer)
{
	if (!bInMatch || !Passer) return;
	Stats[Passer->Team].Passes++;
	PendingPasser = Passer;
}

void ASoccerGameMode::OnBallGained(ASoccerPlayer* Receiver)
{
	if (!Receiver) return;
	if (const ASoccerPlayer* Passer = PendingPasser.Get())
	{
		if (Receiver != Passer && Receiver->Team == Passer->Team)
		{
			Stats[Passer->Team].PassesCompleted++;
		}
	}
	PendingPasser.Reset();
	PossessionTeam = Receiver->Team;
}

void ASoccerGameMode::OnKeeperSave(ASoccerPlayer* Keeper)
{
	if (!Keeper || !bShotPending || PendingShotTeam == Keeper->Team) return;
	bShotPending = false;
	if (GetWorld()->GetTimeSeconds() - PendingShotTime > 4.f) return; // это уже не тот удар
	Stats[PendingShotTeam].ShotsOnTarget++;
	Stats[Keeper->Team].Saves++;
	PlayOoh(0.8f);
}

int32 ASoccerGameMode::GetPossessionPercent(int32 InTeam) const
{
	const float Total = Stats[0].Possession + Stats[1].Possession;
	return Total > 0.f ? FMath::RoundToInt(100.f * Stats[InTeam].Possession / Total) : 50;
}

// ---------------------------------------------------------------------------
//  Звук
// ---------------------------------------------------------------------------
void ASoccerGameMode::InitAudio()
{
	Audio.Init();
	SoundWave = NewObject<USoundWaveProcedural>(this);
	SoundWave->SetSampleRate(FSoccerAudio::SampleRate);
	SoundWave->NumChannels = 1;
	SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	SoundWave->bLooping = true;
	SoundComp = UGameplayStatics::CreateSound2D(this, SoundWave, 1.f, 1.f, 0.f, nullptr, false, false);
	if (SoundComp)
	{
		SoundComp->Play();
	}
}

// Каждый кадр докладываем в звук столько сэмплов, чтобы впереди было ~0.1 с
void ASoccerGameMode::PumpAudio(float Dt)
{
	if (!SoundWave || !SoundComp) return;
	Audio.Volume = Save ? Save->SoundVolume / 10.f : 0.8f;
	Audio.CrowdLevel = bInMatch ? 1.f : 0.5f;

	const int32 Queued = SoundWave->GetAvailableAudioByteCount() / (int32)sizeof(int16);
	const int32 Target = FMath::Clamp(FMath::CeilToInt(FSoccerAudio::SampleRate * FMath::Max(0.08f, Dt * 3.f)), 1024, 8192);
	const int32 Need = Target - Queued;
	if (Need <= 0) return;
	AudioBuffer.SetNumUninitialized(Need);
	Audio.Render(AudioBuffer.GetData(), Need);
	SoundWave->QueueAudio(reinterpret_cast<const uint8*>(AudioBuffer.GetData()), Need * (int32)sizeof(int16));
}

void ASoccerGameMode::PlaySfx(ESoccerSound Sound, float Gain)
{
	Audio.Play(Sound, Gain);
}

// «У-у-у» трибун — не чаще раза в пару секунд
void ASoccerGameMode::PlayOoh(float Gain)
{
	if (!bInMatch || OohCooldown > 0.f) return;
	OohCooldown = 2.5f;
	Audio.Play(ESoccerSound::Ooh, Gain);
}

void ASoccerGameMode::OnBallImpact(int32 HitFlags, const FVector& Where, float Speed)
{
	if (HitFlags & ASoccerBall::HitPost)
	{
		PlaySfx(ESoccerSound::Post, FMath::Clamp(Speed / 1500.f, 0.3f, 1.f));
		if (Speed > 700.f) PlayOoh(1.f);
	}
	if (HitFlags & ASoccerBall::HitNet)
	{
		PlaySfx(ESoccerSound::Net, FMath::Clamp(Speed / 1500.f, 0.3f, 1.f));
	}
	if (HitFlags & (ASoccerBall::HitBoard | ASoccerBall::HitEndBoard))
	{
		PlaySfx(ESoccerSound::Board, FMath::Clamp(Speed / 2000.f, 0.15f, 0.8f));
	}
	// Удар прошёл рядом со штангой или над перекладиной
	if ((HitFlags & ASoccerBall::HitEndBoard) && bShotPending && Speed > 700.f &&
	    FMath::Abs(Where.Y) < GoalHalfWidth + 250.f && GetWorld()->GetTimeSeconds() - PendingShotTime < 3.f)
	{
		bShotPending = false;
		PlayOoh(1.f);
	}
}

// ---------------------------------------------------------------------------
//  Тик и камера
// ---------------------------------------------------------------------------
void ASoccerGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Ball) return;

	EventBanner = FMath::Max(0.f, EventBanner - DeltaSeconds);
	OohCooldown = FMath::Max(0.f, OohCooldown - DeltaSeconds);

	// Таймер идёт только во время игры (пауза после гола не считается)
	if (bInMatch && bPlayActive)
	{
		TimeLeft -= DeltaSeconds;

		// Владение мячом
		if (Ball->OwnerPlayer)
		{
			PossessionTeam = Ball->OwnerPlayer->Team;
		}
		if (PossessionTeam >= 0)
		{
			Stats[PossessionTeam].Possession += DeltaSeconds;
		}

		// Стандарт закончился: мяч ввели в игру (удар или мяч сдвинулся), исполнитель его потерял
		if (Restart != ESoccerRestart::None)
		{
			const ASoccerPlayer* Taker = RestartTaker.Get();
			const bool bKicked = Ball->GetLastKickTime() > RestartStartTime;
			const bool bMoved = FVector::Dist2D(Ball->GetActorLocation(), SetPieceSpot) > 150.f;
			if (!Taker || !Taker->HasBall() || bKicked || bMoved || GetWorld()->GetTimeSeconds() - RestartStartTime > 8.f)
			{
				Restart = ESoccerRestart::None;
				RestartTaker.Reset();
			}
		}

		TeamAITimer -= DeltaSeconds;
		if (TeamAITimer <= 0.f)
		{
			TeamAITimer = 0.2f;
			UpdateTeamAI();
		}

		if (TimeLeft <= 0.f)
		{
			TimeLeft = 0.f;
			EndMatch();
		}
	}

	// Трибуны оживляются, когда мяч у любых ворот
	float Danger = 0.f;
	if (bInMatch && !bMatchOver)
	{
		const FVector BallLoc = Ball->GetActorLocation();
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float D = FVector::Dist2D(BallLoc, FVector(Side * HalfLength, 0.f, 0.f));
			Danger = FMath::Max(Danger, FMath::Clamp(1.f - (D - 300.f) / 1400.f, 0.f, 1.f));
		}
	}
	Audio.SetExcitement(Danger);
	PumpAudio(DeltaSeconds);

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
