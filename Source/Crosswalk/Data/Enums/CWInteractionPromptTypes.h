// Copyright Crosswalk Team. All Rights Reserved.
// CWInteractionPromptTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWInteractionPromptTypes.generated.h"

/**
 * 대상 머리 위 프롬프트에 띄울 동작. 위젯은 이 값으로 문구를 고른다.
 *
 * 문구(FText)를 같이 실어보내지 않는 이유: 문구 한 글자 고치는 데 C++ 빌드가 필요해지면
 * UI 쪽이 자기 작업만으로 화면을 못 고친다.
 *
 * 대신 항목이 늘어나도 위젯에서 컴파일 에러가 나지 않는다. 매핑이 빠진 항목은 조용히
 * 빈칸으로 그려지므로, 여기에 값을 추가할 때는 UI 담당에게 알려야 한다.
 * (ViewModel 데이터 계약 4-2 공통 요청)
 */
UENUM(BlueprintType)
enum class ECWInteractionPromptAction : uint8
{
	None				UMETA(DisplayName = "없음"),
	// E - 바닥에 놓인 도구를 집는다.
	PickUpTool			UMETA(DisplayName = "집기"),
	// E - 들고 있는 도구를 내려놓는다.
	DropTool			UMETA(DisplayName = "내려놓기"),
	// E - 작업차량 등 일반 상호작용 대상.
	Interact			UMETA(DisplayName = "상호작용"),
	// E - 보급 박스에서 페인트 통을 꺼낸다. (기획서 6-1)
	PickUpCan			UMETA(DisplayName = "통 꺼내기"),
	// R - 사거리 안 도구에 페인트를 붓는다. (기획서 6-3)
	ChargeTool			UMETA(DisplayName = "페인트 충전"),
	// R - 보급 박스에 페인트 통을 반납한다. (기획서 6-2)
	ReturnCan			UMETA(DisplayName = "통 반납"),
	// R - 아스팔트 건을 재장전한다. (싱크홀 기획서 7-5)
	ReloadAsphaltGun	UMETA(DisplayName = "재장전")
};
