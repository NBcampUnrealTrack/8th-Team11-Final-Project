// Copyright Crosswalk Team. All Rights Reserved.
// CWWeatherComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CWWeatherTypes.h"
#include "Data/Structs/CWWeatherEventPayload.h"
#include "CWWeatherComponent.generated.h"

class UDataTable;
struct FCWWeatherStateRow;

/**
 * 새 날씨 상태로 넘어갈 때 서버와 클라이언트 양쪽에서 발행 (서버는 직접 호출, 클라는 OnRep 경유).
 * 연출을 반영하는 쪽(UCWWeatherControlComponent 등)이 구독한다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCWOnWeatherStateChangedSignature, const FCWWeatherEventPayload&, Payload);

/**
 * 날씨 상태를 서버 권위로 순환시키는 컴포넌트.
 *
 * Calm -> Warning -> Peak -> Decrease -> Calm ... 순서로 도는 Replicated enum 하나만 서버가
 * 확정한다. 실제 연출(Niagara 파라미터, 카메라 기울임 등)은 여기서 다루지 않는다 -
 * UCWWeatherControlComponent(클라 전용, 비-Replicated)가 OnWeatherStateChanged를 구독해서
 * 연출로 옮긴다. 서버 판정과 클라 연출을 분리하는 이 프로젝트의 일반 원칙(Server authority rules)을
 * 날씨에도 그대로 적용한 것.
 *
 * 의도적으로 복제하지 않는 것: 이 상태에서 실제로 굴려진 랜덤 지속시간/풍속값 자체.
 * 클라이언트는 DataTable의 기본값(편차 없이)으로 "얼추 비슷한" 연출을 스스로 재현한다.
 * 순수 배경 연출이라 정확히 같은 랜덤값까지 동기화할 필요가 없다고 판단 (YAGNI) -
 * 나중에 날씨가 게임플레이에 영향을 주게 되면(예: 비가 오면 이동속도 저하) 이 판단을
 * 재검토해야 한다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWeatherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWeatherComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 현재 날씨 상태를 반환 (서버/클라 공통 - 복제된 값을 그대로 읽음)
	UFUNCTION(BlueprintPure, Category = "Weather")
	ECWWeatherState GetCurrentWeatherState() const
	{
		return CurrentWeatherState;
	}

private:
	// 서버 전용: 다음 상태로 넘어가고, 그 상태의 값을 굴리고, 다음 전환 타이머를 건다
	void AdvanceWeatherState();

	// 주어진 상태에 해당하는 DataTable 행을 찾는다. 없으면 nullptr
	const FCWWeatherStateRow* FindStateRow(ECWWeatherState State) const;

	// 클라이언트에서 상태 변경을 감지 (서버에서도 값 갱신 직후 직접 호출한다)
	UFUNCTION()
	void OnRep_CurrentWeatherState();

	// Payload를 만들어 OnWeatherStateChanged로 브로드캐스트
	void BroadcastWeatherState(float RolledWindSpeed, float RolledIntensity, float BlendTime);

protected:
	// 날씨 상태별 지속시간/풍속/강도를 정의하는 DataTable. 행 이름은 ECWWeatherState의 짧은 이름과 맞춘다.
	UPROPERTY(EditDefaultsOnly, Category = "Weather")
	TObjectPtr<UDataTable> WeatherDataTable;

private:
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeatherState)
	ECWWeatherState CurrentWeatherState = ECWWeatherState::Calm;

	FTimerHandle WeatherAdvanceTimerHandle;

public:
	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FCWOnWeatherStateChangedSignature OnWeatherStateChanged;
};
