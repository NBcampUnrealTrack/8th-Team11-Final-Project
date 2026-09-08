// CWParrySyncData.h

#pragma once

#include "CoreMinimal.h"
#include "CWParrySyncData.generated.h"

class AActor;

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

	// 패링 대상 차량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	TObjectPtr<AActor> TargetVehicle = nullptr;

	// 차종별 연출 분기를 위한 ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FName VehicleType = NAME_None;

	// 패링 성공 시 차량이 밀려나거나 멈추는 힘
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	float VehicleReactionStrength = 0.f;

};