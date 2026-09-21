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
	 * 플레이어 기준 커서 방향. **대상 선별(부채꼴 각도)이 이 값을 쓴다.**
	 *
	 * 발사 방향의 기준은 CursorWorldLocation 쪽이고, 이 값은 그것을 못 쓸 때의 2순위로만
	 * 들어간다(ACWBaseVehicle::ResolveLaunchDirection). 선별과 발사가 다른 값을 보는 것은
	 * 의도다 - 선별은 "플레이어가 그쪽을 보고 있나" 를 묻고 발사는 "차를 어디로 보내나" 를
	 * 묻는데, 후자의 기준점은 플레이어가 아니라 차량이어야 찍은 자리에 떨어진다.
	 *
	 * 이 값은 캐릭터가 바라보는 방향이기도 하다(ACWPlayerCharacter::UpdateCursorFacing).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector AttemptDirection = FVector::ForwardVector;

	/**
	 * 플레이어가 찍은 커서의 월드 지점. **차량이 날아갈 목적지다.**
	 *
	 * 차량이 (이 지점 - 차량 위치) 방향으로 날아가므로 찍은 자리에 그대로 떨어진다.
	 * 단 커서 트레이스가 차체를 못 막고 뒤쪽 노면을 찍는 탓에, 화면에서 차 위에 올린 커서는
	 * 실제로 1~2m 밀린 자리에 찍힌다. 그 구간은 차량 쪽에서 거리로 잘라내고 방향 기준으로
	 * 넘긴다(ACWBaseVehicle::MinCursorLaunchDistance).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Parry")
	FVector CursorWorldLocation = FVector::ZeroVector;

	/**
	 * CursorWorldLocation 이 실제로 찍힌 값인가.
	 *
	 * 커서 트레이스가 빗나가면 플레이어 위치가 그대로 돌아온다. 그 좌표를 목적지로 쓰면
	 * 차가 플레이어를 향해 날아오므로, 발사 쪽에서 이 깃발을 보고 건너뛴다.
	 * 그때 AttemptDirection 에는 캐릭터 정면이 들어가 있어 선별은 갈리지 않는다.
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