#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWTemperatureStatusIcon.generated.h"

class UImage;
class UTexture2D;

UCLASS()
class CROSSWALK_API UCWTemperatureStatusIcon : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWTemperatureStatusIcon(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status Icon")
	bool bWorkStatus = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status Icon", meta=(ClampMin="0.0", UIMin="0.0", DisplayName="Additional Vertical Gap"))
	float AdditionalVerticalGap = 6.f;

	virtual void SynchronizeProperties() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void ApplyIconSpacing();
	// 기존 HUD의 상태 판정과 표시 위치를 재사용하기 위한 이미지 전용 자식임
	UPROPERTY()
	TObjectPtr<UTexture2D> MoveDown;
	UPROPERTY()
	TObjectPtr<UTexture2D> MoveUp;
	UPROPERTY()
	TObjectPtr<UTexture2D> WorkDown;
	UPROPERTY()
	TObjectPtr<UTexture2D> WorkUp;
	UPROPERTY(Transient)
	TObjectPtr<UImage> WorkerImage;
	TOptional<bool> LastIncrease;
};
