// CWUIBootstrapComponent.cpp

#include "CWUIBootstrapComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/Core/CWUISubsystem.h"

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
            LogTemp,
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
            LogTemp,
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
            LogTemp,
            Error,
            TEXT("UIBootstrap: UISubsystem is invalid.")
        );

        return;
    }

    // Blueprint에서 UI Root 클래스가 설정됐는지 확인
    if (!UIRootClass)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("UIBootstrap: UIRootClass is not configured.")
        );

        return;
    }

    // Blueprint에서 페이지 클래스가 등록됐는지 확인
    if (PageClasses.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("UIBootstrap: PageClasses is empty.")
        );

        return;
    }

    // 시작 페이지가 유효하게 지정됐는지 확인
    if (InitialPage == ECWUIPage::None)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("UIBootstrap: InitialPage is None.")
        );

        return;
    }

    // UI 설정을 Subsystem에 전달하고 Root를 준비
    UISubsystem->InitializeUI(
        UIRootClass,
        PageClasses
    );

    // 최초 진입일 때만 시작 페이지 표시
    if (UISubsystem->GetCurrentPage() == ECWUIPage::None)
    {
        if (!UISubsystem->OpenPage(InitialPage))
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("UIBootstrap: failed to open initial page.")
            );

            return;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("UIBootstrap: initialization completed.")
    );
}

#pragma endregion