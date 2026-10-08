// CWUIBootstrapComponent.cpp

#include "CWUIBootstrapComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "UI/CWUILog.h"
#include "GameFramework/PlayerController.h"
#include "UI/Core/CWUIRootWidget.h"
#include "UI/Core/CWUISubsystem.h"
#include "UI/Data/CWUIPageConfigDataAsset.h"
#include "Core/CWGameInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/Widget/CWInteractionPromptWidget.h"
#include "UI/Widget/CWStageIntroWidget.h"
#include "UI/Widget/CWRemotePaintWidget.h"
#include "UI/Widget/CWParryStatusWidget.h"
#include "Kismet/GameplayStatics.h"

UCWUIBootstrapComponent::UCWUIBootstrapComponent()
{
	static ConstructorHelpers::FClassFinder<UUserWidget> NoticeClass(
		TEXT("/Game/CrossWalk/UI/Common/Feedback/WBP_ConfirmDialog"));
	SessionNoticeClass = NoticeClass.Class;
}

#pragma region ActorComponentLifecycle

void UCWUIBootstrapComponent::BeginPlay()
{
	Super::BeginPlay();

	// 컴포넌트 소유자가 PlayerController인지 확인
	APlayerController* OwnerPC =
		Cast<APlayerController>(GetOwner());

	if (!IsValid(OwnerPC))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UIBootstrap: owner is not a PlayerController.")
		);

		return;
	}

	// 원격 PlayerController에는 로컬 UI를 생성하지 않음
	if (!OwnerPC->IsLocalController())
	{
		return;
	}

	// 프런트엔드가 준비되는 동안 원점 카메라 대신 레벨의 대기 구도를 사용한다.
	if (InitialPage != ECWUIPage::None)
	{
		const UCWGameInstance* GI = OwnerPC->GetGameInstance<UCWGameInstance>();
		const bool bRoomArrival = GetWorld()->GetNetMode() == NM_Client ||
			(IsValid(GI) && GI->ShouldEnterRoomPage());
		// ClientTravel로 카메라 매니저도 새로 생성된다. 로비가 준비되기 전까지
		// 새 서버 월드의 차량과 대기 카메라를 노출하지 않는다.
		// 인게임/스테이지 화면과 호스트의 방 생성 연출에는 적용하지 않는다.
		if (InitialPage == ECWUIPage::Title && GetWorld()->GetNetMode() == NM_Client &&
			IsValid(OwnerPC->PlayerCameraManager))
		{
			OwnerPC->PlayerCameraManager->SetManualCameraFade(1.f, FLinearColor::Black, false);
		}
		const FName CameraTag = !bRoomArrival && InitialPage == ECWUIPage::Title
			&& !(IsValid(GI) && GI->ShouldReturnToRoomList())
			? TEXT("CWFrontendTitleCamera") : TEXT("CWFrontendLoadingCamera");
		for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(CameraTag))
			{
				OwnerPC->bAutoManageActiveCameraTarget = false;
				OwnerPC->SetViewTarget(*It);
				break;
			}
		}
	}

	// PlayerController가 소유한 LocalPlayer 확인
	ULocalPlayer* LocalPlayer = OwnerPC->GetLocalPlayer();

	if (!IsValid(LocalPlayer))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UIBootstrap: LocalPlayer is invalid.")
		);

		return;
	}

	// LocalPlayer에 생성된 UI Subsystem 확인
	UCWUISubsystem* UISubsystem =
		LocalPlayer->GetSubsystem<UCWUISubsystem>();

	if (!IsValid(UISubsystem))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UIBootstrap: UISubsystem is invalid.")
		);

		return;
	}

	// Blueprint에서 UI Root 클래스가 설정됐는지 확인
	if (!UIRootClass)
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UIBootstrap: UIRootClass is not configured.")
		);

		return;
	}

	// 페이지 정책 데이터가 설정됐는지 확인
	if (!IsValid(PageConfigData))
	{
		UE_LOG(
			LogCWUI,
			Error,
			TEXT("UIBootstrap: PageConfigData is not configured.")
		);

		return;
	}

	// 시작 페이지가 있으면 해당 정책이 등록됐는지 확인
	if (InitialPage != ECWUIPage::None)
	{
		FCWUIPageConfig InitialConfig;

		if (!PageConfigData->FindPageConfig(InitialPage, InitialConfig) ||
			!InitialConfig.WidgetClass)
		{
			UE_LOG(
				LogCWUI,
				Error,
				TEXT("UIBootstrap: InitialPage config is not registered.")
			);

			return;
		}
	}

	// UI 설정을 Subsystem에 전달하고 Root를 준비
	UISubsystem->InitializeUI(
		UIRootClass,
		PageConfigData
	);

	// 설정된 HUD Widget을 현재 HUD 레이어에 추가
	for (const TSubclassOf<UUserWidget>& HUDClass : InitialHUDClasses)
	{
		if (!HUDClass)
		{
			continue;
		}

		UUserWidget* HUDWidget =
			CreateWidget<UUserWidget>(
				OwnerPC,
				HUDClass
			);

		if (!IsValid(HUDWidget))
		{
			UE_LOG(
				LogCWUI,
				Error,
				TEXT("UIBootstrap: failed to create HUD Widget.")
			);

			continue;
		}

		if (!UISubsystem->AddHUDWidget(HUDWidget))
		{
			UE_LOG(
				LogCWUI,
				Error,
				TEXT("UIBootstrap: failed to add HUD Widget.")
			);
		}
	}

	// 프런트엔드에는 생성하지 않고 HUD 전용 월드의 로컬 플레이어에게만 붙임
	if (InitialPage == ECWUIPage::None)
	{
		UCWParryStatusWidget* ParryStatus = CreateWidget<UCWParryStatusWidget>(OwnerPC, UCWParryStatusWidget::StaticClass());
		if (IsValid(ParryStatus))
		{
			ParryStatus->ConfigureDisplay(bShowParryIcon, ParryIconOffset);
		}
		if (!IsValid(ParryStatus) || !UISubsystem->AddHUDWidget(ParryStatus))
		{
			UE_LOG(LogCWUI, Error, TEXT("UIBootstrap: failed to add parry status icon."));
		}
		// 인게임 HUD 레이어에만 붙여 맵 이동과 메뉴의 표시 정책을 함께 따르게 함
		UCWRemotePaintWidget* RemotePaint = CreateWidget<UCWRemotePaintWidget>(OwnerPC, UCWRemotePaintWidget::StaticClass());
		if (!IsValid(RemotePaint) || !UISubsystem->AddHUDWidget(RemotePaint))
		{
			UE_LOG(LogCWUI, Error, TEXT("UIBootstrap: failed to add remote paint gauges."));
		}
		if (!InteractionPromptClass)
		{
			// 애셋을 옮기거나 바꿀 때 C++ 을 다시 빌드하지 않도록 BP 에서 지정함
			UE_LOG(LogCWUI, Warning, TEXT("UIBootstrap: InteractionPromptClass is not set."));
		}
		else
		{
			UCWInteractionPromptWidget* Prompts = CreateWidget<UCWInteractionPromptWidget>(OwnerPC, InteractionPromptClass);
			if (!IsValid(Prompts) || !UISubsystem->AddHUDWidget(Prompts))
			{
				UE_LOG(LogCWUI, Error, TEXT("UIBootstrap: failed to add interaction prompts."));
			}
		}
	}

	// 어느 화면으로 시작할지 세션 연결 상태로 판정
	// 고정된 InitialPage 만으로는 방에 들어와 있는 경우를 구분하지 못함
	ECWUIPage StartPage = InitialPage;

	if (InitialPage != ECWUIPage::None)
	{
		const UCWGameInstance* CWGameInstance =
			OwnerPC->GetGameInstance<UCWGameInstance>();

		if (IsValid(CWGameInstance) && CWGameInstance->ShouldEnterRoomPage())
		{
			StartPage = ECWUIPage::Room;
		}
		else if (IsValid(CWGameInstance) && CWGameInstance->ShouldReturnToRoomList())
		{
			StartPage = ECWUIPage::RoomList;
		}
	}

	// 맵/복제 데이터가 준비되기 전에는 타이틀 UI나 방 연출을 시작하지 않는다.
	if (StartPage != ECWUIPage::None &&
		UISubsystem->GetCurrentPage() == ECWUIPage::None)
	{
		PendingInitialPage = StartPage;
		GetWorld()->GetTimerManager().SetTimer(InitialPageTimer, this,
			&UCWUIBootstrapComponent::TryOpenInitialPage, 0.1f, true);
	}

	// HUD 전용 화면은 Page를 열지 않으므로 초기 입력 모드를 직접 적용
	if (InitialPage == ECWUIPage::None)
	{
		UISubsystem->ApplyInputMode(InitialInputMode);
		const FString IntroStageName = UGameplayStatics::GetCurrentLevelName(this, true);
		// 입장한 스테이지의 안내만 보여주기 위해 두 안내 목록을 위젯에서 구분함
		if (StageIntroClass && (IntroStageName == TEXT("Stage_01") || IntroStageName == TEXT("Stage_02")))
		{
			UCWStageIntroWidget* Intro = CreateWidget<UCWStageIntroWidget>(OwnerPC, StageIntroClass);
			if (IsValid(Intro) && !Intro->HasConfiguredIntroPages())
			{
				// 아직 이미지가 없는 스테이지는 빈 팝업을 열어 조작을 차단하지 않도록 함
				UE_LOG(LogCWUI, Verbose, TEXT("UIBootstrap: no configured intro for %s."), *IntroStageName);
			}
			else if (!IsValid(Intro) || !UISubsystem->OpenPopup(Intro))
			{
				UE_LOG(LogCWUI, Error, TEXT("UIBootstrap: failed to open stage intro."));
			}
			else if (!Intro->BeginIntro())
			{
				UE_LOG(LogCWUI, Error, TEXT("UIBootstrap: stage intro pages are not configured."));
				UISubsystem->ClosePopup(Intro);
			}
		}
	}

	UE_LOG(
		LogCWUI,
		Log,
		TEXT("UIBootstrap: initialization completed.")
	);
}

void UCWUIBootstrapComponent::TryOpenInitialPage()
{
	UWorld* World = GetWorld();
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!World || !IsValid(PC) || !World->HasBegunPlay() || IsAsyncLoading() ||
		World->IsVisibilityRequestPending())
	{
		bInitialWorldReady = false;
		return;
	}
	for (const ULevelStreaming* Level : World->GetStreamingLevels())
	{
		if (Level && ((Level->ShouldBeLoaded() && !Level->IsLevelLoaded()) ||
			(Level->ShouldBeVisible() && !Level->IsLevelVisible())))
		{
			bInitialWorldReady = false;
			return;
		}
	}
	UCWGameInstance* GI = PC->GetGameInstance<UCWGameInstance>();
	const bool bEnteringRoom = World->GetNetMode() == NM_Client ||
		(IsValid(GI) && GI->ShouldEnterRoomPage());
	if (bEnteringRoom && (!IsValid(World->GetGameState()) || !IsValid(PC->PlayerState) ||
		!World->GetGameState()->PlayerArray.Contains(PC->PlayerState)))
	{
		bInitialWorldReady = false;
		return;
	}
	// 준비된 화면이 한 번 렌더링될 시간을 확보한다. 로딩 완료 여부는 위 조건으로 판정한다.
	if (!bInitialWorldReady)
	{
		bInitialWorldReady = true;
		return;
	}
	if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (UCWUISubsystem* UI = LocalPlayer->GetSubsystem<UCWUISubsystem>())
		{
			if (UI->GetCurrentPage() == ECWUIPage::None)
			{
				const ECWUIPage Page = bEnteringRoom ? ECWUIPage::Room :
					(IsValid(GI) && GI->ShouldReturnToRoomList() ? ECWUIPage::RoomList : PendingInitialPage);
				if (!UI->OpenPage(Page))
				{
					return;
				}
				if (Page == ECWUIPage::RoomList && IsValid(GI))
				{
					GI->CompleteRoomListReturn();
				}
			}
			// 복귀 페이지가 준비된 뒤 안내하며, 팝업이 열렸을 때만 대기 내용을 소비함
			if (!bEnteringRoom && IsValid(GI) && !GI->GetPendingSessionNotice().IsEmpty())
			{
				// 클래스가 확인 팝업의 자식이 아니거나 비면 null 이 되어 안내만 건너뜀
				const TSubclassOf<UCWBaseConfirmDialog> NoticeClass(SessionNoticeClass.Get());
				if (!NoticeClass)
				{
					UE_LOG(LogCWUI, Error, TEXT("Session notice: confirm dialog class is invalid."));
					World->GetTimerManager().ClearTimer(InitialPageTimer);
					return;
				}
				if (!UI->ShowNoticePopup(NoticeClass, GI->GetPendingSessionNotice()))
				{
					return;
				}
				GI->ClearPendingSessionNotice();
			}
			World->GetTimerManager().ClearTimer(InitialPageTimer);
		}
	}
}

void UCWUIBootstrapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialPageTimer);
	}
	Super::EndPlay(EndPlayReason);
}

#pragma endregion
