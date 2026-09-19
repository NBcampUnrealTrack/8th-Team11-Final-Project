#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CWSessionTypes.h"
#include "CWPauseMenuWidget.generated.h"

class UCWSessionViewModel;
class UCWUISubsystem;

UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	// 멀티플레이 시간을 멈추지 않고 로컬 조작만 차단함
	// 이번 호출로 실제 열렸는지를 돌려줌
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	bool OpenMenu();

	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void ResumeGame();

	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void OpenSettings();

	// 기존 확인 팝업이 승인된 뒤에만 세션 종료를 요청함
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void ConfirmLeaveRoom();

	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void SetLeaveConfirmation(UUserWidget* InDialog);

	// 클리어 연출이 입력 모드를 이어받으므로 게임 입력을 복원하지 않음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Pause")
	void DismissForClear();

	void RestoreFocus();
	bool IsMenuOpen() const { return bMenuOpen; }

protected:
	// HUD 에 붙는 순간 등록해 키 입력이 첫 진입이어도 찾을 수 있게 함
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UCWUISubsystem* GetUISubsystem() const;
	void UnbindSession();

	UFUNCTION()
	void HandleSessionCompleted(ECWSessionOperation Operation, bool bSuccess);

	UPROPERTY(Transient)
	TWeakObjectPtr<UCWSessionViewModel> SessionViewModel;

	UPROPERTY(Transient)
	TWeakObjectPtr<UUserWidget> LeaveConfirmation;

	bool bMenuOpen = false;
	bool bLeavingRoom = false;
};
