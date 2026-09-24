// Мини-футбол 5×5 — прототип геймплейного ядра.
// Все классы игры объявлены в одном заголовке, реализация — в Soccer.cpp.
//
//  ASoccerBall             — мяч с аркадной физикой (отскоки, трение, удары, дриблинг)
//  ASoccerGoal             — ворота: штанги, сетка и триггер гола
//  ASoccerPlayer           — игрок-капсула: управление человеком, ИИ полевого и вратаря
//  ASoccerPlayerController — геймпад/клавиатура (схема FIFA), переключение игроков
//  ASoccerGameMode         — поле, разметка, спавн, счёт, таймер, сброс, камера, HUD

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "Soccer.generated.h"

class USphereComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UInputAction;
class UInputMappingContext;
class ACameraActor;
class AStaticMeshActor;
class ASoccerPlayer;
class ASoccerGameMode;
class ASoccerPlayerController;
struct FInputActionValue;
struct FKey;

// Размеры поля в сантиметрах (1 uu = 1 см). Поле 40×24 м, ворота 3×2 м.
// Центр поля — (0,0,0). Длинная ось поля — X, ширина — Y.
// Красные (команда 0) атакуют в сторону +X, синие (команда 1) — в сторону −X.
namespace Soccer
{
	constexpr float HalfLength    = 2000.f; // половина длины поля
	constexpr float HalfWidth     = 1200.f; // половина ширины поля
	constexpr float GoalHalfWidth = 150.f;  // половина ширины ворот
	constexpr float GoalHeight    = 200.f;  // высота ворот
	constexpr float GoalDepth     = 100.f;  // глубина ворот (до задней сетки)
	constexpr float BallRadius    = 22.f;   // радиус мяча
	constexpr float PenaltyDepth  = 600.f;  // глубина штрафной площади
	constexpr float MatchLength   = 60.f;   // длительность матча, секунды
	constexpr int32 HumanTeam     = 0;      // человек играет за красных
}

// Тип паса
enum class EPassKind : uint8
{
	Ground,  // A / Space — пас низом
	Through, // Y / E     — пас в разрез (на ход)
	Lob      // X / Q     — навес / длинный пас
};

// ============================================================================
//  МЯЧ
// ============================================================================
UCLASS()
class ASoccerBall : public AActor
{
	GENERATED_BODY()

public:
	ASoccerBall();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Удар по мячу: мяч освобождается и получает скорость NewVelocity.
	// Curve — боковое ускорение (закрутка) на время CurveDuration.
	void Kick(ASoccerPlayer* Kicker, const FVector& NewVelocity,
	          const FVector& Curve = FVector::ZeroVector, float CurveDuration = 0.f);

	// Назначить игрока, который ведёт мяч (nullptr — мяч свободен).
	void SetOwnerPlayer(ASoccerPlayer* NewOwner);

	// Поставить мяч в точку и обнулить всё состояние (после гола / в начале матча).
	void ResetBall(const FVector& Location);

	// Сколько секунд прошло с последнего удара.
	float TimeSinceKick() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY() TObjectPtr<ASoccerPlayer> OwnerPlayer;      // кто сейчас ведёт мяч
	UPROPERTY() TObjectPtr<ASoccerPlayer> LastKicker;       // кто последним бил
	UPROPERTY() TObjectPtr<ASoccerPlayer> IntendedReceiver; // кому адресован пас

	FVector Velocity = FVector::ZeroVector;

	// Параметры аркадной физики
	float Gravity        = 1400.f; // гравитация (сильнее реальной — мяч «падает» бодрее)
	float Bounciness     = 0.55f;  // упругость отскока от газона
	float WallBounciness = 0.7f;   // упругость отскока от бортов
	float RollingFriction= 0.8f;   // трение качения (экспоненциальное затухание, 1/с)
	float AirDrag        = 0.05f;  // сопротивление воздуха

private:
	// Отскоки от бортов, пропуск мяча в ворота только через створ, задняя сетка.
	void CollideWithWalls(FVector& P, const FVector& OldP);

	FVector CurveAccel = FVector::ZeroVector;
	float CurveTimeLeft = 0.f;
	float LastKickTime  = -100.f;
};

// ============================================================================
//  ВОРОТА
// ============================================================================
UCLASS()
class ASoccerGoal : public AActor
{
	GENERATED_BODY()

public:
	ASoccerGoal();
	virtual void BeginPlay() override;

	// Команда, которая защищает эти ворота (гол засчитывается сопернику).
	int32 DefendingTeam = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Posts; // штанги и перекладина
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Nets;  // сетка

private:
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                      UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                      bool bFromSweep, const FHitResult& SweepResult);
};

// ============================================================================
//  ИГРОК (капсула)
// ============================================================================
UCLASS()
class ASoccerPlayer : public ACharacter
{
	GENERATED_BODY()

public:
	ASoccerPlayer();
	virtual void Tick(float DeltaTime) override;

	// Настройка после спавна: команда, роль, «домашняя» позиция в расстановке.
	void Setup(int32 InTeam, bool bInGoalkeeper, const FVector& InHome);
	void ResetToHome();

	// ---------- Действия (их вызывают контроллер человека или ИИ) ----------
	void Pass(EPassKind Kind, const FVector& AimDir);
	void Shoot(const FVector& AimDir, float Power01, bool bFinesse);
	void Tackle();                          // обычный отбор / толчок
	void SlideTackle(const FVector& Dir);   // подкат
	void SkillMove(const FVector& Dir, bool bBig); // финт правым стиком
	void Stun(float Seconds);               // игрок «сбит»: теряет мяч и управление
	void GainBall();                        // забрать мяч себе

	// ---------- Состояние ----------
	bool HasBall() const;
	bool TeamHasBall() const;
	bool CanKickBall() const;   // мяч у ног или рядом (в т.ч. в воздухе — удар головой/с лёта)
	bool IsShielding() const { return bShielding; }
	float AttackSign() const { return Team == 0 ? 1.f : -1.f; }

	int32 Team = 0;
	bool bGoalkeeper = false;
	FVector Home = FVector::ZeroVector;
	float ProtectTime = 0.f; // после финта — короткая «неуязвимость» к отбору

	// Визуал капсулы: тело (цилиндр) + две полусферы, «нос» и маркер над головой
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Feet;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Nose;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Marker;

private:
	void TickHuman(float Dt);
	void TickFieldAI(float Dt);
	void TickAIWithBall(float Dt);
	void TickGoalkeeper(float Dt);
	bool TickDash(float Dt);   // рывок/подкат/финт: движение по заданному направлению
	void TryControlBall();     // подобрать свободный мяч
	bool ShouldChase() const;  // бежать ли ИИ к мячу
	FVector FormationPoint() const;
	void MoveTo(const FVector& Target, float Speed);
	void FaceTowards(const FVector& Target, float Dt);
	void StartDash(const FVector& Dir, float Speed, float Time, bool bSlide);
	ASoccerPlayer* FindPassTarget(const FVector& AimDir) const;
	ASoccerPlayer* NearestOpponent(float& OutDist) const;
	ASoccerGameMode* GM() const;
	ASoccerPlayerController* HumanPC() const;

	bool bShielding = false;
	float StunTime = 0.f;
	float TackleCooldown = 0.f;
	float SkillCooldown = 0.f;
	float AIDecisionTimer = 0.f;
	float GKReactionTimer = 0.f;
	float GKTargetY = 0.f;

	FVector DashDir = FVector::ZeroVector;
	float DashSpeed = 0.f;
	float DashTime = 0.f;
	bool bSliding = false;

	// Скорости
	static constexpr float RunSpeed    = 450.f;
	static constexpr float SprintSpeed = 680.f;
	static constexpr float SlowSpeed   = 260.f; // укрывание мяча / жокей
	static constexpr float KeeperSpeed = 480.f;
};

// ============================================================================
//  КОНТРОЛЛЕР ИГРОКА (геймпад по схеме FIFA + клавиатура)
// ============================================================================
UCLASS()
class ASoccerPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASoccerPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// Взять под управление другого игрока (старый отдаётся ИИ).
	void PossessPlayer(ASoccerPlayer* NewPlayer);

	// Стик -> направление в мире с учётом поворота камеры.
	FVector StickToWorld(const FVector2D& Stick) const;
	// Куда целиться: левый стик, а если он отпущен — куда смотрит игрок.
	FVector AimDirection() const;
	// Заряд удара 0..1 (или −1, если B не зажата).
	float GetShotCharge() const;

	// Текущее состояние ввода — его читает ASoccerPlayer::TickHuman
	FVector2D MoveInput  = FVector2D::ZeroVector; // левый стик
	FVector2D RightInput = FVector2D::ZeroVector; // правый стик
	float SprintAxis = 0.f;   // RT
	float LTAxis     = 0.f;   // LT: укрывание мяча / жокей
	bool bRBHeld       = false; // RB: модификатор финтов / прессинг партнёром
	bool bContainHeld  = false; // A в обороне: сдерживание
	bool bGKRushHeld   = false; // удержание Y в обороне: выход вратаря

private:
	ASoccerPlayer* Current() const;
	void SwitchPlayer(const FVector& Dir); // Dir == 0 -> ближайший к мячу
	UInputAction* NewAction(int32 Dimensions); // 0 — кнопка, 1 — ось (курок), 2 — стик
	void Map(UInputAction* Action, const FKey& Key, bool bSwizzle = false, bool bNegate = false, bool bDeadZone = false);

	// Обработчики ввода
	void OnMove(const FInputActionValue& V);
	void OnMoveStop();
	void OnRight(const FInputActionValue& V);
	void OnRightStop();
	void OnSprint(const FInputActionValue& V);
	void OnSprintStop();
	void OnLT(const FInputActionValue& V);
	void OnLTStop();
	void OnRB();
	void OnRBStop();
	void OnA();
	void OnAStop();
	void OnB();
	void OnBStop();
	void OnX();
	void OnY();
	void OnYStop();
	void OnLB();
	void OnRestart();

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions; // держим ссылки, чтобы GC не удалил

	float ChargeStart = -1.f;     // время начала замаха удара
	bool bRightStickArmed = true; // для распознавания «щелчка» правым стиком
};

// ============================================================================
//  РЕЖИМ ИГРЫ: поле, команды, правила, камера, HUD
// ============================================================================
UCLASS()
class ASoccerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASoccerGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	void OnGoalScored(int32 ScoringTeam);
	void RestartMatch();
	bool IsPlayActive() const { return bPlayActive; }

	// Ближайший к мячу полевой игрок команды (bOnlyAI — пропускать игрока человека).
	ASoccerPlayer* NearestToBall(int32 InTeam, bool bOnlyAI) const;
	float GetCameraYaw() const;

	UPROPERTY() TObjectPtr<ASoccerBall> Ball;
	UPROPERTY() TArray<TObjectPtr<ASoccerPlayer>> Players;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

private:
	void BuildField();
	AStaticMeshActor* SpawnBox(const FVector& Center, const FVector& Size, const FLinearColor& Color,
	                           bool bCollide, float Yaw = 0.f);
	void SpawnTeams();
	void PossessHuman();
	void ResetPositions();
	void UpdateCamera(float Dt);
	void DrawHud();

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;

	int32 Score[2] = { 0, 0 };
	float TimeLeft = Soccer::MatchLength;
	bool bPlayActive = true;
	bool bMatchOver = false;
	FTimerHandle ResetTimer;
	FVector CamFocus = FVector::ZeroVector;

	static constexpr float CameraPitch    = -55.f; // наклон камеры
	static constexpr float CameraYaw      = -90.f; // смотрим «с трибуны»: +X — вправо по экрану
	static constexpr float CameraDistance = 5200.f;
	static constexpr float CameraFOV      = 35.f;  // узкий FOV — почти изометрия
};
