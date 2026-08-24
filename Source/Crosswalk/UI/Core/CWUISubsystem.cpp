// CWUISubsystem.cpp

#include "CWUISubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Core/CWGameInstance.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "MVVMGameSubsystem.h"
#include "Types/MVVMViewModelCollection.h"
#include "Types/MVVMViewModelContext.h"
#include "UI/ViewModel/CWSessionViewModel.h"
#include "CWUIRootWidget.h"
#include "CWUIScreenWidget.h"
#include "Widgets/SWidget.h"

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
			LogTemp,
			Error,
			TEXT("UISubsystem: LocalPlayer is invalid during initialization.")
		);

		return;
	}

	UGameInstance* GameInstance = LocalPlayer->GetGameInstance();

	if (!IsValid(GameInstance))
	{
		UE_LOG(
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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
			LogTemp,
			Error,
			TEXT("UISubsystem: failed to register SessionViewModel.")
		);

		return;
	}

	SessionViewModel = NewSessionViewModel;
	bOwnsSessionViewModelRegistration = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("UISubsystem: SessionViewModel registered.")
	);
}

void UCWUISubsystem::Deinitialize()
{
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
	const TMap<ECWUIPage, TSubclassOf<UCWUIScreenWidget>>& InPageClasses
)
{
	// Bootstrap Component에서 전달받은 UI 설정을 저장
	UIRootClass = InRootClass;

	// 이전 World의 공용 페이지 등록을 유지하면서
	// 현재 World에서 전달된 같은 페이지 설정은 새 값으로 덮어씀
	PageClasses.Append(InPageClasses);

	// 반복 초기화 시에도 기존 페이지 상태는 유지
	EnsureUIRoot();
}

#pragma endregion

#pragma region Page

bool UCWUISubsystem::OpenPage(ECWUIPage TargetPage)
{
	// 전환 중이거나 같은 페이지 요청이면 무시
	if (bIsTransitioning || TargetPage == CurrentPage)
	{
		return false;
	}

	// 요청한 페이지에 등록된 Widget 클래스 검색
	const TSubclassOf<UCWUIScreenWidget>* FoundClass =
		PageClasses.Find(TargetPage);

	if (FoundClass == nullptr || !(*FoundClass))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("UISubsystem: page class is not registered.")
		);

		return false;
	}

	// 현재 World에서 사용할 수 있는 UI Root 준비
	if (!EnsureUIRoot())
	{
		UE_LOG(
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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
			*FoundClass
		);

	if (!IsValid(NewScreen))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("UISubsystem: failed to create screen widget.")
		);

		// 생성 실패 시에도 입력 차단을 반드시 해제
		EndTransition();
		return false;
	}

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
			LogTemp,
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
	CurrentPage = TargetPage;

	// 화면 진입 처리 후 페이지 입력 모드 적용
	NewScreen->OnScreenOpened();
	ApplyInputModeForPage(TargetPage);

	// 화면 전환과 입력 차단 종료
	EndTransition();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("UISubsystem: page opened. Page=%s"),
		*UEnum::GetValueAsString(TargetPage)
	);

	return true;
}

bool UCWUISubsystem::ClosePage(ECWUIInputMode ReturnInputMode)
{
	// 전환 중이거나 열린 페이지가 없으면 닫기 요청을 무시
	if (
		bIsTransitioning ||
		CurrentPage == ECWUIPage::None
		)
	{
		return false;
	}

	BeginTransition();

	CloseCurrentPage();
	ApplyInputMode(ReturnInputMode);

	EndTransition();

	UE_LOG(
		LogTemp,
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
			LogTemp,
			Error,
			TEXT("UISubsystem: LocalPlayer is invalid.")
		);

		return false;
	}

	// Bootstrap Component에서 Root 클래스가 설정됐는지 확인
	if (!UIRootClass)
	{
		UE_LOG(
			LogTemp,
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
			LogTemp,
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
			LogTemp,
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

	// 이전 World의 UI 상태를 폐기
	UIRoot.Reset();
	CurrentScreen.Reset();
	CurrentPage = ECWUIPage::None;

	// Root의 Viewport ZOrder를 명시해 화면에 추가
	NewRoot->AddToPlayerScreen(0);
	UIRoot = NewRoot;

	UE_LOG(
		LogTemp,
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

		// 현재 화면이 있으면 키보드 포커스 대상으로 지정
		if (UCWUIScreenWidget* ScreenWidget = CurrentScreen.Get())
		{
			const TSharedPtr<SWidget> CachedWidget =
				ScreenWidget->GetCachedWidget();

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

		// 현재 화면이 있으면 UI 포커스 대상으로 지정
		if (UCWUIScreenWidget* ScreenWidget = CurrentScreen.Get())
		{
			const TSharedPtr<SWidget> CachedWidget =
				ScreenWidget->GetCachedWidget();

			if (CachedWidget.IsValid())
			{
				InputMode.SetWidgetToFocus(CachedWidget);
			}
		}

		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
		break;
	}

	default:
		break;
	}
}

void UCWUISubsystem::ApplyInputModeForPage(ECWUIPage Page)
{
	// 페이지 종류에 따라 입력 모드 선택
	switch (Page)
	{
	case ECWUIPage::Title:
	case ECWUIPage::MainMenu:
	case ECWUIPage::CreateRoom:
	case ECWUIPage::JoinRoom:
	case ECWUIPage::Room:
	case ECWUIPage::StageSelect:
	case ECWUIPage::Result:
	case ECWUIPage::Settings:
	case ECWUIPage::MultiplayerMenu:
	case ECWUIPage::RoomList:
		ApplyInputMode(ECWUIInputMode::UIOnly);
		break;

		// TODO: Loading 화면 구현 시 마우스 커서 숨김 여부를 확정한다.
	case ECWUIPage::Loading:
		ApplyInputMode(ECWUIInputMode::UIOnly);
		break;

	case ECWUIPage::None:
	default:
		break;
	}
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
	// 현재 Root의 HUD Widget을 모두 정리
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->ClearHUDWidgets();
	}
}

void UCWUISubsystem::SetHUDVisible(bool bVisible)
{
	// 실제 HUD 레이어 표시 변경은 Root에 위임
	if (UCWUIRootWidget* RootWidget = UIRoot.Get())
	{
		RootWidget->SetHUDLayerVisible(bVisible);
	}
}

#pragma endregion
