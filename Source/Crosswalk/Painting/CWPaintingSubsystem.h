// CWPaintingSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CWPaintingSubsystem.generated.h"

class ACWBaseWorkZone;
class UCWWorkerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnWorkZoneRegisteredSignature, ACWBaseWorkZone*, Zone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnWorkZoneUnregisteredSignature, ACWBaseWorkZone*, Zone);

/**
 * 살아 있는 작업 존과 작업자 목록을 들고 있는 조회 창구. (UTrafficSubsystem 과 같은 구조)
 *
 * 왜 필요한가
 *   TActorIterator 는 "그 클래스만 모아둔 목록" 을 도는 게 아니다. 월드의 모든 레벨 액터
 *   배열을 훑으면서 항목마다 타입 테스트를 한다. 그래서 존이 5개든 500개든 스캔 비용이
 *   똑같이 "레벨 액터 총 개수" 에 비례한다. 매 프레임 도는 자리에서는 그대로 낭비가 된다.
 *
 *   Dev 맵은 액터가 적어서 티가 안 나지만, 도심 레벨이 올라오면 선형으로 나빠진다.
 *   폰을 찾을 때는 더 나쁘다. 액터를 훑는 것도 모자라 폰마다 컴포넌트 배열을 또 훑는다.
 *
 * 등록은 권한과 무관하게 모든 머신에서 한다
 *   붓 자취를 찍는 존은 서버뿐 아니라 각 클라이언트에서도 목록을 봐야 한다.
 *   작업 상태와 도구가 리플리케이트되므로 모든 머신이 RPC 없이 같은 판단을 내리는 구조인데,
 *   목록이 서버에만 있으면 원격 화면에서만 자취가 사라진다.
 *
 * 존 등록 이벤트를 여는 이유
 *   ACWGameMode 가 시작할 때 한 번만 존을 모으면, 레벨 스트리밍이나 런타임 스폰으로 늦게
 *   들어온 존이 목표 체크리스트에 영영 안 잡힌다. 등록 시점에 알려주면 그 구멍이 막힌다.
 */
UCLASS()
class CROSSWALK_API UCWPaintingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** WorldContext 로부터 서브시스템을 얻는다. 월드가 없으면 nullptr. */
	static UCWPaintingSubsystem* Get(const UObject* WorldContextObject);

	void RegisterZone(ACWBaseWorkZone* Zone);
	void UnregisterZone(ACWBaseWorkZone* Zone);

	void RegisterWorker(UCWWorkerComponent* Worker);
	void UnregisterWorker(UCWWorkerComponent* Worker);

	/**
	 * 등록된 존 목록. 배열을 그대로 넘겨 매 프레임 호출에서도 할당이 생기지 않게 한다.
	 * 호출부는 약한 참조를 Get() 으로 풀면서 이미 사라진 항목을 건너뛰어야 한다.
	 */
	const TArray<TWeakObjectPtr<ACWBaseWorkZone>>& GetZones() const { return ActiveZones; }

	const TArray<TWeakObjectPtr<UCWWorkerComponent>>& GetWorkers() const { return ActiveWorkers; }

private:
	TArray<TWeakObjectPtr<ACWBaseWorkZone>> ActiveZones;

	TArray<TWeakObjectPtr<UCWWorkerComponent>> ActiveWorkers;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Painting")
	FCWOnWorkZoneRegisteredSignature OnWorkZoneRegistered;

	UPROPERTY(BlueprintAssignable, Category = "Painting")
	FCWOnWorkZoneUnregisteredSignature OnWorkZoneUnregistered;
};
