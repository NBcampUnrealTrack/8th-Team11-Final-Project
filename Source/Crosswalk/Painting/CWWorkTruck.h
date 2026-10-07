// CWWorkTruck.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Enums/CWToolTypes.h"
#include "Interfaces/Interface_Interactable.h"
#include "CWWorkTruck.generated.h"

class ACWBaseTool;
class UStaticMeshComponent;

/**
 * 스테이지 시작 지점에 놓는 작업차량. (작업차량 기획서 3절)
 *
 * 하는 일은 도구 보관함 하나다. 스테이지가 시작되면 모든 작업 도구가 트렁크에 들어가 있고,
 * 플레이어는 상호작용 키로 꺼내거나 반납한다.
 *
 * 휴식구역은 이 액터가 갖고 있지 않다. ACWRestZone 을 차량 앞에 따로 놓는다.
 * 둘을 한 액터로 묶으면 배치할 때 "차량 앞" 거리를 조절할 수가 없다.
 *
 * 도구를 바닥에 늘어놓지 않고 트렁크에 넣는 이유 (기획서 2.1)
 *   플레이어의 첫 행동이 "차량에서 도구를 꺼낸다" 가 되면서 작업자 역할에 몰입하게 되고,
 *   차량이 스테이지의 시작점이자 복귀 지점으로 기능한다.
 *
 * 1인 제한을 걸지 않는 이유
 *   휴식구역과 달리 트렁크는 여러 명이 동시에 써도 된다. 스테이지 시작 직후 전원이 도구를
 *   꺼내는 구간인데 여기에 순서를 강제하면 시작부터 대기열이 생겨 리듬이 죽는다.
 *   같은 도구를 두 명이 집는 경합은 ACWBaseTool::TryPickUp 이 서버에서 이미 막는다.
 *
 * 보관 중인 도구의 상태
 *   ACWBaseTool::SetStored 로 "트렁크에 있다" 를 켜 준다. 도구는 그 값을 복제하면서 자기를 숨기고,
 *   집기와 충전 대상에서 스스로 빠진다. 꺼내는 경로가 이 액터 하나로 묶이는 것은 그 덕분이다.
 *   콜리전은 내리지 않는다. 이유는 ACWBaseTool 의 bStored 주석에 적어 두었다.
 *
 * 네트워크
 *   꺼내기/반납 판정은 전부 서버에서 한다. 클라이언트는 캐릭터의 상호작용 RPC 를 거쳐 들어온다.
 *   보관 목록은 리플리케이트해서 클라이언트의 CanInteract 가 "꺼낼 도구가 남았는가" 를 판정할 수 있게 한다.
 */
UCLASS()
class CROSSWALK_API ACWWorkTruck : public AActor, public IInterface_Interactable
{
	GENERATED_BODY()

public:
	ACWWorkTruck();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/**
	 * 상호작용 키. 서버에서만 들어온다.
	 * 도구를 들고 있으면 반납하고, 맨손이면 트렁크에서 하나 꺼낸다.
	 */
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;

	/** 반납할 도구가 있거나 꺼낼 도구가 남아 있을 때만 상호작용 대상이 된다. */
	virtual bool CanInteract_Implementation(APawn* InstigatorPawn) const override;

	/**
	 * 거리 판정은 트렁크 문 기준이다.
	 *
	 * 차가 길어서 원점으로 재면 트렁크 앞에 서 있어도 3~4 m 로 나온다. 그러면 캐릭터가
	 * "가장 가까운 대상" 을 고를 때 발밑의 도구에 밀려 차량이 선택되지 않는다.
	 */
	virtual FVector GetInteractionLocation_Implementation() const override;

	/**
	 * 도구를 트렁크에 넣는다. 서버에서만 유효하다.
	 * 누가 들고 있는 도구는 넣지 않는다. 먼저 홀더에서 내려놓아야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorkTruck")
	bool StoreTool(ACWBaseTool* Tool);

	/** 트렁크에서 도구 하나를 꺼내 이 폰에 쥐여준다. 서버에서만 유효하다. */
	UFUNCTION(BlueprintCallable, Category = "WorkTruck")
	bool TakeOutTool(APawn* InstigatorPawn);

	/** 도구를 꺼내고 넣는 지점. 트렁크 문 앞이다. */
	UFUNCTION(BlueprintPure, Category = "WorkTruck")
	FVector GetTrunkLocation() const;

private:
	/** 레벨에 놓인 도구를 전부 트렁크에 넣는다. 서버에서만. */
	void CollectStageTools();

	/** 꺼내기 공통 처리. 도구를 트렁크 앞에 꺼내 놓고 홀더에 넘긴다. */
	bool GiveToolToPawn(ACWBaseTool* Tool, APawn* InstigatorPawn);

public:
	// 차량 몸체. 루트 컴포넌트다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorkTruck")
	TObjectPtr<UStaticMeshComponent> TruckMesh;

	// 트렁크 문 위치. 도구가 여기서 나오고 여기로 들어간다.
	// 상호작용 거리도 액터 원점이 아니라 이 지점 기준으로 잰다. 차가 길어서 원점 기준이면
	// 트렁크 앞에 서 있는데도 멀다고 판정된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorkTruck")
	TObjectPtr<USceneComponent> TrunkPoint;

protected:
	// 스테이지 시작 시 레벨에 놓여 있던 도구를 전부 트렁크에 넣을 것인가.
	//
	// 켜두면 기존처럼 레벨에 도구를 배치해도 시작할 때 트렁크로 들어간다.
	// 트렁크에 넣을 도구를 레벨에 배치하지 않게 되면 꺼도 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorkTruck")
	bool bCollectExistingTools = true;

	// 이 거리 안에서만 트렁크를 쓸 수 있다. (cm) TrunkPoint 기준이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorkTruck", meta = (ClampMin = "0.0"))
	float InteractRadius = 250.f;

	// 서버가 거리를 다시 잴 때 쓰는 여유값. 클라이언트와 서버의 위치가 핑만큼 어긋나 있어서
	// 딱 맞게 재면 정상 요청도 거절된다. (홀더의 ServerRangeTolerance 와 같은 이유)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorkTruck", meta = (ClampMin = "1.0"))
	float ServerRangeTolerance = 1.6f;

private:
	// 트렁크에 들어 있는 도구들. 서버가 정하고 전원에게 전파된다.
	UPROPERTY(Replicated)
	TArray<TObjectPtr<ACWBaseTool>> StoredTools;
};
