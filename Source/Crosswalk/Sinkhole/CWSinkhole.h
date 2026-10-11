// CWSinkhole.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sinkhole/Data/CWSinkholeTypes.h"
#include "CWSinkhole.generated.h"

class ACWSinkhole;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UDecalComponent;
class USphereComponent;

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

	/** 예고 동안 균열을 키운다. 예고 중에만 켜지고, 구멍이 열리면 꺼진다. */
	virtual void Tick(float DeltaSeconds) override;

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
	float GetVisualRadius() const;

	/** 지금 낙하 판정 반경. 시각 반경보다 작다. */
	float GetFallRadius() const;

	/** 0 = 손도 안 댐, 1 = 복구 완료. */
	float GetFillRatio() const;

	int32 GetFillCount() const { return FillCount; }

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

	/** 복구가 끝나 사라지기 직전. BP 에서 흙먼지 한 번 재생하는 자리. 서버와 클라이언트 모두에서 불린다. */
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

	/** OnRep 에서 소리를 내도 되는가. 클라이언트가 처음 받은 초기 상태에서는 false 다. */
	bool ShouldPlayReplicatedSound() const;

	/** 진행도와 상태에 맞춰 데칼 크기와 콜리전 반경을 다시 맞춘다. 모든 머신이 각자 부른다. */
	void RefreshVisualAndCollision();

	/** "쩌적" 시점들을 뽑는다. 간격이 점점 좁아지고, 마지막은 구멍이 열리는 순간에 끝난다. */
	void BuildCrackBurstTimes();

	/** 지금까지 흐른 시간에 맞는 드러남 비율(0~1)을 계산한다. */
	float EvaluateCrackReveal(float Elapsed) const;

	/** 균열 드러남 비율을 머티리얼에 넣는다. */
	void ApplyCrackReveal(float Reveal);

	/** 균열 번짐을 끝내고 틱을 끈다. 구멍이 열렸거나 이미 열린 채로 받았을 때. */
	void FinishCrackGrowth();

	/** 구멍이 열린 뒤 균열 데칼의 축소 비율. 메운 만큼 구멍과 같은 단계로 줄어든다. */
	float GetCrackScaleForFill() const;

	/**
	 * 구멍 평면을 "화면에 보이는" 지면 높이에 맞춘다. 모든 머신이 각자 부른다.
	 *
	 * 액터 원점을 그냥 믿으면 안 된다. 스폰 볼륨은 ECC_Visibility 라인 트레이스로 자리를 잡는데,
	 * 프로토타입 맵의 도로 콜리전은 눈에 보이는 아스팔트보다 20cm 위에 뜬 bHiddenInGame 브러시다.
	 * 그래서 평면은 브러시 위(=공중)에 얹히고 데칼만 보이는 아스팔트에 붙어, 쿼터뷰에서 검은 원과
	 * 균열이 따로 떠 있는 것처럼 보인다.
	 *
	 * 데칼은 렌더 지오메트리에만 찍히므로, 평면도 같은 기준(렌더되는 첫 표면)을 쓰면 둘이 겹친다.
	 */
	void AlignVoidPlaneToVisibleGround();

	UFUNCTION()
	void HandleFallOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

public:
	// 구멍 본체. 순수한 검정이어야 하므로 데칼이 아니라 평면 메시다.
	//
	// 데칼은 바닥의 알베도만 갈아끼우고 조명 계산은 바닥 평면 그대로 들어간다. 그래서
	// BaseColor 를 0 으로 깎아도 하늘광이 얹혀 짙은 회색이 되고, 구멍이 아니라 바닥 얼룩으로 읽힌다.
	// Unlit 머티리얼을 물린 메시는 조명을 아예 건너뛰므로 화면에 실제로 검정이 찍힌다.
	//
	// 지형을 함몰시키는 것은 아니다. 여전히 액터를 지우면 원상복구가 끝난다. (기획서 3~4절)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sinkhole")
	TObjectPtr<UStaticMeshComponent> VoidPlane;

	// 구멍 가장자리의 부서진 아스팔트 테. 바닥 요철을 따라가야 해서 이쪽은 데칼로 둔다.
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

	// 균열이 번지기 시작해서 구멍이 열리기까지의 시간(초). (기획서 3-2)
	//
	// 1.2초였을 때는 균열이 생기자마자 구멍이 열려 예고로 읽히지 않았다. 균열이 자라는 게
	// 눈에 보이고 "곧 꺼지겠다" 를 판단해 비킬 시간이 필요해서 3.5초로 늘렸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.0"))
	float WarningDuration = 3.5f;

	// ---- 균열 연출 ----
	// 데칼을 키우지 않고, 균열 텍스처를 중심에서 바깥으로 "드러낸다".
	//
	// 크기를 키우면 선 굵기까지 같이 커져서 땅이 갈라지는 게 아니라 그림이 확대되는 것처럼 보인다.
	// 그래서 데칼은 처음부터 다 번진 크기로 두고, 머티리얼의 원형 마스크 반경(CrackRevealParameterName)만
	// 0~1 로 민다. 마스크 안쪽 균열만 보이므로 선이 바깥으로 뻗어 나가는 그림이 된다.
	//
	// 반경은 연속으로 늘지 않고 CrackBurstCount 번에 나눠 "쩌적" 하고 짧게 튄다.
	// 터지는 간격은 점점 좁아진다(가속). 무너지기 직전에 몰아쳐야 긴장이 생기고 구멍이 열리는 순간과 이어진다.

	// 처음 나타날 때 드러나 있는 비율. 0 이면 첫 "쩌적" 전까지 아무것도 안 보여 예고 시간을 깎아 먹는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrackStartReveal = 0.15f;

	// 예고 동안 균열이 "쩌적" 하고 번지는 횟수.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack", meta = (ClampMin = "1", ClampMax = "20"))
	int32 CrackBurstCount = 6;

	// 한 번 번질 때 걸리는 시간(초). 짧을수록 "쩍" 하고 갈라지는 느낌이 난다. 길면 스르륵 번진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack", meta = (ClampMin = "0.01"))
	float CrackBurstDuration = 0.08f;

	// 번지는 시점을 얼마나 흔들지(0~0.5). 매번 같은 박자로 터지면 기계적으로 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float CrackBurstJitter = 0.3f;

	// 메울 때 균열 데칼이 줄어드는 하한(원래 크기 대비). 1 이면 메워도 균열 크기가 그대로다.
	// 0 이면 구멍과 완전히 같은 비율로 줄어 마지막 발 직전엔 점처럼 작아진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrackMinScaleOnFill = 0.2f;

	// 균열 머티리얼의 원형 마스크 반경 파라미터 이름. 머티리얼에 이 스칼라 파라미터가 있어야 연출이 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole|Crack")
	FName CrackRevealParameterName = TEXT("CrackReveal");

	// 구멍 평면을 보이는 지면에서 얼마나 띄울지(cm).
	//
	// 0 으로 두면 도로 표면과 같은 높이라 지글거린다(z-fighting). 그것만 피하면 1cm 로 충분하지만,
	// 도로에 깔린 납작한 연출물(작업 존 흙자국 2cm, 셀/테두리 메시 0~1cm)보다는 위에 있어야 한다.
	// 구멍이 그것들 아래로 깔리면 흙자국이 구멍 위에 떠 있는 그림이 된다. 싱크홀은 지면을 파낸
	// 것이므로 도로 위 연출물은 전부 구멍에 가려지는 쪽이 맞다.
	//
	// 5cm 는 그 연출물 대역을 넘기면서도 쿼터뷰 거리에서 한 픽셀이 안 되는 값이다. 앞서 문제였던
	// 20cm 짜리 들뜸과는 규모가 다르다. 도로에 더 두꺼운 연출물이 생기면 이 값을 올린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.0"))
	float VoidPlaneHeight = 5.f;

	// 보이는 지면을 찾을 때 액터 원점 아래로 훑는 거리(cm).
	// 도로 콜리전이 보이는 표면과 어긋난 만큼은 덮어야 한다. 너무 크게 잡으면 도로가 끊긴 곳에서
	// 한참 아래의 엉뚱한 바닥을 집는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sinkhole", meta = (ClampMin = "0.0"))
	float GroundProbeDistance = 200.f;

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

	// 균열이 번진 시간. 리플리케이트하지 않는다.
	// 각 머신이 스폰을 받은 순간부터 자기 시계로 센다. 클라이언트는 편도 지연만큼 늦게 시작하지만,
	// 구멍이 열리는 OnRep 에서 다 드러난 상태로 맞추므로 끝이 어긋나지 않는다.
	float CrackGrowElapsed = 0.f;

	// 각 "쩌적" 이 시작되는 시각(초). BeginPlay 에서 한 번 뽑는다. 순수 외형이라 머신마다 달라도 된다.
	TArray<float> CrackBurstStartTimes;

	// BP 에서 잡아 둔 균열 데칼의 원래 스케일. 메울 때 줄이는 기준이다.
	FVector CrackFullRelativeScale = FVector::OneVector;

	// 위 스케일을 잡았는지. BeginPlay 전에 오는 OnRep 이 원래 크기를 덮어쓰지 않게 막는다.
	bool bCrackScaleCaptured = false;

	// 균열 데칼의 동적 머티리얼. 반경 파라미터를 여기에 넣는다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrackMID;
};
