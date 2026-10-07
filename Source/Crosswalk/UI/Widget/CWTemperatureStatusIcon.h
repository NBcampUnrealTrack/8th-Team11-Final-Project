#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWTemperatureStatusIcon.generated.h"

class UImage;
class UTexture2D;

// HUD 의 이동/작업 상태 아이콘 한 칸
// 상승/하락 상태는 HUD BP 가 SetIncreasing 으로 전달하고, 판정과 표시 여부는 하지 않음
// 이미지·크기·위치·간격은 HUD 에 놓인 인스턴스에서 각각 조절함
UCLASS()
class CROSSWALK_API UCWTemperatureStatusIcon : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

	// BP 가 매 프레임 불러도 값이 같으면 이미지를 다시 칠하지 않음
	UFUNCTION(BlueprintCallable, Category = "Status Icon")
	void SetIncreasing(bool bInIncreasing);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void ApplyIconLayout();
	void ApplyIconImage();

public:
	// 이동 행과 작업 행이 같은 클래스를 쓰므로 인스턴스마다 다르게 지정함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon")
	bool bWorkStatus = false;

	// 위아래 행이 이 값의 절반씩 벌어짐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Layout", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Additional Vertical Gap"))
	float AdditionalVerticalGap = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Layout")
	FVector2D IconSize = FVector2D(48.f, 48.f);

	// 기존 체온계 추종 위치 기준의 아이콘 좌표
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Layout")
	FVector2D IconPosition = FVector2D(20.f, 4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Image", meta = (DisplayName = "Increase Image"))
	TObjectPtr<UTexture2D> IncreaseTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Image", meta = (DisplayName = "Decrease Image"))
	TObjectPtr<UTexture2D> DecreaseTexture;

	// 디자이너에는 상태가 없으므로 상승/하락 이미지를 번갈아 확인하는 용도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Icon|Preview")
	bool bPreviewIncreasing = false;

private:
	UPROPERTY(Transient)
	TObjectPtr<UImage> WorkerImage;

	// 첫 SetIncreasing 전에는 미리보기 값을 따름
	bool bStateReceived = false;
	bool bIncreasing = false;
	bool bWarnedMissingIncrease = false;
	bool bWarnedMissingDecrease = false;
};
