#pragma once

#include "CoreMinimal.h"
#include "UI/Core/CWUIScreenWidget.h"
#include "CWRoomScreenWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;

// 접속 순서 대신 서버 좌석 소유자를 따라가므로 중도 퇴장에도 이름표가 유지됨
UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWRoomScreenWidget : public UCWUIScreenWidget
{
	GENERATED_BODY()

protected:
	// 크기 조정마다 네이티브 재빌드가 필요하지 않도록 BP 기본값으로 노출함
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CW|Room|Ready Check", meta = (ClampMin = "8", ClampMax = "100", UIMin = "8", UIMax = "100", DisplayName = "Ready Check Font Size"))
	int32 ReadyCheckFontSize = 50;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

private:
	void CreateSeatLabels();
	void UpdateSeatLabels(const FGeometry& MyGeometry);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> SeatCanvas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SeatLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SeatNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SeatChecks;

	TWeakObjectPtr<AActor> Presentation;
	float SearchCooldown = 0.f;
};
