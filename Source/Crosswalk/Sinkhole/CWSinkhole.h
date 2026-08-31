// CWSinkhole.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sinkhole/Data/CWSinkholeTypes.h"
#include "CWSinkhole.generated.h"

class ACWSinkhole;
class UDecalComponent;
class USphereComponent;

/**
 * 복구 진행도가 바뀔 때. 서버와 클라이언트 양쪽에서 발행된다.
 * FillRatio 는 0~1 로 정규화된 값이라 데칼 스케일과 UI 에 그대로 넣을 수 있다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCWOnSinkholeFillChangedSignature, ACWSinkhole*, Sinkhole, int32, FillCount, float, FillRatio);

/**
 * 도로에 생기는 싱크홀. (싱크홀 기획서 3~4절, 7-8)
 *
 * 지형을 실제로 함몰시키지 않는다. 검은 원 데칼 + 균열 데칼로 "꺼진 것처럼 보이게" 한다.
 * 그래서 복구가 끝나면 액터를 지우는 것만으로 원상복구가 끝난다. 네비메시 갱신도,
 * 도로 메시 교체도, 되돌리기 처리도 필요 없다.
 *
 * 진행도 표현
 *   아스팔트가 한 발 명중할 때마다 검은 원의 반지름이 한 단계 줄어든다.
 *   색 대비에 기대지 않으므로 검은 도로 위 검은 구멍이라는 시인성 문제를 피하고,
 *   거리에 관계없이 "몇 발 남았는지" 가 구멍 크기로 바로 읽힌다.
 *
 *   낙하 콜리전 반경도 같이 줄어든다. 부분적으로 메꾼 싱크홀은 실제로 덜 위험해지므로,
 *   차를 피하러 중간에 이탈했다 돌아와도 그동안의 진행이 헛되지 않는다.
 *
 * 판정 반경
 *   낙하 판정(FallRadiusRatio)은 보이는 크기보다 작고, 명중 판정은 보이는 크기 그대로다.
 *   양쪽 모두 플레이어에게 유리한 방향이다. 가장자리를 스쳤는데 빠지면 억울하고,
 *   가장자리에 걸친 사격이 안 먹히면 조준이 답답하다.
 *
 * 네트워크
 *   FillCount 와 State 만 리플리케이트한다. 데칼 스케일과 콜리전 반경은 그 값에서
 *   각 머신이 직접 계산한다. 시각 요소를 따로 동기화할 이유가 없다.
 *   진행도를 올리는 것은 서버뿐이다. (ACWAsphaltGun 이 서버에서 판정한다)
 */
UCLASS()
class CROSSWALK_API ACWSinkhole : public AActor
{
	GENERATED_BODY()

public:
	ACWSinkhole();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/**
	 * 아스팔트 한 발이 명중했다. 서버에서만 유효하다.
	 * 필요 탄수를 채우면 액터를 파괴해 원상복구를 끝낸다.
	 *
	 * @return 진행도가 실제로 올랐으면 true. 예고 중이거나 이미 완료면 false.
	 */
	bool ApplyAsphaltHit();

	/**
	 * 이 지점이 명중 범위 안인가. 아스팔트 착탄점 판정에 쓴다.
	 * 높이는 보지 않는다. 도로는 평면이고, Z 차이로 빗나갔다고 판정할 이유가 없다.
	 */
	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	bool IsPointInHitRange(const FVector& WorldLocation) const;

	/** 지금 보이는 검은 원의 반지름. 진행도에 따라 줄어든다. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	float GetVisualRadius() const;

	/** 지금 낙하 판정 반경. 시각 반경보다 작다. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	float GetFallRadius() const;

	/** 0 = 손도 안 댐, 1 = 복구 완료. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	float GetFillRatio() const;

	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	int32 GetFillCount() const { return FillCount; }

	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	int32 GetRequiredFillCount() const { return RequiredFillCount; }

	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	ECWSinkholeState GetState() const { return State; }

	/** 지금 밟으면 빠지는 상태인가. 예고 중에는 false 다. */
	UFUNCTION(BlueprintPure, Category = "Sinkhole")
	bool IsFallActive() const { return State == ECWSinkholeState::Active; }

protected:
	/** 예고가 끝나고 구멍이 열린 순간. BP 에서 흙먼지와 사운드를 붙이는 자리. */
	UFUNCTION(BlueprintNativeEvent, Category = "Sinkhole")
	void OnSinkholeOpened();
	virtual void OnSinkholeOpened_Implementation();

	/** 한 발 채워진 직후. BP 에서 아스팔트가 덮이는 연출을 붙이는 자리. */
	UFUNCTION(BlueprintNativeEvent, Category = "Sinkhole")
	void OnFillProgressed(int32 NewFillCount, float NewFillRatio);
	virtual void OnFillProgressed_Implementation(int32 NewFillCount, float NewFillRatio);

	/** 복구가 끝나 사라지기 직전. BP 에서 흙먼지 한 번 재생하는 자리. */
	UFUNCTION(BlueprintNativeEvent, Category = "Sinkhole")
	void OnSinkholeFilled();
	virtual void OnSinkholeFilled_Implementation();

private:
	UFUNCTION()
	void OnRep_FillCount();

	UFUNCTION()
	void OnRep_State();

	/** 예고 시간이 끝났을 때. 서버에서만 돈다. */
	void OpenSinkhole();

	/** 진행도와 상태에 맞춰 데칼 크기와 콜리전 반경을 다시 맞춘다. 모든 머신이 각자 부른다. */
	void RefreshVisualAndCollision();

	UFUNCTION()
	void HandleFallOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

public:
	// 구멍 본체. 검은 원이다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sinkhole")
	TObjectPtr<UDecalComponent> HoleDecal;

	// 주변 균열. 검은 원과 분리해 두는 이유는 이것만 랜덤 회전시키기 위해서다.
	// 에셋을 늘리지 않고도 싱크홀이 매번 다르게 보인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sinkhole")
	TObjectPtr<UDecalComponent> CrackDecal;

	// 낙하 판정. 반경은 진행도에 따라 코드가 다시 잡으므로 에디터 값은 의미가 없다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sinkhole")
	TObjectPtr<USphereComponent> FallTrigger;

protected:
	// 손대지 않은 상태의 검은 원 반지름(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "10.0"))
	float InitialRadius = 150.f;

	// 복구에 필요한 아스팔트 명중 수. (기획서 7-3)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "1"))
	int32 RequiredFillCount = 6;

	// 시각 반경 대비 낙하 판정 반경의 비율. 1 보다 작게 둬야 가장자리를 스친 것이 봐준 판정이 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float FallRadiusRatio = 0.85f;

	// 균열이 번지고 나서 구멍이 열리기까지의 시간(초). (기획서 3-2)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.0"))
	float WarningDuration = 1.2f;

	// 복구 완료 후 액터가 실제로 사라지기까지의 유예(초).
	// 마지막 연출이 클라이언트에 전달될 시간을 벌기 위한 것이라 짧게 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.0"))
	float FilledDestroyDelay = 0.4f;

private:
	// 지금까지 명중한 아스팔트 수. 서버만 올리고 전원이 받는다.
	UPROPERTY(ReplicatedUsing = OnRep_FillCount)
	int32 FillCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ECWSinkholeState State = ECWSinkholeState::Warning;

	// 예고 → 활성 전환 타이머. 서버에서만 돈다.
	FTimerHandle WarningTimerHandle;

	// 델리게이트는 최하단에 배치
public:
	UPROPERTY(BlueprintAssignable, Category = "Sinkhole")
	FCWOnSinkholeFillChangedSignature OnFillChanged;
};
