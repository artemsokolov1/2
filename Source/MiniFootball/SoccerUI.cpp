// Мини-футбол 5×5 — интерфейс на Slate: главное меню со всеми разделами, HUD матча и пауза.
// Всё строится кодом, без ассетов. Шрифт — стандартный Roboto движка (в нём есть кириллица).
// Каждая кнопка работает мышью, клавиатурой (стрелки + Enter) и геймпадом (стик/крестовина + A).
// Назад — B на геймпаде или Backspace.

#include "Soccer.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

using namespace Soccer;

namespace
{
// ============================================================================
//  Стиль: цвета, шрифты, кисти
// ============================================================================
FLinearColor ColLime()     { return FLinearColor(FColor(163, 230, 22)); }     // фирменный салатовый
FLinearColor ColInk()      { return FLinearColor(FColor(14, 16, 20, 235)); }  // тёмные панели
FLinearColor ColInkLight() { return FLinearColor(FColor(34, 38, 46, 240)); }
FLinearColor ColGray()     { return FLinearColor(FColor(150, 156, 165)); }

FSlateFontInfo FontBold(float Size)    { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }
FSlateFontInfo FontRegular(float Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }

FText Ru(const TCHAR* Str) { return FText::FromString(Str); }

// Белая кисть: цвет задаётся через BorderBackgroundColor
const FSlateBrush* WhiteBrush()
{
	static FSlateColorBrush Brush(FLinearColor::White);
	return &Brush;
}

// Кнопка без собственного фона — внешний вид рисует её содержимое
const FButtonStyle* FlatButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));
	return &Style;
}

TSharedRef<SWidget> Txt(const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Color = FLinearColor::White)
{
	return SNew(STextBlock).Text(Text).Font(Font).ColorAndOpacity(Color);
}

// Кнопка «горячая», если под курсором мыши или в фокусе геймпада/клавиатуры
bool IsHot(const TWeakPtr<SWidget>& Weak)
{
	const TSharedPtr<SWidget> W = Weak.Pin();
	return W.IsValid() && (W->IsHovered() || W->HasKeyboardFocus());
}

using FHotFn = TFunction<bool()>;
using FContentFn = TFunction<TSharedRef<SWidget>(const FHotFn&)>;

// Базовая кнопка: содержимое строится функцией, которой передаётся «горячесть» кнопки
TSharedRef<SButton> MakeButton(const TFunction<void()>& OnClick, const FContentFn& BuildContent)
{
	TSharedPtr<SButton> Button;
	SAssignNew(Button, SButton)
		.ButtonStyle(FlatButtonStyle())
		.ContentPadding(FMargin(0.f))
		.OnClicked_Lambda([OnClick]()
		{
			if (OnClick) OnClick();
			return FReply::Handled();
		});
	const TWeakPtr<SWidget> Weak = Button;
	Button->SetContent(BuildContent([Weak]() { return IsHot(Weak); }));
	return Button.ToSharedRef();
}

// Плашка-бейдж: «НОВОЕ», «+1», «24/39»
TSharedRef<SWidget> MakeBadge(const FText& Text, bool bLime)
{
	return SNew(SBorder)
		.BorderImage(WhiteBrush())
		.BorderBackgroundColor(bLime ? ColLime() : FLinearColor(0.92f, 0.92f, 0.92f))
		.Padding(FMargin(8.f, 2.f))
		[
			SNew(STextBlock).Text(Text).Font(FontBold(18)).ColorAndOpacity(FLinearColor::Black)
		];
}

// Пункт меню крупным текстом: белый, при наведении/фокусе — салатовый
TSharedRef<SButton> TextItem(const FText& Label, float Size, const TFunction<void()>& OnClick,
                             const FText& BadgeText = FText::GetEmpty(), bool bLimeBadge = true)
{
	return MakeButton(OnClick, [Label, Size, BadgeText, bLimeBadge](const FHotFn& Hot) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FontBold(Size))
				.ColorAndOpacity_Lambda([Hot]() { return FSlateColor(Hot() ? ColLime() : FLinearColor::White); })
				.ShadowOffset(FVector2D(2.f, 2.f))
				.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
			];
		if (!BadgeText.IsEmpty())
		{
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(14.f, 0.f, 0.f, 0.f)
			[
				MakeBadge(BadgeText, bLimeBadge)
			];
		}
		return Row;
	});
}

// Кнопка-панель (строка списка): тёмный фон, при наведении/фокусе подсвечивается салатовым
TSharedRef<SButton> PanelButton(const TFunction<void()>& OnClick, const FContentFn& BuildContent,
                                const TFunction<FLinearColor()>& Background = TFunction<FLinearColor()>(),
                                const FMargin& Pad = FMargin(18.f, 12.f))
{
	return MakeButton(OnClick, [BuildContent, Background, Pad](const FHotFn& Hot) -> TSharedRef<SWidget>
	{
		return SNew(SBorder)
			.BorderImage(WhiteBrush())
			.Padding(Pad)
			.BorderBackgroundColor_Lambda([Hot, Background]()
			{
				const FLinearColor Base = Background ? Background() : ColInk();
				return FSlateColor(Hot() ? FMath::Lerp(Base, ColLime(), 0.35f) : Base);
			})
			[
				BuildContent(Hot)
			];
	});
}

TSharedRef<SWidget> Logo()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Txt(Ru(TEXT("МИНИ-ФУТБОЛ")), FontBold(30))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(10.f, 0.f, 0.f, 0.f)
		[
			Txt(Ru(TEXT("5×5")), FontBold(30), ColLime())
		];
}

// Число + подпись характеристики: «84 СКР»
TSharedRef<SWidget> StatPair(int32 Value, int32 Index, float NumSize, float LabelSize)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			Txt(FText::AsNumber(Value), FontBold(NumSize))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(4.f, 0.f, 0.f, 1.f)
		[
			Txt(FText::FromString(FSoccerPlayerInfo::StatLabel(Index)), FontRegular(LabelSize), ColGray())
		];
}

// Полоска прогресса испытания
TSharedRef<SWidget> ProgressBar(float Ratio, bool bOnLime)
{
	return SNew(SBox)
		.HeightOverride(6.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder).BorderImage(WhiteBrush())
				.BorderBackgroundColor(bOnLime ? FLinearColor(0.f, 0.f, 0.f, 0.25f) : FLinearColor(1.f, 1.f, 1.f, 0.12f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(360.f * FMath::Clamp(Ratio, 0.f, 1.f))
				[
					SNew(SBorder).BorderImage(WhiteBrush())
					.BorderBackgroundColor(bOnLime ? FLinearColor::Black : ColLime())
				]
			]
		];
}

// Образец формы: футболка сверху, шорты снизу
TSharedRef<SWidget> KitSwatch(const FSoccerKit& Kit)
{
	return SNew(SBox)
		.WidthOverride(46.f)
		.HeightOverride(46.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(0.65f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Kit.Shirt)
			]
			+ SVerticalBox::Slot().FillHeight(0.35f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Kit.Shorts)
			]
		];
}

// Строка футболиста: позиция, имя, рейтинг, характеристики и бейдж справа
TSharedRef<SButton> PlayerRow(const FSoccerPlayerInfo& PlayerInfo, const FText& Tag,
                              const TFunction<void()>& OnClick, bool bSelected = false)
{
	const FSoccerPlayerInfo I = PlayerInfo;
	return PanelButton(OnClick, [I, Tag](const FHotFn&) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(56.f)
				[
					Txt(FText::FromString(I.Position), FontBold(20), ColGray())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(220.f)
				[
					Txt(FText::FromString(I.Name), FontBold(26))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 24.f, 0.f)
			[
				Txt(FText::AsNumber(I.Rating()), FontBold(30), ColLime())
			];
		for (int32 S = 0; S < 6; ++S)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
			[
				StatPair(I.Stat(S), S, 20.f, 14.f)
			];
		}
		Row->AddSlot().FillWidth(1.f)
		[
			SNullWidget::NullWidget
		];
		if (!Tag.IsEmpty())
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeBadge(Tag, true)
			];
		}
		return Row;
	},
	[bSelected]() { return bSelected ? FLinearColor(FColor(44, 58, 22, 240)) : ColInk(); });
}

// Строка «Название ...... Значение» для настроек и клуба
TSharedRef<SButton> SettingRow(const FText& Title, const FText& Value, const TFunction<void()>& OnClick)
{
	return PanelButton(OnClick, [Title, Value](const FHotFn&) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 40.f, 0.f)
			[
				Txt(Title, FontBold(24))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Txt(Value, FontBold(24), ColLime())
			];
	});
}

// ============================================================================
//  Данные меню: испытания, тренировки, друзья
// ============================================================================
struct FChallenge
{
	const TCHAR* Title;
	int32 Target;
	int32 Reward;
	int32 (*Progress)(const USoccerSave*);
};

const TArray<FChallenge>& Challenges()
{
	static const TArray<FChallenge> List = {
		{ TEXT("Сыграйте 5 матчей"),       5,           1000, [](const USoccerSave* S) { return S->MatchesPlayed; } },
		{ TEXT("Выиграйте 3 матча"),       3,           1500, [](const USoccerSave* S) { return S->Wins; } },
		{ TEXT("Забейте 10 голов"),        10,          1000, [](const USoccerSave* S) { return S->GoalsScored; } },
		{ TEXT("Отдайте 30 пасов"),        30,          500,  [](const USoccerSave* S) { return S->PassesMade; } },
		{ TEXT("Пройдите все тренировки"), NumPractice, 2000, [](const USoccerSave* S)
			{
				int32 N = 0;
				for (bool bDone : S->PracticeDone) N += bDone ? 1 : 0;
				return N;
			} },
	};
	return List;
}

int32 ChallengeProgress(const USoccerSave* S, int32 Index)
{
	const FChallenge& C = Challenges()[Index];
	return S ? FMath::Min(C.Progress(S), C.Target) : 0;
}

bool ChallengeClaimed(const USoccerSave* S, int32 Index)
{
	return S && S->ClaimedChallenges.IsValidIndex(Index) && S->ClaimedChallenges[Index];
}

int32 ClaimableCount(const USoccerSave* S)
{
	int32 N = 0;
	for (int32 i = 0; i < Challenges().Num(); ++i)
	{
		if (ChallengeProgress(S, i) >= Challenges()[i].Target && !ChallengeClaimed(S, i)) ++N;
	}
	return N;
}

int32 PracticeCount(const USoccerSave* S)
{
	int32 N = 0;
	if (S)
	{
		for (bool bDone : S->PracticeDone) N += bDone ? 1 : 0;
	}
	return N;
}

struct FPractice
{
	ESoccerMode Mode;
	const TCHAR* Title;
	const TCHAR* Description;
};

const FPractice Practices[NumPractice] = {
	{ ESoccerMode::PracticeShooting, TEXT("Удары по воротам"), TEXT("Вы один против вратаря. Забейте 5 голов за 60 секунд.") },
	{ ESoccerMode::PracticeOneOnOne, TEXT("Один на один"),     TEXT("Против защитника и вратаря. Забейте 3 гола за 60 секунд.") },
	{ ESoccerMode::PracticeAttack,   TEXT("Атака 5 на 2"),     TEXT("Вся команда против защитника и вратаря. Забейте 4 гола.") },
};

struct FFriend
{
	const TCHAR* Name;
	const TCHAR* Club;
	int32 Difficulty;
	const TCHAR* Status;
};

const FFriend Friends[3] = {
	{ TEXT("Дмитрий"), TEXT("Буревестник"), 0, TEXT("в сети") },
	{ TEXT("Анна"),    TEXT("Ракета"),      1, TEXT("в сети") },
	{ TEXT("Максим"),  TEXT("Атлант"),      2, TEXT("ищет соперника") },
};

const TCHAR* DifficultyName(int32 D)
{
	static const TCHAR* Names[3] = { TEXT("Лёгкая"), TEXT("Нормальная"), TEXT("Сложная") };
	return Names[FMath::Clamp(D, 0, 2)];
}

FString Coins(int32 Amount)
{
	return FText::AsNumber(Amount).ToString() + TEXT(" монет");
}

constexpr int32 SwapCost = 750;

int32 UpgradeCost(int32 Value)
{
	return 100 + FMath::Max(0, Value - 50) * 20;
}

} // namespace

// ============================================================================
//  ГЛАВНОЕ МЕНЮ
// ============================================================================
class SSoccerMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSoccerMenu) : _GameMode(nullptr) {}
		SLATE_ARGUMENT(ASoccerGameMode*, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	TSharedPtr<SWidget> GetFirstFocus() const { return FirstFocus; }

private:
	enum class EPage : uint8 { Main, Squad, Store, Social, Upgrades, Practice, Club, Challenges, Swaps, Settings };

	void SetPage(EPage NewPage);
	void Refresh() { SetPage(Page); }
	void Say(const FString& Message) { StatusText = FText::FromString(Message); }
	USoccerSave* GetSave() const { return GM.IsValid() ? GM->GetSave() : nullptr; }
	void Persist(bool bRefreshLineup);
	TSharedRef<SButton> Track(const TSharedRef<SButton>& Button);

	// Страницы
	TSharedRef<SWidget> BuildPage();
	TSharedRef<SWidget> Frame(const FText& Title, const FText& Subtitle, const TSharedRef<SWidget>& Body);
	TSharedRef<SWidget> BuildMain();
	TSharedRef<SWidget> BuildProfile();
	TSharedRef<SWidget> BuildPromo();
	TSharedRef<SWidget> BuildChallengePanel();
	TSharedRef<SButton> ChallengeRow(int32 Index, const TFunction<void()>& OnClick);
	TSharedRef<SWidget> BuildSquad();
	TSharedRef<SWidget> BuildStore();
	TSharedRef<SWidget> BuildSocial();
	TSharedRef<SWidget> BuildUpgrades();
	TSharedRef<SWidget> BuildPractice();
	TSharedRef<SWidget> BuildClub();
	TSharedRef<SWidget> BuildChallenges();
	TSharedRef<SWidget> BuildSwaps();
	TSharedRef<SWidget> BuildSettings();

	// Действия
	void BuyOrEquipKit(int32 Index);
	void UpgradeStat(int32 StatIndex);
	void ClaimChallenge(int32 Index);
	void SwapPlayer(int32 Index);

	TWeakObjectPtr<ASoccerGameMode> GM;
	EPage Page = EPage::Main;
	TSharedPtr<SBox> PageBox;
	TSharedPtr<SWidget> FirstFocus;
	FText StatusText;
	int32 SelectedPlayer = 3;
	bool bConfirmReset = false;
};

void SSoccerMenu::Construct(const FArguments& InArgs)
{
	GM = InArgs._GameMode;

	ChildSlot
	[
		SNew(SOverlay)
		// Лёгкое затемнение 3D-сцены
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.3f))
		]
		// Тёмная полоса слева под пунктами меню
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(880.f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.5f))
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(PageBox, SBox)
		]
	];

	SetPage(EPage::Main);
}

FReply SSoccerMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Назад: B на геймпаде, Backspace или Esc на клавиатуре
	const FKey Key = InKeyEvent.GetKey();
	const bool bBack = Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::BackSpace || Key == EKeys::Escape;
	if (bBack && Page != EPage::Main)
	{
		SetPage(EPage::Main);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SSoccerMenu::SetPage(EPage NewPage)
{
	if (NewPage != Page)
	{
		StatusText = FText::GetEmpty();
		bConfirmReset = false;
	}
	Page = NewPage;

	// Магазин открыли — бейдж «НОВОЕ» больше не показываем
	USoccerSave* S = GetSave();
	if (Page == EPage::Store && S && !S->bStoreVisited)
	{
		S->bStoreVisited = true;
		Persist(false);
	}

	FirstFocus.Reset();
	PageBox->SetContent(BuildPage());
	if (FirstFocus.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(FirstFocus, EFocusCause::SetDirectly);
	}
}

void SSoccerMenu::Persist(bool bRefreshLineup)
{
	if (!GM.IsValid()) return;
	GM->SaveProgress();
	if (bRefreshLineup) GM->RefreshLineup();
}

TSharedRef<SButton> SSoccerMenu::Track(const TSharedRef<SButton>& Button)
{
	// Первая кнопка страницы получает фокус геймпада
	if (!FirstFocus.IsValid()) FirstFocus = Button;
	return Button;
}

TSharedRef<SWidget> SSoccerMenu::BuildPage()
{
	if (!GetSave()) return SNullWidget::NullWidget;
	switch (Page)
	{
	case EPage::Squad:      return BuildSquad();
	case EPage::Store:      return BuildStore();
	case EPage::Social:     return BuildSocial();
	case EPage::Upgrades:   return BuildUpgrades();
	case EPage::Practice:   return BuildPractice();
	case EPage::Club:       return BuildClub();
	case EPage::Challenges: return BuildChallenges();
	case EPage::Swaps:      return BuildSwaps();
	case EPage::Settings:   return BuildSettings();
	default:                return BuildMain();
	}
}

// Общая рамка раздела: логотип, заголовок, подзаголовок, содержимое, строка статуса, «НАЗАД»
TSharedRef<SWidget> SSoccerMenu::Frame(const FText& Title, const FText& Subtitle, const TSharedRef<SWidget>& Body)
{
	const TSharedRef<SButton> Back = Track(TextItem(Ru(TEXT("НАЗАД")), 30.f, [this]() { SetPage(EPage::Main); }));

	return SNew(SBox)
		.Padding(FMargin(64.f, 40.f, 64.f, 36.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Logo()
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 28.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Title).Font(FontBold(64)).ColorAndOpacity(FLinearColor::White)
				.ShadowOffset(FVector2D(2.f, 2.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 20.f)
			[
				SNew(SBox).WidthOverride(1100.f).HAlign(HAlign_Left)
				[
					SNew(STextBlock).Text(Subtitle).Font(FontRegular(20)).ColorAndOpacity(ColGray()).AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Left)
			[
				Body
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f)
			[
				SNew(STextBlock).Font(FontBold(22)).ColorAndOpacity(ColLime())
				.Text_Lambda([this]() { return StatusText; })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Back
			]
		];
}

// ---------------------------------------------------------------------------
//  Главная страница
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildMain()
{
	USoccerSave* S = GetSave();
	const TWeakObjectPtr<ASoccerGameMode> G = GM;
	const int32 Claimable = ClaimableCount(S);
	const FText StoreBadge = !S->bStoreVisited ? Ru(TEXT("НОВОЕ")) : FText::GetEmpty();
	const FText PracticeBadge = FText::FromString(FString::Printf(TEXT("%d/%d"), PracticeCount(S), NumPractice));
	const FText ChallengeBadge = Claimable > 0 ? FText::FromString(FString::Printf(TEXT("+%d"), Claimable)) : FText::GetEmpty();
	const float Big = 72.f;
	const float Small = 30.f;
	const FMargin SmallGap(0.f, 4.f, 0.f, 0.f);

	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Logo()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 60.f, 0.f, 0.f)
		[
			Track(TextItem(Ru(TEXT("ИГРАТЬ")), Big, [G]() { if (G.IsValid()) G->StartMatch(ESoccerMode::Match); }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			TextItem(Ru(TEXT("СОСТАВ")), Big, [this]() { SetPage(EPage::Squad); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			TextItem(Ru(TEXT("МАГАЗИН")), Big, [this]() { SetPage(EPage::Store); }, StoreBadge)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 36.f, 0.f, 0.f)
		[
			TextItem(Ru(TEXT("ДРУЗЬЯ")), Small, [this]() { SetPage(EPage::Social); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("УЛУЧШЕНИЯ ИГРОКОВ")), Small, [this]() { SetPage(EPage::Upgrades); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("ТРЕНИРОВКИ")), Small, [this]() { SetPage(EPage::Practice); }, PracticeBadge, false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("КЛУБ")), Small, [this]() { SetPage(EPage::Club); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("ИСПЫТАНИЯ")), Small, [this]() { SetPage(EPage::Challenges); }, ChallengeBadge)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("ОБМЕНЫ")), Small, [this]() { SetPage(EPage::Swaps); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("НАСТРОЙКИ")), Small, [this]() { SetPage(EPage::Settings); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(SmallGap)
		[
			TextItem(Ru(TEXT("ВЫХОД")), Small, [G]() { if (G.IsValid()) G->QuitGame(); })
		];

	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildProfile()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 90.f, 0.f, 0.f)
		[
			BuildPromo()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 24.f, 0.f, 0.f)
		[
			BuildChallengePanel()
		];

	return SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(64.f, 40.f, 0.f, 0.f)
		[
			Left
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 40.f, 56.f, 0.f)
		[
			SNew(SBox).WidthOverride(460.f)
			[
				Right
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 56.f, 20.f)
		[
			Txt(Ru(TEXT("Вер. 0.3.0 · прототип")), FontRegular(16), ColGray())
		];
}

// Профиль клуба (клик — раздел «Клуб»)
TSharedRef<SWidget> SSoccerMenu::BuildProfile()
{
	USoccerSave* S = GetSave();
	const TWeakObjectPtr<ASoccerGameMode> G = GM;
	const FSoccerKit Kit = GetSoccerKits()[S->KitIndex];
	const FString Club = S->ClubName;

	return PanelButton([this]() { SetPage(EPage::Club); }, [Kit, Club, G](const FHotFn&) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(52.f).HeightOverride(52.f)
				[
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Kit.Shirt)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Club.Left(1))).Font(FontBold(26))
						.ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1.f, 1.f))
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Txt(FText::FromString(Club), FontBold(24))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(FontRegular(18)).ColorAndOpacity(ColLime())
					.Text_Lambda([G]()
					{
						const USoccerSave* Sv = G.IsValid() ? G->GetSave() : nullptr;
						return Sv ? FText::FromString(FString::Printf(TEXT("%s   ·   побед: %d"), *Coins(Sv->Coins), Sv->Wins))
						          : FText::GetEmpty();
					})
				]
			];
	});
}

// Промо-баннер (клик — сразу в матч)
TSharedRef<SWidget> SSoccerMenu::BuildPromo()
{
	const TWeakObjectPtr<ASoccerGameMode> G = GM;
	return PanelButton([G]() { if (G.IsValid()) G->StartMatch(ESoccerMode::Match); },
		[](const FHotFn&) -> TSharedRef<SWidget>
		{
			return SNew(SBox)
				.HeightOverride(190.f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						Txt(Ru(TEXT("НОВЫЙ СЕЗОН")), FontBold(22))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						Txt(Ru(TEXT("КУБОК 5×5")), FontBold(54), ColLime())
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						Txt(Ru(TEXT("Сыграйте матч и заработайте монеты")), FontRegular(18), FLinearColor(0.85f, 0.85f, 0.9f))
					]
				];
		},
		[]() { return FLinearColor(FColor(62, 20, 72, 235)); },
		FMargin(26.f, 12.f));
}

// Блок испытаний справа (клик по строке — раздел «Испытания»)
TSharedRef<SWidget> SSoccerMenu::BuildChallengePanel()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(ColInkLight()).Padding(FMargin(20.f, 12.f))
			[
				Txt(Ru(TEXT("ИСПЫТАНИЯ")), FontBold(30))
			]
		];
	for (int32 i = 0; i < 4 && i < Challenges().Num(); ++i)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			ChallengeRow(i, [this]() { SetPage(EPage::Challenges); })
		];
	}
	return Box;
}

TSharedRef<SButton> SSoccerMenu::ChallengeRow(int32 Index, const TFunction<void()>& OnClick)
{
	const USoccerSave* S = GetSave();
	const FChallenge C = Challenges()[Index];
	const int32 Progress = ChallengeProgress(S, Index);
	const bool bClaimed = ChallengeClaimed(S, Index);
	const bool bClaimable = Progress >= C.Target && !bClaimed;
	const FLinearColor Ink = bClaimable ? FLinearColor::Black : FLinearColor::White;
	const FString RewardStr = bClaimed ? FString(TEXT("Награда получена"))
	                        : bClaimable ? FString::Printf(TEXT("Забрать: +%s"), *Coins(C.Reward))
	                                     : FString::Printf(TEXT("+%s"), *Coins(C.Reward));
	const float Ratio = (float)Progress / FMath::Max(1, C.Target);

	return PanelButton(OnClick, [C, Progress, bClaimable, Ink, RewardStr, Ratio](const FHotFn&) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 16.f, 0.f)
				[
					Txt(FText::FromString(C.Title), FontBold(20), Ink)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					Txt(FText::FromString(FString::Printf(TEXT("%d/%d"), Progress, C.Target)), FontBold(20), Ink)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
			[
				Txt(FText::FromString(RewardStr), FontRegular(16), bClaimable ? FLinearColor::Black : ColGray())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ProgressBar(Ratio, bClaimable)
			];
	},
	[bClaimable]() { return bClaimable ? ColLime() : ColInk(); });
}

// ---------------------------------------------------------------------------
//  СОСТАВ — выбор игрока, которым начинаем матч
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildSquad()
{
	USoccerSave* S = GetSave();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < S->Squad.Num(); ++i)
	{
		const bool bStarter = S->StarterIndex == i;
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PlayerRow(S->Squad[i], bStarter ? Ru(TEXT("В СТАРТЕ")) : FText::GetEmpty(), [this, i]()
			{
				USoccerSave* Sv = GetSave();
				if (!Sv) return;
				if (i == 0)
				{
					Say(TEXT("Вратарём управляет компьютер — выберите полевого игрока"));
				}
				else
				{
					Sv->StarterIndex = i;
					Persist(true);
					Say(FString::Printf(TEXT("В начале матча вы управляете: %s"), *Sv->Squad[i].Name));
				}
				Refresh();
			}, bStarter))
		];
	}
	return Frame(Ru(TEXT("СОСТАВ")),
	             Ru(TEXT("Выберите игрока, которым начнёте матч. Во время игры переключайтесь кнопкой LB.")), List);
}

// ---------------------------------------------------------------------------
//  МАГАЗИН — формы за монеты
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildStore()
{
	USoccerSave* S = GetSave();
	const TArray<FSoccerKit>& Kits = GetSoccerKits();

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[
			Txt(FText::FromString(TEXT("Баланс: ") + Coins(S->Coins)), FontBold(26), ColLime())
		];

	for (int32 i = 0; i < Kits.Num(); ++i)
	{
		const FSoccerKit Kit = Kits[i];
		const bool bOwned = S->OwnedKits.IsValidIndex(i) && S->OwnedKits[i];
		const bool bEquipped = S->KitIndex == i;
		const FText State = bEquipped ? Ru(TEXT("НАДЕТО"))
		                  : bOwned    ? Ru(TEXT("КУПЛЕНО · НАДЕТЬ"))
		                              : FText::FromString(Coins(Kit.Price));

		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PanelButton([this, i]() { BuyOrEquipKit(i); }, [Kit, State, bEquipped](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						KitSwatch(Kit)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 60.f, 0.f)
					[
						SNew(SBox).WidthOverride(320.f)
						[
							Txt(FText::FromString(TEXT("Форма «") + Kit.Name + TEXT("»")), FontBold(26))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						bEquipped ? MakeBadge(State, true) : Txt(State, FontBold(22), ColLime())
					];
			}))
		];
	}
	return Frame(Ru(TEXT("МАГАЗИН")),
	             Ru(TEXT("Новая форма для вашей команды. Монеты даются за матчи, голы, тренировки и испытания.")), List);
}

void SSoccerMenu::BuyOrEquipKit(int32 Index)
{
	USoccerSave* S = GetSave();
	const TArray<FSoccerKit>& Kits = GetSoccerKits();
	if (!S || !Kits.IsValidIndex(Index)) return;

	if (!S->OwnedKits[Index])
	{
		if (S->Coins < Kits[Index].Price)
		{
			Say(FString::Printf(TEXT("Не хватает монет: нужно ещё %d"), Kits[Index].Price - S->Coins));
			Refresh();
			return;
		}
		S->Coins -= Kits[Index].Price;
		S->OwnedKits[Index] = true;
		Say(TEXT("Куплена форма «") + Kits[Index].Name + TEXT("»"));
	}
	else
	{
		Say(TEXT("Надета форма «") + Kits[Index].Name + TEXT("»"));
	}
	S->KitIndex = Index;
	Persist(true);
	Refresh();
}

// ---------------------------------------------------------------------------
//  ДРУЗЬЯ — вызов на матч против клуба друга
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildSocial()
{
	const TWeakObjectPtr<ASoccerGameMode> G = GM;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FFriend& F : Friends)
	{
		const FFriend Pal = F;
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PanelButton([G, Pal]() { if (G.IsValid()) G->StartMatch(ESoccerMode::Match, Pal.Club, Pal.Difficulty); },
			[Pal](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(48.f).HeightOverride(48.f)
						[
							SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(ColInkLight())
							.HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								Txt(FText::FromString(FString(Pal.Name).Left(1)), FontBold(24), ColLime())
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.f, 0.f, 60.f, 0.f)
					[
						SNew(SBox).WidthOverride(560.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Txt(FText::FromString(Pal.Name), FontBold(24))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								Txt(FText::FromString(FString::Printf(TEXT("Клуб «%s»   ·   сложность: %s   ·   %s"),
								                                      Pal.Club, DifficultyName(Pal.Difficulty), Pal.Status)),
								    FontRegular(16), ColGray())
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeBadge(Ru(TEXT("СЫГРАТЬ")), true)
					];
			}))
		];
	}
	return Frame(Ru(TEXT("ДРУЗЬЯ")),
	             Ru(TEXT("Бросьте вызов клубу друга: его команда сыграет против вас под управлением компьютера.")), List);
}

// ---------------------------------------------------------------------------
//  УЛУЧШЕНИЯ ИГРОКОВ — прокачка характеристик за монеты
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildUpgrades()
{
	USoccerSave* S = GetSave();
	SelectedPlayer = FMath::Clamp(SelectedPlayer, 0, S->Squad.Num() - 1);

	// Слева — выбор игрока
	TSharedRef<SVerticalBox> Roster = SNew(SVerticalBox);
	for (int32 i = 0; i < S->Squad.Num(); ++i)
	{
		const FSoccerPlayerInfo P = S->Squad[i];
		const bool bSel = i == SelectedPlayer;
		Roster->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PanelButton([this, i]() { SelectedPlayer = i; Refresh(); }, [P](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(50.f)[ Txt(FText::FromString(P.Position), FontBold(18), ColGray()) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(210.f)[ Txt(FText::FromString(P.Name), FontBold(24)) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Txt(FText::AsNumber(P.Rating()), FontBold(26), ColLime())
					];
			},
			[bSel]() { return bSel ? FLinearColor(FColor(44, 58, 22, 240)) : ColInk(); }))
		];
	}

	// Справа — характеристики выбранного игрока
	const FSoccerPlayerInfo Sel = S->Squad[SelectedPlayer];
	TSharedRef<SVerticalBox> Stats = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Txt(FText::FromString(FString::Printf(TEXT("%s · рейтинг %d · %s"), *Sel.Name, Sel.Rating(), *Coins(S->Coins))),
			    FontBold(22), ColLime())
		];
	for (int32 StatIdx = 0; StatIdx < 6; ++StatIdx)
	{
		const int32 Value = Sel.Stat(StatIdx);
		const FText Price = Value >= 99 ? Ru(TEXT("МАКСИМУМ"))
		                                : FText::FromString(FString::Printf(TEXT("+1 за %s"), *Coins(UpgradeCost(Value))));
		Stats->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			PanelButton([this, StatIdx]() { UpgradeStat(StatIdx); }, [StatIdx, Value, Price](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(80.f)[ Txt(FText::FromString(FSoccerPlayerInfo::StatLabel(StatIdx)), FontBold(22), ColGray()) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(80.f)[ Txt(FText::AsNumber(Value), FontBold(28)) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(260.f).HAlign(HAlign_Right)[ Txt(Price, FontBold(20), ColLime()) ]
					];
			})
		];
	}

	const TSharedRef<SWidget> Body = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(400.f)[ Roster ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(30.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(500.f)[ Stats ]
		];

	return Frame(Ru(TEXT("УЛУЧШЕНИЯ ИГРОКОВ")),
	             Ru(TEXT("Выберите игрока слева и прокачайте характеристику справа. СКР — скорость, УДР — сила удара, ЗАЩ — отбор, ФИЗ — борьба за мяч.")),
	             Body);
}

void SSoccerMenu::UpgradeStat(int32 StatIndex)
{
	USoccerSave* S = GetSave();
	if (!S || !S->Squad.IsValidIndex(SelectedPlayer)) return;

	FSoccerPlayerInfo& P = S->Squad[SelectedPlayer];
	int32& Value = P.Stat(StatIndex);
	if (Value >= 99)
	{
		Say(TEXT("Эта характеристика уже на максимуме"));
	}
	else if (S->Coins < UpgradeCost(Value))
	{
		Say(FString::Printf(TEXT("Не хватает монет: нужно %d"), UpgradeCost(Value)));
	}
	else
	{
		S->Coins -= UpgradeCost(Value);
		++Value;
		Persist(true);
		Say(FString::Printf(TEXT("%s: %s %d, рейтинг %d"), *P.Name, FSoccerPlayerInfo::StatLabel(StatIndex), Value, P.Rating()));
	}
	Refresh();
}

// ---------------------------------------------------------------------------
//  ТРЕНИРОВКИ — короткие сценарии
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildPractice()
{
	USoccerSave* S = GetSave();
	const TWeakObjectPtr<ASoccerGameMode> G = GM;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < NumPractice; ++i)
	{
		const FPractice Pr = Practices[i];
		const bool bDone = S->PracticeDone.IsValidIndex(i) && S->PracticeDone[i];
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PanelButton([G, Pr]() { if (G.IsValid()) G->StartMatch(Pr.Mode); }, [Pr, bDone](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 60.f, 0.f)
					[
						SNew(SBox).WidthOverride(640.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Txt(FText::FromString(Pr.Title), FontBold(28))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								Txt(FText::FromString(Pr.Description), FontRegular(17), ColGray())
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeBadge(bDone ? Ru(TEXT("ПРОЙДЕНО")) : Ru(TEXT("+500 МОНЕТ")), bDone)
					];
			}))
		];
	}
	return Frame(Ru(TEXT("ТРЕНИРОВКИ")),
	             FText::FromString(FString::Printf(TEXT("Пройдено %d из %d. Выполните цель за 60 секунд — и получите награду."),
	                                               PracticeCount(S), NumPractice)),
	             List);
}

// ---------------------------------------------------------------------------
//  КЛУБ — название и форма
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildClub()
{
	USoccerSave* S = GetSave();
	const TArray<FSoccerKit>& Kits = GetSoccerKits();

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
		[
			Track(SettingRow(Ru(TEXT("Название клуба (нажмите, чтобы сменить)")), FText::FromString(S->ClubName), [this]()
			{
				USoccerSave* Sv = GetSave();
				if (!Sv) return;
				const TArray<FString>& Names = GetClubNames();
				const int32 Cur = Names.IndexOfByKey(Sv->ClubName);
				Sv->ClubName = Names[(Cur + 1) % Names.Num()];
				Persist(false);
				Say(TEXT("Теперь ваш клуб называется «") + Sv->ClubName + TEXT("»"));
				Refresh();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			Txt(Ru(TEXT("Ваши формы")), FontBold(26), ColGray())
		];

	for (int32 i = 0; i < Kits.Num(); ++i)
	{
		if (!S->OwnedKits.IsValidIndex(i) || !S->OwnedKits[i]) continue;
		const FSoccerKit Kit = Kits[i];
		const bool bEquipped = S->KitIndex == i;
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			PanelButton([this, i]() { BuyOrEquipKit(i); }, [Kit, bEquipped](const FHotFn&) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						KitSwatch(Kit)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 60.f, 0.f)
					[
						SNew(SBox).WidthOverride(320.f)[ Txt(FText::FromString(Kit.Name), FontBold(26)) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						bEquipped ? MakeBadge(Ru(TEXT("НАДЕТО")), true) : Txt(Ru(TEXT("НАДЕТЬ")), FontBold(22), ColLime())
					];
			})
		];
	}

	List->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
	[
		TextItem(Ru(TEXT("БОЛЬШЕ ФОРМ — В МАГАЗИНЕ")), 26.f, [this]() { SetPage(EPage::Store); })
	];

	return Frame(Ru(TEXT("КЛУБ")), Ru(TEXT("Название клуба видно на табло матча, форма — в меню и на поле.")), List);
}

// ---------------------------------------------------------------------------
//  ИСПЫТАНИЯ — задания с наградами
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildChallenges()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < Challenges().Num(); ++i)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SBox).WidthOverride(620.f)
			[
				Track(ChallengeRow(i, [this, i]() { ClaimChallenge(i); }))
			]
		];
	}
	return Frame(Ru(TEXT("ИСПЫТАНИЯ")),
	             Ru(TEXT("Выполняйте задания и нажимайте на выполненные (салатовые), чтобы забрать монеты.")), List);
}

void SSoccerMenu::ClaimChallenge(int32 Index)
{
	USoccerSave* S = GetSave();
	if (!S || !Challenges().IsValidIndex(Index)) return;

	const FChallenge& C = Challenges()[Index];
	const int32 Progress = ChallengeProgress(S, Index);
	if (ChallengeClaimed(S, Index))
	{
		Say(TEXT("Награда за это испытание уже получена"));
	}
	else if (Progress < C.Target)
	{
		Say(FString::Printf(TEXT("Ещё не выполнено: %d из %d"), Progress, C.Target));
	}
	else
	{
		S->Coins += C.Reward;
		S->ClaimedChallenges[Index] = true;
		Persist(false);
		Say(FString::Printf(TEXT("Получено: +%s"), *Coins(C.Reward)));
	}
	Refresh();
}

// ---------------------------------------------------------------------------
//  ОБМЕНЫ — игрок на случайного новичка той же позиции
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildSwaps()
{
	USoccerSave* S = GetSave();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[
			Txt(FText::FromString(TEXT("Баланс: ") + Coins(S->Coins)), FontBold(26), ColLime())
		];
	const FText SwapTag = FText::FromString(TEXT("ОБМЕНЯТЬ · ") + Coins(SwapCost));
	for (int32 i = 0; i < S->Squad.Num(); ++i)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(PlayerRow(S->Squad[i], SwapTag, [this, i]() { SwapPlayer(i); }))
		];
	}
	return Frame(Ru(TEXT("ОБМЕНЫ")),
	             Ru(TEXT("Отдайте игрока и получите случайного новичка той же позиции — может повезти, а может и нет.")), List);
}

void SSoccerMenu::SwapPlayer(int32 Index)
{
	USoccerSave* S = GetSave();
	if (!S || !S->Squad.IsValidIndex(Index)) return;
	if (S->Coins < SwapCost)
	{
		Say(FString::Printf(TEXT("Не хватает монет: обмен стоит %d"), SwapCost));
		Refresh();
		return;
	}

	const FString OldName = S->Squad[Index].Name;
	const FString Position = S->Squad[Index].Position;
	const int32 Base = FMath::RandRange(62, 82);
	FSoccerPlayerInfo NewPlayer = ASoccerGameMode::MakeRandomPlayer(Position, Base);
	// Без однофамильцев в составе
	for (int32 Try = 0; Try < 10; ++Try)
	{
		const bool bTaken = S->Squad.ContainsByPredicate([&NewPlayer](const FSoccerPlayerInfo& X) { return X.Name == NewPlayer.Name; });
		if (!bTaken) break;
		NewPlayer = ASoccerGameMode::MakeRandomPlayer(Position, Base);
	}

	S->Coins -= SwapCost;
	S->Squad[Index] = NewPlayer;
	Persist(true);
	Say(FString::Printf(TEXT("%s ушёл. Новый игрок: %s, рейтинг %d"), *OldName, *NewPlayer.Name, NewPlayer.Rating()));
	Refresh();
}

// ---------------------------------------------------------------------------
//  НАСТРОЙКИ
// ---------------------------------------------------------------------------
TSharedRef<SWidget> SSoccerMenu::BuildSettings()
{
	USoccerSave* S = GetSave();
	const TWeakObjectPtr<ASoccerGameMode> G = GM;

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Track(SettingRow(Ru(TEXT("Длительность матча")),
			                 FText::FromString(FString::Printf(TEXT("%d мин"), S->MatchMinutes)), [this]()
			{
				USoccerSave* Sv = GetSave();
				if (!Sv) return;
				Sv->MatchMinutes = Sv->MatchMinutes % 3 + 1;
				Persist(false);
				Refresh();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SettingRow(Ru(TEXT("Сложность соперника")), FText::FromString(DifficultyName(S->Difficulty)), [this]()
			{
				USoccerSave* Sv = GetSave();
				if (!Sv) return;
				Sv->Difficulty = (Sv->Difficulty + 1) % 3;
				Persist(false);
				Refresh();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 24.f)
		[
			SettingRow(Ru(TEXT("Сбросить прогресс")),
			           bConfirmReset ? Ru(TEXT("Нажмите ещё раз для подтверждения")) : Ru(TEXT("монеты, состав, испытания")),
			           [this, G]()
			{
				if (!bConfirmReset)
				{
					bConfirmReset = true;
					Say(TEXT("Весь прогресс будет удалён. Нажмите ещё раз, чтобы подтвердить."));
				}
				else if (G.IsValid())
				{
					bConfirmReset = false;
					G->ResetProgress();
					Say(TEXT("Прогресс сброшен"));
				}
				Refresh();
			})
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).WidthOverride(1000.f)
			[
				SNew(STextBlock).Font(FontRegular(18)).ColorAndOpacity(ColGray()).AutoWrapText(true)
				.Text(Ru(TEXT("Управление (геймпад): левый стик — бег · A — пас · B — удар · X — навес · Y — пас в разрез · "
				              "RT — рывок · LT — укрывание / жокей · RB — финты и изящный удар / прессинг · LB — смена игрока · "
				              "LB + B — удар «парашютом» · правый стик — финт · Start — пауза.\n"
				              "Зажмите кнопку паса или удара — белая стрелка покажет направление, а шкала под игроком — силу. "
				              "Мяч в воздухе рядом — A/B/X/Y играют головой.\n"
				              "Клавиатура: WASD, Space, F, Q, E, Shift, Ctrl, R, Tab, стрелки, P.")))
			]
		];

	return Frame(Ru(TEXT("НАСТРОЙКИ")), Ru(TEXT("Нажмите на строку, чтобы изменить значение.")), List);
}

// ============================================================================
//  МЕНЮ ПАУЗЫ
// ============================================================================
class SSoccerPause : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSoccerPause) : _GameMode(nullptr) {}
		SLATE_ARGUMENT(ASoccerGameMode*, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	TSharedPtr<SWidget> FirstFocus;

private:
	TWeakObjectPtr<ASoccerGameMode> GM;
};

void SSoccerPause::Construct(const FArguments& InArgs)
{
	GM = InArgs._GameMode;
	const TWeakObjectPtr<ASoccerGameMode> G = GM;

	const TSharedRef<SButton> Resume = TextItem(Ru(TEXT("ПРОДОЛЖИТЬ")), 44.f, [G]() { if (G.IsValid()) G->TogglePause(); });
	FirstFocus = Resume;

	const FText ScoreLine = G.IsValid()
		? FText::FromString(FString::Printf(TEXT("%s  %d : %d  %s"), *G->GetTeamName(0), G->GetScore(0), G->GetScore(1), *G->GetTeamName(1)))
		: FText::GetEmpty();

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(ColInk()).Padding(FMargin(70.f, 40.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Txt(Ru(TEXT("ПАУЗА")), FontBold(64), ColLime())
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 26.f)
				[
					Txt(ScoreLine, FontRegular(22), ColGray())
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Resume
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f)
				[
					TextItem(Ru(TEXT("НАЧАТЬ ЗАНОВО")), 36.f, [G]() { if (G.IsValid()) G->RestartMatch(); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					TextItem(Ru(TEXT("ВЫЙТИ В МЕНЮ")), 36.f, [G]() { if (G.IsValid()) G->ReturnToMenu(); })
				]
			]
		]
	];
}

FReply SSoccerPause::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Start / B / P / Backspace — продолжить игру
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right ||
	    Key == EKeys::P || Key == EKeys::BackSpace || Key == EKeys::Escape)
	{
		if (GM.IsValid()) GM->TogglePause();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ============================================================================
//  HUD МАТЧА: имена сверху, табло с таймером, карточки игроков внизу, баннеры
// ============================================================================
namespace
{
TSharedRef<SWidget> NamePlate(const TWeakObjectPtr<ASoccerGameMode>& G, int32 Team, int32 Index)
{
	auto Get = [G, Team, Index]() -> ASoccerPlayer* { return G.IsValid() ? G->GetTeamPlayer(Team, Index) : nullptr; };

	return SNew(SBox)
		.WidthOverride(126.f)
		.Visibility_Lambda([Get]() { return Get() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.03f, 0.82f))
				.Padding(FMargin(6.f, 5.f)).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(FontRegular(15)).ColorAndOpacity(FLinearColor::White)
					.Text_Lambda([Get]()
					{
						const ASoccerPlayer* P = Get();
						return P ? FText::FromString(P->Info.Name) : FText::GetEmpty();
					})
				]
			]
			// Подчёркивание у игрока, которым управляет человек
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(3.f)
				[
					SNew(SBorder).BorderImage(WhiteBrush())
					.BorderBackgroundColor_Lambda([Get]()
					{
						const ASoccerPlayer* P = Get();
						return FSlateColor(P && P->IsPlayerControlled() ? ColLime() : FLinearColor::Transparent);
					})
				]
			]
		];
}

TSharedRef<SWidget> Scoreboard(const TWeakObjectPtr<ASoccerGameMode>& G)
{
	auto TeamName = [G](int32 Team) { return G.IsValid() ? FText::FromString(G->GetTeamName(Team)).ToUpper() : FText::GetEmpty(); };
	auto ScoreBox = [G](int32 Team) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.1f, 0.11f, 0.13f, 1.f))
			.Padding(FMargin(10.f, 0.f))
			[
				SNew(STextBlock).Font(FontBold(26)).ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([G, Team]() { return G.IsValid() ? FText::AsNumber(G->GetScore(Team)) : FText::GetEmpty(); })
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.015f, 0.018f, 0.022f, 0.95f))
			.Padding(FMargin(14.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(160.f).HAlign(HAlign_Right)
					[
						SNew(STextBlock).Font(FontBold(20)).ColorAndOpacity(FLinearColor::White)
						.Text_Lambda([TeamName]() { return TeamName(0); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 3.f, 0.f)
				[
					ScoreBox(0)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.f, 0.f, 12.f, 0.f)
				[
					ScoreBox(1)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(160.f).HAlign(HAlign_Left)
					[
						SNew(STextBlock).Font(FontBold(20)).ColorAndOpacity(FLinearColor::White)
						.Text_Lambda([TeamName]() { return TeamName(1); })
					]
				]
			]
		]
		// Таймер в салатовой плашке
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(ColLime()).Padding(FMargin(12.f, 1.f))
			[
				SNew(STextBlock).Font(FontBold(24)).ColorAndOpacity(FLinearColor::Black)
				.Text_Lambda([G]()
				{
					const int32 Sec = G.IsValid() ? FMath::CeilToInt(G->GetTimeLeft()) : 0;
					return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Sec / 60, Sec % 60));
				})
			]
		]
		// В тренировке — цель
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(FontBold(18)).ColorAndOpacity(ColLime())
			.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
			.Visibility_Lambda([G]() { return G.IsValid() && G->IsPractice() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Text_Lambda([G]()
			{
				return G.IsValid() ? FText::FromString(FString::Printf(TEXT("ЦЕЛЬ: %d / %d ГОЛОВ"), G->GetScore(0), G->GetPracticeTarget()))
				                   : FText::GetEmpty();
			})
		];
}

// Карточка игрока: слева — ваш игрок, справа — соперник у мяча (как на трансляции)
TSharedRef<SWidget> PlayerCard(const TWeakObjectPtr<ASoccerGameMode>& G, bool bHuman)
{
	auto Get = [G, bHuman]() -> ASoccerPlayer*
	{
		if (!G.IsValid()) return nullptr;
		return bHuman ? G->GetHumanPlayer() : G->GetFocusOpponent();
	};

	const TSharedRef<SWidget> Portrait = SNew(SBox)
		.WidthOverride(56.f)
		.HeightOverride(56.f)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.BorderBackgroundColor_Lambda([Get]()
			{
				const ASoccerPlayer* P = Get();
				return FSlateColor(P ? P->ShirtColor : FLinearColor::Gray);
			})
			[
				SNew(STextBlock).Font(FontBold(26)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1.f, 1.f))
				.Text_Lambda([Get]()
				{
					const ASoccerPlayer* P = Get();
					return P ? FText::FromString(P->Info.Name.Left(1)) : FText::GetEmpty();
				})
			]
		];

	const TSharedRef<SWidget> NameBox = SNew(SBox)
		.WidthOverride(210.f)
		.Padding(FMargin(14.f, 0.f))
		.VAlign(VAlign_Center)
		.HAlign(bHuman ? HAlign_Left : HAlign_Right)
		[
			SNew(STextBlock).Font(FontBold(24)).ColorAndOpacity(FLinearColor::White)
			.Text_Lambda([Get]()
			{
				const ASoccerPlayer* P = Get();
				return P ? FText::FromString(P->Info.Name).ToUpper() : FText::GetEmpty();
			})
		];

	const TSharedRef<SWidget> RatingBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(FontRegular(18)).ColorAndOpacity(ColGray())
			.Text_Lambda([Get]()
			{
				const ASoccerPlayer* P = Get();
				return P ? FText::FromString(P->Info.Position) : FText::GetEmpty();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(FontBold(26)).ColorAndOpacity(ColLime())
			.Text_Lambda([Get]()
			{
				const ASoccerPlayer* P = Get();
				return P ? FText::AsNumber(P->Info.Rating()) : FText::GetEmpty();
			})
		];

	// Ваш игрок: портрет | имя | рейтинг. Соперник — зеркально.
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
	if (bHuman)
	{
		Header->AddSlot().AutoWidth()[ Portrait ];
		Header->AddSlot().AutoWidth()[ NameBox ];
		Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)[ RatingBox ];
	}
	else
	{
		Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)[ RatingBox ];
		Header->AddSlot().AutoWidth()[ NameBox ];
		Header->AddSlot().AutoWidth()[ Portrait ];
	}

	TSharedRef<SHorizontalBox> StatsRow = SNew(SHorizontalBox);
	for (int32 S = 0; S < 6; ++S)
	{
		StatsRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(STextBlock).Font(FontBold(20)).ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([Get, S]()
				{
					const ASoccerPlayer* P = Get();
					return P ? FText::AsNumber(P->Info.Stat(S)) : FText::GetEmpty();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(4.f, 0.f, 0.f, 1.f)
			[
				Txt(FText::FromString(FSoccerPlayerInfo::StatLabel(S)), FontRegular(14), ColGray())
			]
		];
	}

	return SNew(SVerticalBox)
		.Visibility_Lambda([Get]() { return Get() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(bHuman ? HAlign_Left : HAlign_Right)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.015f, 0.018f, 0.022f, 0.94f))
			.Padding(0.f)
			[
				Header
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(bHuman ? HAlign_Left : HAlign_Right).Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
			.Padding(FMargin(8.f, 3.f))
			[
				StatsRow
			]
		];
}

// «ГОЛ!» / «ФОЛ!» / «ПЕНАЛЬТИ!» и итог матча по центру экрана
TSharedRef<SWidget> Banner(const TWeakObjectPtr<ASoccerGameMode>& G)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Font(FontBold(100)).ColorAndOpacity(ColLime())
			.Text_Lambda([G]() { return G.IsValid() ? G->GetEventText() : FText::GetEmpty(); })
			.ShadowOffset(FVector2D(4.f, 4.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))
			.Visibility_Lambda([G]()
			{
				return G.IsValid() && G->GetEventBannerTime() > 0.f && !G->IsMatchOver()
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.015f, 0.018f, 0.022f, 0.94f))
			.Padding(FMargin(48.f, 24.f))
			.Visibility_Lambda([G]() { return G.IsValid() && G->IsMatchOver() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Txt(Ru(TEXT("ФИНАЛЬНЫЙ СВИСТОК")), FontRegular(22), ColGray())
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f)
				[
					SNew(STextBlock).Font(FontBold(48)).ColorAndOpacity(ColLime())
					.Text_Lambda([G]() { return G.IsValid() ? G->GetResultText() : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Txt(Ru(TEXT("Возвращаемся в главное меню...")), FontRegular(20), ColGray())
				]
			]
		];
}
} // namespace

// ============================================================================
//  Точки входа (вызываются из ASoccerGameMode)
// ============================================================================
TSharedRef<SWidget> SoccerUI::MakeMenu(ASoccerGameMode* GameMode, TSharedPtr<SWidget>& OutFocus)
{
	const TSharedRef<SSoccerMenu> Menu = SNew(SSoccerMenu).GameMode(GameMode);
	OutFocus = Menu->GetFirstFocus();
	if (!OutFocus.IsValid()) OutFocus = Menu;
	return Menu;
}

TSharedRef<SWidget> SoccerUI::MakePause(ASoccerGameMode* GameMode, TSharedPtr<SWidget>& OutFocus)
{
	const TSharedRef<SSoccerPause> Pause = SNew(SSoccerPause).GameMode(GameMode);
	OutFocus = Pause->FirstFocus;
	if (!OutFocus.IsValid()) OutFocus = Pause;
	return Pause;
}

TSharedRef<SWidget> SoccerUI::MakeHud(ASoccerGameMode* GameMode)
{
	const TWeakObjectPtr<ASoccerGameMode> G(GameMode);

	// Верхняя полоса: 5 имён | табло | 5 имён
	TSharedRef<SHorizontalBox> Top = SNew(SHorizontalBox);
	for (int32 i = 0; i < 5; ++i)
	{
		Top->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(3.f, 0.f)[ NamePlate(G, 0, i) ];
	}
	Top->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(10.f, 0.f)[ Scoreboard(G) ];
	for (int32 i = 0; i < 5; ++i)
	{
		Top->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(3.f, 0.f)[ NamePlate(G, 1, i) ];
	}

	return SNew(SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 12.f, 0.f, 0.f)
		[
			Top
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(56.f, 0.f, 0.f, 36.f)
		[
			PlayerCard(G, true)
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 56.f, 36.f)
		[
			PlayerCard(G, false)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Banner(G)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 12.f)
		[
			Txt(Ru(TEXT("Зажмите пас или удар — белая линия покажет полёт мяча   ·   Start / P — пауза")),
			    FontRegular(15), FLinearColor(1.f, 1.f, 1.f, 0.55f))
		];
}
