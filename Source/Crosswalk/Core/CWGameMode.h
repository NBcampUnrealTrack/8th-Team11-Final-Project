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
 *   존이 UCWPaintingSubsystem 에 등록될 때마다 목록에 넣고, 진행도가 바뀌면 완료 개수를 다시 센다.
 *   시작할 때 한 번만 모으지 않는 이유: 레벨 스트리밍이나 런타임 스폰으로 늦게 들어온 존이
 *   체크리스트에 영영 안 잡힌다.
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

	// 연결된 플레이어 전원을 방 대기실 맵으로 이동
	// Listen Server 서버에서만 실행 가능
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Stage")
	bool ReturnToRoom();

private:
	/** 완료된 존 수를 다시 세어 게임 상태에 반영하고, 전부 끝났으면 클리어로 넘긴다. */
	void RefreshStageProgress();

	/**
	 * 집계를 다음 틱에 한 번만 돌도록 예약한다.
	 * 존이 한꺼번에 등록돼도 집계는 한 번만 돈다.
	 */
	void ScheduleStageProgressRefresh();

	/** 예약된 집계를 실제로 돌린다. */
	void FlushStageProgressRefresh();

	/** 존이 등록되면 목표 목록에 넣고 진행도 알림을 구독한다. */
	UFUNCTION()
	void HandleZoneRegistered(ACWBaseWorkZone* Zone);

	/** 존이 사라지면 목표 목록에서 뺀다. 레벨 스트리밍으로 내려간 존이 여기로 온다. */
	UFUNCTION()
	void HandleZoneUnregistered(ACWBaseWorkZone* Zone);

	UFUNCTION()
	void HandleZoneProgress(ACWBaseWorkZone* Zone, float Progress);

protected:
	// 인게임 종료 후 돌아갈 방 대기실 맵
	// 대기실 맵은 Room GameMode를 사용
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Stage")
	TSoftObjectPtr<UWorld> RoomMap;

private:
	// 이 스테이지의 작업 목표들. 서버에서만 채운다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACWBaseWorkZone>> WorkZones;

	// 집계가 이미 예약돼 있는가. 같은 틱에 존이 여러 개 등록될 때 중복 예약을 막는다.
	bool bStageProgressRefreshScheduled = false;
};
