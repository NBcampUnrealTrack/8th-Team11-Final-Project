// CWAsphaltGunnerComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWAsphaltGunnerComponent.generated.h"

class ACWAsphaltGun;

/**
 * 플레이어에 붙는 아스팔트 건 조작 컴포넌트. (싱크홀 기획서 7-2, 7-12)
 *
 * 도구 자체(ACWAsphaltGun)는 월드에 놓인 액터라 클라이언트가 소유하지 않는다.
 * 그래서 클라이언트 → 서버 RPC 는 도구가 아니라 폰이 소유한 이 컴포넌트에서 나가야 한다.
 * 도구 집기가 UCWToolHolderComponent 를 거치는 것과 같은 이유, 같은 모델이다.
 *
 * 이 컴포넌트가 하는 일은 세 가지뿐이다.
 *   1) 커서가 가리키는 지면 좌표를 구한다
 *   2) 서버에 사격/재장전을 요청한다
 *   3) 총구 이펙트와 발사음을 로컬에서 즉시 재생한다
 *
 * 잔탄과 재장전 상태는 도구가 들고 있다. 페인트 잔량을 도구가 들고 있는 것과 같다.
 * 도구를 넘겨주면 그 상태도 함께 넘어가야 하기 때문이다.
 *
 * 클라이언트 예측을 하지 않는 이유
 *   산포 난수를 서버가 굴리므로 착탄점을 클라이언트가 미리 알 수 없다. 예측해서 궤적을
 *   그리면 서버 결과와 어긋나 순간이동이 보인다. 대신 총구 연출만 즉시 재생하면
 *   핑만큼의 지연이 있어도 손맛이 살아서 체감되지 않는다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWAsphaltGunnerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWAsphaltGunnerComponent();

	/**
	 * 발사 입력. Enhanced Input 의 Started/Triggered 에 연결한다.
	 * 커서 지점을 구해 서버에 요청한다. 실제 발사 여부는 서버가 정한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asphalt|Gun")
	void Fire();

	/** 수동 재장전 입력. */
	UFUNCTION(BlueprintCallable, Category = "Asphalt|Gun")
	void Reload();

	/** 지금 들고 있는 도구가 아스팔트 건이면 반환한다. 아니면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "Asphalt|Gun")
	ACWAsphaltGun* GetHeldAsphaltGun() const;

protected:
	/** 발사 요청을 보낸 순간. BP 에서 총구 이펙트와 발사음을 붙이는 자리. 로컬에서만 불린다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Asphalt|Gun")
	void OnLocalFireRequested();

private:
	UFUNCTION(Server, Reliable)
	void Server_Fire(const FVector& AimLocation);

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	/**
	 * 커서가 가리키는 지면 위 지점을 구한다.
	 * @return 지면을 찾았으면 true
	 */
	bool GetAimLocationUnderCursor(FVector& OutAimLocation) const;

protected:
	// 커서 아래 지면을 찾을 때 쓰는 트레이스 길이(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asphalt|Gun", meta = (ClampMin = "100.0"))
	float AimTraceDistance = 20000.f;

private:
	// 마지막으로 요청을 보낸 시각. 입력 하한 검사에 쓴다.
	//
	// 서버도 연사 간격을 다시 검사하므로 이건 신뢰 경계가 아니다.
	// 의미 없는 RPC 를 줄이기 위한 것뿐이다.
	float LastFireRequestTime = -1000.f;
};
