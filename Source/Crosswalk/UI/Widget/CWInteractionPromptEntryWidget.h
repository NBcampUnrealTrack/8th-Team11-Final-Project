// CWInteractionPromptEntryWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Structs/CWInteractionPrompt.h"
#include "CWInteractionPromptEntryWidget.generated.h"

class UTextBlock;

// 실제로 그리기로 정한 한 줄
// 위젯 트리를 뒤지지 않고도 표시 계약을 검사할 수 있게 판단 결과를 남김
USTRUCT()
struct FCWInteractionPromptEntryLine
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FKey Key;

	UPROPERTY(Transient)
	FText Label;

	// 매핑이 없는 키를 E 나 R 로 추측해 그리지 않음
	UPROPERTY(Transient)
	bool bKeyShown = false;
};

// 대상 하나의 프롬프트를 종이 한 장에 담음
// 종이·글씨·간격은 파생 WBP 가 소유하고 여기서는 값과 표시 여부만 정함
// Abstract 를 붙이지 않는 이유는 자동 테스트가 이 클래스만으로 표시 계약을 검사하기 때문임
UCLASS(Blueprintable)
class CROSSWALK_API UCWInteractionPromptEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCWInteractionPromptEntryWidget(const FObjectInitializer& ObjectInitializer);

	// 0번이 위, 1번이 아래 행
	// 대상 하나에 최대 2개라는 계약에 맞춰 행을 고정으로 둠
	void SetPrompts(const TArray<FCWInteractionPrompt>& InPrompts);

protected:
	// 문구 수정이 C++ 빌드로 이어지지 않도록 UI 에서 관리함
	UPROPERTY(EditDefaultsOnly, Category = "CW|InteractionPrompt")
	TMap<ECWInteractionPromptAction, FText> ActionLabels;

	// 디자인은 WBP 가 가지므로 없어도 동작해야 함
	// 자동 테스트가 C++ 클래스만으로 인스턴스를 만들어 필수 바인딩을 쓸 수 없음
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UWidget> Row_Primary;

	// 키 칸은 배경까지 함께 감춰야 빈 검은 상자가 남지 않음
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UWidget> Box_PrimaryKey;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UTextBlock> Text_PrimaryKey;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UTextBlock> Text_PrimaryAction;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UWidget> Row_Secondary;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UWidget> Box_SecondaryKey;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UTextBlock> Text_SecondaryKey;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "CW|InteractionPrompt")
	TObjectPtr<UTextBlock> Text_SecondaryAction;

private:
	// 고정 행 수. 계약이 바뀌면 여기와 WBP 를 같이 늘려야 함
	static constexpr int32 MaxLines = 2;

	// 문구가 없는 동작은 줄을 만들지 않음
	bool BuildLine(const FCWInteractionPrompt& Prompt, FCWInteractionPromptEntryLine& OutLine) const;

	void ApplyRow(UWidget* Row, UWidget* KeyBox, UTextBlock* KeyText, UTextBlock* ActionText,
		const FCWInteractionPromptEntryLine* Line) const;

	UPROPERTY(Transient)
	TArray<FCWInteractionPromptEntryLine> Lines;

	friend class FCWInteractionPromptDisplayTest;
};
