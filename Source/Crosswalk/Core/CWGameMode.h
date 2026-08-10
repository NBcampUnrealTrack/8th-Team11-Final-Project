#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CWGameMode.generated.h"

class ACWBaseWorkZone;
class ACWGameState;

/**
 * 프로토타입 게임모드.
 *
 * 기본값으로 C++ 캐릭터/컨트롤러를 물려 둔다.
 * 메쉬와 입력 애셋을 붙인 BP_PlayerCharacter 를 만들었으면
 * 이 클래스를 상속한 BP_GameMode 에서 DefaultPawnClass 만 갈아 끼우면 된다.
 *
 * 스테이지 판정 (기획서 2절 코어 루프의 마지막 단계)
 *   레벨에 놓인 작업 존을 전부 모아 두고, 진행도가 바뀔 때마다 완료 개수를 다시 센다.
 *   전부 끝나면 클리어다. 결과는 ACWGameState 에 실어 전원에게 보낸다.
 *
 *   완료 개수를 누적하지 않고 매번 다시 세는 이유:
 *   완료는 되돌아갈 수 있다. 차량이 페인트를 밟으면 끝났던 존이 다시 열린다.
 *   (ACWBaseWorkZone::SetProgress 주석 참고) 카운터를 올리는 방식이면 그 순간 어긋난다.
 */
UCLASS()
class CROSSWALK_API ACWGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACWGameMode();

	virtual void BeginPlay() override;

private:
	/** 레벨의 작업 존을 모아 진행도 알림을 구독한다. */
	void CollectWorkZones();

	/** 완료된 존 수를 다시 세어 게임 상태에 반영하고, 전부 끝났으면 클리어로 넘긴다. */
	void RefreshStageProgress();

	UFUNCTION()
	void HandleZoneProgress(ACWBaseWorkZone* Zone, float Progress);

private:
	// 이 스테이지의 작업 목표들. 서버에서만 채운다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACWBaseWorkZone>> WorkZones;
};
