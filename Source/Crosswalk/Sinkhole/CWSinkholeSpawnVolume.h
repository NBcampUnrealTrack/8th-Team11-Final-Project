// CWSinkholeSpawnVolume.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWSinkholeSpawnVolume.generated.h"

class ACWSinkhole;
class UBoxComponent;

/**
 * 싱크홀이 생길 수 있는 범위. 레벨에 박스를 놓고 그 안에서 무작위로 발생시킨다.
 * (싱크홀 기획서 3-1)
 *
 * 위치를 고정하지 않는 이유: 스테이지를 반복 플레이하면 동선을 외워서 무력화할 수 있다.
 * 매 판 다른 곳에 생겨야 압박 지점이 매번 달라진다.
 *
 * 스테이지마다 이 볼륨을 다르게 배치해 플레이 흐름을 설계한다.
 * 차도 위에 걸쳐 두면 회피 동선이 좁아지고, 작업 구역 근처에 두면 작업 중 압박이 커진다.
 *
 * 서버에서만 스폰한다. 싱크홀 액터가 리플리케이트되므로 클라이언트는 받기만 한다.
 */
UCLASS()
class CROSSWALK_API ACWSinkholeSpawnVolume : public AActor
{
	GENERATED_BODY()

public:
	ACWSinkholeSpawnVolume();

	virtual void BeginPlay() override;

	/** 지금 즉시 하나 생성한다. 서버에서만 유효하다. @return 생성했으면 true */
	UFUNCTION(BlueprintCallable, Category = "Sinkhole|Spawn")
	bool SpawnSinkhole();

	/** 주기 스폰을 켜고 끈다. 스테이지 시작/종료에서 부른다. 서버에서만 유효하다. */
	UFUNCTION(BlueprintCallable, Category = "Sinkhole|Spawn")
	void SetSpawningEnabled(bool bEnabled);

private:
	/** 스폰 주기가 돌아왔을 때. 서버에서만 돈다. */
	void HandleSpawnTimer();

	/**
	 * 볼륨 안에서 기존 싱크홀과 겹치지 않는 지점을 찾는다.
	 * @return 찾았으면 true. 시도 횟수 안에 못 찾으면 false.
	 */
	bool FindSpawnLocation(FVector& OutLocation) const;

	/** 지금 살아 있는 싱크홀 수. 동시 개수 제한에 쓴다. */
	int32 CountActiveSinkholes() const;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn")
	TObjectPtr<UBoxComponent> SpawnBounds;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn")
	TSubclassOf<ACWSinkhole> SinkholeClass;

	// 레벨 시작과 동시에 주기 스폰을 시작할지. 게임모드가 직접 켜고 싶으면 false 로 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn")
	bool bAutoStart = true;

	// 스폰 간격(초). 이 값과 동시 개수 제한이 스테이지 난이도를 만든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "1.0"))
	float SpawnInterval = 20.f;

	// 첫 스폰까지의 지연(초). 시작하자마자 발밑에 생기면 준비할 시간이 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "0.0"))
	float InitialSpawnDelay = 15.f;

	// 동시에 존재할 수 있는 싱크홀 수.
	//
	// 상한이 없으면 방치할수록 도로가 구멍투성이가 되어 회복이 불가능해진다.
	// 플레이어가 따라잡을 수 있는 선을 여기서 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "1"))
	int32 MaxConcurrentSinkholes = 3;

	// 싱크홀끼리 떨어져야 하는 최소 거리(cm).
	//
	// 겹쳐서 생기면 하나처럼 보여서 몇 발을 쏴야 하는지 읽히지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "0.0"))
	float MinDistanceBetweenSinkholes = 500.f;

	// 지점을 찾는 최대 시도 횟수. 볼륨이 좁으면 자리를 못 찾을 수 있어 무한 루프를 막는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "1"))
	int32 MaxPlacementAttempts = 16;

	// 볼륨 안에서 뽑은 지점에서 아래로 쏘는 트레이스 길이(cm). 도로 높이를 찾는 데 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sinkhole|Spawn", meta = (ClampMin = "10.0"))
	float GroundTraceDistance = 2000.f;

private:
	FTimerHandle SpawnTimerHandle;
};
