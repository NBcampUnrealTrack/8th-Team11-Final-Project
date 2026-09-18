// CWTrafficMath.h

#pragma once

#include "CoreMinimal.h"

/**
 * 트래픽 파트가 공유하는 작은 기하 계산.
 *
 * 패링 후보 선별(UCWTrafficSubsystem::PassesAttemptFilter)과 발사 방향 데드존
 * (ACWBaseVehicle::ApplySelfLaunchDeadZone)이 같은 부채꼴 판정을 쓴다. 각자 코사인을
 * 따로 계산하던 때는 경계 비교가 한쪽은 <, 다른 쪽은 <= 로 갈려 있었다. 여기 하나로 모은다.
 */
namespace CWTrafficMath
{
	/**
	 * Direction 이 Axis 를 중심으로 한 부채꼴 안에 있는가. 경계는 안쪽으로 친다.
	 *
	 * @param FullAngleDegrees  부채꼴 전체 각(도). 반각이 아니다. 0 이면 정확히 축 방향만,
	 *                          360 이면 모든 방향이 들어온다. 범위 밖 값은 그 안으로 잘린다.
	 *
	 * 두 벡터는 정규화돼 있어야 한다. 비교는 코사인으로만 한다 - acos 를 쓰면 통과하는
	 * 대다수의 호출까지 값을 치르므로, 각도 숫자가 필요한 로그 한 줄에서만 AngleBetweenDegrees 를 쓴다.
	 */
	inline bool IsInsideCone(const FVector& Direction, const FVector& Axis, float FullAngleDegrees)
	{
		const float HalfAngleDegrees = FMath::Clamp(FullAngleDegrees, 0.f, 360.f) * 0.5f;

		return FVector::DotProduct(Direction, Axis) >= FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
	}

	/** 정규화된 두 벡터 사이의 각(도). 로그 표기용이다. */
	inline float AngleBetweenDegrees(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.f, 1.f)));
	}
}
