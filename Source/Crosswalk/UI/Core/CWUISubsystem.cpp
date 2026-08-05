// CWUISubsystem.cpp

#include "CWUISubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "CWUIRootWidget.h"
#include "CWUIScreenWidget.h"
#include "Widgets/SWidget.h"

#pragma region Initialize

void UCWUISubsystem::InitializeUI(
    TSubclassOf<UCWUIRootWidget> InRootClass,
    const TMap<ECWUIPage, TSubclassOf<UCWUIScreenWidget>>& InPageClasses
)
{
    // Bootstrap Component에서 전달받은 UI 설정을 저장
    UIRootClass = InRootClass;
    PageClasses = InPageClasses;

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
    ApplyInputMode(TargetPage);

    // 화면 전환과 입력 차단 종료
    EndTransition();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("UISubsystem: page opened. Page=%d"),
        static_cast<int32>(TargetPage)
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

    // 이전 World의 Root와 Screen 참조를 함께 폐기
    UIRoot.Reset();
    CurrentScreen.Reset();

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

void UCWUISubsystem::ApplyInputMode(ECWUIPage Page)
{
    // 입력 모드를 적용할 LocalPlayer 확인
    ULocalPlayer* LocalPlayer = GetLocalPlayer();

    if (!IsValid(LocalPlayer))
    {
        return;
    }

    // 마우스와 입력 모드를 제어할 PlayerController 확인
    APlayerController* PlayerController =
        LocalPlayer->GetPlayerController(GetWorld());

    if (!IsValid(PlayerController))
    {
        return;
    }

    // 페이지 종류에 따라 입력 모드 선택
    switch (Page)
    {
        // 메뉴 계열 페이지는 같은 UI 전용 입력 모드 사용
    case ECWUIPage::MainMenu:
    case ECWUIPage::Settings:
    {
        FInputModeUIOnly InputMode;

        // 현재 화면의 캐시된 Slate Widget으로 키보드 포커스 설정
        if (UCWUIScreenWidget* ScreenWidget = CurrentScreen.Get())
        {
            const TSharedPtr<SWidget> CachedWidget =
                ScreenWidget->GetCachedWidget();

            if (CachedWidget.IsValid())
            {
                InputMode.SetWidgetToFocus(CachedWidget);
            }
        }

        // 게임 입력을 막고 UI 입력과 마우스 커서 활성화
        PlayerController->SetInputMode(InputMode);
        PlayerController->SetShowMouseCursor(true);
        break;
    }

    // 아직 입력 정책이 정해지지 않은 페이지는 변경하지 않음
    default:
        break;
    }
}

#pragma endregion