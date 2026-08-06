// CWUISubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Data/Enums/CWUITypes.h"
#include "CWUISubsystem.generated.h"

class UCWUIRootWidget;
class UCWUIScreenWidget;
class UUserWidget;

UCLASS()
class CROSSWALK_API UCWUISubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()

#pragma region Initialize

public:
    // UI 클래스 설정을 저장하고 Root를 준비
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    void InitializeUI(
        TSubclassOf<UCWUIRootWidget> InRootClass,
        const TMap<ECWUIPage, TSubclassOf<UCWUIScreenWidget>>& InPageClasses
    );

#pragma endregion

#pragma region Page

public:
    // 요청한 페이지로 화면을 전환
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    bool OpenPage(ECWUIPage TargetPage);
    
    // 현재 표시 중인 페이지를 반환
    UFUNCTION(BlueprintPure, Category = "CW|UI")
    ECWUIPage GetCurrentPage() const;

private:
    // 현재 페이지를 닫고 참조를 정리
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    void CloseCurrentPage();
    
    // 화면의 페이지 전환 요청을 처리
    UFUNCTION()
    void HandleRequestOpenPage(ECWUIPage TargetPage);

#pragma endregion

#pragma region Root

private:
    // 현재 World에서 사용할 Root를 확인하거나 생성
    bool EnsureUIRoot();

#pragma endregion

#pragma region Transition

private:
    // 화면 전환 상태와 입력 차단을 시작
    void BeginTransition();

    // 화면 전환 상태와 입력 차단을 종료
    void EndTransition();

#pragma endregion

#pragma region Input

public:
    // 로컬 PlayerController에 지정한 입력 모드를 적용
    UFUNCTION(BlueprintCallable, Category = "CW|UI|Input")
    void ApplyInputMode(ECWUIInputMode InInputMode);

private:
    // 페이지 종류에 대응하는 입력 모드를 선택
    void ApplyInputModeForPage(ECWUIPage Page);

#pragma endregion

#pragma region UIConfiguration

private:
    // 현재 사용할 Root Widget 클래스
    UPROPERTY()
    TSubclassOf<UCWUIRootWidget> UIRootClass;

    // 페이지별로 생성할 화면 클래스
    UPROPERTY()
    TMap<ECWUIPage, TSubclassOf<UCWUIScreenWidget>> PageClasses;

#pragma endregion

#pragma region RuntimeState

private:
    // 현재 World에 표시 중인 Root
    UPROPERTY(Transient)
    TWeakObjectPtr<UCWUIRootWidget> UIRoot;

    // 현재 Screen Layer에 표시 중인 화면
    UPROPERTY(Transient)
    TWeakObjectPtr<UCWUIScreenWidget> CurrentScreen;

    // 현재 표시 중인 페이지
    ECWUIPage CurrentPage = ECWUIPage::None;

    // 화면 전환 진행 여부
    bool bIsTransitioning = false;

#pragma endregion

#pragma region HUD

public:
    // 현재 Root의 HUD 레이어에 Widget을 추가
    UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
    bool AddHUDWidget(UUserWidget* InWidget);

    // 현재 Root의 HUD 레이어에서 Widget을 제거
    UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
    bool RemoveHUDWidget(UUserWidget* InWidget);

    // 현재 Root의 HUD 레이어를 모두 정리
    UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
    void ClearHUDWidgets();

#pragma endregion

};