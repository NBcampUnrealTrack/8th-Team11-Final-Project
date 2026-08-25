// CWUIRootWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWUIRootWidget.generated.h"

class UCWToastWidget;
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

#pragma region HUD

public:
	// HUD 레이어에 Widget을 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	bool AddHUDWidget(UUserWidget* InWidget);

	// HUD 레이어에서 Widget을 제거
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	bool RemoveHUDWidget(UUserWidget* InWidget);

	// HUD 레이어의 모든 Widget을 제거
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	void ClearHUDWidgets();

	// HUD 레이어 전체의 표시 여부를 변경
	UFUNCTION(BlueprintCallable, Category = "CW|UI|HUD")
	void SetHUDLayerVisible(bool bVisible);

#pragma endregion

#pragma region Feedback

public:
	// 비차단 알림 Widget을 Toast 레이어에 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool AddToastWidget(UUserWidget* InWidget);

	// 입력을 차단하는 확인 Widget을 Popup 레이어에 추가
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool AddPopupWidget(UUserWidget* InWidget);

	// 지정한 확인 Widget을 Popup 레이어에서 제거
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	bool RemovePopupWidget(UUserWidget* InWidget);

	// Root에 설정된 Toast Widget 클래스를 반환
	TSubclassOf<UCWToastWidget> GetToastWidgetClass() const;

#pragma endregion

#pragma region UIConfiguration

protected:
	// 공용 비차단 알림에 사용할 Toast Widget 클래스
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|Feedback")
	TSubclassOf<UCWToastWidget> ToastWidgetClass;

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
