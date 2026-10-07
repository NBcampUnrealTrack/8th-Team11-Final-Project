#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWBaseConfirmDialog.generated.h"

class UTextBlock;

// 확인·취소 결과를 받는 쪽이 BP 이벤트(Create Event)나 함수로 지정하는 콜백
DECLARE_DYNAMIC_DELEGATE(FCWConfirmResultSignature);

// 확인 팝업의 공통 부모
// 레이아웃·버튼은 BP 가 갖고, 버튼 이벤트는 Confirm·Cancel 만 부름
// 결과는 한 번만 확정되고, 팝업이 사라질 때(맵 이동·소유자 파기·밖에서 닫힘)는 어느 콜백도 부르지 않음
UCLASS(Blueprintable)
class CROSSWALK_API UCWBaseConfirmDialog : public UUserWidget
{
	GENERATED_BODY()

	// 자동 테스트가 보호된 입력·소멸 경로를 직접 부르기 위함
	friend struct FCWConfirmDialogTestAccess;

public:
	UCWBaseConfirmDialog(const FObjectInitializer& ObjectInitializer);

	// 메시지와 결과 콜백을 지정. bShowCancel 이 false 면 안내용(확인 버튼만)으로 동작
	void Setup(const FText& Message, bool bShowCancel, FCWConfirmResultSignature InOnConfirmed, FCWConfirmResultSignature InOnCancelled);

	// 이미 열린 팝업의 메시지를 바꿈 (해상도 확인의 남은 시간 갱신 등)
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void SetMessage(FText Message);

	// 확인 버튼 이벤트가 부름. 확인 콜백 1회 뒤 팝업을 닫음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void Confirm();

	// 취소 버튼 이벤트와 ESC 가 부름. 취소 콜백 1회 뒤 팝업을 닫음
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void Cancel();

protected:
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// 결과 확정은 한 번만 하며, 콜백이 같은 팝업을 닫았을 수 있어 아직 붙어 있을 때만 닫음
	void Resolve(bool bConfirmed);

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Message;

	// 안내용으로 쓸 때 숨기는 버튼 - 버튼 BP 클래스에 의존하지 않도록 UserWidget 으로 받음
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UUserWidget> WBP_ButtonCommon_Cancel;

	FCWConfirmResultSignature OnConfirmed;
	FCWConfirmResultSignature OnCancelled;
	bool bResolved = false;
};
