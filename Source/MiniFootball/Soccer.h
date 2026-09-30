// Мини-футбол 5×5 — прототип.
// Все классы игры объявлены в одном заголовке. Реализация:
//   Soccer.cpp   — мяч, ворота, игроки и ИИ, управление, режим игры, сохранения
//   SoccerUI.cpp — интерфейс на Slate: главное меню, HUD матча, пауза
//   SoccerAudio  — звуки, синтезированные в коде (свисток, удары, трибуны)
//
//  ASoccerBall             — мяч: гравитация, сопротивление воздуха, вращение (Магнус), отскоки, штанги, столкновения с игроками
//  ASoccerGoal             — ворота: штанги, сетка и триггер гола
//  ASoccerAimLine          — белая линия «куда полетит мяч» при замахе
//  ASoccerPlayer           — игрок-капсула: управление человеком, ИИ полевого и вратаря
//  ASoccerPlayerController — геймпад/клавиатура (схема FIFA), переключение игроков, замах с силой
//  ASoccerHUD              — шкала силы удара и выносливость под игроком, радар
//  ASoccerGameMode         — поле, стадион, меню ↔ матч, счёт, таймер, камера, награды,
//                            роли ИИ команд, стандарты, статистика матча, звук
//  USoccerSave             — прогресс: монеты, состав, форма, испытания, настройки

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SaveGame.h"
#include "SoccerAudio.h"
#include "SoccerLook.h"
#include "Soccer.generated.h"

class USoundWaveProcedural;
class UAudioComponent;
class USphereComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;
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

// Variants A/B/C of an action, compared in game: D-pad up/down (PageUp/PageDown) picks
// the action, left/right ([ / ]) the variant. Kept in GameUserSettings.ini and in the
// console variables mf.Var.<Action> (for the test harness).
namespace SoccerVariants
{
	enum EAction : int32 { KickOnRun, Dribble, Receive, Keeper, Support, Defense, Tempo, Num };
	struct FAction { const TCHAR* Id; const TCHAR* Title; const TCHAR* Names[3]; };
	const FAction& Describe(int32 Action);
	int32 Get(int32 Action);
	void Set(int32 Action, int32 Variant);
	void Load();
	// Number settings after the actions in the same D-pad list (the training camera):
	// left/right steps the value; the last entry puts them all back to the Goals values.
	int32 NumTunes();                      // including the reset entry
	const TCHAR* TuneTitle(int32 Tune);
	FString TuneValue(int32 Tune);         // empty for the reset entry
	void StepTune(int32 Tune, int32 Dir);
	inline int32 NumEntries() { return Num + NumTunes(); }
}

// Размеры в сантиметрах (1 uu = 1 см). Поле 40×27 м, ворота 5×2.2 м.
// Центр поля — (0,0,0). Длинная ось поля — X, ширина — Y.
// Команда 0 (человек) атакует в сторону +X, команда 1 — в сторону −X.
namespace Soccer
{
	constexpr float HalfLength    = 2000.f; // половина длины поля
	constexpr float HalfWidth     = 1350.f; // половина ширины поля
	// 5 x 2.2 m: ~2.7 keeper heights (a 1.88 m giraffe). At 3 x 2 m the keeper covered
	// almost the whole goal and a shot into the corner was no better than one at him.
	constexpr float GoalHalfWidth = 250.f;  // половина ширины ворот
	constexpr float GoalHeight    = 220.f;  // высота ворот
	constexpr float GoalDepth     = 100.f;  // глубина ворот (до задней сетки)
	constexpr float BoardGap      = 10.f;   // борт стоит сразу за боковой линией — линии «настоящие»
	constexpr float BallRadius    = 11.f;   // радиус мяча: стандартный размер футбольного мяча
	constexpr float PlayerRadius  = 35.f;   // радиус корпуса игрока (столкновения мяча с игроками)
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
	PracticeAttack,   // тренировка: атака 5 на 2
	FreeTraining     // свободная комната: один игрок, без таймера и ИИ
};

// Что сейчас «заряжает» человек (зажатая кнопка удара/паса)
enum class ECharge : uint8 { None, Pass, Through, Lob, Shot };

// Возобновление игры: пока мяч не введён, соперники исполнителя держат дистанцию
enum class ESoccerRestart : uint8
{
	None,
	Kickoff,  // с центра поля (соперники — за центральным кругом)
	FreeKick, // штрафной (соперники — не ближе 5 м, у ворот — стенка)
	Penalty   // пенальти (все, кроме бьющего и вратарей, — за штрафной)
};

// Роль ИИ полевого игрока (раздаёт ASoccerGameMode::UpdateTeamAI)
enum class ESoccerAIRole : uint8
{
	Support, // своя команда с мячом — открываться; иначе — держать позицию
	Chase,   // бежать на мяч / на перехват
	Press,   // прессинг владельца мяча
	Cover,   // страховка между мячом и своими воротами
	Mark     // персональная опека соперника
};

// Статистика команды за матч
struct FSoccerMatchStats
{
	float Possession = 0.f;   // секунды владения мячом
	int32 Shots = 0;
	int32 ShotsOnTarget = 0;
	int32 Passes = 0;
	int32 PassesCompleted = 0;
	int32 Saves = 0;
	int32 Fouls = 0;
};

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

// Готовый удар: скорость и вращение мяча, точка прицела и адресат паса
struct FSoccerKick
{
	FVector Velocity = FVector::ZeroVector;
	FVector Spin = FVector::ZeroVector;   // угловая скорость мяча, рад/с (эффект Магнуса)
	FVector Target = FVector::ZeroVector; // куда целимся (для стрелки направления)
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
	UPROPERTY() FSoccerLook Look;      // внешность (нос, глаза, полнота...), см. SoccerLook.h

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
	UPROPERTY() int32 SoundVolume = 8;             // громкость звука 0..10
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

	// Удар: мяч освобождается и получает скорость и вращение (верхнее/нижнее/боковое).
	void Kick(ASoccerPlayer* Kicker, const FVector& NewVelocity, const FVector& NewSpin = FVector::ZeroVector);
	// Толчок мяча по газону с заданной скоростью (владелец не меняется).
	void Touch(const FVector& NewVelocity);
	float TrapUntil = 0.f; // receive variant C: the ball is killed at the feet until then
	float LeadUntil = 0.f; // receive variant B: first touch out in front along LeadDir
	FVector LeadDir = FVector::ZeroVector;

	// Назначить игрока, который ведёт мяч (nullptr — мяч свободен).
	void SetOwnerPlayer(ASoccerPlayer* NewOwner);

	// Поставить мяч в точку и обнулить всё состояние.
	void ResetBall(const FVector& Location);

	// Сколько секунд прошло с последнего удара и когда он был.
	float TimeSinceKick() const;
	float GetLastKickTime() const { return LastKickTime; }

	// Где будет мяч через T секунд (по газону — с трением; для перехватов и приёма у ИИ).
	FVector PredictLocation(float T) const;

	// Во что мяч ударился за шаг физики (для звуков и реакции трибун)
	static constexpr int32 HitPost = 1;      // штанга или перекладина
	static constexpr int32 HitBoard = 2;     // боковой борт
	static constexpr int32 HitEndBoard = 4;  // борт за линией ворот (мимо ворот или выше)
	static constexpr int32 HitNet = 8;       // сетка ворот

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY() TObjectPtr<ASoccerPlayer> OwnerPlayer;      // кто ведёт мяч (или держит вратарь)
	UPROPERTY() TObjectPtr<ASoccerPlayer> LastKicker;       // кто последним бил
	UPROPERTY() TObjectPtr<ASoccerPlayer> IntendedReceiver; // кому адресован пас

	FVector Velocity = FVector::ZeroVector;
	FVector Spin = FVector::ZeroVector; // угловая скорость, рад/с

	// Параметры физики мяча
	float Gravity         = 1400.f;  // гравитация (сильнее реальной — мяч «падает» бодрее)
	float Bounciness      = 0.55f;   // упругость отскока от газона
	float WallBounciness  = 0.7f;    // упругость отскока от бортов
	float PostBounciness  = 0.6f;    // упругость отскока от штанг и перекладины
	float RollingFriction = 0.8f;    // трение качения (экспоненциальное затухание, 1/с)
	float AirDragQuad     = 6e-5f;   // сопротивление воздуха: a = −k·|v|·v (быстрый мяч тормозит сильнее)
	float MagnusCoeff     = 0.02f;   // эффект Магнуса: a = k·(ω × v)
	float SpinDecayAir    = 0.4f;    // затухание вращения в полёте, 1/с

	// Свободная тренировка: мяч ведётся касаниями вокруг собаки (ApplyControlledTouch),
	// свободный мяч и удары — по общей физике.
	bool bTrainingPhysics = false;

	static constexpr float PostRadius = 6.f; // радиус штанг и перекладины

private:
	// Один шаг физики: гравитация, сопротивление, Магнус, трение, отскоки, борта, ворота.
	// Возвращает флаги Hit* — во что ударился мяч.
	int32 Integrate(FVector& P, FVector& V, FVector& W, float Dt) const;
	// Борта вокруг поля и ворота: внутрь только через створ, круглые штанги и перекладина, сетка.
	int32 CollideWithWalls(FVector& P, const FVector& OldP, FVector& V) const;
	// Training court: fence, side boards and the goals measured from football_court.glb.
	int32 CollideWithTrainingCourt(FVector& P, const FVector& OldP, FVector& V) const;
	// Мяч отскакивает от корпуса и ног игроков (кроме того, кто ведёт мяч).
	void CollideWithPlayers(FVector& P);
	void ApplyControlledTouch(FVector& P, float Dt);
	FVector CarryCenter = FVector::ZeroVector; // smoothed body centre of the ball carrier
	bool bCarryCenterValid = false;
	float CarryAngle = 0.f; // ball around the carrier: world yaw (rad) and distance (cm)
	float CarryR = 0.f;
	bool bCarryPolarValid = false;

	float LastKickTime = -100.f;
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

	// Белая стрелка от мяча в направлении паса/удара
	void ShowArrow(const FVector& From, const FVector& To);
	// Goals corner: the lime flight of the cross (gravity only, drag and spin ignored)
	void ShowArc(const FVector& From, const FVector& Velocity, float Gravity);
	void HidePath();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	static constexpr int32 MaxSegments = 24; // arrow: shaft + two barbs; arc: all of them
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

	// Dribble rhythm, read by the ball's carry in free training
	float GetDribblePhase() const { return DribblePhase; }
	bool IsCloseControl() const { return bCloseControl; }
	bool IsSprintingNow() const { return bSprinting; }
	// Where the body actually is: motion matching may shift the pelvis away from the capsule.
	FVector GetBodyCenter() const;
	// Facing of the visible torso (hips and shoulders averaged, smoothed over the stride).
	FVector GetBodyForward() const;

	// Motion Matching (Epic Game Animation Sample): BP_SoccerDog feeds these into
	// BPI_SandboxCharacter_Pawn::Get_PropertiesForAnimation for the hidden mannequin.
	UFUNCTION(BlueprintPure, Category = "Soccer|Animation")
	bool WantsSprintForAnimation() const;
	UFUNCTION(BlueprintPure, Category = "Soccer|Animation")
	USkeletalMeshComponent* GetMotionBody() const { return MotionBody; }

	// Настройка после спавна: команда, роль, позиция в расстановке, карточка, форма.
	void Setup(int32 InTeam, bool bInGoalkeeper, const FVector& InHome, float InHomeYaw,
	           const FSoccerPlayerInfo& InInfo, int32 InRosterIndex,
	           const FLinearColor& Shirt, const FLinearColor& Shorts);
	void ResetToHome();
	// 3D-модель футболиста (Mixamo) с анимациями «стоит» и «бежит». У каждой анимации — своя
	// модель с тем же скелетом (при автоимпорте каждый FBX получает свой скелет). Без модели — капсула.
	void ApplyCharacterModel(USkeletalMesh* InIdleMesh, UAnimSequence* InIdle, USkeletalMesh* InRunMesh, UAnimSequence* InRun);

	// ---------- Удары и пасы ----------
	// Расчёт удара без исполнения (для стрелки направления) и исполнение.
	// bWithError — добавить разброс по точности (навык, сила, угол тела, прессинг, удар в касание).
	FSoccerKick PlanPass(EPassKind Kind, const FVector& AimDir, float Power01, bool bWithError) const;
	FSoccerKick PlanShot(const FVector& AimDir, float Power01, bool bFinesse, bool bChip, bool bWithError) const;
	// Если мяч укатился дальше, чем достаёт нога, удар откладывается: игрок добегает и бьёт.
	void Pass(EPassKind Kind, const FVector& AimDir, float Power01 = 0.5f);
	void Shoot(const FVector& AimDir, float Power01, bool bFinesse, bool bChip = false);
	// FIFA fake shot (shoot, then pass while charging): the wind-up only, the ball stays.
	void FakeShot();
	void Header(bool bShot, const FVector& AimDir); // прыжок и удар головой по мячу в воздухе
	// First-time kick: pressed while the pass is still on its way, it fires the moment the
	// ball reaches the foot, with no reception touch.
	bool IsBallComingToMe() const;
	void QueueFirstTime(ECharge Kind, const FVector& AimDir, float Power01, bool bFinesse, bool bChip);
	ECharge FirstTimeKind = ECharge::None;
	FVector FirstTimeAim = FVector::ZeroVector;
	float FirstTimePower = 0.5f;
	bool bFirstTimeFinesse = false;
	bool bFirstTimeChip = false;
	float FirstTimeUntil = 0.f;
	void TickFirstTime();

	// ---------- Оборона и прочее ----------
	void Tackle();                          // отбор ногой: попал в мяч — выбил, попал в ноги — фол
	void SlideTackle(const FVector& Dir);   // подкат
	void SkillMove(const FVector& Dir, bool bBig); // финт правым стиком: откидка мяча в сторону
	void Stun(float Seconds);               // игрок «сбит»: теряет мяч и управление
	void GainBall();                        // забрать мяч себе
	float GetBallGainTime() const { return BallGainTime; }

	// ---------- ИИ ----------
	void SetAIRole(ESoccerAIRole InRole, ASoccerPlayer* InMark = nullptr);
	void PrepareRestart(float Delay);       // исполнитель стандарта: пауза перед розыгрышем
	// Через сколько секунд игрок успеет к мячу (99 — не успеет) и где встретит его.
	float InterceptTime(FVector& OutPoint) const;

	// ---------- Состояние ----------
	bool HasBall() const;
	bool TeamHasBall() const;
	bool CanKickBall() const;   // мяч в зоне удара ногой (у ног или рядом, в т.ч. с лёта)
	bool CanHeadBall() const;   // мяч в воздухе рядом — можно сыграть головой
	FVector GetDribbleSpot() const; // где держится мяч при ведении (у ног, перед игроком)
	float GetStamina() const { return Stamina; }
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
	// Цветной круг под ногами — цвет команды (нужен, когда у всех одинаковая 3D-модель)
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
	bool IsGiraffe() const { return bGiraffeModel; }
	bool IsPlayingAction() const { return DogAction != nullptr; }
	mutable bool bRunningInBehind = false; // set by ComputeSupportPoint: this support move is a run past the last line
	void SnapBodyYaw() { bBodyYawValid = false; } // after a teleport: the carry must not use the old heading
	// Keeper beaten by the shot kicked at KickTime: it goes past him, not off his body.
	bool IsBeatenBy(float KickTime) const { return bGoalkeeper && GKDecisionKick == KickTime && !bGKWillSave; }

private:
	void UpdateAnimation();
	friend class FDogVisualIntegrationTest;
	void ApplyDogModel(USkeletalMesh* Mesh);
	void SetupMotionMatching();
	void UpdateDogAnimation();
	void PlayDogAction(FName Action, float Duration = 0.f);
	void TickHuman(float Dt);
	void TickFieldAI(float Dt);
	void TickAIWithBall(float Dt);
	void TickGoalkeeper(float Dt);
	void TryKeeperSave();      // вратарь: отбить/поймать мяч в зоне досягаемости
	void DecideShot();         // вратарь: решить (один раз на удар), возьмёт ли он мяч
	void KeeperCatch();        // вратарь: поймать мяч в руки
	void UpdateBodyPose(float Dt); // наклон модели в подкате и в броске вратаря
	bool TickDash(float Dt);   // рывок/подкат/финт: движение по заданному направлению
	void TryControlBall();     // приём мяча (первое касание)
	void TickDribble(float Dt);        // ведение: ритм касаний (мяч держится у ног, см. GetDribbleSpot)
	void TickHumanKeeper(float Dt);    // ваш вратарь с мячом в руках: вы выбираете, куда отдать пас
	void KeeperDistribute();           // вратарь сам вводит мяч в игру
	void HandOverFromKeeper();         // после паса вратаря управление переходит к полевому игроку
	bool TickPendingKick(float Dt);    // добежать до мяча и выполнить отложенный удар
	void UpdateLocomotion(float Dt);   // инерция: разгон, торможение, радиус поворота, выносливость
	float SprintSpeedNow() const;      // скорость спринта с учётом усталости
	float KickErrorDegrees(int32 Skill, float Power01, const FVector& Dir, bool bFirstTime) const;
	void QueueKick(ECharge Kind, const FVector& AimDir, float Power01, bool bFinesse, bool bChip);
	void ExecuteQueued();
	FVector FormationPoint() const;
	// ---------- ИИ полевого игрока ----------
	int32 AILevel() const;                     // 0..2: сложность для соперника, «нормально» для партнёров
	void TickPress(float Dt);                  // прессинг владельца: сдерживание и отбор в удачный момент
	void TickRestartHold(float Dt);            // стандарт у соперника: держать дистанцию
	FVector ComputeSupportPoint() const;       // куда открыться под пас
	bool DecideWithBall(int32 Level);          // удар / пас / продолжить ведение
	void TakeRestart();                        // ИИ разыгрывает стандарт
	float EvaluateShot(float& OutAimFrac, float& OutPower, bool& bOutFinesse) const;
	float EvaluatePass(const ASoccerPlayer* Mate, EPassKind Kind, FVector& OutTarget) const;
	bool FindBestPass(EPassKind& OutKind, FVector& OutTarget, float& OutScore) const;
	FVector DribbleDirection() const;          // к воротам, огибая соперников
	float SpaceAhead() const;                  // сколько свободного места впереди (до соперника)
	FVector ShotAimDir(float AimFrac) const;   // направление «стика» для удара в точку створа (−1..1)
	void MoveTo(const FVector& Target, float Speed);
	void FaceTowards(const FVector& Target, float Dt);
	void StartDash(const FVector& Dir, float Speed, float Time, bool bSlide, bool bTurn = true);
	void ExecuteKick(const FSoccerKick& Plan);
	float SpeedFactor() const;
	ASoccerPlayer* FindPassTarget(const FVector& AimDir) const;
	ASoccerPlayer* NearestOpponent(float& OutDist) const;
	ASoccerGameMode* GM() const;
	ASoccerPlayerController* HumanPC() const;

	bool bShielding = false;
	bool bSprinting = false;      // в этом кадре бежит спринтом (тратит выносливость)
	bool bCloseControl = false;   // LT с мячом — короткие касания
	float Stamina = 1.f;          // выносливость 0..1: спринт тратит, шаг восстанавливает
	float TouchCooldown = 0.f;
	float ControlCooldown = 0.f;
	float DribblePhase = 0.f;     // ритм «касаний» при ведении (мяч чуть отходит от ноги и возвращается)
	bool bSlideResolved = false;  // подкат уже выбил мяч или сфолил

	// Отложенный удар (мяч впереди, игрок добегает до него)
	bool bPendingKick = false;
	ECharge PendingKind = ECharge::None;
	FVector PendingAim = FVector::ZeroVector;
	float PendingPower = 0.f;
	bool bPendingFinesse = false;
	bool bPendingChip = false;
	float PendingTime = 0.f;

	float StunTime = 0.f;
	float TackleCooldown = 0.f;
	float SkillCooldown = 0.f;
	float AIDecisionTimer = 0.f;

	// Роль ИИ и точка открывания
	ESoccerAIRole AIRole = ESoccerAIRole::Support;
	TWeakObjectPtr<ASoccerPlayer> MarkTarget;
	FVector SupportPoint = FVector::ZeroVector;
	float SupportTimer = 0.f;
	float GKReactionTimer = 0.f;
	float GKTargetY = 0.f;
	float GKDecisionKick = -1000.f; // удар, по которому вратарь уже решил, берёт ли он его
	bool bGKWillSave = false;
	float GKTackleCooldown = 0.f;
	float GKHoldTime = 0.f;       // сколько вратарь держит мяч в руках
	bool bGKDived = false;        // бросок по текущему удару уже сделан

	// Поза тела: подкат (лечь назад) и бросок вратаря (упасть вбок)
	float SlidePoseTime = 0.f;
	float DivePoseTime = 0.f;
	float SlideAlpha = 0.f;
	float DiveAlpha = 0.f;
	float DiveSign = 1.f;
	bool bPoseDirty = false;

	UPROPERTY() TObjectPtr<USkeletalMesh> IdleMesh;
	UPROPERTY() TObjectPtr<USkeletalMesh> RunMesh;
	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> CurrentAnim;
	// Hidden Epic mannequin running motion matching; the dog copies its pose (SoccerDog.cpp).
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> MotionBody;
	float ActionWeight = 0.f;
	float BodyYaw = 0.f;
	bool bBodyYawValid = false;
	void UpdateBodyYaw(float Dt);
	UPROPERTY() TMap<FName, TObjectPtr<UAnimSequence>> DogAnimations;
	// Mocap takes are long: play only [X, Y] seconds around the action (kick contact etc.).
	TMap<FName, FVector2f> DogClipWindows;
	TMap<FName, float> DogClipRate;
	TMap<FName, float> DogClipContact; // seconds from window start to foot-ball contact
	TMap<FName, uint8> DogClipFoot;    // kicking foot of the take: 1 left, 2 right
	FName LastActionName;              // the action PlayDogAction started last
	uint8 ActionFoot = 0;              // layered kick: 0 whole body, 1/2 left/right leg + torso
	float LastActionContact = -1.f;    // its real-time delay to the ball contact (variant)
	float KickSlowSpeed = 0.f;         // speed cap while KickSlowTime runs
	float DogActionStart = 0.f;
	// Kick planned on the button press, released when the foot reaches the ball.
	FSoccerKick DelayedKick;
	float DelayedKickTime = -1.f;
	void KickAtContact(const FSoccerKick& Plan, FName Action);
	// Right-stick skill move in training: a steered cut instead of a dash (see TickHuman).
	FVector SkillDir = FVector::ZeroVector;
	float SkillTime = 0.f;
	float KickSlowTime = 0.f; // slowed down while planting for a pass/shot
	float BallGainTime = -100.f; // when this player last got the ball (pressers give time)
	UPROPERTY() TObjectPtr<UAnimSequence> DogAction;
	float DogActionEnd = 0.f;
	float DogActionRate = 1.f;
	bool bDogModel = false;
	bool bGiraffeModel = false; // SK_Giraffe on the dog skeleton (taller, own kit/retargeter)
	float NextTrainingStateLogTime = 0.f;
	int32 TrainingStateLogCount = 0;

	FVector DashDir = FVector::ZeroVector;
	float DashSpeed = 0.f;
	float DashTime = 0.f;
	bool bSliding = false;

	// Базовые скорости (умножаются на характеристику СКР)
	static constexpr float RunSpeed    = 450.f;
	static constexpr float SprintSpeed = 680.f;
	static constexpr float SlowSpeed   = 260.f; // укрывание мяча / жокей
	static constexpr float KeeperSpeed = 420.f;
	static constexpr float RunAnimSpeed = 300.f; // скорость (см/с), при которой анимация бега идёт 1:1
	static constexpr float MeshYawOffset = -90.f; // модели Mixamo после импорта смотрят вдоль +Y
	static constexpr float KickReach = 95.f;      // до какого расстояния достаёт нога
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
	// В PIE локальный игрок может появиться после BeginPlay контроллера.
	void EnsureInputMapping();

	// Стик -> направление в мире с учётом поворота камеры.
	FVector StickToWorld(const FVector2D& Stick) const;
	// Куда целиться: левый стик, а если он отпущен — куда смотрит игрок.
	FVector AimDirection() const;
	// Сила замаха 0..1 (или −1, если ничего не зажато).
	float GetCharge() const;
	// Team-mate the pass being aimed would go to (white tag above him, Goals style)
	const ASoccerPlayer* GetAimReceiver() const { return AimReceiver.Get(); }
	TWeakObjectPtr<ASoccerPlayer> AimReceiver;
	bool bHoldMoveUntilNeutral = false; // after a pass switch: ignore the stick until released

	// Текущее состояние ввода — его читает ASoccerPlayer::TickHuman
	FVector2D MoveInput  = FVector2D::ZeroVector; // левый стик
	FVector2D RightInput = FVector2D::ZeroVector; // правый стик
	float SprintAxis = 0.f;   // RT
	float LTAxis     = 0.f;   // LT: укрывание мяча / жокей
	bool bRBHeld       = false; // RB: модификатор финтов / прессинг партнёром
	bool bLBHeld       = false; // LB в атаке: LB + B — удар «парашютом»
	bool bContainHeld  = false; // A в обороне: сдерживание
	bool bGKRushHeld   = false; // удержание Y в обороне: выход вратаря
	// Variant picker (SoccerVariants): the action shown and until when the HUD shows it
	int32 VariantAction = 0;
	float VariantShowUntil = -100.f;

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
	void OnLBStop();
	void OnStart();
	void OnR3();
	void OnVariantAction(int32 Step);
	void OnVariantPick(int32 Step);

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions; // держим ссылки, чтобы GC не удалил

	ECharge Charging = ECharge::None; // какая кнопка удара/паса зажата
	float ChargeStart = 0.f;          // когда начали замах
	bool bRightStickArmed = true;     // для распознавания «щелчка» правым стиком
	float LastSwitchTime = -100.f;    // для перебора игроков повторными нажатиями LB
	float NextTrainingMoveLogTime = 0.f;
	int32 TrainingMoveLogCount = 0;
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

private:
	// Радар (мини-карта поля) внизу по центру: игроки цветом формы, мяч — белая точка
	void DrawRadar(const ASoccerGameMode* G);
	void DrawPlayerTag(const ASoccerPlayer* P, const FLinearColor& Color, float Charge); // Charge < 0: no power bar
	void DrawVariantPicker(const ASoccerPlayerController* PC);
	void DrawIntro(const ASoccerGameMode* G);
	// Smooth round tag (a drawn texture: rectangles read as a pixel diamond)
	UPROPERTY() TObjectPtr<UTexture2D> TagTexture;
	UTexture2D* GetTagTexture();
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
	void OnFoul(ASoccerPlayer* Offender, ASoccerPlayer* Victim); // фол: штрафной или пенальти
	// Для статистики и звука
	void OnShot(ASoccerPlayer* Shooter);
	void OnPassMade(ASoccerPlayer* Passer);
	void OnBallGained(ASoccerPlayer* Receiver);
	void OnKeeperSave(ASoccerPlayer* Keeper);
	void OnBallImpact(int32 HitFlags, const FVector& Where, float Speed); // штанга, борт, сетка
	void PlaySfx(ESoccerSound Sound, float Gain = 1.f);

	// ---------- Стандарты ----------
	ESoccerRestart GetRestart() const { return Restart; }
	bool IsRestartTaker(const ASoccerPlayer* P) const;
	// Kick-in by the human's team: the AI takes it, the human plays the one getting open.
	bool IsKickIn() const { return bKickIn && Restart != ESoccerRestart::None; }
	bool bKickIn = false;
	bool bCorner = false;
	bool IsFirstPersonCorner() const { return bCorner && Restart != ESoccerRestart::None && RestartTaker.IsValid() && RestartTaker->IsPlayerControlled(); }           // the running restart is a corner (first-person view for yours)
	bool bPendingCorner = false;
	// Kick-off not taken yet: everyone except the taker stands still
	bool IsKickoffWaiting(const ASoccerPlayer* P) const { return Restart == ESoccerRestart::Kickoff && P && RestartTaker.Get() != P; }
	bool MustKeepDistance(const ASoccerPlayer* P) const; // стоять ли игроку в стороне от мяча
	float GetRestartRadius() const;

	// ---------- Статистика ----------
	const FSoccerMatchStats& GetStats(int32 InTeam) const { return Stats[InTeam]; }
	int32 GetPossessionPercent(int32 InTeam) const;

	// ---------- Данные для HUD и ИИ ----------
	bool IsPlayActive() const { return bPlayActive; }
	// Match intro (Goals, 15 s): loading card, pitch-level shot, whip, fly-over, line-up + VS
	bool IsIntro() const { return IntroTime >= 0.f; }
	float GetIntroTime() const { return IntroTime; }
	float IntroTime = -1.f;
	void BeginIntro();
	void EndIntro();
	void TickIntro(float Dt);
	bool UpdateIntroCamera();
	// Goal celebration (Goals): cards and radar step aside, confetti and spark fountains
	bool IsCelebrating() const { return GetWorld() && GetWorld()->GetTimeSeconds() < CelebrateUntil; }
	float CelebrateUntil = -1.f;
	bool IsInMatch() const { return bInMatch; }
	bool IsMatchOver() const { return bMatchOver; }
	bool IsPractice() const { return MatchMode == ESoccerMode::PracticeShooting || MatchMode == ESoccerMode::PracticeOneOnOne || MatchMode == ESoccerMode::PracticeAttack; }
	bool IsFreeTraining() const { return MatchMode == ESoccerMode::FreeTraining; }
	int32 GetScore(int32 InTeam) const { return Score[InTeam]; }
	float GetTimeLeft() const { return TimeLeft; }
	float GetEventBannerTime() const { return EventBanner; }
	const FText& GetEventText() const { return EventText; }
	const FText& GetResultText() const { return ResultText; }
	FString GetTeamName(int32 InTeam) const;
	int32 GetPracticeTarget() const;
	int32 GetDifficulty() const { return MatchDifficulty; }
	ASoccerPlayer* GetTeamPlayer(int32 InTeam, int32 Index) const;
	ASoccerPlayer* GetHumanPlayer() const;
	ASoccerPlayer* GetFocusOpponent() const;
	ASoccerPlayer* GetGoalkeeper(int32 InTeam) const;
	float GetCameraYaw() const { return CameraYaw; }
	float GetControlCameraYaw() const;
	// R3 in free training: close follow camera <-> wide broadcast view.
	void ToggleTrainingCamera() { bTrainingWideCamera = !bTrainingWideCamera; }
	bool bTrainingWideCamera = false;

	UPROPERTY() TObjectPtr<ASoccerBall> Ball;
	UPROPERTY() TObjectPtr<ASoccerAimLine> AimLine;
	UPROPERTY() TArray<TObjectPtr<ASoccerPlayer>> Players;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TArray<TObjectPtr<AActor>> GeneratedFieldActors;
	UPROPERTY() TArray<TObjectPtr<AActor>> TrainingRoomActors;

private:
	void LoadProgress();
	void LoadCharacterAssets();
	void EnsureLighting();
	void BuildField();
	AStaticMeshActor* SpawnBox(const FVector& Center, const FVector& Size, const FLinearColor& Color,
	                           bool bCollide, float Yaw = 0.f);
	ASoccerPlayer* SpawnPlayer(int32 InTeam, int32 RosterIdx, const FSoccerPlayerInfo& PlayerInfo,
	                           const FVector& Location, float Yaw);
	void SpawnTeams();
	void SpawnTrainingRoom();
	void SpawnMatchCourt();
	void CheckBallOut();
	bool bOutOfPlayRestart = false; // kick-in / corner / goal kick: no wall
	bool bPendingKickIn = false;    // the coming set piece is a kick-in
	float TrainingBallOutTime = -1.f; // training: the ball rolled out, put it back after a moment
	void SpawnEnvironment(float Sx, float Sy);
	void SpawnLineup();
	void DestroyPlayers();
	void PossessHuman();
	void ResetPositions();
	void StartSetPiece();
	void BeginRestart(ESoccerRestart Kind, ASoccerPlayer* Taker, const FVector& Spot);
	void UpdateTeamAI();
	void InitAudio();
	void PumpAudio(float Dt);
	void PlayOoh(float Gain);
	void EndMatch();
	void UpdateCamera(float Dt);
	void ShowWidget(TSharedPtr<SWidget>& Holder, const TSharedRef<SWidget>& Widget, int32 ZOrder);
	void HideWidget(TSharedPtr<SWidget>& Holder);
	void SetUIInput(const TSharedPtr<SWidget>& Focus);
	void SetGameInput();

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<USoccerSave> Save;
	// Модель и анимации футболиста из Content/Characters/Footballer (если импортированы)
	UPROPERTY() TObjectPtr<USkeletalMesh> FootballerMesh;     // модель для Idle (и по умолчанию)
	UPROPERTY() TObjectPtr<USkeletalMesh> FootballerRunMesh;  // модель со скелетом анимации бега
	UPROPERTY() TObjectPtr<UAnimSequence> FootballerIdle;
	UPROPERTY() TObjectPtr<UAnimSequence> FootballerRun;

	TSharedPtr<SWidget> MenuWidget;
	TSharedPtr<SWidget> HudWidget;
	TSharedPtr<SWidget> PauseWidget;

	TArray<FSoccerPlayerInfo> AwaySquad;
	FString AwayName;
	ESoccerMode MatchMode = ESoccerMode::Match;
	int32 MatchDifficulty = 1;
	int32 Score[2] = { 0, 0 };
	float TimeLeft = 60.f;
	float EventBanner = 0.f;     // сколько ещё показывать «ГОЛ!» / «ФОЛ!» / «ПЕНАЛЬТИ!»
	FText EventText;
	// Стандарт после фола
	TWeakObjectPtr<ASoccerPlayer> SetPieceTaker;
	FVector SetPieceSpot = FVector::ZeroVector;
	bool bPenalty = false;
	// Возобновление игры (разводка / штрафной / пенальти), пока мяч не введён
	ESoccerRestart Restart = ESoccerRestart::None;
	TWeakObjectPtr<ASoccerPlayer> RestartTaker;
	float RestartStartTime = 0.f;
	int32 KickoffTeam = 0;        // кто разводит с центра (пропустившая гол команда)
	float TeamAITimer = 0.f;

	// Статистика матча
	FSoccerMatchStats Stats[2];
	int32 PossessionTeam = -1;
	TWeakObjectPtr<ASoccerPlayer> PendingPasser; // пас в пути: засчитать точным, если примет партнёр
	bool bShotPending = false;                   // удар в пути: в створ, если гол или сейв
	int32 PendingShotTeam = 0;
	float PendingShotTime = 0.f;

	// Звук: один бесконечный процедурный звук, который наполняет микшер FSoccerAudio
	UPROPERTY() TObjectPtr<USoundWaveProcedural> SoundWave;
	UPROPERTY() TObjectPtr<UAudioComponent> SoundComp;
	FSoccerAudio Audio;
	TArray<int16> AudioBuffer;
	float OohCooldown = 0.f;
	float MenuTime = 0.f;
	bool bInMatch = false;
	bool bPlayActive = false;
	bool bMatchOver = false;
	FText ResultText;
	FTimerHandle ResetTimer;
	FTimerHandle MenuTimer;
	FVector CamFocus = FVector::ZeroVector;
	FVector TrainingCamFocus = FVector::ZeroVector; // сглаженная точка взгляда камеры тренировки
	float TrainingCamYawNow = 0.f;                   // Goals camera: smoothed heading (also the stick's frame)
	float TrainingCamDist = 0.f;                     // Goals camera: smoothed distance to the player
	bool bTrainingCamValid = false;
	bool bSideCamProps = false;
	float MatchCamYaw = -90.f;                       // match camera heading (the stick's frame)                     // camera-side props hidden for the R3 view
	float NextTraceShot = 0.f;   // mf.ShotEvery
	int32 TraceShotIndex = 0;
	static constexpr float TrainingCameraYaw = 0.f; // тренировка: камера смотрит вдоль поля на ворота (+X)

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
