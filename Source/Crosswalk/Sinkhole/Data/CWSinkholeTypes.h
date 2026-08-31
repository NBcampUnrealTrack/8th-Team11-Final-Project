// CWSinkholeTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWSinkholeTypes.generated.h"

/**
 * 싱크홀의 생애 단계. (싱크홀 기획서 3-2)
 *
 * Warning 을 따로 두는 이유: 예고 없이 발밑에 생겨서 빠지면 플레이어는 자기 실수가 아니라
 * 게임 탓으로 받아들인다. 균열이 먼저 번지는 1초 남짓의 구간을 만들어 두면
 * "피할 수 있었는데 못 피했다" 가 되어 페널티를 납득한다.
 *
 * Warning 동안에는 낙하 콜리전이 꺼져 있다. 보이기 시작한 순간부터 빠지면 예고의 의미가 없다.
 */
UENUM(BlueprintType)
enum class ECWSinkholeState : uint8
{
	Warning		UMETA(DisplayName = "예고"),
	Active		UMETA(DisplayName = "활성"),
	Filled		UMETA(DisplayName = "복구 완료")
};
