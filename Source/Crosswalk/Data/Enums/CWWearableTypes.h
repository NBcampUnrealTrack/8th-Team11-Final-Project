//CWWearableTypes.h


#pragma once

#include "CoreMinimal.h"
#include "CWWearableTypes.generated.h"

/*
기존 컴포넌트를 합치는 리팩토리 작업용 파일.
헬맷같은 본 따라가는 스태틱 메시의 경우는 소켓에 붙이고,
몸이나 팔 다리 같이 움직이는 부위의 옷 같은 경우는 리더 포즈 사용.(예전의 마스터포즈 컴포넌트)
 */
UENUM(BlueprintType)
enum class ECWWearAttachMode : uint8
{
	/**
	 * 소켓 부착. 스태틱 메시로 붙임.
	 */
	Socket			UMETA(DisplayName = "소켓 부착"),

	/**
	 *리더포즈. 스켈레톤을 몸과 같은 걸 사용한 메시여야 몸과 똑같이 관절 부분이 접힘.
	 */
	LeaderPose		UMETA(DisplayName = "리더 포즈"),

	/** 메시를 추가하지 않고 몸의 머티리얼을 교체한다. */
	BodyMaterial	UMETA(DisplayName = "몸 색상")
};
