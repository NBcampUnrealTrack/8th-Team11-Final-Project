// CWTrafficVehicleSpawner.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficVehicleEntry.h"
#include "Data/CWTrafficVehicleSpawnParams.h"
#include "CWTrafficVehicleSpawner.generated.h"

class ACWBaseVehicle;
class ACWTrafficLane;
class UDataTable;

/**
 * 지정한 차선에 일정 주기로 차량을 흘려보낸다.
 * 왕복 2차선은 차선마다 스포너를 하나씩 두고 각자 간격을 굴린다.
 *
 * 개체별 편차(간격 / 속도 / 좌우 위치)와 차종 비율을 여기서 굴려 차량에 넘긴다.
 * 판 전체를 한 곡선으로 굴리는 값(주기·차종)은 ACWTrafficDifficulty 가 진행도에 따라 밀어 넣는다.
 */
UCLASS()
class CROSSWALK_API ACWTrafficVehicleSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACWTrafficVehicleSpawner();

	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void StartSpawning();

	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void StopSpawning();

	/**
	 * 지금 1대 스폰을 요청한다. 차선 입구가 앞차에 점유돼 있으면 거절한다.
	 *
	 * 자체 간격 타이머가 매번 부르는 경로이고, 밖에서 1대 넣고 싶을 때 쓸 유일한 공개 진입점이다.
	 * (신호 제어기는 스폰을 건드리지 않는다 - ACWTrafficDirector 클래스 주석. 지금 밖에서
	 * 부르는 곳은 없다) 차종 추첨·개체 편차·입구 점유 검사가 전부 여기를 지나므로,
	 * 어디서 부르든 같은 판정을 탄다.
	 *
	 * @return 스폰한 차량. 입구가 막혔거나 스폰 가능한 차종이 없으면 nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	ACWBaseVehicle* RequestSpawn();

	/**
	 * VehicleMix에서 가중치 추첨으로 차종을 고른다. 목록이 비면 VehicleClass로 떨어진다.
	 *
	 * BlueprintPure 가 아니다. 난수를 굴리는 함수를 Pure 로 두면 BP 에서 핀을 당길 때마다
	 * 다시 실행돼서, 같은 노드의 결과를 두 번 읽으면 서로 다른 차종이 나온다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	TSubclassOf<ACWBaseVehicle> PickVehicleClass() const;

	/**
	 * 스폰 간격을 바꾼다. ACWTrafficDifficulty 가 스테이지 진행도에 따라 부른다.
	 *
	 * 이미 걸려 있는 타이머는 건드리지 않고 **다음 차례부터** 적용된다. 남은 시간을
	 * 다시 계산해 끼워 넣으면 단계가 바뀌는 순간 차가 갑자기 튀어나오거나 한 박자
	 * 통째로 비는데, 그 순간만 부자연스러워지고 얻는 것이 없다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void SetSpawnInterval(float NewMin, float NewMax);

	/** 차종 구성을 바꾼다. 빈 목록은 무시한다 - 실수로 차가 아예 안 나오는 상태를 만들지 않기 위해서다. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void SetVehicleMix(const TArray<FCWTrafficVehicleEntry>& NewMix);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 1대 스폰한 뒤 다음 간격을 뽑아 다시 타이머를 건다. */
	void HandleSpawnTimer();

	/**
	 * 차종을 지정해 실제로 스폰한다. 입구 점유는 보지 않는다.
	 *
	 * 블루프린트에 열지 않는다. 입구 점유 판정(IsLaneEntranceBlocked)은 "가장 최근에 스폰한
	 * 차가 맨 뒤" 를 전제하는데, 밖에서 임의 거리에 스폰하면 그 전제가 깨져 다음 차가
	 * 겹쳐 나온다. 밖에서 1대 넣고 싶으면 RequestSpawn 이다.
	 */
	ACWBaseVehicle* SpawnVehicleOfClass(TSubclassOf<ACWBaseVehicle> SpawnClass, float StartDistance);

	/**
	 * 개체별 편차를 굴린다. 서버에서만 부른다.
	 *
	 * 속도는 여기서 굴리지 않는다. 차종 이름은 뽑힌 차체가 들고 있고 그 추첨은
	 * 차량 쪽에 있어서, 여기서는 범위표 참조만 실어 보낸다.
	 */
	FCWTrafficVehicleSpawnParams RollSpawnParams(float StartDistance) const;

	/** 레벨 시작 시점에 차선 위에 이미 굴러가는 차를 흩뿌린다. */
	void PrewarmLane();

	/** 하한이 상한보다 크게 설정돼도 안전하도록 정리한 스폰 간격. */
	float RollSpawnInterval() const;

	/** 스폰 가능한 차종이 하나라도 설정돼 있는지. */
	bool HasSpawnableVehicleClass() const;

	/** 차종 CDO에서 전체 길이(cm)를 읽는다. */
	float GetVehicleLengthOf(TSubclassOf<ACWBaseVehicle> InClass) const;

	/** 등장할 수 있는 차종 중 가장 긴 길이(cm). 조합을 모르는 프리웜의 보수적 기준. */
	float GetLargestVehicleLength() const;

	/** 길이가 다른 두 차량의 중심이 최소한 떨어져 있어야 하는 거리(cm). */
	float GetRequiredCenterGap(float LengthA, float LengthB) const;

	/** 들어올 차종 기준으로 차선 입구가 아직 앞차에 점유돼 있는지. */
	bool IsLaneEntranceBlocked(TSubclassOf<ACWBaseVehicle> IncomingClass) const;

	/**
	 * 같은 TargetLane 을 보는 스포너가 또 있으면 경고한다.
	 *
	 * 복제해서 만들면 차선 참조가 딸려오는데, 그 상태는 크래시도 겹침도 없이 조용히
	 * 틀린다 - 다른 차선만 비고 "저 줄에만 차가 나온다" 로 보인다.
	 */
	void WarnOnSharedTargetLane() const;

protected:
	/** 차량을 흘려보낼 차선. 레벨에 배치한 ACWTrafficLane 을 지정한다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Spawner")
	TObjectPtr<ACWTrafficLane> TargetLane;

	/**
	 * 섞어 내보낼 차종과 비율. 비워두면 VehicleClass 한 종류만 나온다.
	 * ACWTrafficDifficulty 의 단계에 VehicleMix 가 있으면 그 값으로 덮어써진다 (로그가 남는다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	TArray<FCWTrafficVehicleEntry> VehicleMix;

	/** VehicleMix가 비었을 때 쓰는 단일 차종. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	TSubclassOf<ACWBaseVehicle> VehicleClass;

	/** 스폰 간격 하한(초). 기획 값 5초. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.1"))
	float SpawnIntervalMin = 5.f;

	/** 스폰 간격 상한(초). 기획 값 8초. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.1"))
	float SpawnIntervalMax = 8.f;

	/** BeginPlay에 바로 굴릴지. 신호등 연동 시 끄고 수동으로 시작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	bool bAutoStart = true;

	/** 첫 차량이 나오기까지의 고정 지연(초). 차선끼리 스폰이 겹치는 걸 피할 때 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0"))
	float InitialDelay = 0.f;

	/**
	 * 첫 스폰 타이밍도 랜덤으로 흔들지.
	 * 끄면 매 플레이마다 시작 그림이 똑같아진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	bool bRandomizeFirstSpawn = true;

	/**
	 * 레벨 시작 시점에 차선 위에 미리 깔아둘 차량 수.
	 * 0이면 빈 도로에서 시작한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0"))
	int32 PrewarmVehicleCount = 1;

	/**
	 * 차종별 주행 속도 범위표(FCWVehicleSpeedRow). 비워 두면 모든 차가 차종 기본 속도로 달린다.
	 *
	 * **레벨의 스포너 인스턴스마다 꽂지 말고 BP_VehicleSpawner 애셋 기본값에 한 번만 꽂는다.**
	 * 차종 속도는 스테이지마다 달라질 값이 아니라서, 인스턴스마다 두면 레벨을 늘릴 때마다
	 * 같은 표를 다시 지정하게 되고 한 군데를 빠뜨리면 그 차선만 조용히 기본 속도로 달린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	TObjectPtr<UDataTable> VehicleSpeedTable;

	/** 차선 중심선 대비 좌우로 흔들 최대 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0"))
	float LaneOffsetRange = 20.f;

	/** 두 차량의 차체 사이에 확보할 여유 간격(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0"))
	float SpawnClearance = 200.f;

private:
	FTimerHandle SpawnTimerHandle;

	/** 가장 최근에 스폰한 차량. 차선 입구 점유 확인용. */
	TWeakObjectPtr<ACWBaseVehicle> LastSpawnedVehicle;
};
