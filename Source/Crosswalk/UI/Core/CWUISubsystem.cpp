// CWUISubsystem.cpp

#include "CWUISubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Blueprint/UserWidget.h"
#include "Core/CWGameInstance.h"
#include "UI/CWUILog.h"
#include "UI/Feedback/CWToastWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "MVVMGameSubsystem.h"
#include "Types/MVVMViewModelCollection.h"
#include "Types/MVVMViewModelContext.h"
#include "UI/ViewModel/CWSessionViewModel.h"
#include "UI/Widget/CWPauseMenuWidget.h"
#include "UI/Widget/CWStageIntroWidget.h"
#include "CWUIRootWidget.h"
#include "CWUIScreenWidget.h"
#include "Widgets/SWidget.h"
#include "Layout/Children.h"
#include "MVVMSubsystem.h"
#include "View/MVVMView.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// 루트가 포커스를 받지 못하면 조작 가능한 자식으로 이동함
	TSharedPtr<SWidget> FindFocusableWidget(const TSharedPtr<SWidget>& Widget)
	{
		if (!Widget.IsValid() || !Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
		{
			return nullptr;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			return Widget;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Child = FindFocusableWidget(Children->GetChildAt(Index)))
			{
				return Child;
			}
		}
		return nullptr;
	}

	// WBP_Toast의 페이드 애니메이션 전체 길이
	constexpr float ToastDisplayDuration = 2.5f;
}

#pragma region Initialize

void UCWUISubsystem::Initialize(
	FSubsystemCollectionBase& Collection
)
{
	Super::Initialize(Collection);

	// 이 LocalPlayer를 소유한 GameInstance 확인
	ULocalPlayer* LocalPlayer = GetLocalPlayer();

	if (!IsValid(LocalPlayer))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: LocalPlayer is invalid during initialization.")
		);

		return;
	}

	UGameInstance* GameInstance = LocalPlayer->GetGameInstance();

	if (!IsValid(GameInstance))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: GameInstance is invalid during initialization.")
		);

		return;
	}

	// 프로젝트 전용 GameInstance인지 확인
	UCWGameInstance* CWGameInstance =
		Cast<UCWGameInstance>(GameInstance);

	if (!IsValid(CWGameInstance))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: GameInstance is not CWGameInstance.")
		);

		return;
	}

	// GameInstance에 생성된 MVVM 전역 컬렉션 확인
	UMVVMGameSubsystem* MVVMSubsystem =
		GameInstance->GetSubsystem<UMVVMGameSubsystem>();

	if (!IsValid(MVVMSubsystem))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: MVVMGameSubsystem is invalid.")
		);

		return;
	}

	UMVVMViewModelCollectionObject* ViewModelCollection =
		MVVMSubsystem->GetViewModelCollection();

	if (!IsValid(ViewModelCollection))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: global ViewModel collection is invalid.")
		);

		return;
	}

	// Widget의 Global Viewmodel Collection 설정과 동일한 Context 구성
	FMVVMViewModelContext SessionContext;
	SessionContext.ContextClass =
		UCWSessionViewModel::StaticClass();
	SessionContext.ContextName =
		TEXT("SessionViewModel");

	// 화면 분할이나 재초기화로 이미 등록된 경우 기존 인스턴스 재사용
	UMVVMViewModelBase* ExistingViewModel =
		ViewModelCollection->FindViewModelInstance(
			SessionContext
		);

	if (UCWSessionViewModel* ExistingSessionViewModel =
		Cast<UCWSessionViewModel>(ExistingViewModel))
	{
		SessionViewModel = ExistingSessionViewModel;

		UE_LOG(
			LogCWUI,
			Log,
			TEXT("UISubsystem: existing SessionViewModel reused.")
		);

		return;
	}

	// GameInstance 수명에 맞춰 전역 Session ViewModel 생성
	UCWSessionViewModel* NewSessionViewModel =
		NewObject<UCWSessionViewModel>(GameInstance);

	if (!IsValid(NewSessionViewModel))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to create SessionViewModel.")
		);

		return;
	}

	// GameInstance 이벤트를 구독하고 현재 로그인·세션 상태를 즉시 반영
	NewSessionViewModel->Initialize(CWGameInstance);

	// 전역 컬렉션에 등록해 Widget이 이름으로 조회할 수 있게 함
	const bool bRegistered =
		ViewModelCollection->AddViewModelInstance(
			SessionContext,
			NewSessionViewModel
		);

	if (!bRegistered)
	{
		NewSessionViewModel->Uninitialize();

		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to register SessionViewModel.")
		);

		return;
	}

	SessionViewModel = NewSessionViewModel;
	bOwnsSessionViewModelRegistration = true;

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: SessionViewModel registered.")
	);
}

void UCWUISubsystem::Deinitialize()
{
	// 전역 VM 제거 알림이 살아 있는 화면의 바인딩을 다시 초기화하지 않도록 함
	const ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	const UGameInstance* OwningGameInstance = IsValid(OwningLocalPlayer)
		? OwningLocalPlayer->GetGameInstance() : nullptr;
	if (!IsValid(OwningGameInstance))
	{
		UE_LOG(LogCWUI, Warning, TEXT("UISubsystem: MVVM cleanup skipped because owning GameInstance is invalid."));
	}
	else
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			// 종료 시 PC의 Player가 먼저 해제돼 LocalPlayer 비교로 화면을 놓칠 수 있음
			if (IsValid(*It) && !It->IsTemplate() && It->GetGameInstance() == OwningGameInstance)
			{
				if (UMVVMView* View = UMVVMSubsystem::GetViewFromUserWidget(*It); IsValid(View))
				{
					View->UninitializeEvents();
					View->UninitializeBindings();
					View->UninitializeSources();
				}
			}
		}
	}

	// 종료 중 토스트와 대기 요청 및 타이머 정리
	ResetToastState();

	// 종료 중 표시된 팝업을 제거하고 입력 복원 상태 정리
	if (UUserWidget* PopupWidget = CurrentPopup.Get())
	{
		PopupWidget->RemoveFromParent();
	}

	CurrentPopup.Reset();
	PauseMenu.Reset();
	bPauseMenuBlocked = false;
	CurrentInputMode = ECWUIInputMode::UIOnly;
	PopupReturnInputMode = ECWUIInputMode::UIOnly;

	UCWSessionViewModel* RegisteredViewModel =
		SessionViewModel.Get();

	// 이 서브시스템이 직접 등록한 인스턴스만 컬렉션에서 제거
	if (
		bOwnsSessionViewModelRegistration &&
		IsValid(RegisteredViewModel)
		)
	{
		ULocalPlayer* LocalPlayer = GetLocalPlayer();
		UGameInstance* GameInstance =
			IsValid(LocalPlayer)
			? LocalPlayer->GetGameInstance()
			: nullptr;

		UMVVMGameSubsystem* MVVMSubsystem =
			IsValid(GameInstance)
			? GameInstance->GetSubsystem<UMVVMGameSubsystem>()
			: nullptr;

		UMVVMViewModelCollectionObject* ViewModelCollection =
			IsValid(MVVMSubsystem)
			? MVVMSubsystem->GetViewModelCollection()
			: nullptr;

		if (IsValid(ViewModelCollection))
		{
			FMVVMViewModelContext SessionContext;
			SessionContext.ContextClass =
				UCWSessionViewModel::StaticClass();
			SessionContext.ContextName =
				TEXT("SessionViewModel");

			// Widget이 더 이상 종료 중인 ViewModel을 조회하지 않도록 제거
			ViewModelCollection->RemoveViewModel(
				SessionContext
			);
		}

		// GameInstance의 동적 델리게이트 구독 정리
		RegisteredViewModel->Uninitialize();
	}

	SessionViewModel.Reset();
	bOwnsSessionViewModelRegistration = false;

	Super::Deinitialize();
}

void UCWUISubsystem::InitializeUI(
	TSubclassOf<UCWUIRootWidget> InRootClass,
	UCWUIPageConfigDataAsset* InPageConfigData
)
{
	// Bootstrap Component에서 전달받은 UI 설정을 저장
	UIRootClass = InRootClass;

	// 이전 World의 페이지 정책을 유지하면서
	// 현재 World에서 전달된 같은 페이지는 새 값으로 덮어씀
	if (IsValid(InPageConfigData))
	{
		PageConfigs.Append(InPageConfigData->GetPageConfigs());
	}

	// 반복 초기화 시에도 기존 페이지 상태는 유지
	EnsureUIRoot();
}

#pragma endregion

#pragma region Page

bool UCWUISubsystem::OpenPage(ECWUIPage TargetPage)
{
	FPageChangeScope PageScope(*this);

	// 전환 중이거나 같은 페이지 요청이면 무시
	if (bIsTransitioning || TargetPage == CurrentPage)
	{
		return false;
	}

	// 요청한 페이지의 정책 검색
	const FCWUIPageConfig* FoundConfig =
		PageConfigs.Find(TargetPage);

	if (FoundConfig == nullptr || !FoundConfig->WidgetClass)
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: page config is not registered.")
		);

		return false;
	}

	// 현재 World에서 사용할 수 있는 UI Root 준비
	if (!EnsureUIRoot())
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: UI Root is unavailable.")
		);

		return false;
	}

	// 현재 LocalPlayer 확인
	ULocalPlayer* LocalPlayer = GetLocalPlayer();

	if (!IsValid(LocalPlayer))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: LocalPlayer is invalid.")
		);

		return false;
	}

	// 새 화면의 OwningPlayer로 사용할 PlayerController 확인
	APlayerController* PlayerController =
		LocalPlayer->GetPlayerController(GetWorld());

	if (!IsValid(PlayerController))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: PlayerController is invalid.")
		);

		return false;
	}

	// 화면 교체가 끝날 때까지 추가 전환 요청 차단
	BeginTransition();

	// 요청한 페이지의 Widget 생성
	UCWUIScreenWidget* NewScreen =
		CreateWidget<UCWUIScreenWidget>(
			PlayerController,
			FoundConfig->WidgetClass
		);

	if (!IsValid(NewScreen))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to create screen widget.")
		);

		// 생성 실패 시에도 입력 차단을 반드시 해제
		EndTransition();
		return false;
	}

	// 현재 페이지가 HUD를 숨긴 상태였으면 그 페이지 열기 전 상태를 계승한다.
	// 닫기를 거치지 않고 페이지를 직접 교체해도 원래 HUD 상태가 살아남는다.
	const bool bBaseHUDVisible =
		bCurrentPageHidesHUD ? bHUDWasVisibleBeforePage : bIsHUDVisible;

	// 직접 전환이면 최초 페이지 열기 전 입력 모드를 계승한다.
	// CloseCurrentPage 이후에는 CurrentPage=None 이라 직접 전환을 판정할 수 없으므로
	// 반드시 CloseCurrentPage 앞에서 계산한다.
	const ECWUIInputMode BaseInputMode =
		(CurrentPage != ECWUIPage::None)
		? InputModeBeforeCurrentPage
		: CurrentInputMode;

	// 닫을 때 돌아갈 페이지도 같은 이유로 여기서 잡음
	// CloseCurrentPage 가 CurrentPage 를 None 으로 만들어 뒤에서 읽으면 항상 비어 있음
	const ECWUIPage PreviousPage = CurrentPage;

	// 새 화면 생성 성공 후 기존 화면 정리
	CloseCurrentPage();

	// 새 화면의 페이지 전환 요청을 Subsystem에 연결
	NewScreen->OnRequestOpenPage.AddUniqueDynamic(
		this,
		&UCWUISubsystem::HandleRequestOpenPage
	);

	// 새 화면을 표시할 UI Root 확인
	UCWUIRootWidget* RootWidget = UIRoot.Get();

	if (!IsValid(RootWidget))
	{
		// 표시하지 못한 화면의 Delegate 연결 해제
		NewScreen->OnRequestOpenPage.RemoveDynamic(
			this,
			&UCWUISubsystem::HandleRequestOpenPage
		);

		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: UI Root became invalid.")
		);

		EndTransition();
		return false;
	}

	// 새 화면을 Screen Layer에 표시
	RootWidget->SetScreenContent(NewScreen);

	// 현재 화면과 페이지 상태 갱신
	CurrentScreen = NewScreen;

	// 이 페이지를 닫을 때 돌아갈 곳
	// 세션 정보로 화면을 고르면 그 값이 어긋나는 순간 갈 곳을 잃음
	PageBeforeCurrentPage = PreviousPage;

	CurrentPage = TargetPage;

	// 이 페이지를 닫을 때 복원할 기준 상태 저장
	bHUDWasVisibleBeforePage = bBaseHUDVisible;
	InputModeBeforeCurrentPage = BaseInputMode;
	bCurrentPageHidesHUD = FoundConfig->bHideHUD;

	// 화면 진입 처리 후 HUD 정책과 입력 모드 적용
	NewScreen->OnScreenOpened();

	if (bCurrentPageHidesHUD)
	{
		SetHUDVisible(false);
	}
	else if (bIsHUDVisible != bBaseHUDVisible)
	{
		SetHUDVisible(bBaseHUDVisible);
	}

	ApplyInputMode(FoundConfig->InputMode);

	// 화면 전환과 입력 차단 종료
	EndTransition();

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: page opened. Page=%s"),
		*UEnum::GetValueAsString(TargetPage)
	);

	return true;
}

bool UCWUISubsystem::ClosePage()
{
	FPageChangeScope PageScope(*this);

	// 전환 중이거나 열린 페이지가 없으면 닫기 요청을 무시
	if (
		bIsTransitioning ||
		CurrentPage == ECWUIPage::None
		)
	{
		return false;
	}

	BeginTransition();

	// 저장해 둔 기준 상태 복원
	const bool bRestoreHUD = bCurrentPageHidesHUD;
	const bool bTargetHUDVisible = bHUDWasVisibleBeforePage;
	const ECWUIInputMode ReturnInputMode = InputModeBeforeCurrentPage;

	CloseCurrentPage();

	// 이 페이지가 HUD를 숨겼던 경우에만 원래 상태로 되돌린다
	if (bRestoreHUD)
	{
		SetHUDVisible(bTargetHUDVisible);
	}

	bCurrentPageHidesHUD = false;

	ApplyInputMode(ReturnInputMode);

	EndTransition();

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: page closed.")
	);

	return true;
}

void UCWUISubsystem::CloseCurrentPage()
{
	// 현재 표시 중인 화면 확인
	UCWUIScreenWidget* ScreenWidget = CurrentScreen.Get();

	if (IsValid(ScreenWidget))
	{
		// 닫히는 화면의 페이지 전환 요청 연결 해제
		ScreenWidget->OnRequestOpenPage.RemoveDynamic(
			this,
			&UCWUISubsystem::HandleRequestOpenPage
		);

		// 화면 제거 전에 애니메이션과 시각 상태 정리
		ScreenWidget->OnScreenClosing();
	}

	// Root의 Screen Layer에서 현재 화면 제거
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->ClearScreenContent();
	}

	if (IsValid(ScreenWidget))
	{
		// 화면 제거 후 참조 정리와 로그 처리
		ScreenWidget->OnScreenClosed();
	}

	// 이전 World의 화면을 붙잡지 않도록 참조 초기화
	CurrentScreen.Reset();
	CurrentPage = ECWUIPage::None;
}

UCWUISubsystem::FPageChangeScope::FPageChangeScope(UCWUISubsystem& InOwner)
	: Owner(InOwner)
{
	if (Owner.PageChangeDepth++ == 0)
	{
		Owner.PageAtOperationStart = Owner.CurrentPage;
	}
}

UCWUISubsystem::FPageChangeScope::~FPageChangeScope()
{
	// 깊이를 먼저 내려 구독자가 다시 페이지를 열어도 새 범위로 동작하게 함
	if (--Owner.PageChangeDepth > 0)
	{
		return;
	}

	const ECWUIPage OldPage = Owner.PageAtOperationStart;
	if (Owner.CurrentPage != OldPage)
	{
		Owner.OnPageChanged.Broadcast(OldPage, Owner.CurrentPage);
	}
}

ECWUIPage UCWUISubsystem::GetCurrentPage() const
{
	// 현재 Screen Layer에 표시 중인 페이지 반환
	return CurrentPage;
}

void UCWUISubsystem::HandleRequestOpenPage(ECWUIPage TargetPage)
{
	// Widget의 요청을 공통 화면 전환 함수로 전달
	OpenPage(TargetPage);
}

#pragma endregion

#pragma region Root

bool UCWUISubsystem::EnsureUIRoot()
{
	// 월드 전환 초기화가 페이지를 None 으로 되돌리므로 통보 범위에 포함
	FPageChangeScope PageScope(*this);

	// 현재 저장된 UI Root 확인
	UCWUIRootWidget* RootWidget = UIRoot.Get();

	// 현재 World 소속이며 실제 화면에 붙어 있는지 확인
	const bool bRootUsable =
		IsValid(RootWidget) &&
		RootWidget->GetWorld() == GetWorld() &&
		RootWidget->IsInViewport();

	if (bRootUsable)
	{
		return true;
	}

	// UI Root를 소유할 LocalPlayer 확인
	ULocalPlayer* LocalPlayer = GetLocalPlayer();

	if (!IsValid(LocalPlayer))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: LocalPlayer is invalid.")
		);

		return false;
	}

	// Bootstrap Component에서 Root 클래스가 설정됐는지 확인
	if (!UIRootClass)
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: UIRootClass is not configured.")
		);

		return false;
	}

	// Root Widget의 OwningPlayer로 사용할 PlayerController 확인
	APlayerController* PlayerController =
		LocalPlayer->GetPlayerController(GetWorld());

	if (!IsValid(PlayerController))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: PlayerController is invalid.")
		);

		return false;
	}

	// 현재 World에서 사용할 UI Root 생성
	UCWUIRootWidget* NewRoot =
		CreateWidget<UCWUIRootWidget>(
			PlayerController,
			UIRootClass
		);

	if (!IsValid(NewRoot))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to create UI Root.")
		);

		return false;
	}

	// 생성이 확정된 뒤에 이전 화면의 페이지 요청 연결을 해제
	if (UCWUIScreenWidget* ScreenWidget = CurrentScreen.Get())
	{
		ScreenWidget->OnRequestOpenPage.RemoveDynamic(
			this,
			&UCWUISubsystem::HandleRequestOpenPage
		);
	}

	// 이전 World의 토스트 상태만 폐기
	// 현재 World에서 새로 요청된 토스트 큐는 유지
	if (ToastWorld.Get() != GetWorld())
	{
		ResetToastState();
	}

	// 이전 World의 UI 상태를 폐기
	UIRoot.Reset();
	CurrentScreen.Reset();
	PauseMenu.Reset();
	bPauseMenuBlocked = false;
	CurrentPage = ECWUIPage::None;

	// 이전 월드의 복귀 지점이 새 월드에서 조회되지 않게 함
	PageBeforeCurrentPage = ECWUIPage::None;

	// 참가 시 숨겼던 이전 Root의 상태가 새 월드의 HUD 복원값으로 남지 않도록 함
	bIsHUDVisible = true;
	bHUDWasVisibleBeforePage = true;
	bCurrentPageHidesHUD = false;
	NewRoot->SetHUDLayerVisible(true);

	// Root의 Viewport ZOrder를 명시해 화면에 추가
	NewRoot->AddToPlayerScreen(0);
	UIRoot = NewRoot;

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: UI Root created.")
	);

	return true;
}

#pragma endregion

#pragma region Transition

void UCWUISubsystem::BeginTransition()
{
	// 전환 중 추가 페이지 요청 차단
	bIsTransitioning = true;

	// 최상단 Transition Layer로 UI 입력 차단
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->SetTransitionBlocking(true);
	}
}

void UCWUISubsystem::EndTransition()
{
	// Transition Layer의 UI 입력 차단 해제
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->SetTransitionBlocking(false);
	}

	// 새로운 페이지 전환 요청 허용
	bIsTransitioning = false;
}

#pragma endregion

#pragma region Input

void UCWUISubsystem::SetPauseMenu(UCWPauseMenuWidget* InMenu)
{
	PauseMenu = InMenu;
}

void UCWUISubsystem::ClearPauseMenu(UCWPauseMenuWidget* InMenu)
{
	if (PauseMenu.Get() == InMenu)
	{
		PauseMenu.Reset();
	}
}

UCWSessionViewModel* UCWUISubsystem::GetSessionViewModel() const
{
	return SessionViewModel.Get();
}

bool UCWUISubsystem::HandleRoomLeaveResult(ECWSessionOperation Operation, bool bSuccess)
{
	if (Operation != ECWSessionOperation::DestroyRoom)
	{
		return false;
	}

	const UCWSessionViewModel* ViewModel = GetSessionViewModel();
	if (bSuccess)
	{
		const FString MenuMap = IsValid(ViewModel) ? ViewModel->GetMenuMapPath() : FString();
		if (!MenuMap.IsEmpty())
		{
			UGameplayStatics::OpenLevel(GetWorld(), FName(*MenuMap));
			return true;
		}

		// 방은 나갔지만 돌아갈 맵이 설정되지 않은 개발 설정 오류 - 화면이 멈춘 것처럼 보이지 않게 알림
		UE_LOG(LogCWUI, Error, TEXT("UISubsystem: menu map path is not configured."));
		ShowToast(NSLOCTEXT("CWSessionUI", "MenuMapMissing", "메뉴 맵 설정을 확인해 주세요."));
		return true;
	}

	// 실패 사유는 뷰모델이 갖고 있으며, 뷰모델이 없으면 일반 문구로 대신함
	ShowToast(IsValid(ViewModel) ? ViewModel->GetLastFailureText() : NSLOCTEXT("CWSessionUI", "LeaveRoomFailed", "방을 나가지 못했습니다. 다시 시도해 주세요."));
	return true;
}

UUserWidget* UCWUISubsystem::GetFocusTargetWidget() const
{
	if (UUserWidget* Popup = CurrentPopup.Get(); IsValid(Popup))
	{
		return Popup;
	}

	if (UCWUIScreenWidget* Screen = CurrentScreen.Get(); IsValid(Screen))
	{
		return Screen;
	}

	// 페이지가 없는 인게임에서는 열려 있는 일시정지 메뉴가 포커스 주인
	// 확인창을 취소했을 때 메뉴로 포커스를 돌려주는 경로이기도 함
	if (UCWPauseMenuWidget* Menu = PauseMenu.Get(); IsValid(Menu) && Menu->IsMenuOpen())
	{
		return Menu;
	}

	return nullptr;
}

void UCWUISubsystem::SetPauseMenuBlocked(bool bBlocked)
{
	bPauseMenuBlocked = bBlocked;
}

void UCWUISubsystem::DismissOverlaysForClear()
{
	if (UCWPauseMenuWidget* Menu = PauseMenu.Get(); IsValid(Menu))
	{
		Menu->DismissForClear();
	}

	// 메뉴가 연 안내는 위에서 닫혔으므로 남은 것은 입장 안내뿐이며, 확인창 등 다른 팝업은 건드리지 않음
	if (UCWStageIntroWidget* Intro = Cast<UCWStageIntroWidget>(CurrentPopup.Get()); IsValid(Intro))
	{
		ClosePopup(Intro);
	}
}

bool UCWUISubsystem::OpenPauseMenu()
{
	// 어느 조건에서 걸렸는지 남김. 거절도 결과이므로 항상 기록함
	UE_LOG(
		LogCWUI,
		Log,
		TEXT("[PauseMenu] open requested. Page=%d Blocked=%d MenuValid=%d"),
		static_cast<int32>(CurrentPage),
		bPauseMenuBlocked ? 1 : 0,
		PauseMenu.IsValid() ? 1 : 0
	);

	// 페이지가 열려 있으면 그 위에 겹쳐 열지 않음
	if (CurrentPage != ECWUIPage::None || bPauseMenuBlocked)
	{
		return false;
	}

	UCWPauseMenuWidget* Menu = PauseMenu.Get();

	if (!IsValid(Menu))
	{
		return false;
	}

	// 이번 요청으로 실제 열렸는지를 그대로 돌려줌
	// IsMenuOpen 은 이미 열려 있던 경우에도 true 라 요청 성공 여부와 다름
	return Menu->OpenMenu();
}

bool UCWUISubsystem::RestorePauseMenuFocus()
{
	UCWPauseMenuWidget* Menu = PauseMenu.Get();
	if (CurrentPage != ECWUIPage::None || !IsValid(Menu) || Menu->GetWorld() != GetWorld() || !Menu->IsMenuOpen())
	{
		return false;
	}
	ApplyInputMode(ECWUIInputMode::UIOnly);
	Menu->RestoreFocus();
	return true;
}

void UCWUISubsystem::ApplyInputMode(ECWUIInputMode InInputMode)
{
	// 입력 모드를 적용할 LocalPlayer 확인
	ULocalPlayer* LocalPlayer = GetLocalPlayer();

	if (!IsValid(LocalPlayer))
	{
		return;
	}

	// 입력과 마우스 커서를 제어할 PlayerController 확인
	APlayerController* PlayerController =
		LocalPlayer->GetPlayerController(GetWorld());

	if (!IsValid(PlayerController))
	{
		return;
	}

	// 입력 모드가 바뀌면 기존 키 해제 이벤트를 받지 못할 수 있으므로
	// Enhanced Input에 남아 있는 눌림 상태를 먼저 초기화
	PlayerController->FlushPressedKeys();

	// 요청받은 정책에 따라 입력 방식과 커서 모양 변경
	switch (InInputMode)
	{
	case ECWUIInputMode::GameOnly:
	{
		FInputModeGameOnly InputMode;

		// 게임 전용 입력에서는 마우스 커서를 숨김
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
		break;
	}

	case ECWUIInputMode::UIOnly:
	{
		FInputModeUIOnly InputMode;

		// Title·MainMenu·Room 등의 UI 화면에서는 일반 커서 사용
		PlayerController->DefaultMouseCursor =
			EMouseCursor::Default;

		PlayerController->CurrentMouseCursor =
			EMouseCursor::Default;

		// 팝업이 열려 있으면 팝업을 우선 포커스 대상으로 사용
		if (UUserWidget* FocusWidget = GetFocusTargetWidget(); IsValid(FocusWidget))
		{
			const TSharedPtr<SWidget> CachedWidget =
				FindFocusableWidget(FocusWidget->GetCachedWidget());

			if (CachedWidget.IsValid())
			{
				InputMode.SetWidgetToFocus(CachedWidget);
			}
		}

		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
		break;
	}

	case ECWUIInputMode::GameAndUI:
	{
		FInputModeGameAndUI InputMode;

		// 인게임에서는 작업 위치를 가리키는 십자가 커서 사용
		PlayerController->DefaultMouseCursor =
			EMouseCursor::Crosshairs;

		PlayerController->CurrentMouseCursor =
			EMouseCursor::Crosshairs;

		// 게임 화면을 클릭해도 커서를 계속 표시
		InputMode.SetHideCursorDuringCapture(false);

		// 게임과 UI를 함께 사용할 때 커서를 뷰포트에 강제로 고정하지 않음
		InputMode.SetLockMouseToViewportBehavior(
			EMouseLockMode::DoNotLock
		);

		// 팝업이 열려 있으면 팝업을 우선 포커스 대상으로 사용
		bool bFocusAssigned = false;

		if (UUserWidget* FocusWidget = GetFocusTargetWidget(); IsValid(FocusWidget))
		{
			const TSharedPtr<SWidget> CachedWidget =
				FindFocusableWidget(FocusWidget->GetCachedWidget());

			if (CachedWidget.IsValid())
			{
				InputMode.SetWidgetToFocus(CachedWidget);
				bFocusAssigned = true;
			}
		}

		// 포커스 주인이 없으면 뷰포트로 돌려놓음
		// SetFocusAndLocking 은 위젯을 안 주면 포커스를 그대로 두므로(PlayerController.cpp:6313),
		// 방금 닫힌 위젯이 포커스를 쥔 채 키를 삼켜 다시 열 수 없게 됨
		if (!bFocusAssigned)
		{
			if (const UGameViewportClient* ViewportClient = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
			{
				const TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();

				if (ViewportWidget.IsValid())
				{
					InputMode.SetWidgetToFocus(ViewportWidget);
				}
			}
		}

		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
		break;
	}

	default:
		break;
	}

	// 팝업을 닫을 때 복원할 수 있도록 실제 적용된 입력 모드 저장
	CurrentInputMode = InInputMode;
}

#pragma endregion

#pragma region HUD

bool UCWUISubsystem::AddHUDWidget(UUserWidget* InWidget)
{
	if (!IsValid(InWidget) || !EnsureUIRoot())
	{
		return false;
	}

	// 실제 HUD 부착은 Root에 위임
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		return RootWidget->AddHUDWidget(InWidget);
	}

	return false;
}

bool UCWUISubsystem::RemoveHUDWidget(UUserWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}

	// 실제 HUD 제거는 Root에 위임
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		return RootWidget->RemoveHUDWidget(InWidget);
	}

	return false;
}

void UCWUISubsystem::ClearHUDWidgets()
{
	UE_LOG(LogCWUI, Log, TEXT("[HUDState] Clear widgets World=%s Page=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(CurrentPage));

	// 현재 Root의 HUD Widget을 모두 정리
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->ClearHUDWidgets();
	}
}

bool UCWUISubsystem::CanShowInteractionPrompts() const
{
	const UCWPauseMenuWidget* Menu = PauseMenu.Get();
	return bIsHUDVisible && !bIsTransitioning && !bPauseMenuBlocked && !CurrentPopup.IsValid()
		&& CurrentPage == ECWUIPage::None && CurrentInputMode != ECWUIInputMode::UIOnly
		&& !(IsValid(Menu) && Menu->IsMenuOpen());
}

void UCWUISubsystem::SetClearSummary(float ElapsedSeconds, int32 StarCount)
{
	bClearSummaryValid = true;
	ClearSummaryMap = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
	ClearSummaryElapsedSeconds = FMath::Max(ElapsedSeconds, 0.f);
	ClearSummaryStarCount = FMath::Clamp(StarCount, 0, 3);
	UE_LOG(LogCWUI, Log, TEXT("[ClearSummary] set. Map=%s Elapsed=%.1f Stars=%d"), *ClearSummaryMap, ClearSummaryElapsedSeconds, ClearSummaryStarCount);
}

bool UCWUISubsystem::TryGetClearSummary(float& OutElapsedSeconds, int32& OutStarCount) const
{
	OutElapsedSeconds = 0.f;
	OutStarCount = 0;

	if (!bClearSummaryValid)
	{
		UE_LOG(LogCWUI, Warning, TEXT("[ClearSummary] requested but nothing was recorded."));
		return false;
	}

	// 맵 이동 뒤에도 서브시스템이 남으므로 다른 스테이지에서 기록한 값은 쓰지 않음
	const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
	if (ClearSummaryMap != CurrentMap)
	{
		UE_LOG(LogCWUI, Warning, TEXT("[ClearSummary] stale. Recorded=%s Current=%s"), *ClearSummaryMap, *CurrentMap);
		return false;
	}

	OutElapsedSeconds = ClearSummaryElapsedSeconds;
	OutStarCount = ClearSummaryStarCount;
	return true;
}

void UCWUISubsystem::ClearClearSummary()
{
	bClearSummaryValid = false;
	ClearSummaryMap.Reset();
	ClearSummaryElapsedSeconds = 0.f;
	ClearSummaryStarCount = 0;
}

void UCWUISubsystem::SetHUDVisible(bool bVisible)
{
	// 실제 HUD 레이어와 내부 추적 상태를 함께 변경
	UCWUIRootWidget* RootWidget = UIRoot.Get();

	if (!IsValid(RootWidget))
	{
		return;
	}

	if (bIsHUDVisible != bVisible)
	{
		UE_LOG(LogCWUI, Log, TEXT("[HUDState] Visible=%d -> %d World=%s Page=%d PageHidesHUD=%d RestoreVisible=%d"),
			bIsHUDVisible, bVisible, *GetNameSafe(GetWorld()), static_cast<int32>(CurrentPage),
			bCurrentPageHidesHUD, bHUDWasVisibleBeforePage);
	}
	RootWidget->SetHUDLayerVisible(bVisible);
	// 복원 계산에서 현재 상태를 참조하므로 함께 갱신
	bIsHUDVisible = bVisible;
}

#pragma endregion

#pragma region Feedback

bool UCWUISubsystem::OpenPopup(UUserWidget* InWidget)
{
	// 유효한 팝업과 Root가 준비된 경우에만 표시
	if (!IsValid(InWidget) || !EnsureUIRoot())
	{
		return false;
	}

	// 현재 구조는 동시에 하나의 차단 팝업만 허용
	if (CurrentPopup.IsValid())
	{
		UE_LOG(
			LogCWUI,
			Warning,
			TEXT("UISubsystem: popup is already open.")
		);

		return false;
	}

	UCWUIRootWidget* RootWidget = UIRoot.Get();

	if (!IsValid(RootWidget))
	{
		return false;
	}

	// 팝업을 닫을 때 현재 화면의 입력 모드로 돌아갈 수 있도록 저장
	const ECWUIInputMode PreviousInputMode = CurrentInputMode;

	if (!RootWidget->AddPopupWidget(InWidget))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to add popup widget.")
		);

		return false;
	}

	PopupReturnInputMode = PreviousInputMode;
	CurrentPopup = InWidget;

	// 차단 팝업이 열려 있는 동안 게임 입력을 받지 않도록 전환
	ApplyInputMode(ECWUIInputMode::UIOnly);

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: popup opened.")
	);

	return true;
}

bool UCWUISubsystem::ClosePopup(UUserWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}

	// 다른 팝업이 현재 팝업을 닫지 못하도록 동일 인스턴스인지 확인
	if (
		!CurrentPopup.IsValid() ||
		CurrentPopup.Get() != InWidget
		)
	{
		UE_LOG(
			LogCWUI,
			Warning,
			TEXT("UISubsystem: requested popup is not currently open.")
		);

		return false;
	}

	UCWUIRootWidget* RootWidget = UIRoot.Get();

	if (
		!IsValid(RootWidget) ||
		!RootWidget->RemovePopupWidget(InWidget)
		)
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to remove popup widget.")
		);

		return false;
	}

	const ECWUIInputMode ReturnInputMode =
		PopupReturnInputMode;

	// 입력 모드를 적용하기 전에 참조를 비워 기존 화면으로 포커스 복귀
	CurrentPopup.Reset();
	PopupReturnInputMode = ECWUIInputMode::UIOnly;

	ApplyInputMode(ReturnInputMode);

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UISubsystem: popup closed.")
	);

	return true;
}

UCWBaseConfirmDialog* UCWUISubsystem::ShowConfirmPopup(
	TSubclassOf<UCWBaseConfirmDialog> DialogClass,
	FText Message,
	FCWConfirmResultSignature OnConfirmed,
	FCWConfirmResultSignature OnCancelled
)
{
	return OpenConfirmDialog(DialogClass, Message, true, OnConfirmed, OnCancelled);
}

UCWBaseConfirmDialog* UCWUISubsystem::ShowNoticePopup(TSubclassOf<UCWBaseConfirmDialog> DialogClass, FText Message)
{
	return OpenConfirmDialog(DialogClass, Message, false, FCWConfirmResultSignature(), FCWConfirmResultSignature());
}

UCWBaseConfirmDialog* UCWUISubsystem::OpenConfirmDialog(
	TSubclassOf<UCWBaseConfirmDialog> DialogClass,
	const FText& Message,
	bool bShowCancel,
	FCWConfirmResultSignature OnConfirmed,
	FCWConfirmResultSignature OnCancelled
)
{
	// 클래스가 비면 아무것도 열지 않고 알림 - 크래시 대신 기능만 비활성화
	if (!DialogClass)
	{
		UE_LOG(LogCWUI, Error, TEXT("UISubsystem: confirm popup class is not set."));
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = IsValid(LocalPlayer) ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	if (!IsValid(PlayerController))
	{
		UE_LOG(LogCWUI, Error, TEXT("UISubsystem: PlayerController is invalid for confirm popup."));
		return nullptr;
	}

	UCWBaseConfirmDialog* Dialog = CreateWidget<UCWBaseConfirmDialog>(PlayerController, DialogClass);
	if (!IsValid(Dialog))
	{
		UE_LOG(LogCWUI, Error, TEXT("UISubsystem: failed to create confirm popup."));
		return nullptr;
	}

	Dialog->Setup(Message, bShowCancel, OnConfirmed, OnCancelled);

	// 이미 다른 팝업이 열려 있으면 열지 못하며, 이때 콜백은 부르지 않음
	if (!OpenPopup(Dialog))
	{
		return nullptr;
	}

	// ESC 가 이 팝업으로 오도록 포커스를 줌
	Dialog->SetUserFocus(PlayerController);
	return Dialog;
}

void UCWUISubsystem::CopyTextToClipboard(const FString& Text)
{
	// 빈 문자열로 덮어쓰면 사용자가 갖고 있던 내용만 날아감
	if (Text.IsEmpty())
	{
		return;
	}

	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

void UCWUISubsystem::ShowToast(const FText& Message)
{
	// 빈 문구는 표시하지 않음
	if (Message.IsEmpty())
	{
		return;
	}

	// 맵 이동 후 이전 World의 토스트 상태가 남아 있으면 폐기
	if (ToastWorld.Get() != GetWorld())
	{
		ResetToastState();
	}

	// 이번 요청부터 현재 World의 토스트 상태로 관리
	ToastWorld = GetWorld();

	// 현재 표시 중인 문구와 같은 연속 요청은 무시
	if (
		CurrentToast.IsValid() &&
		CurrentToastMessage.EqualTo(Message)
		)
	{
		return;
	}

	// 큐의 마지막 문구와 같은 연속 요청도 중복 등록하지 않음
	if (
		ToastQueue.Num() > 0 &&
		ToastQueue.Last().EqualTo(Message)
		)
	{
		return;
	}

	ToastQueue.Add(Message);

	// 표시 중인 토스트가 없으면 즉시 다음 문구 표시
	if (!CurrentToast.IsValid())
	{
		ShowNextToast();
	}
}

void UCWUISubsystem::ShowNextToast()
{
	// 이미 표시 중이거나 대기 문구가 없으면 처리하지 않음
	if (CurrentToast.IsValid() || ToastQueue.IsEmpty())
	{
		return;
	}

	if (!EnsureUIRoot())
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to prepare UI Root for toast.")
		);

		return;
	}

	UCWUIRootWidget* RootWidget = UIRoot.Get();

	if (!IsValid(RootWidget))
	{
		return;
	}

	const TSubclassOf<UCWToastWidget> ToastClass =
		RootWidget->GetToastWidgetClass();

	if (!ToastClass)
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: ToastWidgetClass is not configured on UI Root.")
		);

		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController =
		IsValid(LocalPlayer)
		? LocalPlayer->GetPlayerController(GetWorld())
		: nullptr;

	if (!IsValid(PlayerController))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: PlayerController is invalid while creating toast.")
		);

		return;
	}

	const FText Message = ToastQueue[0];

	UCWToastWidget* NewToast =
		CreateWidget<UCWToastWidget>(
			PlayerController,
			ToastClass
		);

	if (!IsValid(NewToast))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to create toast widget.")
		);

		return;
	}

	NewToast->Setup(Message);

	if (!RootWidget->AddToastWidget(NewToast))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UISubsystem: failed to add toast widget to Root.")
		);

		return;
	}

	// 생성과 부착이 성공한 요청만 큐에서 제거
	ToastQueue.RemoveAt(0);
	CurrentToast = NewToast;
	CurrentToastMessage = Message;

	// 페이드 애니메이션이 끝나면 현재 토스트를 제거
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ToastTimerHandle,
			this,
			&UCWUISubsystem::HandleToastFinished,
			ToastDisplayDuration,
			false
		);
	}

	UE_LOG(
		LogCWUI,
		Verbose,
		TEXT("UISubsystem: toast displayed.")
	);
}

void UCWUISubsystem::ResetToastState()
{
	// 토스트를 표시한 World가 아직 유효하면 예약된 타이머 정리
	if (UWorld* World = ToastWorld.Get())
	{
		World->GetTimerManager().ClearTimer(
			ToastTimerHandle
		);
	}

	// 현재 표시 중인 토스트가 남아 있으면 화면에서 제거
	if (UCWToastWidget* ToastWidget = CurrentToast.Get())
	{
		ToastWidget->RemoveFromParent();
	}

	ToastTimerHandle.Invalidate();
	CurrentToast.Reset();
	CurrentToastMessage = FText::GetEmpty();
	ToastQueue.Reset();
	ToastWorld.Reset();
}

void UCWUISubsystem::HandleToastFinished()
{
	// 현재 토스트를 Root에서 제거
	if (UCWToastWidget* ToastWidget = CurrentToast.Get())
	{
		ToastWidget->RemoveFromParent();
	}

	CurrentToast.Reset();
	CurrentToastMessage = FText::GetEmpty();

	// 대기 중인 다음 문구를 이어서 표시
	ShowNextToast();
}

#pragma endregion
