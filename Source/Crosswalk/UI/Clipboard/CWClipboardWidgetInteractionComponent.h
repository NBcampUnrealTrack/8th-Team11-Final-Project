// CWClipboardWidgetInteractionComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "CWClipboardWidgetInteractionComponent.generated.h"

// 카메라와 프롭 갱신 후 같은 프레임의 종이 위치로 입력을 처리함
UCLASS()
class CROSSWALK_API UCWClipboardWidgetInteractionComponent : public UWidgetInteractionComponent
{
	GENERATED_BODY()

public:
	void UpdatePointer(const FHitResult& InHit)
	{
		SetCustomHitResult(InHit);
		SimulatePointerMovement();
	}

	// 가상 포인터로 선택한 입력칸에 실제 키보드와 IME 포커스를 넘김
	void FocusHardwareUser(int32 UserIndex);
};
