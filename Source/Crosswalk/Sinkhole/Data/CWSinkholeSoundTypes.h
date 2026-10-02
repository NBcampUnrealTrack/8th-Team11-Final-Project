// CWSinkholeSoundTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWSinkholeSoundTypes.generated.h"

class USoundAttenuation;
class USoundBase;

/**
 * 싱크홀 기믹에서 소리가 나는 순간. UCWSinkholeSoundSet 의 키다.
 *
 * 하나하나가 "이미 모든 머신에서 실행되는 지점" 에 대응한다. 그래서 소리를 붙이려고
 * 새 RPC 나 리플리케이트 변수를 만들 일이 없다. (호출 위치는 각 항목 주석 참고)
 *
 * 새 순간을 추가할 때도 같은 조건을 지킨다. 서버에서만 도는 지점(타이머 콜백, 판정 함수)에
 * 걸면 호스트에게만 들린다. 그 순간을 결정하는 리플리케이트 값의 OnRep 에 건다.
 */
UENUM(BlueprintType)
enum class ECWSinkholeSoundCue : uint8
{
	// ACWSinkhole::BeginPlay. 스폰 직후 균열이 번지기 시작할 때.
	Warning				UMETA(DisplayName = "싱크홀 - 균열 예고"),

	// ACWSinkhole::OnRep_State(Active). 예고가 끝나고 구멍이 열릴 때.
	Opened				UMETA(DisplayName = "싱크홀 - 구멍 열림"),

	// ACWSinkhole::OnRep_FillCount. 아스팔트가 한 발 메꿀 때. 마지막 한 발은 Filled 가 대신한다.
	FillProgressed		UMETA(DisplayName = "싱크홀 - 한 발 메꿈"),

	// ACWSinkhole::OnRep_State(Filled). 다 메꿔져 사라질 때.
	Filled				UMETA(DisplayName = "싱크홀 - 복구 완료"),

	// UCWSinkholeFallComponent::OnRep_FallLocked(true). 플레이어가 빠질 때.
	PlayerFell			UMETA(DisplayName = "플레이어 - 낙하"),

	// UCWSinkholeFallComponent::OnRep_FallLocked(false). 스타트 지점에 다시 나타날 때.
	PlayerRespawned		UMETA(DisplayName = "플레이어 - 리스폰"),

	// ACWAsphaltGun::Multicast_PlayProjectile. 발사.
	GunFired			UMETA(DisplayName = "아스팔트 건 - 발사"),

	// ACWAsphaltGun::OnRep_AmmoState. 재장전 시작.
	GunReloadStarted	UMETA(DisplayName = "아스팔트 건 - 재장전 시작"),

	// ACWAsphaltGun::OnRep_AmmoState. 재장전 완료.
	GunReloadFinished	UMETA(DisplayName = "아스팔트 건 - 재장전 완료"),

	// ACWAsphaltSplat::BeginPlay. 싱크홀을 빗나가 바닥에 철퍽 떨어질 때.
	MissSplat			UMETA(DisplayName = "아스팔트 - 빗나가 바닥에 떨어짐")
};

/** 순간 하나에 물리는 소리와 그 재생 조건. */
USTRUCT(BlueprintType)
struct FCWSinkholeSoundEntry
{
	GENERATED_BODY()

	// 비워 두면 그 순간은 조용히 지나간다. 에러가 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	// 재생할 때마다 피치를 1 ± 이 값 안에서 흔든다.
	//
	// 같은 소리가 짧은 간격으로 반복되는 순간(한 발 메꿈, 발사)은 0.05 정도만 줘도
	// 기계적으로 들리지 않는다. 애셋을 여러 개 만들지 않고 해결하는 방법이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PitchVariance = 0.f;

	// 이 소리만 다른 감쇠를 쓰고 싶을 때. 비워 두면 셋 전체의 Attenuation 을 따른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundAttenuation> AttenuationOverride;

	// 소리의 주인공이 이 화면의 플레이어 본인이면 거리 감쇠 없이 2D 로 낸다.
	//
	// 쿼터뷰라 리스너가 캐릭터에서 멀리 떠 있다. 내가 빠지는 소리가 감쇠를 타면 남이 빠지는
	// 소리와 크기가 같아져 "나한테 일어난 일" 로 들리지 않는다. 남의 화면에서는 그대로 3D 다.
	// 주인공이 없는 순간(싱크홀 자체의 소리)에는 켜도 아무 효과가 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bNonSpatialForLocalPlayer = false;
};
