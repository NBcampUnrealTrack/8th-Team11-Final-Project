// CWStageSelectWidgetBase.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWStageSelectWidgetBase.generated.h"

class UCWClipboardPresenterSubsystem;
class ACWClipboardPropActor;
class USizeBox;
class UWidget;

// 클립보드 내리기 연출 완료 후 부모 위젯에 제거 가능 시점을 알림
DECLARE_DYNAMIC_MULTICAST_DELEGATE(
	FCWStageSelectClosePresentationFinishedSignature
);

// 스테이지 선택 UI와 로컬 클립보드 연출의 생명주기를 연결하는 베이스 위젯
UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWStageSelectWidgetBase : public UUserWidget
{
	GENERATED_BODY()

#pragma region UserWidgetOverrides

protected:
	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

#pragma endregion

#pragma region PublicFunctions

public:
	// Designer에 고정 배치된 위젯을 다시 열 때 사용
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Stage Select")
	void BeginOpenStageSelect();

	// 입력을 차단하고 클립보드 내리기 연출 시작
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Stage Select")
	void BeginCloseStageSelect();

#pragma endregion

#pragma region PrivateFunctions

private:
	UFUNCTION()
	void HandleClipboardRaiseFinished();

	UFUNCTION()
	void HandleClipboardLowerFinished();

	void ResolveClipboardPresenter();

	void BindClipboardDelegates();

	void UnbindClipboardDelegates();

	void SetInteractionEnabled(bool bEnabled);

	void FinishCloseStageSelect();

	void AttachPaperContent();
	void RestorePaperContent();

#pragma endregion

#pragma region Variables

protected:
	// ListView와 시작/닫기/이전/다음 버튼을 포함하는 입력 컨테이너
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> Panel_StageSelectInteraction;

	// Designer에서는 일반 패널, 실행 중에는 클립보드 표면으로 옮김
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_ClipboardPaper;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SizeBox_PaperHost;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCWClipboardPresenterSubsystem> ClipboardPresenter;

	TWeakObjectPtr<ACWClipboardPropActor> PaperActor;

	// 연출 완료 후 Designer에서 지정한 원래 Visibility로 복구
	ESlateVisibility OriginalInteractionVisibility =
		ESlateVisibility::SelfHitTestInvisible;

	bool bHasCachedInteractionVisibility = false;

	bool bIsClosing = false;

	bool bCloseCompleted = false;

#pragma endregion

#pragma region Delegates

public:
	UPROPERTY(BlueprintAssignable, Category = "CW|UI|Stage Select")
	FCWStageSelectClosePresentationFinishedSignature
		OnClosePresentationFinished;

#pragma endregion
};
