// CWToastWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CWToastWidget.generated.h"

class UTextBlock;

UCLASS(Abstract, Blueprintable)
class CROSSWALK_API UCWToastWidget : public UUserWidget
{
	GENERATED_BODY()

#pragma region Message

public:
	// 토스트에 표시할 안내 문구를 설정
	UFUNCTION(BlueprintCallable, Category = "CW|UI|Feedback")
	void Setup(const FText& InMessage);

#pragma endregion

#pragma region WidgetBindings

protected:
	// 토스트에 표시할 안내 문구
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Message;

#pragma endregion
};
