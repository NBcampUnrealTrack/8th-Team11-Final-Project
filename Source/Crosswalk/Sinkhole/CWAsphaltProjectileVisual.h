// CWAsphaltProjectileVisual.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CWAsphaltProjectileVisual.generated.h"

class UStaticMeshComponent;

/**
 * 아스팔트 건이 쏜 덩어리의 궤적 연출. (싱크홀 기획서 7-6, 7-12)
 *
 * 이름 그대로 연출만 한다. 판정은 이미 서버가 발사 시점에 끝냈고, 이 액터는 그 결과를
 * 눈으로 보여주기만 한다. 그래서 콜리전이 없고, 리플리케이트되지 않는다.
 *
 * 리플리케이트하지 않는 이유
 *   매 프레임 위치를 동기화하는 순간 움직이는 차량과 똑같은 문제가 생긴다.
 *   대신 ACWAsphaltGun::Multicast_PlayProjectile 이 (시작점, 착탄점, 비행시간) 세 값만
 *   한 번 뿌리고, 각 머신이 이 액터를 로컬로 스폰해 자기 시계로 돌린다.
 *   최악의 경우 궤적이 몇 픽셀 다르게 보일 뿐 판정은 절대 어긋나지 않는다.
 *
 * 콜리전을 켜면 안 되는 이유
 *   판정과 연출을 분리해 둔 설계가 통째로 무너진다. 날아가는 덩어리가 플레이어를 밀거나
 *   차량에 걸리면, 서버가 확정한 착탄점에 도달하지 못하는 일까지 생긴다.
 *
 * 포물선
 *   중력을 시뮬레이션하지 않는다. 직선 보간에 아치 높이만 얹는다.
 *     XY = Lerp(Start, Impact, t)
 *     Z  = Lerp(Start.Z, Impact.Z, t) + ArcHeight * 4t(1-t)
 *   4t(1-t) 는 t=0 과 t=1 에서 0, t=0.5 에서 1 이 되는 곡선이라 양 끝이 시작점과
 *   착탄점에 정확히 붙는다. 착탄점이 이미 정해져 있으므로 물리를 풀 이유가 없다.
 */
UCLASS()
class CROSSWALK_API ACWAsphaltProjectileVisual : public AActor
{
	GENERATED_BODY()

public:
	ACWAsphaltProjectileVisual();

	virtual void Tick(float DeltaTime) override;

	/**
	 * 궤적을 시작한다. 스폰 직후 각 머신에서 한 번만 부른다.
	 *
	 * @param InStartLocation  총구 위치
	 * @param InImpactLocation 서버가 확정한 착탄점
	 * @param InFlightTime     비행 시간(초). 0 이하면 즉시 사라진다.
	 */
	void Launch(const FVector& InStartLocation, const FVector& InImpactLocation, float InFlightTime);

protected:
	/** 착탄한 순간. BP 자식에서 튀는 파티클과 사운드를 붙이는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt")
	void OnImpactReached(const FVector& ReachedLocation);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asphalt")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

protected:
	// 아치 높이를 비행 거리의 몇 배로 잡을지.
	//
	// 고정값으로 두면 멀리 쏠 때 궤적이 납작해 보인다. 거리에 비례시켜야 어느 거리에서도
	// 같은 무게감으로 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt", meta = (ClampMin = "0.0"))
	float ArcHeightRatio = 0.25f;

private:
	FVector StartLocation = FVector::ZeroVector;
	FVector ImpactLocation = FVector::ZeroVector;

	// 이번 발사의 아치 높이(cm). 거리에서 미리 계산해 둔다.
	float ArcHeight = 0.f;

	float FlightTime = 0.f;
	float ElapsedTime = 0.f;
};
