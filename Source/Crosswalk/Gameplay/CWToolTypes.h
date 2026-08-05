// CWToolTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWToolTypes.generated.h"

/**
 * 작업 현장에 존재하는 손 도구.
 *
 * GameplayTag 대신 enum 을 쓴다. 도구가 몇 개뿐이고, enum 이면 에디터 드롭다운으로 골라서 끝난다.
 *
 * 설계 규칙: 도구 종류마다 월드에 실물 액터가 하나만 존재한다.
 * 그래서 같은 종류를 두 사람이 동시에 들 수 없다. (ACWBaseTool 이 서버에서 경합을 막는다)
 */
UENUM(BlueprintType)
enum class ECWToolType : uint8
{
	None			UMETA(DisplayName = "없음"),
	Broom			UMETA(DisplayName = "빗자루"),
	Liner			UMETA(DisplayName = "라인 테두리 도구"),
	WhiteRoller		UMETA(DisplayName = "안쪽 채우기 롤러"),
	BlackRoller		UMETA(DisplayName = "검은색 롤러"),
	AsphaltPatcher	UMETA(DisplayName = "아스팔트 패처")
};
