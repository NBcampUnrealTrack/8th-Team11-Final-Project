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
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

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
		const FName CameraTag = !bRoomArrival && InitialPage == ECWUIPage::Title
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
	const UCWGameInstance* GI = PC->GetGameInstance<UCWGameInstance>();
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
			World->GetTimerManager().ClearTimer(InitialPageTimer);
			if (UI->GetCurrentPage() == ECWUIPage::None)
			{
				UI->OpenPage(bEnteringRoom ? ECWUIPage::Room : PendingInitialPage);
			}
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
