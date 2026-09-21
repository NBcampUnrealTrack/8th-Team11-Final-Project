// CWInteractionPromptWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Structs/CWInteractionPrompt.h"
#include "CWInteractionPromptWidget.generated.h"

class UCanvasPanel;
class UCWInteractionPromptComponent;
class UCWInteractionPromptEntryWidget;

USTRUCT()
struct FCWInteractionPromptGroup
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Target;

	UPROPERTY(Transient)
	TObjectPtr<UCWInteractionPromptEntryWidget> Entry;
};

// 대상 판정은 컴포넌트에 맡기고 로컬 화면의 표시만 담당함
// 종이·글씨는 Entry WBP 가 가지며 여기서는 묶기와 화면 좌표 변환만 함
UCLASS(Blueprintable)
class CROSSWALK_API UCWInteractionPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWInteractionPromptWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 대상 하나마다 하나씩 만드는 안내판
	// 비워 두면 프롬프트가 그려지지 않으므로 BP 에서 반드시 지정함
	UPROPERTY(EditDefaultsOnly, Category = "CW|InteractionPrompt")
	TSubclassOf<UCWInteractionPromptEntryWidget> EntryWidgetClass;

	// 굳이 안내하지 않을 동작
	// 들고 있는 도구를 내려놓는 것처럼 플레이어가 이미 아는 조작을 덜어냄
	UPROPERTY(EditDefaultsOnly, Category = "CW|InteractionPrompt")
	TSet<ECWInteractionPromptAction> HiddenActions;

	// 메시 세로 중심에서 올릴 높이. 0 이면 중심에 맞춤
	UPROPERTY(EditDefaultsOnly, Category = "CW|InteractionPrompt")
	float AnchorHeightOffset = 0.f;

	// 투영된 지점에서 화면으로 밀어낼 양. 대상을 가리지 않도록 오른쪽으로 띄움
	UPROPERTY(EditDefaultsOnly, Category = "CW|InteractionPrompt")
	FVector2D ScreenOffset = FVector2D(24.f, 0.f);

	// WBP 가 캔버스를 가지며 C++ 단독 인스턴스에서는 RebuildWidget 이 대신 만듦
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UCanvasPanel> Canvas_InteractionPrompts;

private:
	UFUNCTION()
	void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandlePromptsChanged(UCWInteractionPromptComponent* Component,
		const TArray<FCWInteractionPrompt>& Prompts);

	void UnbindPrompts();
	void RefreshGroups(const TArray<FCWInteractionPrompt>& Prompts);
	FVector GetTargetAnchor(AActor* Target) const;

	UPROPERTY(Transient)
	TArray<FCWInteractionPromptGroup> Groups;

	TWeakObjectPtr<UCWInteractionPromptComponent> PromptSource;
	TWeakObjectPtr<APlayerController> BoundController;

	friend class FCWInteractionPromptDisplayTest;
};
