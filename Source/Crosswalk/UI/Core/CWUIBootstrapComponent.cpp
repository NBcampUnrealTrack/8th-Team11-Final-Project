// CWUIBootstrapComponent.cpp

#include "CWUIBootstrapComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "UI/CWUILog.h"
#include "GameFramework/PlayerController.h"
#include "UI/Core/CWUIRootWidget.h"
#include "UI/Core/CWUISubsystem.h"
#include "UI/Data/CWUIPageConfigDataAsset.h"

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

	// 시작 Page가 지정된 최초 진입에서만 화면을 표시
	if (InitialPage != ECWUIPage::None &&
		UISubsystem->GetCurrentPage() == ECWUIPage::None)
	{
		if (!UISubsystem->OpenPage(InitialPage))
		{
			UE_LOG(
				LogCWUI,
				Error,
				TEXT("UIBootstrap: failed to open initial page.")
			);

			return;
		}
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

#pragma endregion
