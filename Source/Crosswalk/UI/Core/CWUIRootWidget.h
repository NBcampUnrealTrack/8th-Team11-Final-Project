// CWUIRootWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWUIRootWidget.generated.h"

class UOverlay;
class UUserWidget;

UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWUIRootWidget : public UUserWidget
{
    GENERATED_BODY()

#pragma region PublicFunctions

public:
    // Screen Layer의 현재 화면을 교체
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    void SetScreenContent(UUserWidget* InWidget);

    // Screen Layer의 현재 화면을 제거
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    void ClearScreenContent();

    // 전환 중 전체 UI 입력을 차단
    UFUNCTION(BlueprintCallable, Category = "CW|UI")
    void SetTransitionBlocking(bool bBlocking);

#pragma endregion

#pragma region WidgetBindings

protected:
    // 배타적 화면을 표시할 레이어
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UOverlay> Overlay_ScreenLayer;

    // 인게임 HUD를 표시할 레이어
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UOverlay> Overlay_HUDLayer;

    // 비차단 알림을 표시할 레이어
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UOverlay> Overlay_ToastLayer;

    // 팝업과 메뉴를 표시할 레이어
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UOverlay> Overlay_PopupLayer;

    // 화면 전환과 입력 차단을 표시할 레이어
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UOverlay> Overlay_TransitionLayer;

#pragma endregion
};