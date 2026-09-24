// Мини-футбол 5×5 — прототип.
// Все классы игры объявлены в одном заголовке. Реализация:
//   Soccer.cpp   — мяч, ворота, игроки и ИИ, управление, режим игры, сохранения
//   SoccerUI.cpp — интерфейс на Slate: главное меню, HUD матча, пауза
//
//  ASoccerBall             — мяч с аркадной физикой (отскоки, трение, удары, дриблинг, прогноз траектории)
//  ASoccerGoal             — ворота: штанги, сетка и триггер гола
//  ASoccerAimLine          — белая линия «куда полетит мяч» при замахе
//  ASoccerPlayer           — игрок-капсула: управление человеком, ИИ полевого и вратаря
//  ASoccerPlayerController — геймпад/клавиатура (схема FIFA), переключение игроков, замах с силой
//  ASoccerHUD              — шкала силы удара под игроком
//  ASoccerGameMode         — поле, стадион, меню ↔ матч, счёт, таймер, камера, награды
//  USoccerSave             — прогресс: монеты, состав, форма, испытания, настройки

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SaveGame.h"
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
class SWidget;
struct FInputActionValue;
struct FKey;

// Размеры в сантиметрах (1 uu = 1 см). Поле 40×24 м, ворота 3×2 м.
// Центр поля — (0,0,0). Длинная ось поля — X, ширина — Y.
// Команда 0 (человек) атакует в сторону +X, команда 1 — в сторону −X.
namespace Soccer
{
	constexpr float HalfLength    = 2000.f; // половина длины поля
	constexpr float HalfWidth     = 1200.f; // половина ширины поля
	constexpr float GoalHalfWidth = 150.f;  // половина ширины ворот
	constexpr float GoalHeight    = 200.f;  // высота ворот
	constexpr float GoalDepth     = 100.f;  // глубина ворот (до задней сетки)
	constexpr float BoardGap      = 150.f;  // от боковой линии до рекламного борта
	constexpr float BallRadius    = 22.f;   // радиус мяча
	constexpr float PenaltyDepth  = 600.f;  // глубина штрафной площади
	constexpr int32 HumanTeam     = 0;      // человек играет за команду 0
	constexpr int32 NumPractice   = 3;      // количество тренировок
}

// Тип паса
enum class EPassKind : uint8
{
	Ground,  // A / Space — пас низом
	Through, // Y / E     — пас в разрез (на ход)
	Lob      // X / Q     — навес / длинный пас
};

// Режим матча
enum class ESoccerMode : uint8
{
	Match,            // обычный матч 5×5
	PracticeShooting, // тренировка: удары по воротам
	PracticeOneOnOne, // тренировка: один против защитника и вратаря
	PracticeAttack    // тренировка: атака 5 на 2
};

// Что сейчас «заряжает» человек (зажатая кнопка удара/паса)
enum class ECharge : uint8 { None, Pass, Through, Lob, Shot };

// Игровая форма из магазина
struct FSoccerKit
{
	FString Name;
	FLinearColor Shirt;
	FLinearColor Shorts;
	int32 Price;
};
const TArray<FSoccerKit>& GetSoccerKits();
const TArray<FString>& GetClubNames();

// Готовый удар: скорость мяча, закрутка и адресат паса
struct FSoccerKick
{
	FVector Velocity = FVector::ZeroVector;
	FVector Curve = FVector::ZeroVector;
	float CurveTime = 0.f;
	ASoccerPlayer* Receiver = nullptr;
};

// Карточка футболиста: имя, позиция и характеристики (как в FIFA)
USTRUCT()
struct FSoccerPlayerInfo
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FString Position;     // ВР — вратарь, ЗЩ — защитник, НП — нападающий
	UPROPERTY() int32 Pace = 70;      // СКР — скорость
	UPROPERTY() int32 Shooting = 70;  // УДР — удар
	UPROPERTY() int32 Passing = 70;   // ПАС — пас
	UPROPERTY() int32 Dribbling = 70; // ДРБ — дриблинг
	UPROPERTY() int32 Defending = 70; // ЗАЩ — защита
	UPROPERTY() int32 Physical = 70;  // ФИЗ — физика

	int32 Rating() const;
	int32& Stat(int32 Index);         // 0..5 в порядке СКР УДР ПАС ДРБ ЗАЩ ФИЗ
	int32 Stat(int32 Index) const;
	static const TCHAR* StatLabel(int32 Index);
};

// ============================================================================
//  СОХРАНЕНИЕ ПРОГРЕССА
// ============================================================================
UCLASS()
class USoccerSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 Coins = 1500;
	UPROPERTY() FString ClubName;
	UPROPERTY() int32 KitIndex = 0;
	UPROPERTY() TArray<bool> OwnedKits;
	UPROPERTY() TArray<FSoccerPlayerInfo> Squad;   // 5 игроков: 0 — вратарь
	UPROPERTY() int32 StarterIndex = 3;            // кем управляем в начале матча
	UPROPERTY() int32 MatchMinutes = 1;            // длительность матча, минуты
	UPROPERTY() int32 Difficulty = 1;              // 0 — лёгкая, 1 — нормальная, 2 — сложная
	UPROPERTY() int32 MatchesPlayed = 0;
	UPROPERTY() int32 Wins = 0;
	UPROPERTY() int32 GoalsScored = 0;
	UPROPERTY() int32 PassesMade = 0;
	UPROPERTY() TArray<bool> PracticeDone;
	UPROPERTY() TArray<bool> ClaimedChallenges;
	UPROPERTY() bool bStoreVisited = false;
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

	// Поставить мяч в точку и обнулить всё состояние.
	void ResetBall(const FVector& Location);

	// Сколько секунд прошло с последнего удара.
	float TimeSinceKick() const;

	// Прогноз полёта мяча (та же физика, что в Tick) — для белой линии прицела.
	void PredictPath(const FVector& Start, const FVector& StartVelocity, const FVector& Curve,
	                 float CurveTime, TArray<FVector>& OutPoints) const;

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
	// Один шаг физики: гравитация, трение, отскоки, борта, ворота.
	void Integrate(FVector& P, FVector& V, const FVector& Curve, float& CurveTime, float Dt) const;
	// Борта вокруг поля и «коробка» ворот: внутрь только через створ, изнутри держит сетка.
	void CollideWithWalls(FVector& P, const FVector& OldP, FVector& V) const;

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
//  ЛИНИЯ ПРИЦЕЛА (белая, как в FIFA)
// ============================================================================
UCLASS()
class ASoccerAimLine : public AActor
{
	GENERATED_BODY()

public:
	ASoccerAimLine();
	virtual void BeginPlay() override;

	void ShowPath(const TArray<FVector>& Points);
	void HidePath();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	static constexpr int32 MaxSegments = 48;
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

	// Настройка после спавна: команда, роль, позиция в расстановке, карточка, форма.
	void Setup(int32 InTeam, bool bInGoalkeeper, const FVector& InHome, float InHomeYaw,
	           const FSoccerPlayerInfo& InInfo, int32 InRosterIndex,
	           const FLinearColor& Shirt, const FLinearColor& Shorts);
	void ResetToHome();

	// ---------- Удары и пасы ----------
	// Расчёт удара без исполнения (для линии прицела) и исполнение.
	FSoccerKick PlanPass(EPassKind Kind, const FVector& AimDir, float Power01) const;
	FSoccerKick PlanShot(const FVector& AimDir, float Power01, bool bFinesse, bool bWithError) const;
	void Pass(EPassKind Kind, const FVector& AimDir, float Power01 = 0.5f);
	void Shoot(const FVector& AimDir, float Power01, bool bFinesse);

	// ---------- Оборона и прочее ----------
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
	int32 RosterIndex = 0;       // номер в составе (0 — вратарь)
	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	float ProtectTime = 0.f;     // после финта — короткая «неуязвимость» к отбору
	FLinearColor ShirtColor = FLinearColor::White;

	UPROPERTY() FSoccerPlayerInfo Info;

	// Визуал капсулы: тело, плечи, шорты, голова и маркер над игроком человека
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Top;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Feet;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Head;
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
	void ExecuteKick(const FSoccerKick& Plan);
	float SpeedFactor() const;
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

	// Базовые скорости (умножаются на характеристику СКР)
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
	// Сбросить зажатые кнопки/стики (при паузе, выходе в меню, старте матча).
	void ResetInputState();

	// Стик -> направление в мире с учётом поворота камеры.
	FVector StickToWorld(const FVector2D& Stick) const;
	// Куда целиться: левый стик, а если он отпущен — куда смотрит игрок.
	FVector AimDirection() const;
	// Сила замаха 0..1 (или −1, если ничего не зажато).
	float GetCharge() const;

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
	ASoccerGameMode* GM() const;
	void SwitchPlayer(const FVector& Dir); // Dir == 0 -> ближайший к мячу
	void BeginCharge(ECharge Kind);
	void ReleaseCharge(ECharge Kind);
	void UpdateAimPreview();
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
	void OnXStop();
	void OnY();
	void OnYStop();
	void OnLB();
	void OnStart();

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions; // держим ссылки, чтобы GC не удалил

	ECharge Charging = ECharge::None; // какая кнопка удара/паса зажата
	float ChargeStart = 0.f;          // когда начали замах
	bool bRightStickArmed = true;     // для распознавания «щелчка» правым стиком
};

// ============================================================================
//  HUD на Canvas: шкала силы удара/паса под игроком
// ============================================================================
UCLASS()
class ASoccerHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};

// ============================================================================
//  РЕЖИМ ИГРЫ: стадион, меню ↔ матч, правила, камера, награды
// ============================================================================
UCLASS()
class ASoccerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASoccerGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	// ---------- Поток игры (вызывается из меню) ----------
	void StartMatch(ESoccerMode InMode, const FString& RivalName = FString(), int32 RivalDifficulty = -1);
	void RestartMatch();
	void ReturnToMenu();
	void TogglePause();
	void QuitGame();

	// ---------- Прогресс ----------
	USoccerSave* GetSave() const { return Save; }
	void SaveProgress();
	void ResetProgress();
	void RefreshLineup();  // перерисовать состав в меню (после смены формы/обмена)
	static FSoccerPlayerInfo MakeRandomPlayer(const FString& Position, int32 BaseRating);

	// ---------- События матча ----------
	void OnGoalScored(int32 ScoringTeam);
	void OnHumanPass();

	// ---------- Данные для HUD и ИИ ----------
	bool IsPlayActive() const { return bPlayActive; }
	bool IsInMatch() const { return bInMatch; }
	bool IsMatchOver() const { return bMatchOver; }
	bool IsPractice() const { return MatchMode != ESoccerMode::Match; }
	int32 GetScore(int32 InTeam) const { return Score[InTeam]; }
	float GetTimeLeft() const { return TimeLeft; }
	float GetGoalBannerTime() const { return GoalBanner; }
	const FText& GetResultText() const { return ResultText; }
	FString GetTeamName(int32 InTeam) const;
	int32 GetPracticeTarget() const;
	int32 GetDifficulty() const { return MatchDifficulty; }
	ASoccerPlayer* GetTeamPlayer(int32 InTeam, int32 Index) const;
	ASoccerPlayer* GetHumanPlayer() const;
	ASoccerPlayer* GetFocusOpponent() const;
	// Ближайший к мячу полевой игрок команды (bOnlyAI — пропускать игрока человека).
	ASoccerPlayer* NearestToBall(int32 InTeam, bool bOnlyAI) const;
	float GetCameraYaw() const { return CameraYaw; }

	UPROPERTY() TObjectPtr<ASoccerBall> Ball;
	UPROPERTY() TObjectPtr<ASoccerAimLine> AimLine;
	UPROPERTY() TArray<TObjectPtr<ASoccerPlayer>> Players;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

private:
	void LoadProgress();
	void EnsureLighting();
	void BuildField();
	AStaticMeshActor* SpawnBox(const FVector& Center, const FVector& Size, const FLinearColor& Color,
	                           bool bCollide, float Yaw = 0.f);
	ASoccerPlayer* SpawnPlayer(int32 InTeam, int32 RosterIdx, const FSoccerPlayerInfo& PlayerInfo,
	                           const FVector& Location, float Yaw);
	void SpawnTeams();
	void SpawnLineup();
	void DestroyPlayers();
	void PossessHuman();
	void ResetPositions();
	void EndMatch();
	void UpdateCamera(float Dt);
	void ShowWidget(TSharedPtr<SWidget>& Holder, const TSharedRef<SWidget>& Widget, int32 ZOrder);
	void HideWidget(TSharedPtr<SWidget>& Holder);
	void SetUIInput(const TSharedPtr<SWidget>& Focus);
	void SetGameInput();

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<USoccerSave> Save;

	TSharedPtr<SWidget> MenuWidget;
	TSharedPtr<SWidget> HudWidget;
	TSharedPtr<SWidget> PauseWidget;

	TArray<FSoccerPlayerInfo> AwaySquad;
	FString AwayName;
	ESoccerMode MatchMode = ESoccerMode::Match;
	int32 MatchDifficulty = 1;
	int32 Score[2] = { 0, 0 };
	float TimeLeft = 60.f;
	float GoalBanner = 0.f;
	float MenuTime = 0.f;
	bool bInMatch = false;
	bool bPlayActive = false;
	bool bMatchOver = false;
	FText ResultText;
	FTimerHandle ResetTimer;
	FTimerHandle MenuTimer;
	FVector CamFocus = FVector::ZeroVector;

	// «Телевизионная» камера матча: сбоку и сверху, как на трансляции
	static constexpr float CameraPitch    = -48.f;
	static constexpr float CameraYaw      = -90.f; // смотрим с трибуны: +X — вправо по экрану
	static constexpr float CameraDistance = 3700.f;
	static constexpr float CameraFOV      = 42.f;
};

// ============================================================================
//  ИНТЕРФЕЙС (SoccerUI.cpp) — всё на Slate, без ассетов
// ============================================================================
namespace SoccerUI
{
	// Главное меню со всеми разделами. OutFocus — виджет, который получает фокус геймпада.
	TSharedRef<SWidget> MakeMenu(ASoccerGameMode* GameMode, TSharedPtr<SWidget>& OutFocus);
	// HUD матча: имена сверху, табло, таймер, карточки игроков, баннеры.
	TSharedRef<SWidget> MakeHud(ASoccerGameMode* GameMode);
	// Меню паузы.
	TSharedRef<SWidget> MakePause(ASoccerGameMode* GameMode, TSharedPtr<SWidget>& OutFocus);
}
