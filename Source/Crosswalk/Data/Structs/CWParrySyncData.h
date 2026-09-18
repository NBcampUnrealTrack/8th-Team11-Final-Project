// CWParrySyncData.h

#pragma once

#include "CoreMinimal.h"
#include "CWParrySyncData.generated.h"

class ACWBaseVehicle;

USTRUCT(BlueprintType)
struct FCWParrySyncData
{
	GENERATED_BODY()

public:
	// 서버가 확정한 패링 발생 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector ParryLocation = FVector::ZeroVector;

	// 서버가 확정한 패링 방향
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector ParryDirection = FVector::ForwardVector;

	// 패링 대상 차량. 언제나 차량이라 AActor 로 넓혀 두지 않는다 - 받는 쪽이 캐스트할 이유가 없다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	TObjectPtr<ACWBaseVehicle> TargetVehicle = nullptr;

	// 차종별 연출 분기를 위한 ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FName VehicleType = NAME_None;

	// 패링 성공 시 차량이 밀려나거나 멈추는 힘
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	float VehicleReactionStrength = 0.f;

	/**
	 * 이번 스윙에 쓸 소품. UCWParryPropCatalog::Props 의 인덱스다. 소품이 없으면 -1.
	 *
	 * 서버가 뽑아서 여기 실어 보낸다. 클라이언트가 각자 무작위로 고르면 내 화면에서는
	 * 배트인데 남의 화면에서는 형광봉인 상태가 되고, 사운드까지 어긋나 티가 크게 난다.
	 *
	 * 복제 변수를 따로 두지 않고 이 구조체에 넣은 이유. 몬타주 재생과 인덱스가 서로 다른
	 * 경로로 가면 도착 순서가 보장되지 않아, 인덱스가 늦게 오면 이전 소품이 잠깐 나온다.
	 * 한 패킷에 같이 실으면 그 문제가 생길 수 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	int32 PropIndex = INDEX_NONE;

};