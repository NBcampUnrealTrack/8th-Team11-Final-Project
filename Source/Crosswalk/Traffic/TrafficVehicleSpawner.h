// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TrafficVehicleSpawnParams.h"
#include "TrafficVehicleSpawner.generated.h"

class ABaseVehicle;
class ATrafficLane;

/**
 * 지정한 차선에 일정 주기로 차량을 흘려보낸다.
 * 왕복 2차선은 차선마다 스포너를 하나씩 두고 각자 간격을 굴린다.
 *
 * 개체별 편차(간격 / 속도 / 좌우 위치)를 여기서 굴려 차량에 넘긴다.
 * 트래픽 프로파일 데이터가 붙으면 이 값들이 데이터 테이블로 빠진다.
 */
UCLASS()
class CROSSWALK_API ATrafficVehicleSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATrafficVehicleSpawner();

	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void StartSpawning();

	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	void StopSpawning();

	/** 간격을 무시하고 즉시 1대 스폰. 테스트용이자 내부 스폰 경로. */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Spawner")
	ABaseVehicle* SpawnVehicle(float StartDistance = 0.f);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 1대 스폰한 뒤 다음 간격을 뽑아 다시 타이머를 건다. */
	void HandleSpawnTimer();

	/** 개체별 편차를 굴린다. */
	FTrafficVehicleSpawnParams RollSpawnParams(float StartDistance) const;

	/** 레벨 시작 시점에 차선 위에 이미 굴러가는 차를 흩뿌린다. */
	void PrewarmLane();

	/** 하한이 상한보다 크게 설정돼도 안전하도록 정리한 스폰 간격. */
	float RollSpawnInterval() const;

protected:
	/** 차량을 흘려보낼 차선. 레벨에 배치한 ATrafficLane을 지정한다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Spawner")
	TObjectPtr<ATrafficLane> TargetLane;

	/** 스폰할 차량 클래스. BP_Vehicle을 지정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner")
	TSubclassOf<ABaseVehicle> VehicleClass;

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
	 * 차종 기본 속도에 적용할 편차 비율. 0.1이면 ±10%.
	 * 크게 주면 패링 타이밍 학습이 어려워지므로 밸런스 값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpeedJitter = 0.1f;

	/** 차선 중심선 대비 좌우로 흔들 최대 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Spawner", meta = (ClampMin = "0.0"))
	float LaneOffsetRange = 40.f;

private:
	FTimerHandle SpawnTimerHandle;
};
