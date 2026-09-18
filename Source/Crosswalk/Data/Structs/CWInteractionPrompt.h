// Copyright Crosswalk Team. All Rights Reserved.
// CWInteractionPrompt.h

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Data/Enums/CWInteractionPromptTypes.h"
#include "CWInteractionPrompt.generated.h"

/**
 * 지금 이 키가 향할 대상 하나. (ViewModel 데이터 계약 - 상호작용 프롬프트)
 *
 * "범위 안에 있는 것" 이 아니라 "실제로 잡힐 것" 만 담는다.
 * 박스 반경과 도구 충전 사거리가 둘 다 150cm 라 박스 앞에서는 두 후보가 거의 항상 겹치는데,
 * 범위 기준으로 띄우면 실행되지 않을 프롬프트까지 같이 떠서 애매함이 화면으로 옮겨갈 뿐이다.
 */
USTRUCT(BlueprintType)
struct FCWInteractionPrompt
{
	GENERATED_BODY()

	/** 프롬프트를 띄울 액터. 목록에 들어간 원소라면 항상 유효하다. */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> TargetActor = nullptr;

	/**
	 * 눌러야 할 키. 위젯에 E/R 을 적지 말고 이 값을 그대로 그린다.
	 *
	 * 실제로 등록된 매핑에서 뽑아오므로, 키 배치가 또 바뀌어도 위젯을 고칠 일이 없다.
	 * 매핑을 못 찾으면 EKeys::Invalid 가 들어온다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	FKey Key;

	/** 라벨 문구를 고르는 값. */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	ECWInteractionPromptAction Action = ECWInteractionPromptAction::None;

	bool IsValidPrompt() const
	{
		return TargetActor != nullptr && Action != ECWInteractionPromptAction::None;
	}

	// 값이 실제로 바뀐 프레임에만 델리게이트를 쏘려고 직전 목록과 비교한다.
	bool operator==(const FCWInteractionPrompt& Other) const
	{
		return TargetActor == Other.TargetActor
			&& Key == Other.Key
			&& Action == Other.Action;
	}

	bool operator!=(const FCWInteractionPrompt& Other) const
	{
		return !(*this == Other);
	}
};
