#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWStageIntroWidget.generated.h"

class UImage;
class UTexture2D;

UCLASS()
class CROSSWALK_API UCWStageIntroWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWStageIntroWidget(const FObjectInitializer& ObjectInitializer);

	// 이미지 누락 시 입력 차단 팝업만 남지 않도록 시작을 거절함
	bool BeginIntro(bool bReturnToMenu = false);
	bool HasConfiguredIntroPages() const;

protected:
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Page;

	// 이미지 교체와 순서 변경에 C++ 재빌드가 필요하지 않도록 WBP에 둠
	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|StageIntro", meta = (DisplayName = "1-1 Pages"))
	TArray<TObjectPtr<UTexture2D>> Pages;

	UPROPERTY(EditDefaultsOnly, Category = "CW|UI|StageIntro", meta = (DisplayName = "1-2 Pages"))
	TArray<TObjectPtr<UTexture2D>> Stage02Pages;

private:
	TArray<TObjectPtr<UTexture2D>> GetPagesForContext(bool bCombined) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DisplayPages;

	void AdvancePage();
	void PreviousPage();
	void ShowPage();
	void FinishIntro();

	int32 PageIndex = 0;
	bool bIntroActive = false;
	bool bReturnToPauseMenu = false;
};
