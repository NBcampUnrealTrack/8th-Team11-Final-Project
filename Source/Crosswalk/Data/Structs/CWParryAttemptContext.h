// CWParryAttemptContext.h

#pragma once

#include "CoreMinimal.h"
#include "CWParryAttemptContext.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FCWParryAttemptContext
{
	GENERATED_BODY()

public:
	// 패링을 시도한 액터. 수신 측이 충돌 예측 기준점으로 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	TObjectPtr<AActor> Parrier = nullptr;

	// 패링을 시도한 월드 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector AttemptLocation = FVector::ZeroVector;

	/**
	 * 플레이어 기준 커서 방향. **대상 선별(부채꼴 각도)과 차량이 날아갈 방향을 둘 다 이 값이 정한다.**
	 *
	 * 하나가 둘을 겸하는 것은 의도다. 커서 방향 = 캐릭터가 보는 방향
	 * (ACWPlayerCharacter::UpdateCursorFacing) = 차가 날아가는 방향, 셋이 같아야 화면과 맞는다.
	 * 예전에는 발사만 CursorWorldLocation 을 차량 기준으로 다시 읽어서 셋이 갈렸다.
	 * (ACWBaseVehicle::ResolveLaunchDirection 의 1순위 주석)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector AttemptDirection = FVector::ForwardVector;

	/**
	 * 플레이어가 찍은 커서의 월드 지점. **판정에는 쓰지 않는다 - 진단 로그 전용이다.**
	 *
	 * 이 지점을 목적지로 삼던 것이 발사 방향이 어긋난 원인이었다. 커서 트레이스가 차체를
	 * 못 막고 뒤쪽 노면을 찍는 탓에, 화면에서 차 위에 올린 커서가 실제로는 1~2m 밀린 자리에
	 * 찍힌다. 그 어긋남을 재는 것이 지금 이 값의 용도이고(CWTrafficDiagnostics::LogParryAim),
	 * 커서 좌표를 고치기로 하면 그때 다시 판정에 쓸지 정하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector CursorWorldLocation = FVector::ZeroVector;

	/**
	 * CursorWorldLocation 이 실제로 찍힌 값인가. 이것도 진단 로그 전용이다.
	 *
	 * 커서 트레이스가 빗나가면 플레이어 위치가 그대로 돌아온다. 그때 AttemptDirection 에는
	 * 캐릭터 정면이 들어가므로 판정은 갈리지 않지만, 로그에서 "가리킨 대로 갔다" 와
	 * "트레이스가 빗나가 정면으로 갔다" 는 구분돼야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	bool bHasCursorTarget = false;

	/**
	 * 서버가 패링 대상을 찾을 때 사용할 기본 반경. 0 이면 거리 제한 없음.
	 *
	 * 고정 반경으로는 판정을 맞출 수 없어 0 으로 둔다.
	 * 윈도우 안에 들어온 차량의 거리는 (속도 x 윈도우)라서 차마다 다르고,
	 * 스포너가 개체별 속도 배율을 굴리므로 같은 차종끼리도 다르다.
	 * 750cm/s 기준으로 225 를 잡으면 그보다 빠른 차량이 다시 잘린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	float ParryRadius = 0.f;

	// 패링 판정이 허용되는 부채꼴 전체 각(도). 360 이면 방향 제한 없음
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	float ParryAngle = 360.f;

	// 대상 차종은 여기 없다. 이 컨텍스트를 받는 쪽이 차량 자신이라 자기 차종을 이미 안다.
	// 연출 쪽이 볼 차종은 FCWParrySyncData::VehicleType 이 따로 나른다.

	/**
	 * 이 시도를 판정할 때 차량을 몇 초 되돌려 볼 것인가. (랙 보정)
	 *
	 * 시도한 사람이 화면에서 본 상황은 서버의 지금 상황보다 과거다. 차량 위치가 서버에서
	 * 클라이언트로 오는 데 걸린 시간만큼 뒤처져 있고, 거기에 입력이 서버로 오는 시간이 더 붙는다.
	 * 서버가 자기 지금 상황으로 판정하면 그 차이만큼 판정이 어긋난다.
	 *
	 * 그래서 판정 전에 차량을 이만큼 되돌린다. 값은 UCWParryComponent 가 채운다.
	 * 호스트는 0 이다. 자기가 보는 것이 곧 서버 상황이라 되돌릴 것이 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	float ViewRewindSeconds = 0.f;
};