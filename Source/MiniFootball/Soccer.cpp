// Мини-футбол 5×5 — геймплей: мяч, ворота, игроки и ИИ, управление, режим игры, сохранения.
// Интерфейс (меню, HUD матча, пауза) — в SoccerUI.cpp. Объявления — в Soccer.h.

#include "Soccer.h"

#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
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
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"

// Debug switches for automated checks through the editor (MCP): start straight in the
// training room and let the dog run a figure with the ball without a gamepad.
static TAutoConsoleVariable<int32> CVarAutoTraining(TEXT("mf.AutoTraining"), 0,
	TEXT("1 = skip the menu and start free training on BeginPlay"));
static TAutoConsoleVariable<int32> CVarTrace(TEXT("mf.Trace"), 0,
	TEXT("1 = log the human dog's capsule, hips, feet and ball every frame (MFTRACE lines)"));
static TAutoConsoleVariable<float> CVarShotEvery(TEXT("mf.ShotEvery"), 0.f,
	TEXT("> 0: save a screenshot every N seconds (Saved/Screenshots)"));
static TAutoConsoleVariable<int32> CVarAutoShot(TEXT("mf.AutoShot"), 0,
	TEXT("1: training harness shoots from 6 spots at the left corner, centre and right corner in turn and logs MFSHOT goal/save/miss"));
static TAutoConsoleVariable<int32> CVarBotsOnly(TEXT("mf.BotsOnly"), 0, TEXT("1: in a match nobody is controlled, all ten players are AI (harness)"));
static TAutoConsoleVariable<int32> CVarIntro(TEXT("mf.Intro"), 1, TEXT("0: skip the 15 s match intro (harness)"));
static TAutoConsoleVariable<int32> CVarTestCorner(TEXT("mf.TestCorner"), 0, TEXT("1: award your team a corner now, 2: a free kick (harness)"));
static TAutoConsoleVariable<int32> CVarTestGoal(TEXT("mf.TestGoal"), 0, TEXT("1: score a goal for your team now (harness: goal celebration)"));
static TAutoConsoleVariable<float> CVarAutoReceive(TEXT("mf.AutoReceive"), 0.f,
	TEXT("> 0: test harness plays a 13 m/s pass to the running player every N s (on straight AutoDrive 3/4 legs), from 7 m, 50 deg off his run"));
static TAutoConsoleVariable<float> CVarAutoKick(TEXT("mf.AutoKick"), 0.f,
	TEXT("> 0: test harness kicks after N s on the ball (pass and shot in turn) and puts the ball back at the feet"));
static TAutoConsoleVariable<int32> CVarAutoDrive(TEXT("mf.AutoDrive"), 0,
	TEXT("1 = drive the left stick in a figure-eight, 2 = same with sprint"));

using namespace Soccer;

// Training camera, measured on the Goals training ground (camera solved from the pitch
// markings of recorded frames with OpenCV, reprojection error 2-5 px at 2560x1440):
// hFOV 86 deg, 20 deg down, the camera 29 deg above the player, 12.1 m from him with the
// goal 11.8 m away and 14.9 m at 27 m, heading from the player at the far post (~3 m off
// the goal centre, away from his side). The player's feet land at 48% x 65% of the screen.
// R3: a side camera 50 m from its target, 23 m up, 27 deg down, hFOV 44, aimed between
// the player and the goal. All tunable live: mf.Cam.*
static TAutoConsoleVariable<float> CVarCamFOV(TEXT("mf.Cam.FOV"), 86.f, TEXT("Training camera horizontal FOV (deg)"));
static TAutoConsoleVariable<float> CVarCamPitch(TEXT("mf.Cam.Pitch"), 20.f, TEXT("Training camera tilt down (deg)"));
static TAutoConsoleVariable<float> CVarCamElev(TEXT("mf.Cam.Elev"), 29.f, TEXT("Training camera elevation above the player, seen from him (deg)"));
static TAutoConsoleVariable<float> CVarCamNearGoal(TEXT("mf.Cam.NearGoal"), 11.8f, TEXT("Goal distance (m) at which the camera is NearDist away"));
static TAutoConsoleVariable<float> CVarCamNearDist(TEXT("mf.Cam.NearDist"), 12.1f, TEXT("Camera-player distance (m) at NearGoal"));
static TAutoConsoleVariable<float> CVarCamFarGoal(TEXT("mf.Cam.FarGoal"), 27.f, TEXT("Goal distance (m) at which the camera is FarDist away"));
static TAutoConsoleVariable<float> CVarCamFarDist(TEXT("mf.Cam.FarDist"), 14.9f, TEXT("Camera-player distance (m) at FarGoal"));
static TAutoConsoleVariable<float> CVarCamAimPost(TEXT("mf.Cam.AimPost"), 0.82f, TEXT("Heading aims this fraction of the goal half-width past the centre, away from the player's side"));
static TAutoConsoleVariable<float> CVarCamYawLag(TEXT("mf.Cam.YawLag"), 2.5f, TEXT("Heading follow rate (1/s)"));
static TAutoConsoleVariable<float> CVarCamPosLag(TEXT("mf.Cam.PosLag"), 6.f, TEXT("Position follow rate (1/s)"));
static TAutoConsoleVariable<float> CVarCamSideDist(TEXT("mf.Cam.SideDist"), 50.f, TEXT("R3 side camera: distance to its target (m)"));
static TAutoConsoleVariable<float> CVarCamSidePitch(TEXT("mf.Cam.SidePitch"), 27.f, TEXT("R3 side camera: tilt down (deg)"));
static TAutoConsoleVariable<float> CVarCamSideFOV(TEXT("mf.Cam.SideFOV"), 44.f, TEXT("R3 side camera: horizontal FOV (deg)"));
static TAutoConsoleVariable<int32> CVarCamSide(TEXT("mf.Cam.Side"), 0, TEXT("1: force the R3 side camera in training (harness)"));
// Match camera, solved from the Goals 5v5 recordings (centre circle ellipse + vanishing
// point of the side lines): hFOV 44, 27 deg down, ~50 m from its target (a player is ~7%
// of the screen height, 9% on a side-by-side check). It follows the ball and turns up to ~30 deg toward the goal end.
static TAutoConsoleVariable<float> CVarMatchCamDist(TEXT("mf.MatchCam.Dist"), 35.f, TEXT("Match camera distance to its target (m)"));
static TAutoConsoleVariable<float> CVarMatchCamPitch(TEXT("mf.MatchCam.Pitch"), 27.f, TEXT("Match camera tilt down (deg)"));
static TAutoConsoleVariable<float> CVarMatchCamFOV(TEXT("mf.MatchCam.FOV"), 44.f, TEXT("Match camera horizontal FOV (deg)"));
static TAutoConsoleVariable<float> CVarMatchCamTurn(TEXT("mf.MatchCam.Turn"), 0.f, TEXT("Match camera turn toward the goal end (deg)"));
static TAutoConsoleVariable<float> CVarCamSideTurn(TEXT("mf.Cam.SideTurn"), 4.f, TEXT("R3 side camera: turned toward the goal (deg)"));

// ----------------------------------------------------------------------------
namespace SoccerVariants
{
	static const FAction GActions[Num] = {
		{TEXT("KickOnRun"), TEXT("УДАР НА БЕГУ"), {TEXT("слитно с бегом"), TEXT("короткий замах"), TEXT("с ходу")}},
		{TEXT("Dribble"), TEXT("ВЕДЕНИЕ"), {TEXT("как сейчас"), TEXT("у ног"), TEXT("толчками")}},
		{TEXT("Receive"), TEXT("ПРИЁМ"), {TEXT("как сейчас"), TEXT("на ход"), TEXT("под себя")}},
		{TEXT("Keeper"), TEXT("УДАР И ВРАТАРЬ"), {TEXT("как сейчас"), TEXT("успевает или нет"), TEXT("вратарь слабее")}},
		{TEXT("Support"), TEXT("ОТКРЫВАНИЯ"), {TEXT("как сейчас"), TEXT("ширина и глубина"), TEXT("треугольники")}},
		{TEXT("Defense"), TEXT("ОБОРОНА"), {TEXT("вплотную"), TEXT("как в Goals"), TEXT("средне")}},
		{TEXT("Tempo"), TEXT("ТЕМП ИИ"), {TEXT("много ведёт"), TEXT("как в Goals"), TEXT("в одно касание")}},
		{TEXT("Pass"), TEXT("ПАС НИЗОМ"), {TEXT("как сейчас"), TEXT("по стику"), TEXT("по стику, без разброса")}},
	};
	static int32 GValues[Num] = {1, 1, 0, 1, 1, 1, 1, 2}; // the player's picks (2026-09-29): kick B, dribble B, receive A, keeper B; pass C (09-30)
	static FAutoConsoleVariableRef CVarKickOnRun(TEXT("mf.Var.KickOnRun"), GValues[KickOnRun],
		TEXT("Kick while running: 0 A = kicking leg and torso over the run, 1 B = full clip with a short wind-up, 2 C = ball leaves on the press"));
	static FAutoConsoleVariableRef CVarReceive(TEXT("mf.Var.Receive"), GValues[Receive],
		TEXT("Receive: 0 A = soft trap to the carry, 1 B = first touch into space along the stick, 2 C = killed dead at the feet"));
	static FAutoConsoleVariableRef CVarKeeper(TEXT("mf.Var.Keeper"), GValues[Keeper],
		TEXT("Keeper vs shot: 0 A = save chance by dice, 1 B = saves what he can reach in time, 2 C = dice, weaker keeper"));
	static FAutoConsoleVariableRef CVarSupport(TEXT("mf.Var.Support"), GValues[Support],
		TEXT("Team-mates off the ball: 0 A = as before, 1 B = Goals width and depth by role, 2 C = short triangles round the ball"));
	static FAutoConsoleVariableRef CVarDefense(TEXT("mf.Var.Defense"), GValues[Defense],
		TEXT("AI defending: 0 A = tight press and man-marking, 1 B = Goals (contain ~4.5 m, zones; measured 6.4 m on its big pitch), 2 C = in between"));
	static FAutoConsoleVariableRef CVarTempo(TEXT("mf.Var.Tempo"), GValues[Tempo],
		TEXT("AI with the ball: 0 A = dribbles into space, 1 B = Goals (moves it on, the longer it holds the more it passes), 2 C = one-two touch"));
	static FAutoConsoleVariableRef CVarDribble(TEXT("mf.Var.Dribble"), GValues[Dribble],
		TEXT("Dribble: 0 A = as tuned for the dog, 1 B = close to the feet, 2 C = long knocks (FIFA sprint)"));
	static FAutoConsoleVariableRef CVarPass(TEXT("mf.Var.Pass"), GValues[Pass],
		TEXT("Your ground pass: 0 A = as before (wide cone, near mate wins), 1 B = the mate the stick points at, half the error, 2 C = as B with no random error"));
	static const TCHAR* Section = TEXT("MiniFootball.Variants");

	const FAction& Describe(int32 Action) { return GActions[FMath::Clamp(Action, 0, Num - 1)]; }
	int32 Get(int32 Action) { return FMath::Clamp(GValues[FMath::Clamp(Action, 0, Num - 1)], 0, 2); }
	void Set(int32 Action, int32 Variant)
	{
		Action = FMath::Clamp(Action, 0, Num - 1);
		GValues[Action] = (Variant % 3 + 3) % 3;
		GConfig->SetInt(Section, GActions[Action].Id, GValues[Action], GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	struct FTune { const TCHAR* CVar; const TCHAR* Title; float Step, Min, Max; };
	static const FTune GTunes[] = {
		{TEXT("mf.Cam.FOV"),       TEXT("КАМЕРА: УГОЛ ОБЗОРА"),          2.f,   40.f, 120.f},
		{TEXT("mf.Cam.Pitch"),     TEXT("КАМЕРА: НАКЛОН ВНИЗ"),          1.f,    0.f,  80.f},
		{TEXT("mf.Cam.Elev"),      TEXT("КАМЕРА: ВЫСОТА НАД ИГРОКОМ"),    1.f,    5.f,  85.f},
		{TEXT("mf.Cam.NearDist"),  TEXT("КАМЕРА: ДИСТАНЦИЯ У ВОРОТ, М"),  0.5f,   3.f,  40.f},
		{TEXT("mf.Cam.FarDist"),   TEXT("КАМЕРА: ДИСТАНЦИЯ ВДАЛИ, М"),    0.5f,   3.f,  40.f},
		{TEXT("mf.Cam.AimPost"),   TEXT("КАМЕРА: СМОТРИТ НА ДАЛЬНЮЮ ШТАНГУ"), 0.1f, -1.f, 2.f},
		{TEXT("mf.Cam.YawLag"),    TEXT("КАМЕРА: СКОРОСТЬ ПОВОРОТА"),     0.5f,  0.5f, 20.f},
		{TEXT("mf.Cam.PosLag"),    TEXT("КАМЕРА: СКОРОСТЬ СЛЕДОВАНИЯ"),   0.5f,  0.5f, 20.f},
		{TEXT("mf.Cam.SideDist"),  TEXT("R3 КАМЕРА: ДИСТАНЦИЯ, М"),       2.f,   10.f, 120.f},
		{TEXT("mf.Cam.SidePitch"), TEXT("R3 КАМЕРА: НАКЛОН ВНИЗ"),        1.f,    5.f,  89.f},
		{TEXT("mf.Cam.SideFOV"),   TEXT("R3 КАМЕРА: УГОЛ ОБЗОРА"),        2.f,   15.f, 110.f},
	};
	static constexpr int32 NumCamTunes = UE_ARRAY_COUNT(GTunes);
	static TMap<FString, float> GTuneDefaults; // the measured Goals values, taken at first use
	static const TCHAR* TuneSection = TEXT("MiniFootball.Camera");

	static IConsoleVariable* TuneVar(int32 Tune)
	{
		return IConsoleManager::Get().FindConsoleVariable(GTunes[Tune].CVar);
	}
	static void RememberDefaults()
	{
		if (GTuneDefaults.Num() > 0) return;
		for (int32 T = 0; T < NumCamTunes; ++T)
			if (IConsoleVariable* V = TuneVar(T)) GTuneDefaults.Add(GTunes[T].CVar, V->GetFloat());
	}
	int32 NumTunes() { return NumCamTunes + 1; }
	const TCHAR* TuneTitle(int32 Tune)
	{
		return Tune < NumCamTunes ? GTunes[FMath::Max(0, Tune)].Title : TEXT("КАМЕРА: ВЕРНУТЬ КАК В GOALS (ВЛЕВО/ВПРАВО)");
	}
	FString TuneValue(int32 Tune)
	{
		if (Tune < 0 || Tune >= NumCamTunes) return FString();
		const IConsoleVariable* V = TuneVar(Tune);
		if (!V) return FString();
		return GTunes[Tune].Step < 1.f ? FString::Printf(TEXT("%.1f"), V->GetFloat()) : FString::Printf(TEXT("%.0f"), V->GetFloat());
	}
	void StepTune(int32 Tune, int32 Dir)
	{
		RememberDefaults();
		if (Tune >= NumCamTunes)
		{
			for (int32 T = 0; T < NumCamTunes; ++T)
			{
				if (IConsoleVariable* V = TuneVar(T)) V->Set(GTuneDefaults.FindRef(GTunes[T].CVar), ECVF_SetByConsole);
				GConfig->RemoveKey(TuneSection, GTunes[T].CVar, GGameUserSettingsIni);
			}
			GConfig->Flush(false, GGameUserSettingsIni);
			return;
		}
		IConsoleVariable* V = TuneVar(Tune);
		if (!V) return;
		const FTune& D = GTunes[Tune];
		const float Value = FMath::Clamp(FMath::RoundToFloat((V->GetFloat() + Dir * D.Step) / D.Step) * D.Step, D.Min, D.Max);
		V->Set(Value, ECVF_SetByConsole);
		GConfig->SetFloat(TuneSection, D.CVar, Value, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void Load()
	{
		for (int32 A = 0; A < Num; ++A)
		{
			int32 V = 0;
			if (GConfig->GetInt(Section, GActions[A].Id, V, GGameUserSettingsIni)) GValues[A] = FMath::Clamp(V, 0, 2);
		}
		RememberDefaults();
		for (int32 T = 0; T < NumCamTunes; ++T)
		{
			float Value = 0.f;
			if (GConfig->GetFloat(TuneSection, GTunes[T].CVar, Value, GGameUserSettingsIni))
				if (IConsoleVariable* V = TuneVar(T)) V->Set(Value, ECVF_SetByConsole);
		}
	}
}

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

// Goal line X (training and matches share the pitch)
static float GoalLineX(const ASoccerGameMode*)
{
	return HalfLength;
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
	const bool bHasImportedBall = [&]()
	{
		UStaticMesh* TrainingMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Environment/Training/SM_TrainingBall.SM_TrainingBall"));
		if (!TrainingMesh) return false;
		Mesh->SetStaticMesh(TrainingMesh);
		const float MeshDiameter = TrainingMesh->GetBounds().BoxExtent.GetAbs().GetMax() * 2.f;
		const float VisualScale = MeshDiameter > 1.f ? (BallRadius * 2.f / MeshDiameter) : 1.f;
		Mesh->SetRelativeScale3D(FVector(VisualScale));
		Mesh->SetRelativeLocation(FVector::ZeroVector);
		return true;
	}();
	if (!bHasImportedBall) Paint(Mesh, FLinearColor::White);
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
	bCarryCenterValid = false;
	bCarryPolarValid = false;
	if (NewOwner)
	{
		IntendedReceiver = nullptr;
		// In free training possession means that the player can influence the ball,
		// not that it is welded to the foot. Preserve momentum for a natural first touch.
		if (!bTrainingPhysics)
		{
			Velocity = FVector::ZeroVector;
			Spin = FVector::ZeroVector;
		}
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

	if (OwnerPlayer && OwnerPlayer->bGoalkeeper && !OwnerPlayer->bBallAtFeet)
	{
		// Вратарь держит мяч в руках: between the paws of the model (the capsule-based
		// point floated in the air in front of the smaller dog)
		FVector Target = OwnerPlayer->GetActorLocation() + OwnerPlayer->GetActorForwardVector() * 40.f;
		Target.Z = 110.f;
		const USkeletalMeshComponent* Hands = OwnerPlayer->GetMesh();
		if (Hands && Hands->DoesSocketExist(TEXT("LeftHand")) && Hands->DoesSocketExist(TEXT("RightHand")))
		{
			Target = (Hands->GetSocketLocation(TEXT("LeftHand")) + Hands->GetSocketLocation(TEXT("RightHand"))) * 0.5f
				+ OwnerPlayer->GetActorForwardVector() * (BallRadius * 0.6f);
		}
		P = FMath::VInterpTo(P, Target, Dt, 40.f);
		Velocity = FVector::ZeroVector;
		Spin = FVector::ZeroVector;
	}
	else if (OwnerPlayer)
	{
		const ASoccerGameMode* RG = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
		if (RG && RG->IsRestartTaker(OwnerPlayer) && RG->GetRestart() != ESoccerRestart::Kickoff)
		{
			// a set piece: the ball stays on its spot until it is played (the taker may stand beside it)
			P = RG->GetSetPieceSpot();
			P.Z = BallRadius;
			Velocity = Spin = FVector::ZeroVector;
		}
		else if (bTrainingPhysics)
		{
			// Carried in touches around the dog; the boards still stop it.
			const FVector OldP = P;
			ApplyControlledTouch(P, Dt);
			CollideWithWalls(P, OldP, Velocity);
		}
		else
		{
			// Match mode keeps its existing assisted dribble behaviour.
			const FVector OwnerVel(OwnerPlayer->GetVelocity().X, OwnerPlayer->GetVelocity().Y, 0.f);
			const float CarrySpeed = 18.f;
			P = FMath::VInterpTo(P, OwnerPlayer->GetDribbleSpot() + OwnerVel / CarrySpeed, Dt, CarrySpeed);
			Velocity = OwnerVel;
			Spin = FVector::CrossProduct(FVector::UpVector, Velocity) / BallRadius;
		}
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

	// Roll the visual ball by its angular velocity; without this it slides like a puck.
	const float SpinRate = Spin.Size();
	if (SpinRate > KINDA_SMALL_NUMBER)
	{
		Mesh->AddWorldRotation(FQuat(Spin / SpinRate, SpinRate * Dt));
	}
}

void ASoccerBall::ApplyControlledTouch(FVector& P, float Dt)
{
	// FIFA-style carry: the ball stays in front of the dog and is pushed ahead in a
	// rhythm of touches. Angle and distance around the dog are eased separately, so on
	// a sharp turn the ball swings round the body instead of cutting through it, and
	// possession is never lost to a velocity overshoot.
	if (!OwnerPlayer || Dt <= 0.f) return;
	// Centre: the visible body (motion matching lets the pelvis trail the capsule), low-pass
	// filtered against the stride sway, plus velocity / rate to cancel the filter lag.
	const FVector Body = OwnerPlayer->GetBodyCenter();
	if (!bCarryCenterValid || FVector::DistSquared2D(CarryCenter, Body) > 200.f * 200.f)
	{
		CarryCenter = Body;
		bCarryCenterValid = true;
	}
	constexpr float CenterRate = 7.f;
	CarryCenter += (Body - CarryCenter) * (1.f - FMath::Exp(-CenterRate * Dt));
	const FVector OwnerVelocity(OwnerPlayer->GetVelocity().X, OwnerPlayer->GetVelocity().Y, 0.f);
	const float Speed = OwnerVelocity.Size();
	const FVector Center(CarryCenter.X + OwnerVelocity.X / CenterRate, CarryCenter.Y + OwnerVelocity.Y / CenterRate, Body.Z);
	// In front of the visible torso, running or standing. Following the capsule's velocity swung
	// the ball round to the new heading on a turn before the body had turned: it ran away from him.
	const float LeadLeft = LeadUntil - GetWorld()->GetTimeSeconds();
	const FVector WantDir = LeadLeft > 0.f ? LeadDir : OwnerPlayer->GetBodyForward();

	const bool bClose = OwnerPlayer->IsCloseControl();
	const bool bSprint = OwnerPlayer->IsSprintingNow() || Speed > 470.f; // above run pace
	// Touch shape over one stride: a quick push away from the paw, then the ball
	// slows on the grass while the dog catches up with it.
	const float U = FMath::Frac(OwnerPlayer->GetDribblePhase() / (2.f * PI));
	const float Pulse = U < 0.18f ? FMath::SmoothStep(0.f, 1.f, U / 0.18f) : FMath::Pow(1.f - (U - 0.18f) / 0.82f, 1.6f);
	// Dribble variant (SoccerVariants::Dribble): A as tuned, B close to the feet, C long knocks.
	struct FCarry { float MinR, Base, PushClose, PushRun, PushSprint, PerSpeed; };
	static const FCarry Carries[3] = {
		{36.f, 6.f, 6.f, 22.f, 40.f, 0.01f},
		{38.f, 3.f, 4.f, 9.f, 16.f, 0.005f},
		{40.f, 6.f, 8.f, 40.f, 80.f, 0.012f}};
	const FCarry& C = Carries[SoccerVariants::Get(SoccerVariants::Dribble)];
	const float Push = bClose ? C.PushClose : (bSprint ? C.PushSprint : C.PushRun);
	// Clear of the swinging feet (they reach ~35-40 cm ahead of the pelvis in a running stride);
	// walking and turning on the ball the steps are short and it sits at the foot (Goals).
	const float MinR = FMath::Lerp(30.f, C.MinR, FMath::Clamp(Speed / 450.f, 0.f, 1.f));
	const bool bTrap = GetWorld()->GetTimeSeconds() < TrapUntil;
	float WantR = bTrap ? MinR : MinR + (Speed > 80.f ? (bClose ? 4.f : C.Base) + Push * Pulse + Speed * C.PerSpeed : 0.f);
	if (LeadLeft > 0.f) WantR += 70.f * FMath::Sin(PI * FMath::Clamp(1.f - LeadLeft / 0.5f, 0.f, 1.f)); // out and back

	// The ball's place around the dog is kept as state (angle, distance) that moves with
	// the dog. Re-deriving it from positions every frame let the dog's own motion push
	// the ball round to one side, where it settled next to the paws.
	if (!bCarryPolarValid)
	{
		FVector Rel = P - Center;
		Rel.Z = 0.f;
		CarryAngle = Rel.SizeSquared() > 1.f ? FMath::Atan2(Rel.Y, Rel.X) : FMath::Atan2(WantDir.Y, WantDir.X);
		CarryR = FMath::Max(MinR, float(Rel.Size()));
		bCarryPolarValid = true;
	}
	const float Delta = FMath::FindDeltaAngleRadians(CarryAngle, FMath::Atan2(WantDir.Y, WantDir.X));
	const float TurnRate = (bTrap || LeadLeft > 0.f) ? 20.f : (bClose ? 16.f : 11.f); // how fast the ball swings round (1/s)
	const float MaxStep = (bClose ? 14.f : 10.f) * Dt; // rad per frame
	CarryAngle += FMath::Clamp(Delta * (1.f - FMath::Exp(-TurnRate * Dt)), -MaxStep, MaxStep);
	// Just received: the ball settles in over ~0.3 s instead of snapping onto the paw
	const bool bJustGained = GetWorld()->GetTimeSeconds() - OwnerPlayer->GetBallGainTime() < 0.3f;
	CarryR = FMath::Max(MinR, FMath::FInterpTo(CarryR, WantR, Dt, bTrap ? 30.f : (bJustGained ? 6.f : 20.f)));

	const FVector NewP(Center.X + FMath::Cos(CarryAngle) * CarryR, Center.Y + FMath::Sin(CarryAngle) * CarryR, BallRadius);
	Velocity = (NewP - FVector(P.X, P.Y, BallRadius)) / Dt;
	Velocity.Z = 0.f;
	Spin = FVector::CrossProduct(FVector::UpVector, Velocity) / BallRadius;
	P = NewP;
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

int32 ASoccerBall::CollideWithTrainingCourt(FVector& P, const FVector& OldP, FVector& V) const
{
	// Goal frame measured in football_court.glb: posts at |x| 10.5 m, inner mouth +-1.64 m,
	// crossbar at 1.58 m, back frame at 11.46 m. The fence stands at 12.9 m, boards at 6.1 m.
	// The pitch lines are HalfLength x HalfWidth; the ball may leave the pitch (throw/kick-ins,
	// corners, goal kicks are called by the game mode) and stops at the fence.
	const float LineX = HalfLength, BackX = HalfLength + GoalDepth, HalfW = GoalHalfWidth, BarZ = GoalHeight;
	const float EndX = HalfLength + 300.f, SideY = HalfWidth + 300.f;
	const float R = BallRadius;
	int32 Hits = 0;

	if (FMath::Abs(P.Y) > SideY - R)
	{
		P.Y = FMath::Sign(P.Y) * (SideY - R);
		if (FMath::Abs(V.Y) > 150.f) Hits |= HitBoard;
		V.Y = -FMath::Sign(P.Y) * FMath::Abs(V.Y) * WallBounciness;
	}
	if (FMath::Abs(P.X) > EndX - R)
	{
		P.X = FMath::Sign(P.X) * (EndX - R);
		if (FMath::Abs(V.X) > 150.f) Hits |= HitEndBoard;
		V.X = -FMath::Sign(P.X) * FMath::Abs(V.X) * WallBounciness;
	}

	auto HitBar = [&P, &V, &Hits, R, this](const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float T = FMath::Clamp<float>(FVector::DotProduct(P - A, AB) / AB.SizeSquared(), 0.f, 1.f);
		const FVector D = P - (A + AB * T);
		const float Dist = D.Size();
		const float MinDist = R + PostRadius;
		if (Dist >= MinDist || Dist < 0.01f) return;
		const FVector N = D / Dist;
		P = A + AB * T + N * MinDist;
		const float Vn = FVector::DotProduct(V, N);
		if (Vn < 0.f)
		{
			V -= N * (Vn * (1.f + PostBounciness));
			if (Vn < -200.f) Hits |= HitPost;
		}
	};

	for (int32 S = -1; S <= 1; S += 2)
	{
		const float X = S * LineX;
		HitBar(FVector(X, -HalfW, 0.f), FVector(X, -HalfW, BarZ));
		HitBar(FVector(X,  HalfW, 0.f), FVector(X,  HalfW, BarZ));
		HitBar(FVector(X, -HalfW, BarZ), FVector(X, HalfW, BarZ));

		if (P.X * S <= 0.f) continue;
		const float AX = FMath::Abs(P.X);
		const bool bWasInside = OldP.X * S > LineX && FMath::Abs(OldP.Y) < HalfW && OldP.Z < BarZ;
		if (bWasInside)
		{
			// Inside the goal the net swallows the ball.
			if (AX > BackX - R) { P.X = S * (BackX - R); if (FMath::Abs(V.X) > 200.f) Hits |= HitNet; V.X *= -0.2f; }
			if (FMath::Abs(P.Y) > HalfW - R) { P.Y = FMath::Sign(P.Y) * (HalfW - R); if (FMath::Abs(V.Y) > 200.f) Hits |= HitNet; V.Y *= -0.2f; }
			if (P.Z > BarZ - R) { P.Z = BarZ - R; V.Z = FMath::Min<double>(V.Z, 0.0); }
		}
		else if (AX > LineX && AX < BackX + R && FMath::Abs(P.Y) < HalfW + R && P.Z < BarZ + R)
		{
			// Outside the goal: only the mouth lets the ball in; the net sides, back and roof
			// push it out along the shallowest penetration.
			const bool bThroughMouth = FMath::Abs(OldP.X) <= LineX && FMath::Abs(P.Y) < HalfW && P.Z < BarZ;
			if (bThroughMouth) continue;
			const float Back = BackX + R - AX;
			const float Side = HalfW + R - FMath::Abs(P.Y);
			const float Roof = BarZ + R - P.Z;
			if (Back <= Side && Back <= Roof)
			{
				P.X = S * (BackX + R);
				V.X = S * FMath::Abs(V.X) * 0.3f;
			}
			else if (Side <= Roof)
			{
				P.Y = FMath::Sign(P.Y) * (HalfW + R);
				V.Y = FMath::Sign(P.Y) * FMath::Abs(V.Y) * 0.3f;
			}
			else
			{
				P.Z = BarZ + R;
				V.Z = FMath::Abs(V.Z) * 0.3f;
			}
			if (V.Size() > 200.f) Hits |= HitNet;
		}
	}
	return Hits;
}

int32 ASoccerBall::CollideWithWalls(FVector& P, const FVector& OldP, FVector& V) const
{
	if (bTrainingPhysics) return CollideWithTrainingCourt(P, OldP, V);

	const float R = BallRadius;
	const float FieldHalfLength = bTrainingPhysics ? 1200.f : HalfLength;
	const float FieldHalfWidth = bTrainingPhysics ? 610.f : HalfWidth;
	const float FieldGoalDepth = bTrainingPhysics ? 100.f : GoalDepth;
	const float WallY = FieldHalfWidth + BoardGap; // борт стоит сразу за боковой линией
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
	const bool bWasInGoal = FMath::Abs(OldP.X) > FieldHalfLength;
	if (!bWasInGoal && FMath::Abs(P.X) > FieldHalfLength - R)
	{
		const bool bInMouth = FMath::Abs(P.Y) < GoalHalfWidth && P.Z < GoalHeight;
		if (!bInMouth)
		{
			P.X = FMath::Sign(P.X) * (FieldHalfLength - R);
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
		const float X = S * FieldHalfLength;
		HitBar(FVector(X, -GoalHalfWidth, 0.f), FVector(X, -GoalHalfWidth, GoalHeight));
		HitBar(FVector(X,  GoalHalfWidth, 0.f), FVector(X,  GoalHalfWidth, GoalHeight));
		HitBar(FVector(X, -GoalHalfWidth, GoalHeight), FVector(X, GoalHalfWidth, GoalHeight));
	}

	if (FMath::Abs(P.X) > FieldHalfLength)
	{
		// Внутри ворот сетка гасит мяч
		const float Back = FieldHalfLength + FieldGoalDepth - R;
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
		if (Pl->IsBeatenBy(LastKickTime)) continue;                               // вратарь не успел: мяч мимо него
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
	for (UStaticMeshComponent* Seg : Segments) Paint(Seg, FLinearColor(0.62f, 1.f, 0.05f)); // lime, as the Goals corner arc
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
		Seg->SetWorldScale3D(FVector(D.Size() / 100.f, 0.05f, 0.015f)); // ширина линии 5 см
		Seg->SetVisibility(true);
	};
	A += Dir * 35.f; // starts clear of the ball
	Place(Segments[0], A, B);
	Place(Segments[1], B, B + Dir.RotateAngleAxis(145.f, FVector::UpVector) * 32.f);
	Place(Segments[2], B, B + Dir.RotateAngleAxis(-145.f, FVector::UpVector) * 32.f);
}

void ASoccerAimLine::ShowArc(const FVector& From, const FVector& Velocity, float Gravity)
{
	FVector Prev = From;
	const float Step = 0.06f;
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		const float T = (i + 1) * Step;
		FVector P = From + Velocity * T;
		P.Z = From.Z + Velocity.Z * T - 0.5f * Gravity * T * T;
		UStaticMeshComponent* Seg = Segments[i];
		if (P.Z < 0.f) { Seg->SetVisibility(false); continue; }
		const FVector D = P - Prev;
		Seg->SetWorldLocationAndRotation((P + Prev) * 0.5f, D.Rotation());
		Seg->SetWorldScale3D(FVector(D.Size() / 100.f, 0.06f, 0.06f));
		Seg->SetVisibility(true);
		Prev = P;
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
	if (InIdleMesh->GetPathName().StartsWith(TEXT("/Game/Characters/Dogs/")) || InIdleMesh->GetPathName().StartsWith(TEXT("/Game/Characters/Giraffe/")))
	{
		ApplyDogModel(InIdleMesh);
		return;
	}

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
	Ring->SetVisibility(false); // teams read from the kit colours (Goals style)

	UpdateAnimation();
}

// Анимация по скорости: стоит — Idle, бежит — Running (скорость проигрывания под темп бега).
// Если у анимаций разные скелеты, вместе с анимацией меняется и модель (внешне они одинаковые).
void ASoccerPlayer::UpdateAnimation()
{
	if (bDogModel) { UpdateDogAnimation(); return; }
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
	DogAction = nullptr;
	DogActionEnd = 0.f;
	CurrentAnim = nullptr;
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
	// A carried ball in training is always playable: the kick must fire on the button,
	// not after chasing the far end of a sprint touch.
	if (Ball->bTrainingPhysics && HasBall()) return true;
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
	if (G->IsFreeTraining() && IsPlayerControlled() && TrainingStateLogCount < 5)
	{
		const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetController());
		if (PC && PC->MoveInput.SizeSquared() > 0.04f && GetWorld()->GetTimeSeconds() >= NextTrainingStateLogTime)
		{
			const UCharacterMovementComponent* Movement = GetCharacterMovement();
			UE_LOG(LogTemp, Display, TEXT("Training player state: play=%d stun=%.2f dash=%.2f kick=%d speed=%.1f velocity=%s location=%s"),
				G->IsPlayActive() ? 1 : 0, StunTime, DashTime, bPendingKick ? 1 : 0,
				Movement->MaxWalkSpeed, *Movement->Velocity.ToString(), *GetActorLocation().ToString());
			NextTrainingStateLogTime = GetWorld()->GetTimeSeconds() + 1.f;
			++TrainingStateLogCount;
		}
	}
	// Only a real reception (a pass arriving) gets the trap animation; picking up a
	// loose ball at jogging pace must not interrupt the run cycle.
	const bool bRealReception = (G->Ball->Velocity - GetVelocity()).Size2D() > 450.f;
	G->Ball->SetOwnerPlayer(this);
	BallGainTime = GetWorld()->GetTimeSeconds();
	GKHoldTime = 0.f; // a keeper on a goal kick looks up before playing it
	G->OnBallGained(this);
	AIDecisionTimer = 0.4f; // ИИ «осматривается» перед решением
	// No trap clip for the dogs: the only take is 4.9 s long and squeezed into 0.35 s it
	// shook the body; motion matching already absorbs the reception.
	if (!bDogModel && !DogAction && !bGoalkeeper && bRealReception) PlayDogAction(TEXT("Receive"), 0.35f);
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
	if (Seconds >= 0.6f) PlayDogAction(TEXT("Fall"), Seconds + 0.5f); // brought down: trip and fall
	DelayedKickTime = -1.f; // knocked off the ball mid wind-up
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

	Marker->SetVisibility(false); // the HUD draws the control tag (ASoccerHUD::DrawPlayerTag)
	SlidePoseTime = FMath::Max(0.f, SlidePoseTime - Dt);
	DivePoseTime  = FMath::Max(0.f, DivePoseTime - Dt);
	UpdateAnimation();
	UpdateBodyPose(Dt);

	if (DelayedKickTime >= 0.f)
	{
		DelayedKickTime -= Dt;
		if (DelayedKickTime < 0.f && (HasBall() || CanKickBall())) ExecuteKick(DelayedKick);
	}
	TickFirstTime();
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
	// Kick-off: everyone waits on their spot until the ball is played
	if (G->IsKickoffWaiting(this))
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
		FaceTowards(G->Ball->GetActorLocation(), Dt);
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
	const bool bFreeTraining = GM() && GM()->IsFreeTraining();
	const float SpeedRatio = FMath::Clamp(Speed / 700.f, 0.f, 1.f);
	if (KickSlowTime > 0.f)
	{
		// Planting for a kick: the body stops under the animation instead of the capsule
		// gliding on at running pace (read as sliding on ice).
		KickSlowTime -= GetWorld()->GetDeltaSeconds();
		Move->MaxWalkSpeed = FMath::Min(Move->MaxWalkSpeed, KickSlowSpeed);
	}
	// A whole-body clip (kick, fall, tackle, header) owns the legs: the capsule must not glide on
	// under it (read as skating: the passer after a kick-off pass, a tackled player getting up).
	// A kick keeps most of the run it was struck on; anything else nearly stands.
	if (DogAction && ActionFoot == 0 && !bGoalkeeper && GetWorld()->GetTimeSeconds() < DogActionEnd)
	{
		const bool bKick = LastActionName == TEXT("Pass") || LastActionName == TEXT("Shot") || LastActionName == TEXT("FakeShot");
		Move->MaxWalkSpeed = FMath::Min(Move->MaxWalkSpeed, bKick ? KickSlowSpeed : 40.f);
	}
	if (bFreeTraining || (bDogModel && !bGoalkeeper))
	{
		// Close to Epic's motion matching character (accel 800 -> 300 at sprint, braking 2000,
		// friction 5 -> 3) but quicker off the mark. A capsule much snappier than the animation
		// database makes pose selection flicker.
		const float SprintBlend = FMath::Clamp((Speed - RunSpeed) / (SprintSpeed - RunSpeed), 0.f, 1.f);
		Move->MaxAcceleration = FMath::Lerp(HasBall() ? 1200.f : 1400.f, 900.f, SprintBlend);
		Move->BrakingDecelerationWalking = 2000.f;
		Move->GroundFriction = FMath::Lerp(5.f, 3.f, FMath::Clamp(Speed / 500.f, 0.f, 1.f));
		Move->RotationRate = FRotator(0.f, 720.f, 0.f);
		return;
	}
	const float Accel = bGoalkeeper ? 2600.f : FMath::Lerp(1100.f, 1900.f, PaceAlpha);
	Move->MaxAcceleration = Accel * (HasBall() ? 0.85f : 1.f); // с мячом разгоняемся медленнее
	Move->BrakingDecelerationWalking = bGoalkeeper ? 2500.f : 1500.f;
	Move->GroundFriction = bGoalkeeper ? 8.f : 3.5f;
	Move->RotationRate = FRotator(0.f, FMath::Lerp(720.f, 420.f, SpeedRatio), 0.f);
}

bool ASoccerPlayer::TickDash(float Dt)
{
	if (DashTime <= 0.f) return false;

	DashTime -= Dt;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = !bGoalkeeper; // вратарь прыгает боком
	Move->MaxWalkSpeed = DashSpeed;
	Move->MaxAcceleration = (bSliding || bGoalkeeper) ? 5000.f : 3500.f;
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
	// Just kicked it ourselves: in training give the ball time to leave, or a soft pass
	// is trapped again straight away.
	if (Ball->LastKicker == this && Ball->TimeSinceKick() < (GM()->IsFreeTraining() ? 0.7f : 0.3f)) return;

	const FVector B = Ball->GetActorLocation();
	FVector ToBall = B - GetActorLocation();
	ToBall.Z = 0.f;
	const float Dist = ToBall.Size();

	// Зона приёма: игрок человека дотягивается дальше (помощь при приёме, как в FIFA)
	const float Reach = 85.f; // was 105 for yours: the ball jumped to the feet from a metre away
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
		// Receive variant (SoccerVariants::Receive) for the player you control, on a real pass
		const int32 Variant = SoccerVariants::Get(SoccerVariants::Receive);
		const bool bPassIn = (Ball->Velocity - GetVelocity()).Size2D() > 450.f;
		const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetController());
		// Приём: мяч под контролем и сразу подтягивается к ногам
		GainBall();
		GM()->PlaySfx(ESoccerSound::Touch, 0.6f);
		if (Variant == 1 && bPassIn && PC && !bGoalkeeper)
		{
			// B: the first touch takes the ball out in front along the stick and the player
			// runs onto it; he keeps possession, so it is no loose ball to intercept.
			FVector Dir = PC->StickToWorld(PC->MoveInput);
			Dir.Z = 0.f;
			if (Dir.Size() > 0.3f)
			{
				Ball->LeadDir = Dir.GetSafeNormal();
				Ball->LeadUntil = GetWorld()->GetTimeSeconds() + 0.5f;
			}
		}
		if (Variant == 2 && bPassIn && PC && !bGoalkeeper)
		{
			// C: killed dead at the feet, a short stop in the stride
			Ball->TrapUntil = GetWorld()->GetTimeSeconds() + 0.35f;
			KickSlowTime = 0.3f;
			KickSlowSpeed = FMath::Max(120.f, float(GetVelocity().Size2D()) * 0.45f);
		}
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

// The ball is loose and on its way here: a pass to this player, or rolling at him.
bool ASoccerPlayer::IsBallComingToMe() const
{
	const ASoccerGameMode* G = GM();
	if (!G || !G->Ball || G->Ball->OwnerPlayer) return false;
	const ASoccerBall* Ball = G->Ball;
	if (Ball->IntendedReceiver == this) return true;
	const FVector To = GetActorLocation() - Ball->GetActorLocation();
	const FVector V = Ball->Velocity;
	return V.Size2D() > 250.f && To.Size2D() < 2500.f && FVector::DotProduct(V.GetSafeNormal2D(), To.GetSafeNormal2D()) > 0.85f;
}

bool ASoccerPlayer::IsLooseBallNear() const
{
	const ASoccerGameMode* G = GM();
	if (!G || !G->Ball || G->Ball->OwnerPlayer) return false;
	const FVector B = G->Ball->GetActorLocation();
	return IsBallComingToMe() || (FVector::Dist2D(B, GetActorLocation()) < 400.f && B.Z < 110.f);
}

void ASoccerPlayer::QueueFirstTime(ECharge Kind, const FVector& AimDir, float Power01, bool bFinesse, bool bChip)
{
	FirstTimeKind = Kind;
	FirstTimeAim = AimDir;
	FirstTimePower = Power01;
	bFirstTimeFinesse = bFinesse;
	bFirstTimeChip = bChip;
	FirstTimeUntil = GetWorld()->GetTimeSeconds() + 2.f;
}

void ASoccerPlayer::TickFirstTime()
{
	if (FirstTimeKind == ECharge::None) return;
	const ASoccerGameMode* G = GM();
	const bool bLost = !G || !G->Ball || (G->Ball->OwnerPlayer && G->Ball->OwnerPlayer.Get() != this);
	if (bLost || GetWorld()->GetTimeSeconds() > FirstTimeUntil || StunTime > 0.f)
	{
		FirstTimeKind = ECharge::None;
		return;
	}
	if (!CanKickBall() && !HasBall()) return;
	const ECharge Kind = FirstTimeKind;
	FirstTimeKind = ECharge::None;
	switch (Kind)
	{
	case ECharge::Pass:    Pass(EPassKind::Ground, FirstTimeAim, FirstTimePower); break;
	case ECharge::Through: Pass(EPassKind::Through, FirstTimeAim, FirstTimePower); break;
	case ECharge::Lob:     Pass(EPassKind::Lob, FirstTimeAim, FirstTimePower); break;
	case ECharge::Shot:    Shoot(FirstTimeAim, FirstTimePower, bFirstTimeFinesse, bFirstTimeChip); break;
	default: break;
	}
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
	if (bDogModel) return; // The dog's clips already contain the slide and dive poses.
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
	FVector AssistMove = FVector::ZeroVector;
	// Kick-off, kick-in, corner, free kick: the ball must be played, not dribbled away
	if (GM()->IsRestartTaker(this) && HasBall())
	{
		Move = FVector::ZeroVector;
		// on the ball for a free kick or corner: square to the opponents' goal
		if (PC->MoveInput.SizeSquared() < 0.04f) FaceTowards(FVector(AttackSign() * HalfLength, 0.f, GetActorLocation().Z), Dt);
	}
	// Just switched to on a pass: the stick still holds the passer's run. Wait until it is
	// let go, so the receiver waits for the ball instead of running off.
	if (PC->bHoldMoveUntilNeutral)
	{
		if (PC->MoveInput.Size() < 0.25f || HasBall()) PC->bHoldMoveUntilNeutral = false;
		else Move = FVector::ZeroVector;
	}
	bool bWantSprint = PC->SprintAxis > 0.3f;
	if (SkillTime > 0.f && !SkillDir.IsNearlyZero())
	{
		SkillTime -= Dt;
		Move = SkillDir;
		bWantSprint = true;
	}
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
		// Slower with the ball in matches; in training the same pace keeps motion matching on
		// its run clips instead of flickering between jog and run.
		// same pace with the ball: keeps motion matching on its run clips
	}

	// Помощь при приёме: пас летит мне, а стик отпущен — сам иду навстречу мячу
	if (!bHas && Move.IsNearlyZero() && !Ball->OwnerPlayer && Ball->IntendedReceiver == this)
	{
		FVector Meet;
		InterceptTime(Meet);
		FVector ToMeet = Meet - GetActorLocation();
		ToMeet.Z = 0.f;
		// Ease in: at full pace the player overshot the point and shook back and forth.
		const float MeetDist = ToMeet.Size();
		if (MeetDist > 25.f) AssistMove = ToMeet / MeetDist * FMath::Clamp(MeetDist / 180.f, 0.25f, 1.f);
	}
	// A kick charged or queued onto a loose ball (first time, or after a right-stick knock):
	// run onto it at once; the stick only aims the kick.
	if (!bHas && !Ball->OwnerPlayer && (PC->GetCharge() >= 0.f || FirstTimeKind != ECharge::None))
	{
		FVector Meet;
		InterceptTime(Meet);
		FVector ToMeet = Meet - GetActorLocation();
		ToMeet.Z = 0.f;
		AssistMove = ToMeet.Size() > 25.f ? ToMeet.GetSafeNormal() : FVector::ZeroVector;
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

	// Turning with the ball (measured on Goals: a stick circled round makes the carrier turn on
	// the ball in a ~1.5 m circle, one turn per ~1.6 s). The further the stick is from the run,
	// the slower he goes, down to a walk: a tight turn instead of a wide arc at running pace.
	if (bHas && !Move.IsNearlyZero())
	{
		const FVector Run = GetVelocity().GetSafeNormal2D();
		if (!Run.IsNearlyZero())
		{
			const float Along = FVector::DotProduct(Run, Move.GetSafeNormal2D()); // 1 straight on, 0 at 90 deg
			Speed = FMath::Lerp(160.f, Speed, FMath::SmoothStep(0.f, 0.9f, Along));
		}
	}
	GetCharacterMovement()->MaxWalkSpeed = Speed * SpeedFactor();
	GetCharacterMovement()->bOrientRotationToMovement = !bFaceBall;
	if (bFaceBall)
	{
		FaceTowards(BallLoc, Dt);
	}
	if (G->IsFreeTraining() && !Move.IsNearlyZero())
	{
		// Slide along the boards and around the goals instead of ramming the invisible
		// blockers (the sudden stop jolted the animation).
		const FVector Loc = GetActorLocation();
		const float Margin = 45.f, WallX = HalfLength + 150.f, WallY = HalfWidth + 150.f;
		if (FMath::Abs(Loc.X) > WallX - Margin && Move.X * Loc.X > 0.f) Move.X = 0.f;
		if (FMath::Abs(Loc.Y) > WallY - Margin && Move.Y * Loc.Y > 0.f) Move.Y = 0.f;
		if (Move.Size2D() < 0.2f) Move = FVector::ZeroVector;
	}
	if (!AssistMove.IsNearlyZero())
	{
		AddMovementInput(AssistMove); // receive assist keeps its analog slow-down
	}
	else if (!Move.IsNearlyZero())
	{
		// One running pace, as in FIFA: stick tilt only steers; speed changes with sprint/LT.
		AddMovementInput(Move.GetSafeNormal2D());
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

	// Free training is a passing drill: the team-mate stands on his spot and faces the ball
	if (G->IsFreeTraining())
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
		FaceTowards(BallLoc, Dt);
		return;
	}

	// Стандарт у соперника: стоим в стороне, пока мяч не введён в игру
	if (G->MustKeepDistance(this))
	{
		TickRestartHold(Dt);
		return;
	}
	// Our corner: into the box to attack the cross (near post, far post, penalty spot, the edge)
	if (G->bCorner && G->GetRestartTaker() && G->GetRestartTaker()->Team == Team)
	{
		const FVector Spot = G->GetSetPieceSpot();
		const float S = AttackSign(), Near = FMath::Sign(Spot.Y);
		const FVector Runs[4] = { {S * (HalfLength - 250.f), Near * 180.f, 0.f}, {S * (HalfLength - 300.f), -Near * 220.f, 0.f},
		                          {S * (HalfLength - 550.f), 0.f, 0.f}, {S * (HalfLength - 900.f), -Near * 150.f, 0.f} };
		const FVector Target = Runs[RosterIndex % 4];
		MoveTo(Target, RunSpeed);
		if (FVector::Dist2D(Target, Me) < 150.f)
		{
			GetCharacterMovement()->bOrientRotationToMovement = false;
			FaceTowards(BallLoc, Dt);
		}
		return;
	}

	// Мяч в воздухе рядом — играем головой: у чужих ворот — удар, иначе — скидка вперёд.
	// Only a dropping ball, and not one just headed: otherwise the players headed it back and
	// forth between the teams; the rest is let down and controlled (TryControlBall)
	const bool bHeaderChain = GetWorld()->GetTimeSeconds() - Ball->LastHeaderTime < 1.f;
	if (CanHeadBall() && Ball->TimeSinceKick() > 0.15f && Ball->Velocity.Z < 0.f && !bHeaderChain)
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
		FVector Point = OppLoc + (OwnGoal - OppLoc).GetSafeNormal2D() * 120.f + (BallLoc - OppLoc).GetSafeNormal2D() * 60.f;
		if (const int32 DefVariant = SoccerVariants::Get(SoccerVariants::Defense); DefVariant > 0)
		{
			// Goals-style zones: goal-side of the man but pulled toward the team's shape,
			// so lanes are shut by position instead of a shadow on every opponent
			const float GoalSide = DefVariant == 1 ? 350.f : 220.f, Zone = DefVariant == 1 ? 0.5f : 0.25f;
			const FVector Loose = OppLoc + (OwnGoal - OppLoc).GetSafeNormal2D() * GoalSide + (BallLoc - OppLoc).GetSafeNormal2D() * 80.f;
			Point = FMath::Lerp(Loose, FormationPoint(), Zone);
		}
		Point = ClampToField(Point, 80.f);
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
				SupportPoint = ComputeSupportPoint();
				SupportTimer = bRunningInBehind ? 1.8f : FMath::FRandRange(0.8f, 1.3f); // a run is carried through
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

	// A fresh reception gets a moment: the presser closes down but keeps a gap and
	// does not tackle until the receiver has had time to take a touch (FIFA-like).
	const float SinceGain = GetWorld()->GetTimeSeconds() - Carrier->GetBallGainTime();
	const bool bFreshReception = SinceGain < 1.0f;
	// Точка сдерживания: между мячом и воротами, с упреждением по ходу соперника
	// Defending variant: how far the presser holds off (Goals keeps ~6 m on its big pitch)
	static const float HoldOff[3] = { 130.f, 550.f, 260.f }; // B: 16% of the pitch width, as Goals (11 m of 68)
	const int32 DefVariant = SoccerVariants::Get(SoccerVariants::Defense);
	float Gap = HoldOff[DefVariant];
	if (DefVariant > 0 && FVector::Dist2D(BallLoc, OwnGoal) < 1200.f) Gap = FMath::Min(Gap, 180.f); // near our goal: close down
	// the carrier dwells on the ball: step in to challenge (Goals: an opponent is within 3 m ~20% of the time)
	if (DefVariant > 0) Gap = FMath::Lerp(Gap, 350.f, FMath::Clamp((SinceGain - 2.f) / 1.5f, 0.f, 1.f));
	if (DefVariant > 0 && SinceGain > 4.f) Gap = 90.f; // standing on the ball: go in for it
	const FVector Contain = BallLoc + ToGoal * (bFreshReception ? Gap + 100.f : Gap) + Carrier->GetVelocity() * 0.25f;
	if (Dist > Gap + 250.f && !bFreshReception)
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

	if (TackleCooldown > 0.f || AIDecisionTimer > 0.f || bFreshReception) return;

	if (Dist < 125.f)
	{
		// Отбор: реакция и агрессивность зависят от сложности
		static const float Reaction[3] = { 0.6f, 0.45f, 0.3f };
		static const float Aggression[3] = { 0.15f, 0.22f, 0.3f };
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
	const int32 Variant = SoccerVariants::Get(SoccerVariants::Support);
	// Opponents' last outfield line: a striker waits on it, not past it
	float LastLine = BallLoc.X * S;
	for (const ASoccerPlayer* P : G->Players)
		if (P && P->Team != Team && !P->bGoalkeeper) LastLine = FMath::Max(LastLine, float(P->GetActorLocation().X * S));
	if (Variant == 1)
	{
		// Goals: width and depth by the situation, not by shirt number. Of the outfield
		// team-mates off the ball the most advanced goes ahead of it (and runs in behind),
		// the next holds the far touchline, the last is the safe ball behind.
		const ASoccerPlayer* Carrier = G->Ball->OwnerPlayer;
		const float BallSide = BallLoc.Y >= 0.f ? 1.f : -1.f;
		int32 Rank = 0;
		for (const ASoccerPlayer* P : G->Players)
			if (P && P != this && P != Carrier && P->Team == Team && !P->bGoalkeeper
			    && P->GetActorLocation().X * S > Me.X * S) ++Rank;
		float Pressure = 1e9f;
		for (const ASoccerPlayer* P : G->Players)
			if (P && P->Team != Team) Pressure = FMath::Min(Pressure, float(FVector::Dist2D(P->GetActorLocation(), BallLoc)));
		bRunningInBehind = false;
		if (Rank == 0)
		{
			const float AheadX = FMath::Max(float(BallLoc.X * S) + 700.f, LastLine - 80.f);
			Base = FVector(S * FMath::Min(AheadX, HalfLength - 350.f), BallLoc.Y * -0.3f, 0.f);
			if (Carrier && Carrier != this && Pressure > 180.f && FMath::FRand() < 0.5f)
			{
				// in behind: past the last line, still within passing range
				const float RunX = FMath::Min3(FMath::Max(LastLine + 300.f, float(BallLoc.X * S) + 900.f), float(BallLoc.X * S) + 1700.f, HalfLength - 350.f);
				Base = FVector(S * RunX, BallLoc.Y * 0.3f, 0.f);
				bRunningInBehind = true;
			}
		}
		else if (Rank == 1) Base = FVector(BallLoc.X + S * 250.f, -BallSide * (HalfWidth - 250.f), 0.f);   // far touchline
		else Base = FVector(BallLoc.X - S * 650.f, BallLoc.Y * 0.3f, 0.f);                                   // safe ball behind
	}
	else if (Variant == 2)
	{
		// Short triangles: two angles 8-10 m either side of the ball, one behind
		const float A = (RosterIndex % 2 ? 1.f : -1.f);
		const float Fwd = RosterIndex >= 3 ? 450.f : -350.f;
		Base = FVector(BallLoc.X + S * Fwd, BallLoc.Y + A * 650.f, 0.f);
	}
	else if (bForward)
	{
		Base = FVector(BallLoc.X + S * FMath::FRandRange(400.f, 800.f), Side * FMath::FRandRange(300.f, 750.f), 0.f);
	}
	else
	{
		Base = FVector(BallLoc.X - S * FMath::FRandRange(350.f, 600.f), Side * FMath::FRandRange(250.f, 600.f), 0.f);
	}
	Base.X = FMath::Clamp<double>(Base.X, -HalfLength + 250.0, HalfLength - 250.0);
	Base = ClampToField(Base, 150.f);

	// The most advanced mate may also come short to show for the ball (6-7 m ahead of it, off the
	// line of the last defender): parked on that line he was always marked and no pass reached him.
	const bool bCanComeShort = Variant == 1 && Base.X * S >= LastLine - 150.f;
	const float ShowSide = (Me.Y >= BallLoc.Y ? 1.f : -1.f);
	const FVector ShortBase = ClampToField(FVector(BallLoc.X + S * 650.f, BallLoc.Y + ShowSide * 350.f, 0.f), 150.f);
	const bool bWasRun = bRunningInBehind;
	FVector Best = Base;
	float BestScore = -1000.f;
	for (int32 i = 0; i < (bCanComeShort ? 20 : 10); ++i)
	{
		const float Jitter = Variant == 0 ? 350.f : 200.f; // role spots stay near their role
		const FVector& Around = (bCanComeShort && i % 2) ? ShortBase : Base;
		const FVector C = i <= 1 ? Around
			: ClampToField(Around + FVector(FMath::FRandRange(-Jitter, Jitter), FMath::FRandRange(-Jitter, Jitter), 0.f), 150.f);
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
	// a run in behind only if the high spot won, not the short one
	if (bCanComeShort) bRunningInBehind = bWasRun && FVector::Dist2D(Best, Base) < FVector::Dist2D(Best, ShortBase);
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
	// Tempo (Goals moves the ball by passing; the carrier keeps it briefly)
	const int32 Tempo = SoccerVariants::Get(SoccerVariants::Tempo);
	const float OnBall = GetWorld()->GetTimeSeconds() - BallGainTime;
	if (Tempo == 1) DribbleScore = DribbleScore * 0.55f - 0.25f * FMath::Clamp(OnBall - 1.f, 0.f, 2.f);
	if (Tempo == 2) DribbleScore = DribbleScore * 0.3f - 0.4f * FMath::Clamp(OnBall - 0.4f, 0.f, 2.f);
	// a team-mate breaking in behind is worth finding
	if (Tempo > 0 && Kind != EPassKind::Lob)
		for (const ASoccerPlayer* P : GM()->Players)
			if (P && P->Team == Team && P->bRunningInBehind && FVector::Dist2D(P->GetActorLocation(), Target) < 600.f) { PassScore += 0.2f; break; }

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
	if (G->IsKickIn() && Team == HumanTeam)
	{
		const ASoccerPlayerController* PC = HumanPC();
		const ASoccerPlayer* Mate = PC ? Cast<ASoccerPlayer>(PC->GetPawn()) : nullptr;
		if (Mate && Mate != this)
		{
			// Played to where the human is heading
			const FVector To = Mate->GetActorLocation() + Mate->GetVelocity() * 0.4f - G->Ball->GetActorLocation();
			Pass(EPassKind::Ground, To.GetSafeNormal2D(), FMath::Clamp(To.Size2D() / 2200.f, 0.2f, 0.8f));
			return;
		}
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
	if (!HasBall()) bBallAtFeet = false; // the foot-played ball has gone

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
		if (GKHoldTime > 0.7f && !IsPlayingAction() && !bBallAtFeet) PlayDogAction(TEXT("KeeperDirect"), 0.f); // hands clip, not with the ball at his feet
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
		// (не когда удар его уже переиграл: тогда он не успевает)
		if (Ball->Velocity.X * Side > 300.f && !IsBeatenBy(Ball->GetLastKickTime()))
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
	else if (!Ball->OwnerPlayer && Ball->IntendedReceiver == this)
	{
		FVector Meet; // a back-pass: step out to meet it
		InterceptTime(Meet);
		Target = FVector(Meet.X, Meet.Y, Me.Z);
	}

	MoveTo(Target, bRush ? KeeperSpeed * 1.3f : KeeperSpeed);
	FaceTowards(BallLoc, Dt);

	// Бросок: удар летит в створ, а мяч пройдёт в стороне от вратаря — прыгаем в угол
	// (не на пас своего игрока: его вратарь принимает ногами, см. TryKeeperSave)
	const bool bFromMate = Ball->LastKicker && Ball->LastKicker != this && Ball->LastKicker->Team == Team;
	if (!Ball->OwnerPlayer && !bFromMate && Ball->Velocity.X * Side > 700.f)
	{
		DecideShot();
		const float TMe = (Me.X - BallLoc.X) / Ball->Velocity.X;      // когда мяч будет на уровне вратаря
		const float TLine = (GoalX - BallLoc.X) / Ball->Velocity.X;   // когда — на линии ворот
		const bool bOnTarget = FMath::Abs(BallLoc.Y + Ball->Velocity.Y * TLine) < GoalHalfWidth + 20.f;
		// Dive late enough that the hands arrive with the ball: going down 0.6 s early left
		// the keeper lying on the grass while a long shot was still in the air.
		if (!bGKDived && bOnTarget && TMe > 0.f && TMe < 0.4f)
		{
			const float Lateral = BallLoc.Y + Ball->Velocity.Y * TMe - Me.Y;
			if (FMath::Abs(Lateral) > 45.f && FMath::Abs(Lateral) < 320.f)
			{
				bGKDived = true;
				const FVector DiveDir(0.f, FMath::Sign(Lateral), 0.f);
				const float DashT = FMath::Clamp(TMe, 0.15f, 0.4f);
				const float Need = (FMath::Abs(Lateral) - 30.f) / DashT; // arms reach the last 30 cm
				// Если удар «берётся» — прыжок точный, если нет — вратарь чуть не дотягивается
				StartDash(DiveDir, bGKWillSave ? FMath::Clamp(Need, 350.f, 1300.f) : FMath::Min(Need * 0.7f, 900.f), DashT, false, false);
				DivePoseTime = 0.9f;
				DiveSign = FVector::DotProduct(DiveDir, GetActorRightVector()) >= 0.f ? 1.f : -1.f;
				PlayDogAction(DiveSign < 0.f ? TEXT("DiveLeft") : TEXT("DiveRight"), DivePoseTime);
				return;
			}
			if (FMath::Abs(Lateral) <= 45.f && TMe < 0.3f)
			{
				// at him: blocks with the body, or (beaten) the late reach of a keeper who is too slow
				bGKDived = true;
				PlayDogAction(bGKWillSave ? (FMath::RandBool() ? TEXT("KeeperBlock") : TEXT("KeeperBlock2")) : TEXT("KeeperMiss"), 0.f);
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
	if (HasBall() && GKHoldTime > 0.7f && !IsPlayingAction() && PC->MoveInput.IsNearlyZero()) PlayDogAction(TEXT("KeeperDirect"), 0.f);

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
	if (GM()->IsFreeTraining())
	{
		// Training: the keeper rolls it back to you, ahead of your run
		if (const ASoccerPlayer* You = GM()->GetHumanPlayer())
		{
			const FVector To = You->GetActorLocation() + You->GetVelocity() * 0.5f - BallLoc;
			Pass(EPassKind::Ground, To.GetSafeNormal2D(), FMath::Clamp(float(To.Size2D()) / 2500.f, 0.3f, 0.9f));
			return;
		}
	}
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

	const int32 Variant = SoccerVariants::Get(SoccerVariants::Keeper);
	if (Variant == 1 && FMath::Abs(Ball->Velocity.X) > 1.f)
	{
		// B: no dice. After his reaction time the keeper covers ground at diving speed;
		// the shot is saved if it passes within his reach when it gets to him. Placement
		// and power beat him, a shot at him does not.
		const FVector Me = GetActorLocation();
		const float T = FMath::Max(0.f, float((Me.X - BallLoc.X) / Ball->Velocity.X));
		const float Lateral = FMath::Abs(BallLoc.Y + Ball->Velocity.Y * T - Me.Y);
		static const float Reactions[3] = { 0.36f, 0.3f, 0.25f };
		const float Reaction = Team == HumanTeam ? 0.3f : Reactions[Diff];
		const float Reach = 35.f + FMath::Clamp(T - Reaction, 0.f, 0.5f) * 300.f; // goals are 3 m wide
		bGKWillSave = Lateral < Reach * FMath::FRandRange(0.9f, 1.1f);
		return;
	}
	if (Variant == 2)
	{
		// C: the same dice, a weaker keeper and power counts for more
		Chance -= 0.15f + FMath::Max(0.f, (float)Ball->Velocity.Size2D() - 1200.f) / 4000.f;
	}
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
	// A pass from a team-mate is simply collected in the hands, not treated as a shot.
	const bool bOwnPass = Ball->LastKicker && Ball->LastKicker != this && Ball->LastKicker->Team == Team;
	if (bOwnPass)
	{
		// Back-pass rule: a team-mate's pass is played with the feet: controlled at the feet (not
		// in the hands), then TickGoalkeeper passes it on after a look up
		if (IsPlayerControlled()) return;
		bBallAtFeet = true;
		GainBall();
		return;
	}
	// Hands only inside his own penalty area; outside he clears with the feet
	const float OwnGoalX = -AttackSign() * HalfLength;
	const bool bInOwnBox = FMath::Abs(BallLoc.X - OwnGoalX) < PenaltyDepth && FMath::Abs(BallLoc.Y) < GoalHalfWidth + 400.f;
	if (!bInOwnBox)
	{
		if (!IsPlayerControlled() && CanKickBall()) Pass(EPassKind::Lob, FVector(AttackSign(), 0.f, 0.f), 0.9f);
		return;
	}
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
	const float Height = G->Ball->GetActorLocation().Z;
	PlayDogAction(Height < 60.f ? TEXT("CatchLow") : (Height > 150.f ? TEXT("CatchHigh") : TEXT("CatchMid")), 0.65f);
	bBallAtFeet = false; // caught: in the hands
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
ASoccerPlayer* ASoccerPlayer::FindPassTarget(const FVector& AimDir, float MinDot, float AngleWeight) const
{
	// Лучший партнёр — тот, что ближе всего к направлению прицела и не слишком далеко
	ASoccerPlayer* Best = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	for (ASoccerPlayer* P : GM()->Players)
	{
		if (P == this || P->Team != Team) continue;
		FVector ToP = P->GetActorLocation() - GetActorLocation();
		ToP.Z = 0.f;
		const float Dist = ToP.Size();
		if (Dist < 150.f) continue;
		const float Dot = FVector::DotProduct(ToP / Dist, AimDir);
		// The keeper is a target only for a pass aimed right at him (a back-pass)
		if (Dot < (P->bGoalkeeper ? FMath::Max(0.8f, MinDot) : MinDot)) continue;
		const float Score = Dot * AngleWeight - Dist / 2500.f;
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
	// 40 — 5°, 99 — 1.2°: with 9° a shot from 11 m spread ±2 m at a 3 m goal, aiming was luck
	float Err = FMath::Lerp(5.f, 1.2f, FMath::Clamp((Skill - 40) / 59.f, 0.f, 1.f));
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

	// Your ground pass, variant (SoccerVariants::Pass): B and C pick the mate the stick points
	// at (cone +-40 deg, angle before distance); A is the old wide cone. AI passes stay as A.
	const int32 PassVariant = Kind == EPassKind::Ground && IsPlayerControlled() ? SoccerVariants::Get(SoccerVariants::Pass) : 0;
	ASoccerPlayer* Mate = PassVariant > 0 ? FindPassTarget(Aim, 0.77f, 8.f) : FindPassTarget(Aim);
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
	if (bWithError && PassVariant < 2) // C: no random error
	{
		const float Err = KickErrorDegrees(Info.Passing, Power, Dir, !HasBall()) * (PassVariant == 1 ? 0.5f : 1.f);
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
		const float Arrive = Kind == EPassKind::Through ? 750.f : 900.f; // still lively when it arrives
		// Your ground pass (B/C): the bar sets how hard it arrives, 4.5 m/s on a tap to 20 m/s full
		const float Speed = Mate && PassVariant > 0 ? Dist * Ball->RollingFriction + FMath::Lerp(450.f, 2000.f, Power)
		                  : Mate ? (Dist * Ball->RollingFriction + Arrive) * FMath::Lerp(1.f, 1.4f, Power)
		                         : FMath::Max(Dist * Ball->RollingFriction + 250.f, FMath::Lerp(1100.f, 2000.f, Power));
		Plan.Velocity = Dir * FMath::Clamp(Speed, 950.f, 3000.f);
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
	// Training has no sides: shoot at the goal the stick (or the body) points to.
	float GoalSign = AttackSign();
	if (GM()->IsFreeTraining())
	{
		const float Toward = FMath::Abs(AimDir.X) > 0.2f ? AimDir.X : GetActorForwardVector().X;
		GoalSign = Toward >= 0.f ? 1.f : -1.f;
	}
	const FVector Target(GoalSign * GoalLineX(GM()), AimY, 0.f);
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
		Speed = FMath::Lerp(1700.f, 3500.f, Power) * ShotFactor; // a whippy strike, not a lob
		TargetZ = FMath::Lerp(40.f, 150.f, Power);
		// Yours in the red (last 20% of the bar): it rises over the 2.2 m bar, 3 m at full power
		if (IsPlayerControlled() && Power > 0.8f) TargetZ = FMath::Lerp(128.f, 300.f, (Power - 0.8f) / 0.2f);
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
	// Snap the body only for a turning kick; a small correction read as a twitch.
	const FVector KickDir = Plan.Velocity.GetSafeNormal2D();
	if (FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), KickDir) < 0.35f)
		SetActorRotation(FRotator(0.f, KickDir.Rotation().Yaw, 0.f));
	Ball->Kick(this, Plan.Velocity, Plan.Spin);
	Ball->IntendedReceiver = Plan.Receiver; // ИИ-партнёр побежит принимать
	// FIFA: control moves to the receiver as the pass leaves the foot. The same for your keeper's
	// distribution: the receiver meets it (your stick was still held on someone else and he stood).
	const bool bYourKeeper = bGoalkeeper && Team == HumanTeam;
	if ((IsPlayerControlled() && !bGoalkeeper || bYourKeeper) && Plan.Receiver && Plan.Receiver->Team == Team && !Plan.Receiver->bGoalkeeper)
	{
		if (ASoccerPlayerController* PC = HumanPC())
		{
			PC->PossessPlayer(Plan.Receiver);
			PC->bHoldMoveUntilNeutral = true;
		}
	}
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
	if (DelayedKickTime >= 0.f) return; // already winding up a kick
	const FName PassAction = bGoalkeeper ? (Kind == EPassKind::Ground ? TEXT("KeeperPass") : TEXT("KeeperKick")) : TEXT("Pass");
	const FSoccerKick PassPlan = PlanPass(Kind, AimDir, Power01, true);
	PlayDogAction(PassAction, 0.55f);
	KickAtContact(PassPlan, PassAction);
	GM()->OnPassMade(this);
	if (IsPlayerControlled())
	{
		GM()->OnHumanPass();
	}
}

void ASoccerPlayer::FakeShot()
{
	if (!HasBall() || DelayedKickTime >= 0.f) return;
	PlayDogAction(TEXT("FakeShot"));
	ProtectTime = 0.3f; // the defender bites on it
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
	if (DelayedKickTime >= 0.f) return;
	const FName ShotAction = bGoalkeeper ? TEXT("KeeperKick") : TEXT("Shot");
	const FSoccerKick ShotPlan = PlanShot(AimDir, Power01, bFinesse, bChip, true);
	PlayDogAction(ShotAction, 0.65f);
	KickAtContact(ShotPlan, ShotAction);
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
		const FVector Goal(AttackSign() * GoalLineX(GM()), FMath::Clamp((float)AimDir.Y * 2.f, -1.f, 1.f) * GoalHalfWidth * 0.7f, 0.f);
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
	Ball->LastHeaderTime = GetWorld()->GetTimeSeconds();
	PlayDogAction(TEXT("Header"), 0.55f);
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
	PlayDogAction(TEXT("Tackle"), 0.55f);
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
	if (bDogModel)
	{
		// Matches the clip: ~2 m slide over 0.6 s (1.5x speed), then the get-up while stunned.
		StartDash(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, 330.f, 0.6f, true);
		SlidePoseTime = 1.08f;
		PlayDogAction(TEXT("Slide"));
		return;
	}
	StartDash(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, 1100.f, 0.5f, true);
	SlidePoseTime = 0.5f + 0.45f; // скольжение + время подняться
	PlayDogAction(TEXT("Slide"), SlidePoseTime);
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
	if (GM()->IsFreeTraining())
	{
		// A steered cut through normal movement: motion matching picks the plant-and-turn
		// itself, where a scripted dash overrode it and stuttered.
		// Knock the ball into the space and chase it (it is re-trapped when caught up):
		// the ball goes where the stick points instead of swinging round the dog.
		SkillDir = Dir.GetSafeNormal2D();
		SkillTime = bBig ? 0.5f : 0.4f;
		if (ASoccerBall* KnockBall = GM()->Ball)
		{
			KnockBall->SetOwnerPlayer(nullptr);
			KnockBall->Touch(SkillDir * (bBig ? 800.f : 650.f));
			ControlCooldown = 0.2f;
		}
		return;
	}
	// A sharp cut at sprint pace, not a burst: motion matching has no clips above sprint
	// speed, so a 10 m/s dash read as a teleport.
	StartDash(Dir, bBig ? SprintSpeed * 1.05f : SprintSpeed * 0.95f, bBig ? 0.35f : 0.25f, false);
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
	UInputAction* IA_R3     = NewAction(0);
	UInputAction* IA_VarUp    = NewAction(0);
	UInputAction* IA_VarDown  = NewAction(0);
	UInputAction* IA_VarLeft  = NewAction(0);
	UInputAction* IA_VarRight = NewAction(0);

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
	Map(IA_R3, EKeys::Gamepad_RightThumbstick);      // R3 — вид камеры в тренировке
	Map(IA_R3, EKeys::C);
	Map(IA_Start, EKeys::Enter);
	// --- Крестовина: варианты A/B/C действий (SoccerVariants) ---
	Map(IA_VarUp, EKeys::Gamepad_DPad_Up);
	Map(IA_VarUp, EKeys::PageUp);
	Map(IA_VarDown, EKeys::Gamepad_DPad_Down);
	Map(IA_VarDown, EKeys::PageDown);
	Map(IA_VarLeft, EKeys::Gamepad_DPad_Left);
	Map(IA_VarLeft, EKeys::LeftBracket);
	Map(IA_VarRight, EKeys::Gamepad_DPad_Right);
	Map(IA_VarRight, EKeys::RightBracket);

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
	EIC->BindAction(IA_R3,     ETriggerEvent::Started,   this, &ASoccerPlayerController::OnR3);
	EIC->BindAction(IA_VarUp,    ETriggerEvent::Started, this, &ASoccerPlayerController::OnVariantAction, -1);
	EIC->BindAction(IA_VarDown,  ETriggerEvent::Started, this, &ASoccerPlayerController::OnVariantAction, 1);
	EIC->BindAction(IA_VarLeft,  ETriggerEvent::Started, this, &ASoccerPlayerController::OnVariantPick, -1);
	EIC->BindAction(IA_VarRight, ETriggerEvent::Started, this, &ASoccerPlayerController::OnVariantPick, 1);
	SoccerVariants::Load();
	EnsureInputMapping();
}

void ASoccerPlayerController::BeginPlay()
{
	Super::BeginPlay();
	EnsureInputMapping();
}

void ASoccerPlayerController::EnsureInputMapping()
{
	if (!Context || !GetLocalPlayer()) return;
	if (UEnhancedInputLocalPlayerSubsystem* Sub =
	        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (!Sub->HasMappingContext(Context))
		{
			Sub->AddMappingContext(Context, 0);
			UE_LOG(LogTemp, Display, TEXT("Soccer input mapping installed for %s"), *GetName());
		}
	}
}

void ASoccerPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (CVarBotsOnly.GetValueOnGameThread() > 0 && GetPawn() && GM() && !GM()->IsFreeTraining())
	{
		APawn* Was = GetPawn();
		UnPossess();
		Was->SpawnDefaultController();
	}
	if (const int32 Drive = CVarAutoDrive.GetValueOnGameThread())
	{
		const float T = GetWorld()->GetTimeSeconds();
		// 5/6: as 3/4 but without the ball (it waits inside the far net).
		if (Drive >= 5 && GM() && GM()->Ball && GM()->Ball->OwnerPlayer)
			GM()->Ball->ResetBall(FVector(1110.f, 0.f, BallRadius));
		if (Drive >= 3)
		{
			// 3/4: straight runs with 90-degree turns (a rectangle), 4 = with sprint.
			static const FVector2D Legs[4] = { {0.f, 1.f}, {1.f, 0.f}, {0.f, -1.f}, {-1.f, 0.f} };
			const float Cycle = FMath::Fmod(T, 8.f);
			MoveInput = Legs[Cycle < 2.5f ? 0 : (Cycle < 4.f ? 1 : (Cycle < 6.5f ? 2 : 3))];
			SprintAxis = (Drive == 4 || Drive == 6) ? 1.f : 0.f;
		}
		else
		{
			// Figure-eight: continuous curves plus direction reversals, the hard case for dribbling.
			MoveInput = FVector2D(FMath::Sin(T * 0.7f), FMath::Sin(T * 1.4f) * 0.8f).GetSafeNormal();
			SprintAxis = Drive >= 2 ? 1.f : 0.f;
		}
	}
	if (CVarAutoShot.GetValueOnGameThread() > 0 && GM() && GM()->Ball)
	{
		// 0: place the shooter with the ball, 1: shoot after a second, 2: watch the result.
		static int32 Stage = 0, Count = 0;
		static float StageTime = 2.f, ShotTime = 0.f;
		static const FVector2D Spots[6] = { {1100.f, 0.f}, {1600.f, 0.f}, {1100.f, 500.f}, {1100.f, -500.f}, {700.f, 800.f}, {700.f, -800.f} };
		static const float Aims[3] = { 0.7f, 0.f, -0.7f }; // full stick to a corner
		const float T = GetWorld()->GetTimeSeconds();
		ASoccerPlayer* K = Current();
		ASoccerBall* Ball = GM()->Ball;
		const int32 Spot = Count % 6, Aim = (Count / 6) % 3;
		MoveInput = FVector2D::ZeroVector;
		if (K && T > StageTime)
		{
			if (Stage == 0)
			{
				const FVector2D S = Spots[Spot];
				const FVector At(HalfLength - S.X, S.Y, K->GetActorLocation().Z);
				K->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
				K->SetActorRotation(FRotator::ZeroRotator);
				K->GetCharacterMovement()->StopMovementImmediately();
				Ball->ResetBall(FVector(At.X + 45.f, At.Y, BallRadius));
				K->GainBall();
				Stage = 1;
				StageTime = T + 1.2f;
			}
			else if (Stage == 1)
			{
				K->Shoot(FVector(1.f, Aims[Aim], 0.f), 0.75f, false, false);
				ShotTime = T;
				Stage = 2;
				StageTime = T;
			}
			else
			{
				const FVector B = Ball->GetActorLocation();
				const ASoccerPlayer* Holder = Ball->OwnerPlayer;
				const TCHAR* Result = nullptr;
				static float PrevX = 0.f;
				const ASoccerPlayer* GKeeper = nullptr;
				for (const ASoccerPlayer* Pl : GM()->Players) if (Pl && Pl->bGoalkeeper && Pl->Team != K->Team) GKeeper = Pl;
				const bool bBeaten = GKeeper && GKeeper->IsBeatenBy(Ball->GetLastKickTime());
				// Training puts the ball back on the centre spot the moment it is in the net.
				if (PrevX > HalfLength - 400.f && FMath::Abs(B.X) < 300.f) Result = TEXT("goal");
				else if (B.X > HalfLength + BallRadius) Result = (FMath::Abs(B.Y) < GoalHalfWidth && B.Z < GoalHeight) ? TEXT("goal") : TEXT("miss");
				else if (!bBeaten && ((Holder && Holder->bGoalkeeper) || (T - ShotTime > 0.2f && Ball->Velocity.X < -100.f))) Result = TEXT("save");
				else if (bBeaten && Holder && Holder->bGoalkeeper) Result = TEXT("miss"); // picked up after it went wide
				else if (bBeaten && T - ShotTime > 0.2f && Ball->Velocity.X < -100.f) Result = TEXT("miss"); // post or side netting
				else if (T - ShotTime > 3.f) Result = TEXT("miss");
				PrevX = Result ? 0.f : B.X;
				if (Result)
				{
					const ASoccerPlayer* GK = nullptr;
					for (const ASoccerPlayer* Pl : GM()->Players) if (Pl && Pl->bGoalkeeper && Pl->Team != K->Team) GK = Pl;
					UE_LOG(LogTemp, Display, TEXT("MFSHOT spot %d aim %d result %s variant %d ball %.0f %.0f %.0f keeper %.0f %.0f beaten %d"), Spot, Aim, Result,
						SoccerVariants::Get(SoccerVariants::Keeper), B.X, B.Y, B.Z, GK ? GK->GetActorLocation().X : 0.f, GK ? GK->GetActorLocation().Y : 0.f,
						GK && GK->IsBeatenBy(Ball->GetLastKickTime()) ? 1 : 0);
					++Count;
					Stage = 0;
					StageTime = T + 1.f;
				}
			}
		}
	}
	if (const float RecvEvery = CVarAutoReceive.GetValueOnGameThread(); RecvEvery > 0.f && GM() && GM()->Ball)
	{
		static float NextPass = 3.f;
		const float T = GetWorld()->GetTimeSeconds();
		ASoccerPlayer* K = Current();
		const float Phase = FMath::Fmod(T, 8.f); // AutoDrive 3/4 legs: no turn in the next second
		const bool bStraight = (Phase > 0.2f && Phase < 1.3f) || (Phase > 4.2f && Phase < 5.3f);
		if (K && T > NextPass && bStraight)
		{
			NextPass = T + RecvEvery;
			FVector Run = K->GetVelocity().GetSafeNormal2D();
			if (Run.IsNearlyZero()) Run = K->GetActorForwardVector().GetSafeNormal2D();
			// Aim where the player will be when a 13 m/s ball covers 7 m
			const FVector Meet = K->GetActorLocation() + K->GetVelocity() * (700.f / 1300.f);
			const FVector From = Meet + Run.RotateAngleAxis(50.f, FVector::UpVector) * 700.f;
			GM()->Ball->ResetBall(FVector(From.X, From.Y, BallRadius));
			GM()->Ball->Kick(nullptr, (FVector(Meet.X, Meet.Y, BallRadius) - FVector(From.X, From.Y, BallRadius)).GetSafeNormal() * 1300.f);
			GM()->Ball->IntendedReceiver = K;
			UE_LOG(LogTemp, Display, TEXT("MFRECV %.4f speed %.1f variant %d"), T, K->GetVelocity().Size2D(), SoccerVariants::Get(SoccerVariants::Receive));
		}
	}
	if (const float KickEvery = CVarAutoKick.GetValueOnGameThread(); KickEvery > 0.f && GM() && GM()->Ball)
	{
		static float HaveSince = -1.f, ReturnAt = -1.f;
		static int32 Count = 0;
		const float T = GetWorld()->GetTimeSeconds();
		if (ASoccerPlayer* K = Current())
		{
			const FVector Dir = K->GetVelocity().GetSafeNormal2D();
			if (K->HasBall())
			{
				if (HaveSince < 0.f) HaveSince = T;
				if (T - HaveSince > KickEvery && !Dir.IsNearlyZero())
				{
					const bool bShot = (Count++ % 2) == 1;
					UE_LOG(LogTemp, Display, TEXT("MFKICK %.4f %s speed %.1f variant %d"), T, bShot ? TEXT("shot") : TEXT("pass"),
						K->GetVelocity().Size2D(), SoccerVariants::Get(SoccerVariants::KickOnRun));
					if (bShot) K->Shoot(Dir, 0.5f, false, false);
					else K->Pass(EPassKind::Ground, Dir, 0.6f);
					HaveSince = -1.f;
					ReturnAt = T + 1.3f;
				}
			}
			else
			{
				HaveSince = -1.f;
				if (ReturnAt > 0.f && T > ReturnAt)
				{
					GM()->Ball->ResetBall(K->GetActorLocation() + (Dir.IsNearlyZero() ? K->GetActorForwardVector() : Dir) * 70.f - FVector(0.f, 0.f, K->GetActorLocation().Z - BallRadius));
					ReturnAt = -1.f;
				}
			}
		}
	}
	// Мяч потеряли во время замаха — удар/пас отменяется
	const ASoccerPlayer* P = Current();
	if (Charging != ECharge::None && (!P || (!P->HasBall() && !P->IsLooseBallNear())))
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
	const float Yaw = G ? G->GetControlCameraYaw() : 0.f;
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
	// Полная сила — за 0.6 секунды удержания (было 1 с: шкала набиралась слишком медленно)
	return FMath::Clamp<float>((GetWorld()->GetTimeSeconds() - ChargeStart) / 0.6f, 0.f, 1.f);
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
	if (P && !P->HasBall() && !P->CanKickBall() && P->IsLooseBallNear() && !P->bGoalkeeper)
	{
		P->QueueFirstTime(Kind, AimDirection(), Kind == ECharge::Shot ? FMath::Max(0.15f, Power) : Power, bRBHeld, bLBHeld);
		return;
	}
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
	AimReceiver = nullptr;
	if (P && G->IsFirstPersonCorner())
	{
		// Corner (Goals): the cross's flight is always drawn, lime, and follows the stick
		const float Power = Charging == ECharge::None ? 0.6f : GetCharge();
		const FSoccerKick Plan = P->PlanPass(EPassKind::Lob, AimDirection(), Power, false);
		G->AimLine->ShowArc(G->Ball->GetActorLocation(), Plan.Velocity, G->Ball->Gravity);
		return;
	}
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

	// Goals style: no arrow on the grass; the HUD marks the team-mate the pass goes to.
	G->AimLine->HidePath();
	AimReceiver = Preview == ECharge::Shot ? nullptr : Plan.Receiver;
}

void ASoccerPlayerController::PossessPlayer(ASoccerPlayer* NewPlayer)
{
	if (!NewPlayer || NewPlayer == GetPawn()) return;
	if (CVarBotsOnly.GetValueOnGameThread() > 0 && GM() && !GM()->IsFreeTraining()) return; // bots-only harness

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

		// FIFA LB: the team-mate who gets to the ball first (its predicted path counts,
		// not just where it is now), never the current player. No cycling through the
		// team on repeated presses: that picked far-away players under pressure.
		TMap<const ASoccerPlayer*, float> Reach;
		for (const ASoccerPlayer* P : Order)
		{
			FVector Meet;
			const float T = P->InterceptTime(Meet);
			Reach.Add(P, T < 90.f ? T : 10.f + FVector::Dist2D(P->GetActorLocation(), Meet) / 1000.f);
		}
		ASoccerPlayer* Best = nullptr;
		for (ASoccerPlayer* P : Order)
		{
			if (P != Cur && (!Best || Reach[P] < Reach[Best])) Best = P;
		}
		LastSwitchTime = GetWorld()->GetTimeSeconds();
		PossessPlayer(Best);
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
void ASoccerPlayerController::OnMove(const FInputActionValue& V)
{
	MoveInput = V.Get<FVector2D>();
	const ASoccerGameMode* G = GM();
	if (G && G->IsFreeTraining() && MoveInput.SizeSquared() > 0.04f && TrainingMoveLogCount < 5 &&
		GetWorld()->GetTimeSeconds() >= NextTrainingMoveLogTime)
	{
		const ASoccerPlayer* P = Cast<ASoccerPlayer>(GetPawn());
		UE_LOG(LogTemp, Display, TEXT("Training movement input: stick=%s pawn=%s position=%s mode=%d"),
			*MoveInput.ToString(), *GetNameSafe(P), P ? *P->GetActorLocation().ToString() : TEXT("none"),
			P ? int32(P->GetCharacterMovement()->MovementMode) : -1);
		NextTrainingMoveLogTime = GetWorld()->GetTimeSeconds() + 1.f;
		++TrainingMoveLogCount;
	}
}
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
		else
		{
			SwitchPlayer(Dir);          // без мяча: переключение на игрока в направлении стика
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
	if (Charging == ECharge::Shot && P->HasBall() && GetWorld()->GetTimeSeconds() - ChargeStart < 0.35f)
	{
		// Shoot, then pass before release: FIFA fake shot.
		Charging = ECharge::None;
		P->FakeShot();
		return;
	}
	if (P->HasBall())          BeginCharge(ECharge::Pass);
	else if (P->CanHeadBall()) P->Header(false, AimDirection());
	else if (P->CanKickBall()) P->Pass(EPassKind::Ground, AimDirection(), 0.5f);
	else if (P->IsLooseBallNear()) BeginCharge(ECharge::Pass); // pass first time / onto the knocked ball
	else if (!GM() || !GM()->IsFreeTraining()) bContainHeld = true; // no opponent to contain in training
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
	else if (P->IsLooseBallNear()) BeginCharge(ECharge::Shot); // shoot first time / onto the knocked ball
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

// --- Крестовина: вверх/вниз — действие, влево/вправо — вариант A/B/C ---
void ASoccerPlayerController::OnVariantAction(int32 Step)
{
	// Only the ground pass is being tuned now; the other variants and camera numbers keep
	// their saved values (mf.Var.* / mf.Cam.* still set them from the console).
	VariantAction = SoccerVariants::Pass;
	VariantShowUntil = GetWorld()->GetTimeSeconds() + 3.f;
}

void ASoccerPlayerController::OnVariantPick(int32 Step)
{
	if (VariantAction >= SoccerVariants::Num) SoccerVariants::StepTune(VariantAction - SoccerVariants::Num, Step);
	else SoccerVariants::Set(VariantAction, SoccerVariants::Get(VariantAction) + Step);
	VariantShowUntil = GetWorld()->GetTimeSeconds() + 3.f;
}

// --- Start / Options: пауза ---
void ASoccerPlayerController::OnR3()
{
	if (ASoccerGameMode* G = GM())
	{
		if (G->IsFreeTraining()) G->ToggleTrainingCamera();
	}
}

void ASoccerPlayerController::OnStart()
{
	if (ASoccerGameMode* G = GM())
	{
		if (G->IsIntro()) { G->EndIntro(); return; } // Start skips the intro
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
	if (G && G->IsIntro())
	{
		DrawIntro(G);
		return;
	}
	if (G && G->Ball && G->IsInMatch() && !G->IsMatchOver() && !G->IsFreeTraining() && !G->IsCelebrating())
	{
		DrawRadar(G);
	}

	const ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetOwningPlayerController());
	if (!PC) return;
	DrawVariantPicker(PC);
	const ASoccerPlayer* P = Cast<ASoccerPlayer>(PC->GetPawn());
	if (!P) return;

	// Goals: the lime tag above the player and, while a kick charges, the power bar above it
	// (not over your own head in the first-person corner view)
	if (!G || !G->IsFirstPersonCorner()) DrawPlayerTag(P, FLinearColor(0.62f, 1.f, 0.05f), PC->GetCharge());
	if (const ASoccerPlayer* Receiver = PC->GetAimReceiver())
	{
		DrawPlayerTag(Receiver, FLinearColor(0.95f, 0.95f, 0.95f), -1.f);
	}
	// Stamina lives on the name card (SoccerUI::MakeHud), not under the player.
}

// Intro overlays (Goals): the loading card, black dips between shots, VS and the rosters
void ASoccerHUD::DrawIntro(const ASoccerGameMode* G)
{
	const float T = G->GetIntroTime();
	const float W = Canvas->ClipX, H = Canvas->ClipY, K = H / 1080.f;
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetMediumFont();
	auto Black = [&](float A) { if (A > 0.f) DrawRect(FLinearColor(0.f, 0.f, 0.f, FMath::Clamp(A, 0.f, 1.f)), 0.f, 0.f, W, H); };
	if (T < 3.f)
	{
		// dark frame round the wide shot, the arena name and the loading line
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, W, 60.f * K);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, H - 90.f * K, W, 90.f * K);
		DrawText(TEXT("MINI FOOTBALL ARENA"), FLinearColor::White, 40.f * K, 22.f * K, Big, 6.f * K);
		DrawRect(FLinearColor(0.62f, 1.f, 0.05f), W - 330.f * K, H - 62.f * K, 18.f * K, 18.f * K);
		DrawText(TEXT("ЗАГРУЗКА"), FLinearColor::White, W - 300.f * K, H - 70.f * K, Small, 2.4f * K);
		Black((T - 2.6f) / 0.4f);
		return;
	}
	Black(1.f - (T - 3.f) / 0.4f);           // fade in on the grass-level shot
	if (T > 9.3f && T < 9.7f) Black((T - 9.3f) / 0.4f);
	if (T < 9.7f) return;
	Black(1.f - (T - 9.7f) / 0.3f);
	// score board behind the teams: red | 0 - 0 | blue
	const float BW = 520.f * K, BH = 46.f * K, BX = (W - BW) * 0.5f, BY = H * 0.36f; // above the heads
	DrawRect(FLinearColor(0.75f, 0.06f, 0.18f, 0.9f), BX, BY, BW * 0.5f, BH);
	DrawRect(FLinearColor(0.1f, 0.45f, 0.95f, 0.9f), BX + BW * 0.5f, BY, BW * 0.5f, BH);
	float TW = 0.f, TH = 0.f;
	const FString Score = FString::Printf(TEXT("%d - %d"), G->GetScore(0), G->GetScore(1));
	GetTextSize(Score, TW, TH, Big, 3.f * K);
	DrawText(Score, FLinearColor::White, (W - TW) * 0.5f, BY + (BH - TH) * 0.5f, Big, 3.f * K);
	if (T > 10.25f)
	{
		// VS drops in and settles
		const float S = FMath::Lerp(1.6f, 1.f, FMath::Clamp((T - 10.25f) / 0.25f, 0.f, 1.f)) * 12.f * K;
		GetTextSize(TEXT("VS"), TW, TH, Big, S);
		DrawText(TEXT("VS"), FLinearColor(0.85f, 0.87f, 0.9f), (W - TW) * 0.5f, H * 0.08f, Big, S);
	}
	if (T > 11.f)
	{
		// rosters: home on the left, away on the right
		for (int32 Team = 0; Team < 2; ++Team)
		{
			float Y = H * 0.07f;
			DrawText(G->GetTeamName(Team).ToUpper(), FLinearColor(1.f, 1.f, 1.f, 0.7f), Team == 0 ? 40.f * K : W - 360.f * K, Y - 44.f * K, Small, 2.f * K);
			for (const ASoccerPlayer* P : G->Players)
			{
				if (!P || P->Team != Team) continue;
				const float X = Team == 0 ? 40.f * K : W - 360.f * K;
				DrawRect(FLinearColor(0.03f, 0.04f, 0.05f, 0.75f), X, Y, 320.f * K, 40.f * K);
				DrawRect(P->ShirtColor, X, Y, 6.f * K, 40.f * K);
				DrawText(P->Info.Name, FLinearColor::White, X + 16.f * K, Y + 4.f * K, Small, 1.8f * K);
				Y += 46.f * K;
			}
		}
	}
	Black((T - 14.6f) / 0.4f);
}

// Variant picker: one quiet line at the top, only for a few seconds after a D-pad press.
//   УДАР НА БЕГУ   A  [B]  C   короткий замах
void ASoccerHUD::DrawVariantPicker(const ASoccerPlayerController* PC)
{
	const float Left = PC->VariantShowUntil - GetWorld()->GetTimeSeconds();
	if (Left <= 0.f) return;
	const float Alpha = FMath::Clamp(Left / 0.4f, 0.f, 1.f);
	const ASoccerGameMode* GMode = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	if (PC->VariantAction >= SoccerVariants::Num)
	{
		// Camera number:  КАМЕРА: НАКЛОН ВНИЗ   7/16   ‹ 20 ›
		const int32 Tune = PC->VariantAction - SoccerVariants::Num;
		UFont* TFont = GEngine->GetMediumFont();
		const float TK = Canvas->ClipY / 1080.f, TS = 1.3f * TK;
		const FString Head = FString::Printf(TEXT("%s   %d/%d"), SoccerVariants::TuneTitle(Tune), PC->VariantAction + 1, SoccerVariants::NumEntries());
		const FString Value = SoccerVariants::TuneValue(Tune);
		const FString Shown = Value.IsEmpty() ? FString() : FString::Printf(TEXT("<   %s   >"), *Value);
		float HW = 0.f, HH = 0.f, VW = 0.f, VH = 0.f;
		GetTextSize(Head, HW, HH, TFont, TS);
		GetTextSize(Shown, VW, VH, TFont, TS);
		const float TW = HW + (Shown.IsEmpty() ? 0.f : 30.f * TK + VW);
		const float TX = (Canvas->ClipX - TW) * 0.5f;
		const float TY = (GMode && GMode->IsFreeTraining() ? 40.f : 150.f) * TK;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha), TX - 16.f * TK, TY - 8.f * TK, TW + 32.f * TK, HH + 16.f * TK);
		DrawText(Head, FLinearColor(1.f, 1.f, 1.f, 0.6f * Alpha), TX, TY, TFont, TS);
		if (!Shown.IsEmpty()) DrawText(Shown, FLinearColor(0.62f, 1.f, 0.05f, Alpha), TX + HW + 30.f * TK, TY, TFont, TS);
		return;
	}
	const SoccerVariants::FAction& Action = SoccerVariants::Describe(PC->VariantAction);
	const int32 Chosen = SoccerVariants::Get(PC->VariantAction);
	UFont* Font = GEngine->GetMediumFont();
	const float K = Canvas->ClipY / 1080.f;
	const float S = 1.3f * K;
	const TCHAR* Letters[3] = {TEXT("A"), TEXT("B"), TEXT("C")};
	FString Title = Action.Title;
	float W = 0.f, H = 0.f, TW = 0.f, TH = 0.f;
	GetTextSize(Title, TW, TH, Font, S);
	const float LetterGap = 34.f * K;
	FString Name = Action.Names[Chosen];
	float NW = 0.f, NH = 0.f;
	GetTextSize(Name, NW, NH, Font, S);
	W = TW + 30.f * K + LetterGap * 3.f + 20.f * K + NW;
	H = TH;
	float X = (Canvas->ClipX - W) * 0.5f;
	// In a match it goes under the names and score bar; training has no top bar.
	const ASoccerGameMode* G = GetWorld()->GetAuthGameMode<ASoccerGameMode>();
	const float Y = (G && G->IsFreeTraining() ? 40.f : 150.f) * K;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha), X - 16.f * K, Y - 8.f * K, W + 32.f * K, H + 16.f * K);
	DrawText(Title, FLinearColor(1.f, 1.f, 1.f, 0.6f * Alpha), X, Y, Font, S);
	X += TW + 30.f * K;
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bOn = i == Chosen;
		if (bOn) DrawRect(FLinearColor(0.62f, 1.f, 0.05f, Alpha), X - 7.f * K, Y - 3.f * K, 26.f * K, H + 6.f * K);
		DrawText(Letters[i], bOn ? FLinearColor(0.02f, 0.03f, 0.02f, Alpha) : FLinearColor(1.f, 1.f, 1.f, 0.5f * Alpha), X, Y, Font, S);
		X += LetterGap;
	}
	X += 20.f * K;
	DrawText(Name, FLinearColor(1.f, 1.f, 1.f, 0.9f * Alpha), X, Y, Font, S);
}

// Goals-style tag above a player: a small rounded pill with a dark rim, the name above it.
UTexture2D* ASoccerHUD::GetTagTexture()
{
	if (TagTexture) return TagTexture;
	// 64x64 white disc with an anti-aliased edge; tinted when drawn
	constexpr int32 N = 64;
	TagTexture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
	if (!TagTexture) return nullptr;
	FColor* Px = static_cast<FColor*>(TagTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 y = 0; y < N; ++y)
		for (int32 x = 0; x < N; ++x)
		{
			const float D = FVector2D(x + 0.5f - N * 0.5f, y + 0.5f - N * 0.5f).Size();
			Px[y * N + x] = FColor(255, 255, 255, uint8(255.f * FMath::Clamp(N * 0.5f - 1.f - D, 0.f, 1.f)));
		}
	TagTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	TagTexture->Filter = TF_Bilinear;
	TagTexture->UpdateResource();
	return TagTexture;
}

// Tag and power bar as in Goals, sized from the player's height on screen (measured on a
// Goals frame): tag 17% of it wide, a quarter of it above the head; bar 85% wide, 8% tall,
// segments filling green -> yellow -> orange from the moment the kick is pressed.
void ASoccerHUD::DrawPlayerTag(const ASoccerPlayer* P, const FLinearColor& Color, float Charge)
{
	// The visible figure: top of the head bone, and the ground under it
	const USkeletalMeshComponent* Body = P->GetMesh();
	const FVector Loc = P->GetActorLocation();
	const FVector Top3 = Body && Body->DoesSocketExist(TEXT("HeadTop_End")) ? Body->GetSocketLocation(TEXT("HeadTop_End")) : Loc + FVector(0.f, 0.f, 90.f);
	const FVector Head = Project(Top3);
	const FVector Feet = Project(FVector(Top3.X, Top3.Y, Loc.Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	if (Head.Z <= 0.f || Feet.Z <= 0.f) return;
	const float PH = FMath::Clamp(float(Feet.Y - Head.Y), 30.f, 400.f);
	const float W = FMath::Max(8.f, 0.15f * PH), H = W, Rim = FMath::Max(1.5f, 0.02f * PH);
	const float X = Head.X - W * 0.5f, Y = Head.Y - 0.25f * PH - H;
	// a smooth dot with a thin dark ring: minimal, no pixel steps
	if (UTexture2D* Dot = GetTagTexture())
	{
		DrawTexture(Dot, X - Rim, Y - Rim, W + 2.f * Rim, H + 2.f * Rim, 0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.04f, 0.02f, 0.75f), BLEND_Translucent);
		DrawTexture(Dot, X, Y, W, H, 0.f, 0.f, 1.f, 1.f, Color, BLEND_Translucent);
	}
	if (Charge < 0.f) return;

	constexpr int32 Segments = 10;
	const float BarW = 0.85f * PH, BarH = FMath::Max(4.f, 0.075f * PH), Gap = FMath::Max(1.f, 0.012f * PH);
	const float SegW = (BarW - (Segments - 1) * Gap) / Segments;
	const float BX = Head.X - BarW * 0.5f, BY = Y - 0.1f * PH - BarH;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.02f, 0.85f), BX - Gap, BY - Gap, BarW + 2.f * Gap, BarH + 2.f * Gap);
	const FLinearColor Green(0.3f, 1.f, 0.05f), Yellow(1.f, 0.85f, 0.f), Orange(1.f, 0.42f, 0.f), Red(1.f, 0.05f, 0.02f);
	for (int32 i = 0; i < Segments; ++i)
	{
		// green -> yellow -> orange -> red: the last segment is full power (a shot goes over)
		const float U = float(i) / (Segments - 1) * 3.f;
		const FLinearColor Lit = U < 1.f ? FMath::Lerp(Green, Yellow, U) : (U < 2.f ? FMath::Lerp(Yellow, Orange, U - 1.f) : FMath::Lerp(Orange, Red, U - 2.f));
		DrawRect(Charge * Segments > i ? Lit : FLinearColor(0.12f, 0.13f, 0.12f, 0.9f), BX + i * (SegW + Gap), BY, SegW, BarH);
	}
}

void ASoccerHUD::DrawRadar(const ASoccerGameMode* G)
{
	// Поле в масштабе: вид «с трибуны», как у камеры (+X — вправо, +Y — вниз)
	const float K = Canvas->ClipY / 1080.f;
	const float MapHalfLength = HalfLength;
	const float MapHalfWidth = HalfWidth;
	const float MapPenaltyDepth = PenaltyDepth;
	const float MapBoxHalfWidth = GoalHalfWidth + 400.f;
	// Goals: 10.5% of the screen width, 7.6% above the bottom, slate blue with faint lines
	const float W = 0.105f * Canvas->ClipX;
	const float H = W * MapHalfWidth / MapHalfLength;
	const float X0 = (Canvas->ClipX - W) * 0.5f;
	const float Y0 = Canvas->ClipY * (1.f - 0.076f) - H;
	const float T = FMath::Max(1.f, 1.2f * K); // толщина линий
	const FLinearColor Line(0.75f, 0.8f, 0.85f, 0.45f);

	DrawRect(FLinearColor::FromSRGBColor(FColor(38, 52, 66, 215)), X0, Y0, W, H);
	DrawRect(Line, X0, Y0, W, T);
	DrawRect(Line, X0, Y0 + H - T, W, T);
	DrawRect(Line, X0, Y0, T, H);
	DrawRect(Line, X0 + W - T, Y0, T, H);
	DrawRect(Line, X0 + (W - T) * 0.5f, Y0, T, H);
	const float BoxW = W * MapPenaltyDepth / (2.f * MapHalfLength);
	const float BoxH = H * MapBoxHalfWidth / MapHalfWidth;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float BX = Side == 0 ? X0 : X0 + W - BoxW;
		DrawRect(Line, BX, Y0 + (H - BoxH) * 0.5f, BoxW, T);
		DrawRect(Line, BX, Y0 + (H + BoxH) * 0.5f - T, BoxW, T);
		DrawRect(Line, Side == 0 ? BX + BoxW - T : BX, Y0 + (H - BoxH) * 0.5f, T, BoxH);
	}

	auto ToRadar = [X0, Y0, W, H, MapHalfLength, MapHalfWidth](const FVector& L)
	{
		const float RX = FMath::Clamp((float)(L.X + MapHalfLength) / (2.f * MapHalfLength), 0.f, 1.f);
		const float RY = FMath::Clamp((float)(L.Y + MapHalfWidth) / (2.f * MapHalfWidth), 0.f, 1.f);
		return FVector2D(X0 + RX * W, Y0 + RY * H);
	};

	const APawn* Human = GetOwningPawn();
	for (const ASoccerPlayer* P : G->Players)
	{
		if (!P) continue;
		const FVector2D R = ToRadar(P->GetActorLocation());
		// your team white, theirs in their kit colour, you in lime (Goals)
		const bool bHuman = P == Human;
		const float Size = (bHuman ? 8.f : 6.f) * K;
		const FLinearColor Fill = bHuman ? FLinearColor(0.64f, 0.9f, 0.09f) : (P->Team == HumanTeam ? FLinearColor::White : P->ShirtColor);
		DrawRect(Fill, R.X - Size * 0.5f, R.Y - Size * 0.5f, Size, Size);
	}
	const FVector2D B = ToRadar(G->Ball->GetActorLocation());
	DrawRect(FLinearColor::Black, B.X - 2.5f * K, B.Y - 2.5f * K, 5.f * K, 5.f * K);
	DrawRect(FLinearColor(1.f, 0.85f, 0.1f), B.X - 1.5f * K, B.Y - 1.5f * K, 3.f * K, 3.f * K);
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
	if (CVarAutoTraining.GetValueOnGameThread() != 0 || FParse::Param(FCommandLine::Get(), TEXT("MFAutoTraining")))
		StartMatch(ESoccerMode::FreeTraining);
	else if (FParse::Param(FCommandLine::Get(), TEXT("MFAutoMatch")))
		StartMatch(ESoccerMode::Match);
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
	// Giraffe footballers (Scripts/build_giraffe.py + import_giraffe.py) share the dog's
	// skeleton and clips; the dog stays as the fallback.
	FootballerMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Giraffe/SK_Giraffe.SK_Giraffe"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!FootballerMesh)
		FootballerMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Dogs/SK_Dog_Cavapoo.SK_Dog_Cavapoo"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (FootballerMesh)
	{
		FootballerRunMesh = FootballerMesh;
		UE_LOG(LogTemp, Log, TEXT("Soccer: %s teams enabled"), *FootballerMesh->GetName());
		return;
	}
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
	if (ASoccerPlayerController* SPC = Cast<ASoccerPlayerController>(PC))
	{
		SPC->EnsureInputMapping();
		SPC->ResetInputState();
	}
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	UE_LOG(LogTemp, Display, TEXT("Soccer game input enabled: controller=%s"), *GetNameSafe(PC));
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
	SetNight(false);
	for (AActor* Actor : TrainingRoomActors)
	{
		if (Actor) Actor->Destroy();
	}
	TrainingRoomActors.Reset();
	for (AActor* Actor : GeneratedFieldActors)
	{
		if (!Actor) continue;
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);
	}
	if (Ball) Ball->bTrainingPhysics = false;

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
	for (AActor* Actor : TrainingRoomActors)
	{
		if (Actor) Actor->Destroy();
	}
	TrainingRoomActors.Reset();
	for (AActor* Actor : GeneratedFieldActors)
	{
		if (!Actor) continue;
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);
	}
	if (Ball)
	{
		Ball->bTrainingPhysics = true; // carried in touches, court collision (training = match physics)
		// Values chosen for a responsive first pass; kept public on ASoccerBall for tuning.
		Ball->Gravity = 1400.f;
		Ball->AirDragQuad = 6e-5f;
		Ball->MagnusCoeff = 0.02f;
		Ball->Bounciness = 0.55f;
		Ball->RollingFriction = 0.62f;
	}
	MatchDifficulty = RivalDifficulty >= 0 ? FMath::Clamp(RivalDifficulty, 0, 2) : Save->Difficulty;
	if (IsPractice() || IsFreeTraining())
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
	TimeLeft = IsFreeTraining() ? 0.f : (IsPractice() ? 60.f : Save->MatchMinutes * 60.f);
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

	if (IsFreeTraining())
	{
		// A landscape at Z=0 (Open World template) z-fights with the court floor and flickers as a grid.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetClass()->GetName().StartsWith(TEXT("Landscape")))
			{
				It->SetActorHiddenInGame(true);
				It->SetActorEnableCollision(false);
			}
		}
		SpawnTrainingRoom();
	}
	else
	{
		SpawnMatchCourt();
		SpawnTeams();
	}
	CamFocus = FVector::ZeroVector;

	ShowWidget(HudWidget, SoccerUI::MakeHud(this), 5);
	SetGameInput();
	PossessHuman();
	ResetPositions(); // разводка с центра: управление перейдёт к разводящему
	IntroTime = -1.f;
	if (!IsFreeTraining() && CVarIntro.GetValueOnGameThread() > 0) BeginIntro();
}

// ---------------------------------------------------------------------------
//  Match intro, timed on the Goals recording (0702(14).mp4, 15 s):
//  0-2.7 loading card over a wide shot | 3-5.6 camera at grass level down the pitch |
//  5.6-5.9 whip | 5.9-9.6 fly-over from the corner | 9.7-15 line-up, VS, rosters
// ---------------------------------------------------------------------------
void ASoccerGameMode::BeginIntro()
{
	IntroTime = 0.f;
	bPlayActive = false;
	for (ASoccerPlayer* P : Players) if (P) P->SetActorHiddenInGame(true);
	if (Ball) Ball->SetActorHiddenInGame(true);
}

void ASoccerGameMode::EndIntro()
{
	if (!IsIntro()) return;
	IntroTime = -1.f;
	for (ASoccerPlayer* P : Players) if (P) P->SetActorHiddenInGame(false);
	if (Ball) Ball->SetActorHiddenInGame(false);
	ResetPositions();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager) PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.5f, FLinearColor::Black);
}

void ASoccerGameMode::TickIntro(float Dt)
{
	if (!IsIntro()) return;
	if (CVarIntro.GetValueOnGameThread() == 0) { EndIntro(); return; } // set after the match began (ExecCmds)
	IntroTime += Dt;
	if (IntroTime >= 15.f) { EndIntro(); return; }
	if (IntroTime < 9.7f) return;
	// Line-up: both teams in a row across the far half, facing the camera
	int32 Index[2] = { 0, 0 };
	for (ASoccerPlayer* P : Players)
	{
		if (!P) continue;
		P->SetActorHiddenInGame(false);
		const int32 i = Index[P->Team]++;
		const float X = (P->Team == HumanTeam ? -1.f : 1.f) * (110.f + i * 125.f);
		P->SetActorLocation(FVector(X, -300.f, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		P->SetActorRotation(FRotator(0.f, 90.f, 0.f));
		P->GetCharacterMovement()->StopMovementImmediately();
	}
}

bool ASoccerGameMode::UpdateIntroCamera()
{
	if (!IsIntro()) return false;
	const float T = IntroTime;
	auto Shot = [this](const FVector& Loc, const FVector& At, float FOV)
	{
		Camera->GetCameraComponent()->SetFieldOfView(FOV);
		Camera->SetActorLocationAndRotation(Loc, (At - Loc).Rotation());
	};
	auto Ease = [](float A) { A = FMath::Clamp(A, 0.f, 1.f); return A * A * (3.f - 2.f * A); };
	const FVector LowFrom(-HalfLength - 150.f, 0.f, 50.f), LowTo(-HalfLength + 500.f, 0.f, 60.f), LowAt(HalfLength, 0.f, 250.f);
	// the fly-over looks toward the stands (the far, -Y side)
	const FVector AirFrom(-HalfLength + 300.f, HalfWidth + 400.f, 1000.f), AirTo(-HalfLength + 1200.f, HalfWidth + 200.f, 800.f), AirAt(-500.f, 0.f, 0.f);
	if (T < 3.f)
		Shot(FVector(-HalfLength - 400.f + 60.f * T, HalfWidth + 1200.f, 1100.f), FVector(300.f, 0.f, 0.f), 60.f);
	else if (T < 5.6f)
		Shot(FMath::Lerp(LowFrom, LowTo, Ease((T - 3.f) / 2.6f)), LowAt, 70.f);
	else if (T < 5.9f)
	{
		// whip: a fast turn from the grass-level shot to the fly-over
		const float A = Ease((T - 5.6f) / 0.3f);
		const FRotator R = FMath::Lerp((LowAt - LowTo).Rotation(), (AirAt - AirFrom).Rotation(), A);
		Camera->GetCameraComponent()->SetFieldOfView(70.f);
		Camera->SetActorLocationAndRotation(FMath::Lerp(LowTo, AirFrom, A), R);
	}
	else if (T < 9.7f)
		Shot(FMath::Lerp(AirFrom, AirTo, (T - 5.9f) / 3.8f), AirAt, 65.f);
	else
		Shot(FVector(0.f, 1600.f - 40.f * (T - 9.7f), 170.f), FVector(0.f, -300.f, 120.f), 45.f); // all ten in frame
	return true;
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
	// BP_SoccerDog adds the animation interface for motion matching; without it the
	// native class still plays the dog's own clips.
	static TSubclassOf<ASoccerPlayer> PlayerClass = [] {
		UClass* Found = LoadClass<ASoccerPlayer>(nullptr, TEXT("/Game/Characters/Dogs/BP_SoccerDog.BP_SoccerDog_C"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		return Found ? Found : ASoccerPlayer::StaticClass();
	}();
	ASoccerPlayer* P = GetWorld()->SpawnActor<ASoccerPlayer>(PlayerClass, Location, FRotator(0.f, Yaw, 0.f), Params);
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
		FVector2D(-1380.f, -540.f), // x1.2 with the 48 x 32 m pitch
		FVector2D(-1380.f,  540.f),
		FVector2D(-540.f,  -420.f),
		FVector2D(-540.f,   420.f),
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

void ASoccerGameMode::SpawnTrainingRoom()
{
	DestroyPlayers();
	const int32 Starter = FMath::Clamp(Save->StarterIndex, 1, Save->Squad.Num() - 1);
	// Same court, size and physics as a match (SpawnMatchCourt also brings the surroundings)
	SpawnMatchCourt();
	SpawnPlayer(HumanTeam, Starter, Save->Squad[Starter], FVector(-120.f, 0.f, 92.f), 0.f);
	// A team-mate to pass to: AI, gets open ahead of the ball; the pass hands you control of him
	const int32 Mate = Starter == 3 ? 4 : 3;
	SpawnPlayer(HumanTeam, Mate, Save->Squad[Mate], FVector(300.f, 500.f, 92.f), 0.f);
	// A keeper to beat in the far goal
	if (AwaySquad.Num() > 0)
	{
		SpawnPlayer(1 - HumanTeam, 0, AwaySquad[0], FVector(HalfLength - 70.f, 0.f, 92.f), 180.f);
	}
}

// Surroundings of the court (Goals-style training ground): trees along the open side,
// apartment blocks behind the ends, lamps, benches, hedges and banners on the fence.
// Positions are in the training court's space (fence at +-1325 x +-800 cm, stands on -Y)
// and stretch with the court in matches (Sx, Sy). Props: Poly Haven CC0 via
// Scripts/import_environment.py; banners: Scripts/import_banners.py.
void ASoccerGameMode::SpawnEnvironment(float Sx, float Sy)
{
	auto Mesh = [](const TCHAR* Name) -> UStaticMesh*
	{
		const FString Path = FString::Printf(TEXT("/Game/Environment/Props/%s/%s/StaticMeshes/%s.%s"), Name, Name, Name, Name);
		return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	};
	auto Place = [this, Sx, Sy](UStaticMesh* M, float X, float Y, float Yaw, float Scale, bool bShadow)
	{
		if (!M) return;
		AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(X * Sx, Y * Sy, 0.f), FRotator(0.f, Yaw, 0.f));
		if (!A) return;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		A->SetActorEnableCollision(false);
		A->SetActorScale3D(FVector(Scale));
		TrainingRoomActors.Add(A);
	};
	// Rooftop concrete around the court (the court model ends at its fence)
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		if (UMaterialInterface* Ground = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Training/M_Ground.M_Ground"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -3.f), FRotator::ZeroRotator);
			if (A)
			{
				A->SetMobility(EComponentMobility::Movable);
				UStaticMeshComponent* C = A->GetStaticMeshComponent();
				C->SetStaticMesh(Plane);
				C->SetMaterial(0, Ground);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				A->SetActorEnableCollision(false);
				A->SetActorScale3D(FVector(90.f * Sx, 90.f * Sy, 1.f));
				TrainingRoomActors.Add(A);
			}
		}
	}
	UStaticMesh* Island01 = Mesh(TEXT("SM_Env_TreeIsland01"));
	UStaticMesh* Island02 = Mesh(TEXT("SM_Env_TreeIsland02"));
	UStaticMesh* Small02 = Mesh(TEXT("SM_Env_TreeSmall02"));
	UStaticMesh* Jacaranda = Mesh(TEXT("SM_Env_TreeJacaranda"));
	UStaticMesh* Lamp = Mesh(TEXT("SM_Env_StreetLamp"));
	UStaticMesh* Bench = Mesh(TEXT("SM_Env_Bench"));
	UStaticMesh* Shrub = Mesh(TEXT("SM_Env_Shrub01"));

	// A row of trees behind the open (+Y) fence, sized up to read as a park edge
	UStaticMesh* Row[3] = { Island01, Small02, Island02 };
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = -2100.f + i * 600.f;
		Place(Row[i % 3], X, 1250.f + (i % 2) * 150.f, i * 47.f, 1.9f + 0.25f * (i % 3), true);
	}
	// Big trees at the corners and over the stands
	Place(Jacaranda, -2900.f, 2100.f, 20.f, 0.9f, true);
	Place(Jacaranda, 2900.f, 2000.f, 160.f, 0.8f, true);
	Place(Jacaranda, -1700.f, -2700.f, 90.f, 0.8f, true);
	Place(Jacaranda, 1800.f, -2800.f, 250.f, 0.9f, true);
	// ponytail: the Poly Haven apartments came in with only their window modules (no walls)
	// and hung in the air; left out until the arena backdrop is redone.
	// Lamps at the open corners, benches and a hedge along the open fence
	Place(Lamp, -1450.f, 950.f, 0.f, 1.f, true);
	Place(Lamp, 1450.f, 950.f, 0.f, 1.f, true);
	Place(Bench, -420.f, 930.f, 180.f, 1.f, true);
	Place(Bench, 420.f, 930.f, 180.f, 1.f, true);
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = -1150.f + i * 330.f;
		if (FMath::Abs(X) < 600.f) continue; // benches there
		Place(Shrub, X, 880.f, 0.f, 1.2f, false);
	}

	// Banners on the inside of the fence: a plane facing the pitch
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	auto Banner = [this, Plane, Sx, Sy](const TCHAR* Material, float X, float Y, float Z, const FVector& Facing, float Len, float Height)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Environment/Banners/%s.%s"), Material, Material), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Plane || !M) return;
		const FVector Along = FVector::CrossProduct(FVector::UpVector, Facing).GetSafeNormal();
		// Plane mesh: normal +Z, texture U along +X; face the pitch with U along the fence.
		const FRotator Rot = FRotationMatrix::MakeFromZX(Facing, -Along).Rotator();
		AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(X * Sx, Y * Sy, Z), Rot);
		if (!A) return;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		A->SetActorEnableCollision(false);
		A->SetActorScale3D(FVector(Len / 100.f, Height / 100.f, 1.f));
		TrainingRoomActors.Add(A);
	};
	for (int32 End : {-1, 1})
	{
		const FVector In(-End, 0.f, 0.f);
		Banner(TEXT("M_Banner_Orange"), End * 1318.f, -430.f, 140.f, In, 320.f, 80.f);
		Banner(TEXT("M_Banner_Dark"), End * 1318.f, 430.f, 140.f, In, 320.f, 80.f);
		Banner(TEXT("M_Banner_Flag"), End * 1318.f, End * 690.f, 230.f, In, 80.f, 240.f);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		Banner(i % 2 ? TEXT("M_Banner_Dark") : TEXT("M_Banner_Orange"), -960.f + i * 640.f, -793.f, 140.f, FVector(0.f, 1.f, 0.f), 320.f, 80.f);
	}
}

// Night in the zoo arena. Physical-ish units: floodlights light the pitch to ~10 lux, warm lights
// on the stands, and the exposure is fixed (auto exposure would lift the night back to day).
static TAutoConsoleVariable<float> CVarNightEV(TEXT("mf.Night.EV"), 3.f, TEXT("Night exposure (EV100, fixed); lower = brighter. Applied on the next match/training start"));
static TAutoConsoleVariable<float> CVarNightFlood(TEXT("mf.Night.Flood"), 60000.f, TEXT("Night: floodlight intensity per mast (cd)"));
static TAutoConsoleVariable<float> CVarNightWarm(TEXT("mf.Night.Warm"), 120.f, TEXT("Night: warm lights on the stands (cd)"));
static TAutoConsoleVariable<float> CVarNightMoon(TEXT("mf.Night.Moon"), 45.f, TEXT("Night: the level's sun becomes the even wash over the pitch (lux)"));

void ASoccerGameMode::SetNight(bool bNight)
{
	// back to day: the level's lights get their intensities back (night actors die with TrainingRoomActors)
	for (const TPair<TWeakObjectPtr<ULightComponentBase>, float>& Saved : DayLights)
	{
		if (ULightComponent* L = Cast<ULightComponent>(Saved.Key.Get())) L->SetIntensity(Saved.Value);
		else if (USkyLightComponent* S = Cast<USkyLightComponent>(Saved.Key.Get())) S->SetIntensity(Saved.Value);
	}
	DayLights.Reset();
	if (!bNight) return;

	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ULightComponent* L = It->GetLightComponent();
		DayLights.Emplace(L, L->Intensity);
		L->SetIntensity(CVarNightMoon.GetValueOnGameThread());
		L->SetLightColor(FLinearColor(0.9f, 0.93f, 1.f)); // floodlight white (the day colour is not restored: it was white too)
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* S = It->GetLightComponent();
		DayLights.Emplace(S, S->Intensity);
		S->SetIntensity(0.15f);
	}
	if (APostProcessVolume* PP = GetWorld()->SpawnActor<APostProcessVolume>())
	{
		PP->bUnbound = true;
		PP->Priority = 100.f;
		FPostProcessSettings& S = PP->Settings;
		S.bOverride_AutoExposureMinBrightness = S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMinBrightness = S.AutoExposureMaxBrightness = CVarNightEV.GetValueOnGameThread();
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 1.2f;
		TrainingRoomActors.Add(PP);
	}
	auto Point = [this](const FVector& At, const FLinearColor& Color, float Candela, float Radius)
	{
		APointLight* A = GetWorld()->SpawnActor<APointLight>(At, FRotator::ZeroRotator);
		if (!A) return;
		UPointLightComponent* L = Cast<UPointLightComponent>(A->GetLightComponent());
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(Candela);
		L->SetLightColor(Color);
		L->SetAttenuationRadius(Radius);
		L->SetCastShadows(false);
		TrainingRoomActors.Add(A);
	};
	// floodlights on the masts (arena layout, SourceArt/Environment/Zoo), aimed across the pitch
	const float Flood = CVarNightFlood.GetValueOnGameThread();
	const FVector Masts[6] = { {-1500.f, -3200.f, 2100.f}, {1500.f, -3200.f, 2100.f}, {-3850.f, -900.f, 1900.f},
	                           {-3850.f, 900.f, 1900.f}, {3850.f, -900.f, 1900.f}, {3850.f, 900.f, 1900.f} };
	auto Spot = [this](const FVector& At, const FVector& Dir, float Candela, float Outer, bool bShadows)
	{
		// movable from the start: a stationary light spawned at runtime has no shadow channel and stays dark
		AActor* A = GetWorld()->SpawnActor<AActor>(At, Dir.Rotation());
		if (!A) return;
		USpotLightComponent* L = NewObject<USpotLightComponent>(A);
		L->SetMobility(EComponentMobility::Movable);
		A->SetRootComponent(L);
		L->RegisterComponent();
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(Candela);
		L->SetLightColor(FLinearColor(1.f, 0.95f, 0.86f));
		L->SetOuterConeAngle(Outer);
		L->SetInnerConeAngle(Outer * 0.6f);
		L->SetAttenuationRadius(9000.f);
		L->SetCastShadows(bShadows);
		TrainingRoomActors.Add(A);
	};
	for (int32 i = 0; i < 6; ++i)
	{
		// narrow beams from the mast heads (3 m out in front: the head's geometry would shadow them),
		// aimed at the pitch, so the stands under them are not blown out
		const FVector Aim(Masts[i].X * 0.15f, Masts[i].Y * 0.15f, 0.f);
		const FVector Dir = (Aim - Masts[i]).GetSafeNormal();
		Spot(Masts[i] + Dir * 300.f, Dir, Flood * 0.35f, 28.f, i < 2); // two shadow casters: players get a shadow
	}
	// warm glow on the stands, the gate and blue light off the waterfalls
	const float Warm = CVarNightWarm.GetValueOnGameThread();
	const FLinearColor Amber(1.f, 0.62f, 0.3f);
	for (float X = -2000.f; X <= 2000.f; X += 800.f) Point(FVector(X, -2400.f, 350.f), Amber, Warm, 1400.f);
	for (float Sx : {-1.f, 1.f})
		for (float Y = -1200.f; Y <= 1200.f; Y += 800.f) Point(FVector(Sx * 3150.f, Y, 350.f), Amber, Warm, 1400.f);
	Point(FVector(0.f, -2000.f, 700.f), Amber, Warm * 4.f, 1800.f);
	for (float Sx : {-1.f, 1.f}) Point(FVector(Sx * 2700.f, -2000.f, 300.f), FLinearColor(0.3f, 0.7f, 1.f), Warm * 2.f, 1500.f);
}

// Zoo arena props from the packs and Meshy (SourceArt/Environment/Zoo/Meshy, Scripts/meshy_generate.py).
// Positions are in pitch space (cm): the far side is -Y, the ends are +-X.
static TAutoConsoleVariable<float> CVarZooPropYaw(TEXT("mf.Zoo.PropYaw"), 0.f, TEXT("Yaw added to the Meshy props (their authored facing); applied on the next start"));

static UStaticMesh* ZooMesh(const FString& Folder)
{
	// Scripts/import_zoo_arena.py puts <Folder>/<Name>.glb at <Folder>/<Name>/StaticMeshes/<Name>;
	// the asset registry is only a fallback (in -game it is not scanned yet when a match starts)
	const FString Name = FPaths::GetCleanFilename(Folder);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s/%s/StaticMeshes/%s.%s"), *Folder, *Name, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		return M;
	TArray<FAssetData> Found;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByPath(FName(*Folder), Found, true);
	for (const FAssetData& A : Found)
		if (A.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName()) return Cast<UStaticMesh>(A.GetAsset());
	return nullptr;
}

static void SpawnZooProps(UWorld* World, TArray<TObjectPtr<AActor>>& Out)
{
	const float PropYaw = CVarZooPropYaw.GetValueOnGameThread();
	auto Meshy = [](const TCHAR* Name) { return ZooMesh(FString::Printf(TEXT("/Game/Environment/Zoo/Meshy/%s"), Name)); };
	auto Rock = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/LP_RocksandCliffs/Meshes/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet); };
	// uniform scale that makes the mesh Height tall, and the lift that puts its bottom on Z
	auto Fit = [](const UStaticMesh* M, float Height, float& OutScale, float& OutLift)
	{
		const FBox B = M->GetBoundingBox();
		OutScale = Height / FMath::Max(1.f, float(B.GetSize().Z));
		OutLift = -float(B.Min.Z) * OutScale;
	};
	auto Place = [&](UStaticMesh* M, const FVector& At, float Yaw, float Scale, bool bQuery) -> AStaticMeshActor*
	{
		if (!M) return nullptr;
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, FRotator(0.f, Yaw, 0.f));
		if (!A) return nullptr;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(bQuery ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Visibility, bQuery ? ECR_Block : ECR_Ignore);
		A->SetActorScale3D(FVector(Scale));
		Out.Add(A);
		return A;
	};
	auto Statue = [&](const TCHAR* Name, const FVector& At, float Yaw, float Height)
	{
		UStaticMesh* M = Meshy(Name);
		if (!M) return;
		float S, Lift; Fit(M, Height, S, Lift);
		// stand it on whatever is below (a cliff top), found with a trace against the rocks
		FHitResult Hit; FVector Ground = At;
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 3000.f), At - FVector(0.f, 0.f, 500.f), ECC_Visibility)) Ground = Hit.ImpactPoint;
		Place(M, Ground + FVector(0.f, 0.f, Lift), Yaw + PropYaw, S, false);
	};
	auto Instanced = [&](UStaticMesh* M) -> UHierarchicalInstancedStaticMeshComponent*
	{
		if (!M) return nullptr;
		AActor* A = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
		A->SetRootComponent(Root); Root->RegisterComponent();
		UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(A);
		H->SetStaticMesh(M);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetupAttachment(Root); H->RegisterComponent();
		Out.Add(A);
		return H;
	};

	// far corners: cliffs (they take the traces), the near corners: low rocks
	struct FRockSpot { const TCHAR* Name; FVector At; float Yaw; float Scale; };
	const FRockSpot Rocks[] = {
		{TEXT("SM_Rock_Large_01"), {-3150.f, -2600.f, 0.f}, 30.f, 0.8f}, {TEXT("sm_cliff_13"), {-2650.f, -2250.f, 0.f}, 200.f, 0.55f},
		{TEXT("SM_Cliff_02"), {-3500.f, -1750.f, 0.f}, 110.f, 0.45f},   {TEXT("sm_rock_large_05"), {-2450.f, -2900.f, 0.f}, 0.f, 0.7f},
		{TEXT("sm_rock_large_03"), {3150.f, -2600.f, 0.f}, 160.f, 0.8f}, {TEXT("SM_Cliff_04"), {2650.f, -2250.f, 0.f}, 20.f, 0.5f},
		{TEXT("sm_cliff_14"), {3500.f, -1750.f, 0.f}, 250.f, 0.45f},     {TEXT("sm_rock_large_16"), {2450.f, -2900.f, 0.f}, 90.f, 0.9f},
		{TEXT("sm_cliff_15"), {-3300.f, 2200.f, 0.f}, 60.f, 0.35f},     {TEXT("sm_cliff_16"), {3300.f, 2200.f, 0.f}, 300.f, 0.35f},
	};
	for (const FRockSpot& R : Rocks) Place(Rock(R.Name), R.At, R.Yaw, R.Scale, true);

	// statues: lion on the left cliff (screen left is -X from the match camera), giraffe on the right
	Statue(TEXT("LionStatue"), FVector(-3150.f, -2550.f, 0.f), 60.f, 380.f);
	Statue(TEXT("GiraffeStatue"), FVector(3150.f, -2550.f, 0.f), 120.f, 700.f);
	Statue(TEXT("PolarBear"), FVector(-2500.f, -1650.f, 0.f), 40.f, 190.f);
	Statue(TEXT("Elephant"), FVector(3300.f, -1250.f, 0.f), 200.f, 330.f);
	for (int32 i = 0; i < 5; ++i)
		Statue(TEXT("Flamingo"), FVector(2750.f + (i % 3) * 110.f, -1500.f + (i / 3) * 120.f, 15.f), 150.f + i * 35.f, 150.f);

	// floodlight masts where the spot lights hang (SetNight), 20 m
	if (UStaticMesh* M = Meshy(TEXT("Floodlight")))
	{
		float S, Lift; Fit(M, 2150.f, S, Lift);
		const FVector Masts[6] = { {-1500.f, -3250.f, 0.f}, {1500.f, -3250.f, 0.f}, {-3900.f, -900.f, 0.f}, {-3900.f, 900.f, 0.f}, {3900.f, -900.f, 0.f}, {3900.f, 900.f, 0.f} };
		for (const FVector& At : Masts)
			if (AStaticMeshActor* Mast = Place(M, At + FVector(0.f, 0.f, Lift), (FVector(0.f, 0.f, 0.f) - At).Rotation().Yaw + PropYaw, S, false))
				Mast->GetStaticMeshComponent()->SetCastShadow(false); // the spot lights hang in its lamp head
	}

	FRandomStream Rand(1234);
	// palms: behind the far stands, round the cliffs, beyond the ends and behind the near side
	TArray<UHierarchicalInstancedStaticMeshComponent*> Palms;
	for (const TCHAR* Name : {TEXT("Palm"), TEXT("Palm_2"), TEXT("Palm_3")})
		if (UHierarchicalInstancedStaticMeshComponent* P = Instanced(Meshy(Name))) Palms.Add(P);
	TArray<UHierarchicalInstancedStaticMeshComponent*> Bushes;
	for (const TCHAR* Name : {TEXT("Bush"), TEXT("Bush_2"), TEXT("Bush_3")})
		if (UHierarchicalInstancedStaticMeshComponent* B = Instanced(Meshy(Name))) Bushes.Add(B);
	if (Palms.Num() > 0)
	{
		TArray<FVector> Spots;
		for (float X = -2400.f; X <= 2400.f; X += 450.f) Spots.Add(FVector(X + Rand.FRandRange(-120.f, 120.f), -3400.f + Rand.FRandRange(-150.f, 150.f), 0.f));
		for (float Sx : {-1.f, 1.f})
		{
			for (float Y = -1600.f; Y <= 1600.f; Y += 500.f) Spots.Add(FVector(Sx * 4050.f + Rand.FRandRange(-100.f, 100.f), Y, 0.f));
			for (int32 i = 0; i < 5; ++i) Spots.Add(FVector(Sx * Rand.FRandRange(2300.f, 3900.f), Rand.FRandRange(-3500.f, -3000.f), 0.f));
		}
		for (float X = -3600.f; X <= 3600.f; X += 600.f) Spots.Add(FVector(X, 3700.f + Rand.FRandRange(-100.f, 100.f), 0.f));
		for (const FVector& P : Spots)
		{
			UHierarchicalInstancedStaticMeshComponent* H = Palms[Rand.RandRange(0, Palms.Num() - 1)];
			float S, Lift; Fit(H->GetStaticMesh(), 900.f, S, Lift);
			const float K = S * Rand.FRandRange(0.85f, 1.2f);
			H->AddInstance(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), P + FVector(0.f, 0.f, -float(H->GetStaticMesh()->GetBoundingBox().Min.Z) * K), FVector(K)), true);
			// tropical undergrowth round the foot of every palm
			for (int32 b = 0; b < 2 && Bushes.Num() > 0; ++b)
			{
				UHierarchicalInstancedStaticMeshComponent* B = Bushes[Rand.RandRange(0, Bushes.Num() - 1)];
				float BS, BL; Fit(B->GetStaticMesh(), Rand.FRandRange(120.f, 220.f), BS, BL);
				const FVector At = P + FVector(Rand.FRandRange(-200.f, 200.f), Rand.FRandRange(-200.f, 200.f), BL);
				B->AddInstance(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), At, FVector(BS)), true);
			}
		}
	}
	// lanterns round the pools and the gate
	if (UStaticMesh* M = Meshy(TEXT("Lantern")))
	{
		float S, Lift; Fit(M, 280.f, S, Lift);
		const FVector Spots[] = { {-600.f, -2050.f, 0.f}, {600.f, -2050.f, 0.f}, {-2250.f, -1450.f, 0.f}, {2250.f, -1450.f, 0.f}, {-2900.f, -1300.f, 0.f}, {2900.f, -1300.f, 0.f} };
		for (const FVector& P : Spots) Place(M, P + FVector(0.f, 0.f, Lift), PropYaw, S, false);
	}

	// the crowd: tigers, pandas and wolves on every tier row of the stands (SM_Zoo_Stands layout:
	// rows 85 cm deep, 45 cm rise from 1.3 m; the far side has the gate gap at |x| < 4.5 m)
	// every species and variant that was generated (Scripts/meshy_generate.py): a mixed crowd
	TArray<UHierarchicalInstancedStaticMeshComponent*> Fans;
	TArray<float> FanScale, FanLift;
	for (const TCHAR* Kind : {TEXT("Tiger"), TEXT("Panda"), TEXT("Wolf"), TEXT("Lion"), TEXT("Elephant"), TEXT("Giraffe"), TEXT("Monkey"), TEXT("Fox"), TEXT("Bear")})
		for (const TCHAR* Var : {TEXT(""), TEXT("_2"), TEXT("_3")})
			if (UHierarchicalInstancedStaticMeshComponent* H = Instanced(Meshy(*FString::Printf(TEXT("Fan%s%s"), Kind, Var))))
			{
				float Sc, Li; Fit(H->GetStaticMesh(), 115.f, Sc, Li);
				Fans.Add(H); FanScale.Add(Sc); FanLift.Add(Li);
			}
	auto Seat = [&](const FVector& P, float Yaw)
	{
		if (Rand.FRand() < 0.12f || Fans.Num() == 0) return; // an empty seat here and there
		const int32 K = Rand.RandRange(0, Fans.Num() - 1);
		const float Sc = FanScale[K] * Rand.FRandRange(0.9f, 1.1f);
		// Meshy's figures face their local -Y: -90 turns them to the pitch
		Fans[K]->AddInstance(FTransform(FRotator(0.f, Yaw - 90.f + PropYaw + Rand.FRandRange(-15.f, 15.f), 0.f), P + FVector(0.f, 0.f, FanLift[K] * Sc / FanScale[K]), FVector(Sc)), true);
	};
	for (int32 Row = 0; Row < 10; ++Row)
		for (float X = -2150.f; X <= 2150.f; X += 75.f)
			if (FMath::Abs(X) > 470.f) Seat(FVector(X + Rand.FRandRange(-8.f, 8.f), -(2038.f + Row * 85.f + 45.f), 130.f + Row * 45.f), 90.f);
	for (float Sx : {-1.f, 1.f})
		for (int32 Row = 0; Row < 8; ++Row)
			for (float Y = -1350.f; Y <= 1350.f; Y += 75.f)
				Seat(FVector(Sx * (2838.f + Row * 85.f + 45.f), Y + Rand.FRandRange(-8.f, 8.f), 130.f + Row * 45.f), Sx > 0.f ? 180.f : 0.f);
}

// The match is played in the training court too: the model is stretched to the match
// pitch, its own goals are hidden (they would stretch) and real-size copies stand in.
// The generated field keeps its invisible floor, boards and goal triggers for gameplay.
void ASoccerGameMode::SpawnMatchCourt()
{
	SetNight(false);
	for (AActor* Actor : TrainingRoomActors)
	{
		if (Actor) Actor->Destroy();
	}
	TrainingRoomActors.Reset();
	bSideCamProps = false;
	UStaticMesh* CourtMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Environment/Training/SM_TrainingCourt.SM_TrainingCourt"));
	UStaticMesh* GoalMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Environment/Training/SM_CourtGoal.SM_CourtGoal"));
	UMaterialInterface* Hidden = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Training/M_Invisible.M_Invisible"));
	UMaterialInterface* Grass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Training/M_PitchGrass.M_PitchGrass"));
	if (!CourtMesh || !GoalMesh || !Hidden || !Grass)
	{
		for (AActor* Actor : GeneratedFieldActors)
		{
			if (Actor) { Actor->SetActorHiddenInGame(false); Actor->SetActorEnableCollision(true); }
		}
		return;
	}
	for (AActor* Actor : GeneratedFieldActors)
	{
		if (!Actor) continue;
		Actor->SetActorHiddenInGame(true); // visuals off; floor, boards and goal triggers keep working
		Actor->SetActorEnableCollision(true);
	}
	auto SpawnMesh = [this](UStaticMesh* Mesh, const FVector& Location, float Yaw, const FVector& Scale)
	{
		AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator(0.f, Yaw, 0.f));
		if (!A) return (AStaticMeshActor*)nullptr;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		A->SetActorEnableCollision(false);
		A->SetActorScale3D(Scale);
		TrainingRoomActors.Add(A);
		return A;
	};
	// Court lines sit at 10.48 x 6.10 m in the model
	if (AStaticMeshActor* Court = SpawnMesh(CourtMesh, FVector::ZeroVector, 0.f, FVector(HalfLength / 1048.f, HalfWidth / 610.f, 1.f)))
	{
		UStaticMeshComponent* C = Court->GetStaticMeshComponent();
		C->SetCastShadow(false);
		C->SetMaterial(1, Hidden); // the model's own goals
		UMaterialInstanceDynamic* Pitch = UMaterialInstanceDynamic::Create(Grass, Court);
		const TPair<const TCHAR*, float> Field[] = {
			{TEXT("FieldHX"), HalfLength}, {TEXT("FieldHY"), HalfWidth},
			{TEXT("FieldPBD"), PenaltyDepth}, {TEXT("FieldPBW"), GoalHalfWidth + 400.f},
			{TEXT("FieldGAD"), 200.f}, {TEXT("FieldGAW"), GoalHalfWidth + 150.f},
			{TEXT("FieldCR"), 300.f}, {TEXT("FieldPSD"), PenaltyDepth}, {TEXT("FieldCA"), 60.f},
			{TEXT("FieldStyle"), IsFreeTraining() ? 0.f : 1.f}}; // match: the Goals arena's blue court
		for (const TPair<const TCHAR*, float>& F : Field) Pitch->SetScalarParameterValue(F.Key, F.Value);
		C->SetMaterial(0, Pitch);
		if (!IsFreeTraining())
		{
			// The match camera looks from the +Y side: the near fence of the court model
			// would fill the bottom of the screen. Its materials clip beyond ClipY.
			for (int32 Slot = 2; Slot < C->GetNumMaterials(); ++Slot)
			{
				UMaterialInstanceDynamic* Clip = UMaterialInstanceDynamic::Create(C->GetMaterial(Slot), Court);
				Clip->SetScalarParameterValue(TEXT("ClipY"), HalfWidth + 200.f);
				C->SetMaterial(Slot, Clip);
			}
		}
	}
	// Goal mesh: origin on the goal line, mouth 3.28 m wide, crossbar 1.58 m, 0.92 m deep
	const FVector GoalScale(GoalDepth / 92.f, GoalHalfWidth / 164.f, GoalHeight / 158.f);
	for (int32 Side : {-1, 1})
	{
		SpawnMesh(GoalMesh, FVector(Side * HalfLength, 0.f, 0.f), Side > 0 ? 0.f : 180.f, GoalScale);
	}
	// Night-zoo arena (SourceArt/Environment/Zoo, Scripts/import_zoo_arena.py): boards on the
	// ball's walls (+3 m), stands, animal crowd, the ZOO gate and decor, authored in pitch space.
	// The glTF import flips Y, so it is turned round to put the gate on the far side (-Y).
	TArray<UStaticMesh*> Zoo;
	// Blender parts; the crowd, rocks, statues, palms and masts come from the packs and Meshy (SpawnZooProps)
	for (const TCHAR* Part : {TEXT("Boards"), TEXT("Stands"), TEXT("Gate"), TEXT("Water"), TEXT("Skyline")})
	{
		if (UStaticMesh* M = ZooMesh(FString::Printf(TEXT("/Game/Environment/Zoo/SM_Zoo_%s"), Part)))
			Zoo.Add(M);
	}
	if (Zoo.Num() >= 3)
	{
		for (UStaticMesh* M : Zoo) SpawnMesh(M, FVector::ZeroVector, 180.f, FVector::OneVector);
		SpawnZooProps(GetWorld(), TrainingRoomActors);
		// the old court's own stands, fence and lights give way to the arena; its grass stays
		if (AStaticMeshActor* Court = TrainingRoomActors.Num() > 0 ? Cast<AStaticMeshActor>(TrainingRoomActors[0]) : nullptr)
		{
			UStaticMeshComponent* C = Court->GetStaticMeshComponent();
			for (int32 Slot = 2; Slot < C->GetNumMaterials(); ++Slot) C->SetMaterial(Slot, Hidden);
		}
		// dark plaza under the whole arena (the court floor ends short of the stands)
		if (AStaticMeshActor* Ground = SpawnMesh(CubeMesh, FVector(0.f, 0.f, -8.f), 0.f, FVector(120.f, 100.f, 0.1f)))
			Paint(Ground->GetStaticMeshComponent(), FLinearColor(0.035f, 0.035f, 0.045f));
		SetNight(true);
		return;
	}
	SpawnEnvironment(HalfLength / 1048.f, HalfWidth / 610.f);
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
	UE_LOG(LogTemp, Display, TEXT("Soccer possession: pawn=%s controller=%s play=%d movement=%d"),
		*GetNameSafe(PC->GetPawn()), *GetNameSafe(Target ? Target->GetController() : nullptr),
		bPlayActive ? 1 : 0, Target ? int32(Target->GetCharacterMovement()->MovementMode) : -1);
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
	GeneratedFieldActors.Add(A);
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
		GeneratedFieldActors.Add(Goal);
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
	PlaySfx(ESoccerSound::Net, 1.2f); // the ball in the net, whatever its speed
	const ASoccerPlayer* Scorer = Ball->LastKicker.Get();
	EventText = Scorer ? FText::FromString(FString(TEXT("ГОЛ!  ")) + Scorer->Info.Name).ToUpper() : FText::FromString(TEXT("ГОЛ!"));
	CelebrateUntil = GetWorld()->GetTimeSeconds() + 5.f; // cards and radar step aside until the restart
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

// The whole ball over a line (and not into the goal): kick-in, corner or goal kick, as in
// futsal. Training has no opponents to give it to: the ball is simply put back in.
void ASoccerGameMode::CheckBallOut()
{
	if (!bPlayActive || !Ball || Restart != ESoccerRestart::None) return;
	const FVector B = Ball->GetActorLocation();
	const bool bOverSide = FMath::Abs(B.Y) > HalfWidth + BallRadius;
	const bool bInMouth = FMath::Abs(B.Y) < GoalHalfWidth && B.Z < GoalHeight;
	const bool bOverEnd = FMath::Abs(B.X) > HalfLength + BallRadius && !bInMouth;
	if (!bOverSide && !bOverEnd) return;

	if (IsFreeTraining())
	{
		// Let it roll off the pitch like a real ball, then restart everything, as after a goal
		if (TrainingBallOutTime < 0.f)
		{
			TrainingBallOutTime = GetWorld()->GetTimeSeconds();
			GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::ResetPositions, 1.2f, false);
		}
		return;
	}
	const ASoccerPlayer* Last = Ball->LastKicker.Get();
	const int32 LastTeam = Last ? Last->Team : (PossessionTeam >= 0 ? PossessionTeam : 0);
	const int32 Team = 1 - LastTeam; // the other side restarts
	FVector Spot;
	ASoccerPlayer* Taker = nullptr;
	FString Label;
	if (bOverSide)
	{
		Spot = FVector(FMath::Clamp<double>(B.X, -HalfLength + 30.0, HalfLength - 30.0), FMath::Sign(B.Y) * (HalfWidth - 15.f), BallRadius);
		Label = TEXT("АУТ");
	}
	else
	{
		const float EndSign = FMath::Sign(B.X);
		const int32 Defending = EndSign > 0.f ? 1 : 0; // the +X goal is team 1's
		if (Team != Defending)
		{
			Spot = FVector(EndSign * (HalfLength - 15.f), FMath::Sign(B.Y) * (HalfWidth - 15.f), BallRadius);
			Label = TEXT("УГЛОВОЙ");
		}
		else
		{
			// Goal kick: the keeper puts it back in play from his area
			Spot = FVector(EndSign * (HalfLength - 150.f), 0.f, BallRadius);
			Label = TEXT("ОТ ВОРОТ");
			for (ASoccerPlayer* P : Players)
			{
				if (P && P->Team == Team && P->bGoalkeeper) Taker = P;
			}
		}
	}
	if (!Taker)
	{
		float Best = TNumericLimits<float>::Max();
		for (ASoccerPlayer* P : Players)
		{
			if (!P || P->Team != Team || P->bGoalkeeper) continue;
			const float D = FVector::DistSquared2D(P->GetActorLocation(), Spot);
			if (D < Best) { Best = D; Taker = P; }
		}
	}
	if (!Taker) return;
	bPlayActive = false;
	// The ball rolls on out of play; StartSetPiece puts it on the spot.
	SetPieceSpot = Spot;
	bPendingKickIn = bOverSide;
	bPendingCorner = Label == TEXT("УГЛОВОЙ");
	SetPieceTaker = Taker;
	bPenalty = false;
	bOutOfPlayRestart = true;
	EventText = FText::FromString(Label);
	EventBanner = 1.5f;
	PlaySfx(ESoccerSound::Whistle);
	GetWorldTimerManager().SetTimer(ResetTimer, this, &ASoccerGameMode::StartSetPiece, 1.2f, false);
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
		P->SnapBodyYaw();
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
	if (!bPenalty && !bOutOfPlayRestart && FVector::Dist2D(SetPieceSpot, GoalCenter) < 1400.f)
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

	// Исполнитель — пострадавший: у мяча, лицом к воротам соперника. On a corner he stands beside
	// the ball, off the pitch (Goals): behind it he stood in the camera's line to the box.
	if (bPendingCorner)
		Place(Taker, SetPieceSpot + FVector(0.f, FMath::Sign(SetPieceSpot.Y) * 75.f, 0.f) - ToGoal * 35.f, GoalCenter);
	else
		Place(Taker, SetPieceSpot - ToGoal * 70.f, GoalCenter);
	Taker->GainBall();
	Taker->ProtectTime = 1.f; // соперник не отбирает мяч в первую секунду
	BeginRestart(bPenalty ? ESoccerRestart::Penalty : ESoccerRestart::FreeKick, Taker, SetPieceSpot);
	bKickIn = bPendingKickIn;
	bPendingKickIn = false;
	bCorner = bPendingCorner;
	bPendingCorner = false;
	bGoalKick = bOutOfPlayRestart && Taker->bGoalkeeper; // from the ground in his area, not from the hands
	Taker->bBallAtFeet = bGoalKick;
	if (bKickIn && Taker->Team == HumanTeam)
	{
		// FIFA-style kick-in the way the player wants it: the team-mate takes it, you are
		// the nearest outfield player and get open; the pass comes to you (TakeRestart).
		ASoccerPlayer* Mate = nullptr;
		for (ASoccerPlayer* P : Players)
		{
			if (!P || P == Taker || P->Team != HumanTeam || P->bGoalkeeper) continue;
			if (!Mate || FVector::DistSquared2D(P->GetActorLocation(), SetPieceSpot) < FVector::DistSquared2D(Mate->GetActorLocation(), SetPieceSpot))
				Mate = P;
		}
		if (ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController()); Mate && PC) PC->PossessPlayer(Mate);
		Taker->PrepareRestart(1.6f); // time to get open
	}

	SetPieceTaker.Reset();
	bOutOfPlayRestart = false;
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

	CelebrateUntil = -1.f;
	for (ASoccerPlayer* P : Players)
	{
		P->ResetToHome();
	}
	Ball->ResetBall(FVector(0.f, 0.f, BallRadius));
	SetPieceTaker.Reset();
	PendingPasser.Reset();
	bShotPending = false;
	bPlayActive = true;
	if (IsFreeTraining())
	{
		TrainingBallOutTime = -1.f;
		bTrainingCamValid = false; // the camera cuts to the restart instead of flying there
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			if (PC->PlayerCameraManager) PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.35f, FLinearColor::Black);
		PossessHuman(); // back to the starter: the team-mate went home and must not stand on the spot
		if (ASoccerPlayer* P = GetHumanPlayer())
		{
			// Every goal (and every ball out of play) restarts from the centre spot
			P->SetActorLocation(FVector(-60.f, 0.f, 92.f), false, nullptr, ETeleportType::TeleportPhysics);
			P->SetActorRotation(FRotator(0.f, 0.f, 0.f));
			Ball->ResetBall(FVector(0.f, 0.f, BallRadius));
			Ball->SetOwnerPlayer(P);
		}
		return;
	}

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
		// Your kick-off is yours to take (the others wait until the ball is played)
		if (Kicker->Team == HumanTeam)
		{
			if (ASoccerPlayerController* PC = Cast<ASoccerPlayerController>(GetWorld()->GetFirstPlayerController()))
				PC->PossessPlayer(Kicker);
		}
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
	bKickIn = bCorner = bGoalKick = false; // StartSetPiece marks kick-ins, corners and goal kicks after this
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
	TickIntro(DeltaSeconds);
	if (CVarTrace.GetValueOnGameThread() != 0 && bPlayActive && Ball && Ball->OwnerPlayer && !IsFreeTraining())
	{
		// Open passing options for whoever has the ball: team-mates 5-22 m away whose lane
		// no opponent is within 1.5 m of (harness metric for the support variants)
		static float NextOpt = 0.f;
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextOpt)
		{
			NextOpt = Now + 0.25f;
			const ASoccerPlayer* C = Ball->OwnerPlayer;
			const FVector From = C->GetActorLocation();
			int32 Open = 0, Ahead = 0;
			float Near = 1e9f, Press = 1e9f, MinX = From.X, MaxX = From.X, MinY = From.Y, MaxY = From.Y;
			for (const ASoccerPlayer* O : Players)
				if (O && O->Team != C->Team && !O->bGoalkeeper) Press = FMath::Min(Press, float(FVector::Dist2D(From, O->GetActorLocation())));
			for (const ASoccerPlayer* M : Players)
			{
				if (!M || M == C || M->Team != C->Team || M->bGoalkeeper) continue;
				const FVector To = M->GetActorLocation();
				const float D = FVector::Dist2D(From, To);
				Near = FMath::Min(Near, D);
				MinX = FMath::Min(MinX, float(To.X)); MaxX = FMath::Max(MaxX, float(To.X));
				MinY = FMath::Min(MinY, float(To.Y)); MaxY = FMath::Max(MaxY, float(To.Y));
				if (D < 500.f || D > 2200.f) continue;
				float Lane = 1e9f;
				for (const ASoccerPlayer* O : Players)
					if (O && O->Team != C->Team) Lane = FMath::Min(Lane, DistToSegment2D(O->GetActorLocation(), From, To));
				if (Lane < 150.f) continue;
				++Open;
				if ((To.X - From.X) * C->AttackSign() > 200.f) ++Ahead;
			}
			UE_LOG(LogTemp, Display, TEXT("MFOPT team %d open %d ahead %d variant %d near %.0f press %.0f width %.0f depth %.0f"), C->Team, Open, Ahead,
				SoccerVariants::Get(SoccerVariants::Support), Near, Press, MaxY - MinY, MaxX - MinX);
			FString Mates;
			for (const ASoccerPlayer* M : Players)
				if (M && M != C && M->Team == C->Team && !M->bGoalkeeper)
					Mates += FString::Printf(TEXT(" %d:%.0f,%.0f%s"), M->RosterIndex, (M->GetActorLocation().X - From.X) * C->AttackSign(), M->GetActorLocation().Y - From.Y, M->bRunningInBehind ? TEXT("R") : TEXT(""));
			UE_LOG(LogTemp, Display, TEXT("MFDBG team %d carrier %d gk %d x %.0f human %d |%s"), C->Team, C->RosterIndex, C->bGoalkeeper ? 1 : 0, From.X * C->AttackSign(), C->IsPlayerControlled() ? 1 : 0, *Mates);
		}
	}
	if (CVarTestGoal.GetValueOnGameThread() > 0 && bPlayActive && Ball)
	{
		CVarTestGoal->Set(0, ECVF_SetByConsole); // ExecCmds set it at console priority
		Ball->LastKicker = GetHumanPlayer();
		OnGoalScored(HumanTeam);
	}
	if (const int32 TestSetPiece = CVarTestCorner.GetValueOnGameThread(); TestSetPiece > 0 && bPlayActive && Ball && GetHumanPlayer())
	{
		CVarTestCorner->Set(0, ECVF_SetByConsole);
		bPlayActive = false;
		const bool bCornerTest = TestSetPiece == 1;
		SetPieceSpot = bCornerTest ? FVector(HalfLength - 15.f, HalfWidth - 15.f, BallRadius) : FVector(HalfLength - 900.f, 300.f, BallRadius);
		SetPieceTaker = GetHumanPlayer();
		bPenalty = false;
		bOutOfPlayRestart = bCornerTest;
		bPendingCorner = bCornerTest;
		StartSetPiece();
	}
	if (!Ball) return;

	if (CVarTrace.GetValueOnGameThread() != 0)
	{
		if (const ASoccerPlayer* P = GetHumanPlayer())
		{
			const USkeletalMeshComponent* M = P->GetMesh();
			const FVector C = P->GetActorLocation(), V = P->GetVelocity(), B = Ball->GetActorLocation();
			const FVector H = M->GetSocketLocation(TEXT("Hips")), L = M->GetSocketLocation(TEXT("LeftFoot")), R = M->GetSocketLocation(TEXT("RightFoot"));
			UE_LOG(LogTemp, Display, TEXT("MFTRACE %.4f %.4f C %.1f %.1f V %.1f %.1f H %.1f %.1f %.1f L %.1f %.1f %.1f R %.1f %.1f %.1f B %.1f %.1f %.1f own %d"),
				GetWorld()->GetTimeSeconds(), DeltaSeconds, C.X, C.Y, V.X, V.Y, H.X, H.Y, H.Z, L.X, L.Y, L.Z, R.X, R.Y, R.Z, B.X, B.Y, B.Z, Ball->OwnerPlayer == P ? 1 : 0);
		}
	}
	if (const float Every = CVarShotEvery.GetValueOnGameThread(); Every > 0.f)
	{
		NextTraceShot -= DeltaSeconds;
		if (NextTraceShot <= 0.f)
		{
			NextTraceShot = Every;
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("MFShot_%05d"), TraceShotIndex++), true, false); // with the HUD
		}
	}

	EventBanner = FMath::Max(0.f, EventBanner - DeltaSeconds);
	OohCooldown = FMath::Max(0.f, OohCooldown - DeltaSeconds);

	// Таймер идёт только во время игры (пауза после гола не считается)
	if (bInMatch && bPlayActive)
	{
		if (!IsFreeTraining()) TimeLeft -= DeltaSeconds;

		// Владение мячом
		if (Ball->OwnerPlayer)
		{
			PossessionTeam = Ball->OwnerPlayer->Team;
		}
		if (PossessionTeam >= 0)
		{
			Stats[PossessionTeam].Possession += DeltaSeconds;
		}

		CheckBallOut();
		// Стандарт закончился: мяч ввели в игру (удар или мяч сдвинулся), исполнитель его потерял
		if (Restart != ESoccerRestart::None)
		{
			const ASoccerPlayer* Taker = RestartTaker.Get();
			const bool bKicked = Ball->GetLastKickTime() > RestartStartTime;
			const bool bMoved = FVector::Dist2D(Ball->GetActorLocation(), SetPieceSpot) > 150.f;
			// The 8 s give-up only unsticks an AI taker: yours waits as long as you like (it ran out on
			// a waiting kick-off and the team-mates ran into the opponents' half before the ball was played)
			const bool bGiveUp = Taker && !Taker->IsPlayerControlled() && GetWorld()->GetTimeSeconds() - RestartStartTime > 8.f;
			if (!IsIntro() && (!Taker || !Taker->HasBall() || bKicked || bMoved || bGiveUp))
			{
				Restart = ESoccerRestart::None;
				RestartTaker.Reset();
			}
		}

		TeamAITimer -= DeltaSeconds;
		if (!IsFreeTraining() && TeamAITimer <= 0.f)
		{
			TeamAITimer = 0.2f;
			UpdateTeamAI();
		}

		if (!IsFreeTraining() && TimeLeft <= 0.f)
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
	if (UpdateIntroCamera()) return;

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
	if (false)
	{
		// Broadcast view from the side, like the match camera, scaled to the small court:
		// follows the ball along the pitch and only a little across it.
		// On the player, leaning a little toward the ball when it is away from him.
		const ASoccerPlayer* Human = GetHumanPlayer();
		FVector Focus = Human ? FMath::Lerp(Human->GetActorLocation(), Ball->GetActorLocation(), 0.25f) : Ball->GetActorLocation();
		Focus.X = FMath::Clamp<double>(Focus.X, -650.0, 650.0);
		Focus.Y = FMath::Clamp<double>(Focus.Y * 0.4 - 120.0, -260.0, 160.0);
		Focus.Z = 0.f;
		CamFocus = FMath::VInterpTo(CamFocus, Focus, Dt, 2.5f);
		// Steep enough to look over the near-side fence instead of through it.
		const FRotator Rot(-50.f, CameraYaw, 0.f);
		Camera->GetCameraComponent()->SetFieldOfView(55.f);
		Camera->SetActorLocationAndRotation(CamFocus - Rot.Vector() * 1750.f, Rot);
		return;
	}
	if (IsFreeTraining())
	{
		const ASoccerPlayer* P = GetHumanPlayer();
		if (!P) return;
		FVector Me = P->GetActorLocation();
		Me.Z = 0.f;
		const FVector Goal(HalfLength, 0.f, 0.f); // the training goal (its keeper stands there)
		const bool bSideCam = bTrainingWideCamera || CVarCamSide.GetValueOnGameThread() > 0;
		if (bSideCam != bSideCamProps)
		{
			// Trees, lamps and benches on the camera's side would stand in front of the side view
			bSideCamProps = bSideCam;
			for (AActor* Prop : TrainingRoomActors)
			{
				if (Prop && Prop->GetActorLocation().Y > HalfWidth + 100.f) Prop->SetActorHiddenInGame(bSideCam);
			}
		}
		if (bSideCam)
		{
			// R3: the Goals side camera, aimed between the player and the goal
			FVector Target(FMath::Lerp(Goal.X, Me.X, 0.5f), Me.Y * 0.25f, 0.f);
			TrainingCamFocus = FMath::VInterpTo(TrainingCamFocus.IsZero() ? Target : TrainingCamFocus, Target, Dt, 2.f);
			const FRotator Side(-CVarCamSidePitch.GetValueOnGameThread(), -90.f + CVarCamSideTurn.GetValueOnGameThread(), 0.f);
			Camera->GetCameraComponent()->SetFieldOfView(CVarCamSideFOV.GetValueOnGameThread());
			Camera->SetActorLocationAndRotation(TrainingCamFocus - Side.Vector() * CVarCamSideDist.GetValueOnGameThread() * 100.f, Side);
			bTrainingCamValid = false;
			return;
		}
		// Goals: behind the player on the line to the far post, closer as he nears the goal
		const float SideFrac = FMath::Clamp(float(Me.Y) / 300.f, -1.f, 1.f);
		const FVector Aim = Goal - FVector(0.f, SideFrac * GoalHalfWidth * CVarCamAimPost.GetValueOnGameThread(), 0.f);
		FVector ToAim = Aim - Me;
		ToAim.Z = 0.f;
		const float GoalDist = ToAim.Size() / 100.f;
		const float WantYaw = ToAim.Size() > 50.f ? float(ToAim.Rotation().Yaw) : TrainingCamYawNow;
		const float NearGoal = CVarCamNearGoal.GetValueOnGameThread(), FarGoal = CVarCamFarGoal.GetValueOnGameThread();
		const float Alpha = (GoalDist - NearGoal) / FMath::Max(1.f, FarGoal - NearGoal);
		const float WantDist = FMath::Clamp(FMath::Lerp(CVarCamNearDist.GetValueOnGameThread(), CVarCamFarDist.GetValueOnGameThread(), Alpha), 9.f, 17.f) * 100.f;
		if (!bTrainingCamValid)
		{
			TrainingCamYawNow = WantYaw;
			TrainingCamDist = WantDist;
			TrainingCamFocus = Me;
			bTrainingCamValid = true;
		}
		TrainingCamYawNow += FMath::FindDeltaAngleDegrees(TrainingCamYawNow, WantYaw) * (1.f - FMath::Exp(-CVarCamYawLag.GetValueOnGameThread() * Dt));
		TrainingCamDist = FMath::FInterpTo(TrainingCamDist, WantDist, Dt, 1.5f);
		TrainingCamFocus = FMath::VInterpTo(TrainingCamFocus, Me, Dt, CVarCamPosLag.GetValueOnGameThread());
		const float Elev = FMath::DegreesToRadians(CVarCamElev.GetValueOnGameThread());
		const FVector Heading = FRotator(0.f, TrainingCamYawNow, 0.f).Vector();
		FVector CamLoc = TrainingCamFocus - Heading * TrainingCamDist * FMath::Cos(Elev) + FVector(0.f, 0.f, TrainingCamDist * FMath::Sin(Elev));
		// Stay inside the fences (Goals' pitch is bigger, its camera never met them): past
		// them the fence mesh came between the camera and the player.
		CamLoc.X = FMath::Clamp<double>(CamLoc.X, -HalfLength - 200.0, HalfLength + 200.0);
		CamLoc.Y = FMath::Clamp<double>(CamLoc.Y, -HalfWidth - 150.0, HalfWidth + 150.0);
		Camera->GetCameraComponent()->SetFieldOfView(CVarCamFOV.GetValueOnGameThread());
		Camera->SetActorLocationAndRotation(CamLoc, FRotator(-CVarCamPitch.GetValueOnGameThread(), TrainingCamYawNow, 0.f));
		return;
	}
	if (false)
	{
		const ASoccerPlayer* P = GetHumanPlayer();
		if (!P) return;
		// Goals/FIFA training view: high behind the player, looking down the pitch at the
		// attacking goal, wide enough to read the whole box. The heading is fixed so the
		// stick never rotates with the camera; the focus leads the player toward the goal
		// and trails him smoothly.
		const FRotator TrainingView(-30.f, TrainingCameraYaw, 0.f);
		FVector Focus = P->GetActorLocation() + FVector(260.f, 0.f, 0.f);
		Focus.Y *= 0.75f; // stay a little toward the centre line, as broadcast cameras do
		Focus.Z = 0.f;
		TrainingCamFocus = FMath::VInterpTo(TrainingCamFocus.IsZero() ? Focus : TrainingCamFocus, Focus, Dt, 3.5f);
		// Near our own goal the camera would back out through the end fence; instead it
		// keeps its distance but rises and looks down more steeply.
		constexpr float TrainingCamDistance = 1350.f;
		const float BackLimitX = -HalfLength - 250.f; // just inside the end fence
		const float Back = FMath::Clamp(float(TrainingCamFocus.X - BackLimitX),
			180.f, TrainingCamDistance * FMath::Cos(FMath::DegreesToRadians(-TrainingView.Pitch)));
		FRotator View = TrainingView;
		View.Pitch = -FMath::RadiansToDegrees(FMath::Acos(Back / TrainingCamDistance));
		Camera->GetCameraComponent()->SetFieldOfView(62.f);
		Camera->SetActorLocationAndRotation(TrainingCamFocus - View.Vector() * TrainingCamDistance, View);
		return;
	}

	// Your corner (Goals): the taker's own eyes, looking into the box; the stick aims from there
	if (IsFirstPersonCorner())
	{
		// Goals corner view: low behind the taker's shoulder, looking across the box
		const FVector Box(FMath::Sign(SetPieceSpot.X) * (HalfLength - 450.f), 0.f, 0.f);
		const FVector D = (Box - SetPieceSpot).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, D);
		// Low and close behind the ball, the box in the middle of the frame and the ball low in it
		// (measured on a Goals corner): from 8 m back and 3.8 m up the box was a speck far away.
		const FVector Eye = SetPieceSpot - D * 650.f + Side * 60.f + FVector(0.f, 0.f, 300.f);
		// 15 deg down: the ball ~10 deg below the centre, the box ~8 deg above it
		const FRotator Look(-15.f, (Box - Eye).Rotation().Yaw, 0.f);
		MatchCamYaw = Look.Yaw;
		Camera->GetCameraComponent()->SetFieldOfView(60.f);
		Camera->SetActorLocationAndRotation(Eye, Look);
		return;
	}
	// Матч: the Goals broadcast camera from the side, following the ball
	if (!bSideCamProps)
	{
		// the camera stands on the +Y side: trees and lamps there would block the view
		bSideCamProps = true;
		for (AActor* Prop : TrainingRoomActors)
			if (Prop && Prop->GetActorLocation().Y > HalfWidth + 100.f) Prop->SetActorHiddenInGame(true);
	}
	FVector Target = Ball->GetActorLocation();
	Target.X = FMath::Clamp<double>(Target.X, -HalfLength + 400.f, HalfLength - 400.f);
	Target.Y = FMath::Clamp<double>(Target.Y * 0.4, -500.0, 500.0);
	Target.Z = 0.f;
	CamFocus = FMath::VInterpTo(CamFocus, Target, Dt, 2.5f);
	const float Turn = FMath::Clamp(float(CamFocus.X) / (HalfLength * 0.6f), -1.f, 1.f) * CVarMatchCamTurn.GetValueOnGameThread();
	MatchCamYaw = CameraYaw + Turn;
	const FRotator Rot(-CVarMatchCamPitch.GetValueOnGameThread(), MatchCamYaw, 0.f);
	Camera->GetCameraComponent()->SetFieldOfView(CVarMatchCamFOV.GetValueOnGameThread());
	Camera->SetActorLocationAndRotation(CamFocus - Rot.Vector() * CVarMatchCamDist.GetValueOnGameThread() * 100.f, Rot);
}

float ASoccerGameMode::GetControlCameraYaw() const
{
	// In free practice, movement is camera-relative and the camera follows the
	// player. Using the fixed broadcast yaw here made the stick disagree with view.
	if (IsFreeTraining() && Camera)
	{
		// camera yaw stays fixed while its position follows the player
		// the stick follows the Goals camera's heading; the R3 side view looks along -Y
		return bTrainingWideCamera ? -90.f + CVarCamSideTurn.GetValueOnGameThread() : TrainingCamYawNow;
	}
	return MatchCamYaw; // the stick turns with the match camera
}
