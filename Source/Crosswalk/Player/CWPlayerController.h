#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CWPlayerController.generated.h"

/**
 * 쿼터뷰 플레이어 컨트롤러.
 *
 * 지금은 마우스 커서를 켜는 게 전부지만 반드시 필요하다.
 * UCWWorkerComponent 가 커서 아래를 트레이스해서 작업 지점을 잡기 때문에
 * 커서가 꺼져 있으면 도구를 들고 있어도 게이지가 전혀 오르지 않는다.
 */
UCLASS()
class CROSSWALK_API ACWPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACWPlayerController();
};
