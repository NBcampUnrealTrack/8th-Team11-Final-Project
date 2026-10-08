#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWRemotePaintWidget.generated.h"

class ACWPlayerCharacter;
class ACWBaseTool;
class APlayerState;
class UCanvasPanel;

USTRUCT()
struct FCWRemotePaintEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<ACWPlayerCharacter> Character;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Status;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerState> BoundPlayerState;

	UPROPERTY(Transient)
	TWeakObjectPtr<ACWBaseTool> LastTool;
};

UCLASS()
class CROSSWALK_API UCWRemotePaintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWRemotePaintWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

private:
	void RefreshPlayers();
	void AddPlayer(ACWPlayerCharacter* Character);
	void ApplyToolOnlyLayout(UUserWidget* Status) const;
	bool BindPlayerState(UUserWidget* Status, APlayerState* PlayerState) const;

	// 상태창 자체를 재사용해 아이콘과 도구별 잔량 색상 처리가 갈라지지 않게 함
	UPROPERTY()
	TSubclassOf<UUserWidget> StatusStyleClass;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TArray<FCWRemotePaintEntry> Entries;

	float PlayerRefreshRemaining = 0.f;

	// 구성 오류는 0.25초·매 프레임마다 반복되므로 한 번만 남김
	bool bWarnedInvalidStatus = false;
	mutable bool bWarnedBindFailure = false;
};
