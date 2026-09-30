// CWClipboardScreenWidget.h

#pragma once

#include "CoreMinimal.h"
#include "UI/Core/CWUIScreenWidget.h"
#include "Styling/SlateTypes.h"
#include "CWClipboardScreenWidget.generated.h"

class ACWClipboardPropActor;
class UCWClipboardPresenterSubsystem;
class USizeBox;
class UEditableTextBox;
class SConstraintCanvas;
class SEditableTextBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCWClipboardReturnFinishedSignature);

// 세션 작업은 기존 BP에 두고 종이 표시와 전환 수명만 담당함
UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWClipboardScreenWidget : public UCWUIScreenWidget
{
	GENERATED_BODY()

public:
	virtual void RequestOpenPage(ECWUIPage TargetPage) override;

	UFUNCTION(BlueprintCallable, Category = "CW|UI|Clipboard")
	void ReturnToClipboardPage();

	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Clipboard")
	FCWClipboardReturnFinishedSignature OnClipboardReturnFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_ClipboardPaper;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SizeBox_PaperHost;

	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|Clipboard")
	bool bClipboardPopup = false;

	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|Clipboard", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float ClipboardScale = 1.3f;

private:
	UFUNCTION()
	void HandleRaiseFinished();
	UFUNCTION()
	void HandleLowerFinished();
	void HandlePaperCrossfadeFinished();
	void SetPaperInput(bool bEnabled);
	void RestoreHostContent();
	void UpdateScreenTitleInput();
	void RemoveScreenTitleInput();

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> TitleInputSource;
	TSharedPtr<SConstraintCanvas> ScreenTitleLayer;
	TSharedPtr<SEditableTextBox> ScreenTitleInput;
	FMargin ScreenTitleRect;
	ESlateVisibility TitleSourceVisibility = ESlateVisibility::Visible;

	// 종이의 원본 입력칸은 화면 입력칸이 포커스를 가진 동안만 글자를 숨기므로 복원할 값을 들고 있음
	FEditableTextBoxStyle TitleSourceStyle;
	FEditableTextBoxStyle TitleHiddenStyle;
	FText TitleSourceHint;
	bool bTitleSourceTextHidden = false;

	// 화면 입력칸은 이 스타일의 주소를 계속 참조하므로 입력칸보다 오래 살아야 함
	FEditableTextBoxStyle ScreenTitleStyle;

	UPROPERTY(Transient)
	TObjectPtr<UCWClipboardPresenterSubsystem> Presenter;

	TWeakObjectPtr<ACWClipboardPropActor> PaperActor;
	TWeakObjectPtr<UCWClipboardScreenWidget> PreviousPage;
	ESlateVisibility PaperVisibility = ESlateVisibility::SelfHitTestInvisible;
	ECWUIPage PendingPage = ECWUIPage::None;
	bool bClosing = false;
	bool bReturning = false;
	bool bInputReady = true;
};
