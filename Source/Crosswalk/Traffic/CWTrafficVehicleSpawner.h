// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficVehicleEntry.h"
#include "Data/CWTrafficVehicleSpawnParams.h"
#include "CWTrafficVehicleSpawner.generated.h"

class ACWBaseVehicle;
class ACWTrafficLane;

/**
 * 지정한 차선에 일정 주기로 차량을 흘려보낸다.
 * 왕복 2차선은 차선마다 스포너를 하나씩 두고 각자 간격을 굴린다.
 *
 * 개체별 편차(간격 / 속도 / 좌우 위치)와 차종 비율을 여기서 굴려 차량에 넘긴다.
 * 트래픽 프로파일 데이터 에셋이 붙으면 이 값들이 그쪽으로 빠진다.
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

	/** 차간 간격을 무시하고 즉시 1대 스폰. 차종은 비율대로 뽑는다. 테스트용. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	ACWBaseVehicle* SpawnVehicle(float StartDistance = 0.f);

	/** VehicleMix에서 가중치 추첨으로 차종을 고른다. 목록이 비면 VehicleClass로 떨어진다. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Spawner")
	TSubclassOf<ACWBaseVehicle> PickVehicleClass() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 1대 스폰한 뒤 다음 간격을 뽑아 다시 타이머를 건다. */
	void HandleSpawnTimer();

	/** 차종을 지정해 실제로 스폰한다. */
	ACWBaseVehicle* SpawnVehicleOfClass(TSubclassOf<ACWBaseVehicle> SpawnClass, float StartDistance);

	/** 개체별 편차를 굴린다. */
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

protected:
	/** 차량을 흘려보낼 차선. 레벨에 배치한 ATrafficLane을 지정한다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Spawner")
	TObjectPtr<ACWTrafficLane> TargetLane;

	/**
	 * 섞어 내보낼 차종과 비율. 비워두면 VehicleClass 한 종류만 나온다.
	 * 트래픽 프로파일 데이터로 승격될 때 이 배열이 그대로 에셋으로 옮겨간다.
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
	 * 차종 기본 속도에 적용할 편차 비율. 0.05면 ±5%.
	 * 크게 주면 패링 타이밍 학습이 어려워지므로 밸런스 값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpeedJitter = 0.05f;

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
