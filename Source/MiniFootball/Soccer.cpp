// Мини-футбол 5×5 — реализация. См. Soccer.h и README.md.

#include "Soccer.h"

#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputActionValue.h"

using namespace Soccer;

// ----------------------------------------------------------------------------
//  Вспомогательное
// ----------------------------------------------------------------------------

// Пути к простым мешам движка (есть в любом проекте, ничего импортировать не нужно)
#define MESH_CUBE     TEXT("/Engine/BasicShapes/Cube.Cube")
#define MESH_SPHERE   TEXT("/Engine/BasicShapes/Sphere.Sphere")
#define MESH_CYLINDER TEXT("/Engine/BasicShapes/Cylinder.Cylinder")
#define MESH_CONE     TEXT("/Engine/BasicShapes/Cone.Cone")

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

// Точка внутри поля (с отступом от бортов)
static FVector ClampToField(FVector P, float Margin = 60.f)
{
	P.X = FMath::Clamp<double>(P.X, -HalfLength + Margin, HalfLength - Margin);
	P.Y = FMath::Clamp<double>(P.Y, -HalfWidth + Margin, HalfWidth - Margin);
	return P;
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
	}
	else
	{
		// Закрутка (изящный удар): боковое ускорение в первые доли секунды полёта
		if (CurveTimeLeft > 0.f)
		{
			Velocity += CurveAccel * Dt;
			CurveTimeLeft -= Dt;
		}

		const bool bGrounded = P.Z <= BallRadius + 1.f && FMath::Abs(Velocity.Z) < 1.f;
		if (!bGrounded)
		{
			Velocity.Z -= Gravity * Dt;
		}

		// Трение: на газоне — качение, в воздухе — слабое сопротивление.
		// Экспоненциальное затухание: v(t) = v0 * e^(-k t) -> пройденный путь до остановки = v0 / k.
		const float Damp = FMath::Exp(-(bGrounded ? RollingFriction : AirDrag) * Dt);
		Velocity.X *= Damp;
		Velocity.Y *= Damp;
		if (bGrounded && Velocity.Size2D() < 15.f)
		{
			Velocity = FVector::ZeroVector; // мяч остановился
		}

		P += Velocity * Dt;

		// Отскок от газона
		if (P.Z < BallRadius)
		{
			P.Z = BallRadius;
			if (Velocity.Z < -150.f)
			{
				Velocity.Z = -Velocity.Z * Bounciness;
				Velocity.X *= 0.9f;
				Velocity.Y *= 0.9f;
			}
			else
			{
				Velocity.Z = 0.f;
			}
		}

		if (IntendedReceiver && TimeSinceKick() > 2.5f)
		{
			IntendedReceiver = nullptr; // пас «протух»
		}
	}

	CollideWithWalls(P, OldP);

	// Телепорт без sweep: оверлапы (триггер гола) всё равно обновляются.
	SetActorLocation(P);
}

void ASoccerBall::CollideWithWalls(FVector& P, const FVector& OldP)
{
	const float R = BallRadius;

	// Боковые борта (как в зале: мяч не уходит в аут, а отскакивает)
	if (FMath::Abs(P.Y) > HalfWidth - R)
	{
		P.Y = FMath::Sign(P.Y) * (HalfWidth - R);
		Velocity.Y = -FMath::Sign(P.Y) * FMath::Abs(Velocity.Y) * WallBounciness;
	}

	// Лицевые борта: пропускаем мяч внутрь только через створ ворот
	const bool bWasInGoal = FMath::Abs(OldP.X) > HalfLength;
	if (!bWasInGoal && FMath::Abs(P.X) > HalfLength - R)
	{
		const bool bInMouth = FMath::Abs(P.Y) < GoalHalfWidth - R && P.Z < GoalHeight - R;
		if (!bInMouth)
		{
			P.X = FMath::Sign(P.X) * (HalfLength - R);
			Velocity.X = -FMath::Sign(P.X) * FMath::Abs(Velocity.X) * WallBounciness;
		}
	}

	// Внутри ворот — сетка гасит мяч
	if (FMath::Abs(P.X) > HalfLength)
	{
		const float Back = HalfLength + GoalDepth - R;
		if (FMath::Abs(P.X) > Back)
		{
			P.X = FMath::Sign(P.X) * Back;
			Velocity.X *= -0.2f;
		}
		if (FMath::Abs(P.Y) > GoalHalfWidth - R)
		{
			P.Y = FMath::Sign(P.Y) * (GoalHalfWidth - R);
			Velocity.Y *= -0.2f;
		}
		if (P.Z > GoalHeight - R)
		{
			P.Z = GoalHeight - R;
			Velocity.Z = FMath::Min<double>(Velocity.Z, 0.0);
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
	Trigger->SetBoxExtent(FVector((GoalDepth - MinX) * 0.5f, GoalHalfWidth, GoalHeight * 0.5f));
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
	for (UStaticMeshComponent* C : Nets)  Paint(C, FLinearColor(0.6f, 0.6f, 0.6f));
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(MESH_CUBE);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(MESH_CONE);

	// Визуальная «капсула» = цилиндр + две сферы
	USceneComponent* Parent = GetCapsuleComponent();
	Body = MakeVisual(this, Parent, TEXT("Body"), Cyl.Object, FVector::ZeroVector, FVector(0.7f, 0.7f, 1.1f));
	Head = MakeVisual(this, Parent, TEXT("Head"), Sph.Object, FVector(0.f, 0.f, 55.f), FVector(0.7f));
	Feet = MakeVisual(this, Parent, TEXT("Feet"), Sph.Object, FVector(0.f, 0.f, -55.f), FVector(0.7f));
	// «Нос» — показывает, куда смотрит игрок
	Nose = MakeVisual(this, Parent, TEXT("Nose"), Cube.Object, FVector(35.f, 0.f, 45.f), FVector(0.25f, 0.2f, 0.15f));
	// Маркер над головой — виден только у игрока, которым управляет человек
	Marker = MakeVisual(this, Parent, TEXT("Marker"), Cone.Object, FVector(0.f, 0.f, 150.f), FVector(0.4f),
	                    FRotator(180.f, 0.f, 0.f));
	Marker->SetCastShadow(false);
	Marker->SetVisibility(false);
}

void ASoccerPlayer::Setup(int32 InTeam, bool bInGoalkeeper, const FVector& InHome)
{
	Team = InTeam;
	bGoalkeeper = bInGoalkeeper;
	Home = InHome;

	// Красные / синие; вратари — оранжевый и бирюзовый, чтобы их было видно
	FLinearColor Color = Team == 0 ? FLinearColor(0.8f, 0.05f, 0.05f) : FLinearColor(0.05f, 0.15f, 0.85f);
	if (bGoalkeeper)
	{
		Color = Team == 0 ? FLinearColor(1.f, 0.45f, 0.f) : FLinearColor(0.f, 0.75f, 0.75f);
	}
	Paint(Body, Color);
	Paint(Head, Color);
	Paint(Feet, Color);
	Paint(Nose, FLinearColor(0.05f, 0.05f, 0.05f));
	Paint(Marker, FLinearColor(1.f, 0.9f, 0.f));

	ResetToHome();
}

void ASoccerPlayer::ResetToHome()
{
	SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator(0.f, Team == 0 ? 0.f : 180.f, 0.f));
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

	StunTime       = FMath::Max(0.f, StunTime - Dt);
	TackleCooldown = FMath::Max(0.f, TackleCooldown - Dt);
	SkillCooldown  = FMath::Max(0.f, SkillCooldown - Dt);
	ProtectTime    = FMath::Max(0.f, ProtectTime - Dt);
	AIDecisionTimer -= Dt;

	// После гола и после финального свистка все стоят
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
	GetCharacterMovement()->MaxWalkSpeed = Speed;
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

	GetCharacterMovement()->MaxWalkSpeed = Speed;
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
		float OppDist = 0.f;
		NearestOpponent(OppDist);
		if (OppDist < 200.f)
		{
			AIDecisionTimer = 0.6f;
			if (FindPassTarget(ToGoal) && FMath::FRand() < 0.6f)
			{
				Pass(EPassKind::Ground, ToGoal);
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
	ASoccerBall* Ball = GM()->Ball;
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector Me = GetActorLocation();
	const float Side = -AttackSign();          // с какой стороны наши ворота
	const float GoalX = Side * HalfLength;

	// Раз в 0.15 с пересчитываем, куда встать (задержка реакции — иначе вратарь непробиваем)
	GKReactionTimer -= Dt;
	if (GKReactionTimer <= 0.f)
	{
		GKReactionTimer = 0.15f;
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
		GKTargetY = FMath::Clamp(GKTargetY, -GoalHalfWidth + 30.f, GoalHalfWidth - 30.f);
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

	// Отбить мяч, если дотягиваемся
	const FVector ToBall = BallLoc - Me;
	const bool bReach = ToBall.Size2D() < 110.f && BallLoc.Z < 230.f;
	const bool bJustKicked = Ball->LastKicker == this && Ball->TimeSinceKick() < 0.5f;
	if (bReach && !bTeammateHasBall && !bJustKicked)
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

void ASoccerPlayer::Pass(EPassKind Kind, const FVector& AimDir)
{
	if (!CanKickBall()) return;
	ASoccerBall* Ball = GM()->Ball;
	const FVector From = Ball->GetActorLocation();
	const FVector Aim = AimDir.IsNearlyZero() ? GetActorForwardVector() : AimDir.GetSafeNormal2D();

	ASoccerPlayer* Mate = FindPassTarget(Aim);
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
		// Партнёра по направлению нет — просто отдаём мяч в ту сторону
		Target = From + Aim * (Kind == EPassKind::Lob ? 1500.f : 900.f);
	}
	Target = ClampToField(Target, 100.f);

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	const FVector Dir = Flat / Dist;

	FVector Vel;
	if (Kind == EPassKind::Lob)
	{
		// Навес: время полёта T = 2*Vz/g, горизонтальная скорость = путь / T
		const float Vz = FMath::Clamp(400.f + Dist * 0.25f, 500.f, 950.f);
		const float T = 2.f * Vz / Ball->Gravity;
		Vel = Dir * (Dist / T) + FVector(0.f, 0.f, Vz);
	}
	else
	{
		// Мяч по газону: с экспоненциальным трением путь = (v0 - v1) / k,
		// значит v0 = путь * k + скорость_прихода.
		const float Arrive = Kind == EPassKind::Through ? 350.f : 450.f;
		const float Speed = FMath::Clamp(Dist * Ball->RollingFriction + Arrive, 600.f, 2400.f);
		Vel = Dir * Speed;
	}

	SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	Ball->Kick(this, Vel);
	Ball->IntendedReceiver = Mate; // ИИ-партнёр побежит принимать
}

void ASoccerPlayer::Shoot(const FVector& AimDir, float Power01, bool bFinesse)
{
	if (!CanKickBall()) return;
	ASoccerBall* Ball = GM()->Ball;
	const FVector From = Ball->GetActorLocation();
	const float Power = FMath::Clamp(Power01, 0.f, 1.f);

	// Куда в створ: поперечная составляющая стика выбирает угол ворот
	float AimY = FMath::Clamp((float)AimDir.Y * 2.f, -1.f, 1.f) * GoalHalfWidth * 0.8f;
	if (!bFinesse)
	{
		AimY += FMath::FRandRange(-1.f, 1.f) * 50.f * Power; // сильный удар — менее точный
	}
	const FVector Target(AttackSign() * HalfLength, AimY, 0.f);

	FVector Flat = Target - From;
	Flat.Z = 0.f;
	const float Dist = FMath::Max(1.f, (float)Flat.Size());
	FVector Dir = Flat / Dist;

	float Speed = FMath::Lerp(1300.f, 3000.f, Power);
	float Lift = Power * 250.f + (Dist > 1500.f ? 150.f : 0.f);
	FVector Curve = FVector::ZeroVector;
	float CurveTime = 0.f;

	if (bFinesse)
	{
		// ИЗЯЩНЫЙ УДАР (RB + B): медленнее, точнее и с закруткой.
		// Отклоняем старт на A градусов «наружу» и компенсируем боковым ускорением c:
		// −v·sin(A)·T + c·T²/2 = 0  =>  c = 2·v·sin(A) / T   (приближённо, без учёта трения)
		Speed *= 0.8f;
		Lift = 120.f;
		const float AngleDeg = 12.f;
		const float T = Dist / Speed;
		FVector Perp = FVector::CrossProduct(FVector::UpVector, Dir);
		if (Perp.Y * AimY > 0.f) Perp = -Perp; // стартуем от центра ворот наружу
		Dir = (Dir * FMath::Cos(FMath::DegreesToRadians(AngleDeg)) - Perp * FMath::Sin(FMath::DegreesToRadians(AngleDeg))).GetSafeNormal();
		Curve = Perp * (2.f * Speed * FMath::Sin(FMath::DegreesToRadians(AngleDeg)) / T);
		CurveTime = T;
	}

	SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	Ball->Kick(this, Dir * Speed + FVector(0.f, 0.f, Lift), Curve, CurveTime);
}

void ASoccerPlayer::Tackle()
{
	if (TackleCooldown > 0.f) return;
	TackleCooldown = 0.8f;

	ASoccerBall* Ball = GM()->Ball;
	ASoccerPlayer* Carrier = Ball->OwnerPlayer;
	if (!Carrier || Carrier->Team == Team) return;

	const FVector ToCarrier = Carrier->GetActorLocation() - GetActorLocation();
	if (ToCarrier.Size2D() > 140.f)
	{
		StartDash(ToCarrier, 800.f, 0.15f, false); // далеко — короткий выпад к сопернику
		return;
	}

	// Шанс отбора: человеку проще, укрывание мяча и финт соперника его снижают
	float Chance = IsPlayerControlled() ? 0.7f : 0.35f;
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
	// С RB — более длинный финт и более долгая защита от отбора.
	SkillCooldown = bBig ? 0.9f : 0.5f;
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

	UInputAction* IA_Move    = NewAction(2);
	UInputAction* IA_Right   = NewAction(2);
	UInputAction* IA_Sprint  = NewAction(1);
	UInputAction* IA_LT      = NewAction(1);
	UInputAction* IA_RB      = NewAction(0);
	UInputAction* IA_A       = NewAction(0);
	UInputAction* IA_B       = NewAction(0);
	UInputAction* IA_X       = NewAction(0);
	UInputAction* IA_Y       = NewAction(0);
	UInputAction* IA_LB      = NewAction(0);
	UInputAction* IA_Restart = NewAction(0);

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

	// --- Кнопки (Xbox A/B/X/Y = PlayStation ✖/⭕/⬛/▲) ---
	Map(IA_A, EKeys::Gamepad_FaceButton_Bottom);     // A ✖ — пас / сдерживание
	Map(IA_A, EKeys::SpaceBar);
	Map(IA_B, EKeys::Gamepad_FaceButton_Right);      // B ⭕ — удар / отбор
	Map(IA_B, EKeys::F);
	Map(IA_X, EKeys::Gamepad_FaceButton_Left);       // X ⬛ — навес / подкат
	Map(IA_X, EKeys::Q);
	Map(IA_Y, EKeys::Gamepad_FaceButton_Top);        // Y ▲ — пас в разрез / выход вратаря
	Map(IA_Y, EKeys::E);
	Map(IA_Restart, EKeys::Gamepad_Special_Right);   // Start / Options — новый матч
	Map(IA_Restart, EKeys::Enter);

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
	EIC->BindAction(IA_Y,      ETriggerEvent::Started,   this, &ASoccerPlayerController::OnY);
	EIC->BindAction(IA_Y,      ETriggerEvent::Completed, this, &ASoccerPlayerController::OnYStop);
	EIC->BindAction(IA_LB,     ETriggerEvent::Started,   this, &ASoccerPlayerController::OnLB);
	EIC->BindAction(IA_Restart,ETriggerEvent::Started,   this, &ASoccerPlayerController::OnRestart);
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
	// Мяч потеряли во время замаха — удар отменяется
	const ASoccerPlayer* P = Current();
	if (ChargeStart >= 0.f && (!P || !P->HasBall()))
	{
		ChargeStart = -1.f;
	}
}

ASoccerPlayer* ASoccerPlayerController::Current() const
{
	// Пока мяч не в игре (гол / конец матча) — кнопки действий не работают
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	if (!G || !G->IsPlayActive()) return nullptr;
	return Cast<ASoccerPlayer>(GetPawn());
}

FVector ASoccerPlayerController::StickToWorld(const FVector2D& Stick) const
{
	// Вверх по стику = «от камеры», вправо = вправо по экрану
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	const float Yaw = G ? G->GetCameraYaw() : 0.f;
	const FVector Forward = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Right = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
	return Forward * Stick.Y + Right * Stick.X;
}

FVector ASoccerPlayerController::AimDirection() const
{
	const FVector Dir = StickToWorld(MoveInput);
	if (Dir.Size() > 0.2f) return Dir.GetSafeNormal2D();
	const ASoccerPlayer* P = Current();
	return P ? P->GetActorForwardVector() : FVector::ForwardVector;
}

float ASoccerPlayerController::GetShotCharge() const
{
	if (ChargeStart < 0.f) return -1.f;
	// Полный заряд — за 1 секунду удержания
	return FMath::Clamp<float>(GetWorld()->GetTimeSeconds() - ChargeStart, 0.2f, 1.f);
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
	ChargeStart = -1.f;
	bContainHeld = false;
}

void ASoccerPlayerController::SwitchPlayer(const FVector& Dir)
{
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	ASoccerPlayer* Cur = Current();
	if (!G || !G->Ball || !Cur) return;

	ASoccerPlayer* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : G->Players)
	{
		if (P == Cur || P->Team != HumanTeam || P->bGoalkeeper) continue;
		float Score;
		if (Dir.IsNearlyZero())
		{
			// LB: ближайший к мячу
			Score = FVector::Dist2D(P->GetActorLocation(), G->Ball->GetActorLocation());
		}
		else
		{
			// Правый стик: партнёр в направлении щелчка
			FVector To = P->GetActorLocation() - Cur->GetActorLocation();
			To.Z = 0.f;
			const float Dot = FVector::DotProduct(To.GetSafeNormal(), Dir);
			if (Dot < 0.5f) continue;
			Score = To.Size() * (2.f - Dot);
		}
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

// --- A / ✖: пас низом (или головой) | в обороне — сдерживание (удержание) ---
void ASoccerPlayerController::OnA()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->CanKickBall()) P->Pass(EPassKind::Ground, AimDirection());
	else                  bContainHeld = true;
}
void ASoccerPlayerController::OnAStop() { bContainHeld = false; }

// --- B / ⭕: удар (зажать — сила, отпустить — удар; с RB — изящный) | в обороне — отбор/толчок ---
void ASoccerPlayerController::OnB()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->HasBall())
	{
		ChargeStart = GetWorld()->GetTimeSeconds(); // начинаем замах
	}
	else if (P->CanKickBall())
	{
		P->Shoot(AimDirection(), 0.8f, bRBHeld);    // мяч рядом/в воздухе: удар с лёта или головой
	}
	else
	{
		P->Tackle();
	}
}
void ASoccerPlayerController::OnBStop()
{
	ASoccerPlayer* P = Current();
	if (P && ChargeStart >= 0.f)
	{
		P->Shoot(AimDirection(), GetShotCharge(), bRBHeld);
	}
	ChargeStart = -1.f;
}

// --- X / ⬛: навес / длинный пас | в обороне — подкат ---
void ASoccerPlayerController::OnX()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->CanKickBall()) P->Pass(EPassKind::Lob, AimDirection());
	else                  P->SlideTackle(AimDirection());
}

// --- Y / ▲: пас в разрез | в обороне удержание — выход вратаря ---
void ASoccerPlayerController::OnY()
{
	ASoccerPlayer* P = Current();
	if (!P) return;
	if (P->CanKickBall()) P->Pass(EPassKind::Through, AimDirection());
	else                  bGKRushHeld = true;
}
void ASoccerPlayerController::OnYStop() { bGKRushHeld = false; }

// --- LB / L1: переключиться на игрока, ближайшего к мячу ---
void ASoccerPlayerController::OnLB() { SwitchPlayer(FVector::ZeroVector); }

// --- Start / Options: новый матч ---
void ASoccerPlayerController::OnRestart()
{
	if (ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>())
	{
		G->RestartMatch();
	}
}

// ============================================================================
//  РЕЖИМ ИГРЫ
// ============================================================================

ASoccerGameMode::ASoccerGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = ASoccerPlayerController::StaticClass();
	DefaultPawnClass = nullptr; // пешек спавним сами

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(MESH_CUBE);
	CubeMesh = Cube.Object;
}

void ASoccerGameMode::BeginPlay()
{
	Super::BeginPlay();

	BuildField();

	Ball = GetWorld()->SpawnActor<ASoccerBall>(FVector(0.f, 0.f, BallRadius), FRotator::ZeroRotator);

	SpawnTeams();

	Camera = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	UpdateCamera(1.f);

	PossessHuman();
}

// Вызывается движком для нового игрока. Пешку по умолчанию не спавним — берём красного нападающего.
void ASoccerGameMode::RestartPlayer(AController* NewPlayer)
{
	PossessHuman();
}

void ASoccerGameMode::PossessHuman()
{
	ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || Players.Num() < 4 || !Camera) return; // поле ещё не построено — повторим из BeginPlay
	PC->PossessPlayer(Players[3]);
	PC->SetViewTarget(Camera);
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
	const FLinearColor Grass(0.05f, 0.35f, 0.08f);
	const FLinearColor Line(0.95f, 0.95f, 0.95f);
	const FLinearColor Board(0.12f, 0.12f, 0.15f);
	const float LineW = 10.f;  // ширина линий разметки
	const float LineZ = 0.5f;  // чуть выше газона, чтобы не мерцало

	// Газон (с запасом за линиями). Верх газона — Z = 0.
	SpawnBox(FVector(0.f, 0.f, -10.f), FVector(2.f * HalfLength + 700.f, 2.f * HalfWidth + 400.f, 20.f), Grass, true);

	// Борта — не дают игрокам уйти с поля (для мяча борта считаются в коде мяча)
	SpawnBox(FVector(0.f,  HalfWidth + 70.f, 40.f), FVector(2.f * HalfLength + 600.f, 20.f, 80.f), Board, true);
	SpawnBox(FVector(0.f, -HalfWidth - 70.f, 40.f), FVector(2.f * HalfLength + 600.f, 20.f, 80.f), Board, true);
	SpawnBox(FVector( HalfLength + 280.f, 0.f, 40.f), FVector(20.f, 2.f * HalfWidth + 160.f, 80.f), Board, true);
	SpawnBox(FVector(-HalfLength - 280.f, 0.f, 40.f), FVector(20.f, 2.f * HalfWidth + 160.f, 80.f), Board, true);

	// Внешние линии и средняя линия
	SpawnBox(FVector(0.f,  HalfWidth, LineZ), FVector(2.f * HalfLength, LineW, 1.f), Line, false);
	SpawnBox(FVector(0.f, -HalfWidth, LineZ), FVector(2.f * HalfLength, LineW, 1.f), Line, false);
	SpawnBox(FVector( HalfLength, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);
	SpawnBox(FVector(-HalfLength, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);
	SpawnBox(FVector(0.f, 0.f, LineZ), FVector(LineW, 2.f * HalfWidth, 1.f), Line, false);

	// Центральный круг (радиус 3 м) из коротких отрезков + центральная точка
	const int32 Segments = 32;
	const float CircleR = 300.f;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Angle = 2.f * PI * i / Segments;
		const FVector Pos(FMath::Cos(Angle) * CircleR, FMath::Sin(Angle) * CircleR, LineZ);
		const float SegLen = 2.f * PI * CircleR / Segments + 2.f;
		SpawnBox(Pos, FVector(LineW, SegLen, 1.f), Line, false, FMath::RadiansToDegrees(Angle));
	}
	SpawnBox(FVector(0.f, 0.f, LineZ), FVector(25.f, 25.f, 1.f), Line, false);

	// Штрафные площади (упрощённо — прямоугольники 6×11 м), точки пенальти, ворота
	const float BoxHalfW = GoalHalfWidth + 400.f;
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float FrontX = Side * (HalfLength - PenaltyDepth);
		SpawnBox(FVector(FrontX, 0.f, LineZ), FVector(LineW, 2.f * BoxHalfW, 1.f), Line, false);
		SpawnBox(FVector(Side * (HalfLength - PenaltyDepth * 0.5f),  BoxHalfW, LineZ), FVector(PenaltyDepth, LineW, 1.f), Line, false);
		SpawnBox(FVector(Side * (HalfLength - PenaltyDepth * 0.5f), -BoxHalfW, LineZ), FVector(PenaltyDepth, LineW, 1.f), Line, false);
		SpawnBox(FVector(FrontX, 0.f, LineZ), FVector(25.f, 25.f, 1.f), Line, false);

		// Ворота на +X защищают синие (1), на −X — красные (0)
		ASoccerGoal* Goal = GetWorld()->SpawnActor<ASoccerGoal>(
			FVector(Side * HalfLength, 0.f, 0.f), FRotator(0.f, Side > 0 ? 0.f : 180.f, 0.f));
		Goal->DefendingTeam = Side > 0 ? 1 : 0;
	}
}

void ASoccerGameMode::SpawnTeams()
{
	// Расстановка 1-2-2 для красных (синие — зеркально по X).
	// Индекс 0 — вратарь, 1–2 — защитники, 3–4 — нападающие.
	const FVector2D Layout[5] = {
		FVector2D(-HalfLength + 70.f, 0.f),
		FVector2D(-1150.f, -450.f),
		FVector2D(-1150.f,  450.f),
		FVector2D(-450.f,  -350.f),
		FVector2D(-450.f,   350.f),
	};
	const float SpawnZ = 92.f; // полувысота капсулы + зазор

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 TeamIdx = 0; TeamIdx < 2; ++TeamIdx)
	{
		const float Mirror = TeamIdx == 0 ? 1.f : -1.f;
		for (int32 i = 0; i < 5; ++i)
		{
			const FVector Home(Layout[i].X * Mirror, Layout[i].Y, SpawnZ);
			ASoccerPlayer* P = GetWorld()->SpawnActor<ASoccerPlayer>(Home, FRotator::ZeroRotator, Params);
			P->Setup(TeamIdx, i == 0, Home);
			Players.Add(P);
		}
	}
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

float ASoccerGameMode::GetCameraYaw() const
{
	return CameraYaw;
}

void ASoccerGameMode::OnGoalScored(int32 ScoringTeam)
{
	if (!bPlayActive) return; // мяч уже в сетке / матч окончен
	Score[ScoringTeam]++;
	bPlayActive = false;
	// Через 2 секунды — расстановка заново и розыгрыш с центра
	GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::ResetPositions, 2.f, false);
}

void ASoccerGameMode::ResetPositions()
{
	if (bMatchOver) return;
	for (ASoccerPlayer* P : Players)
	{
		P->ResetToHome();
	}
	Ball->ResetBall(FVector(0.f, 0.f, BallRadius));
	bPlayActive = true;
}

void ASoccerGameMode::RestartMatch()
{
	GetWorldTimerManager().ClearTimer(ResetTimer);
	Score[0] = Score[1] = 0;
	TimeLeft = MatchLength;
	bMatchOver = false;
	ResetPositions();
	// человек снова управляет нападающим
	PossessHuman();
}

void ASoccerGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Ball) return;

	// Таймер идёт только во время игры (пауза после гола не считается)
	if (bPlayActive)
	{
		TimeLeft -= DeltaSeconds;
		if (TimeLeft <= 0.f)
		{
			TimeLeft = 0.f;
			bPlayActive = false;
			bMatchOver = true;
		}
	}

	UpdateCamera(DeltaSeconds);
	DrawHud();
}

void ASoccerGameMode::UpdateCamera(float Dt)
{
	if (!Camera || !Ball) return;
	// Плавно следим за мячом, но не уезжаем далеко за пределы поля
	FVector Target = Ball->GetActorLocation();
	Target.X = FMath::Clamp<double>(Target.X, -HalfLength + 900.f, HalfLength - 900.f);
	Target.Y = FMath::Clamp<double>(Target.Y, -400.f, 400.f);
	Target.Z = 0.f;
	CamFocus = FMath::VInterpTo(CamFocus, Target, Dt, 2.5f);

	const FRotator Rot(CameraPitch, CameraYaw, 0.f);
	Camera->SetActorLocationAndRotation(CamFocus - Rot.Vector() * CameraDistance, Rot);
}

void ASoccerGameMode::DrawHud()
{
	if (!GEngine) return;

	// Без UMG: выводим текст отладочными сообщениями (у каждого свой ключ — строка обновляется)
	GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::White,
		FString::Printf(TEXT("КРАСНЫЕ  %d : %d  СИНИЕ        Время: %d"),
		                Score[0], Score[1], FMath::CeilToInt(TimeLeft)),
		true, FVector2D(1.8f, 1.8f));

	if (bMatchOver)
	{
		const TCHAR* Result = Score[0] > Score[1] ? TEXT("Победа красных!")
		                    : Score[0] < Score[1] ? TEXT("Победа синих!")
		                                          : TEXT("Ничья");
		GEngine->AddOnScreenDebugMessage(2, 0.1f, FColor::Yellow,
			FString::Printf(TEXT("МАТЧ ОКОНЧЕН — %s   (Start / Enter — новый матч)"), Result),
			true, FVector2D(1.5f, 1.5f));
	}
	else if (!bPlayActive)
	{
		GEngine->AddOnScreenDebugMessage(2, 0.1f, FColor::Yellow, TEXT("ГОЛ!"), true, FVector2D(3.f, 3.f));
	}

	// Шкала силы удара, пока зажата B
	if (const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		const float Charge = PC->GetShotCharge();
		if (Charge >= 0.f)
		{
			GEngine->AddOnScreenDebugMessage(3, 0.1f, FColor::Orange,
				TEXT("Сила удара: ") + FString::ChrN(FMath::RoundToInt(Charge * 20.f), TEXT('|')));
		}
	}
}
