// CWTrafficDifficulty.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CWTrafficDifficultyStep.h"
#include "CWTrafficDifficulty.generated.h"

class ACWGameState;
class ACWTrafficVehicleSpawner;

/** 적용 중인 단계가 바뀔 때. 연출이나 사운드를 붙일 자리다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCWOnTrafficDifficultyStepChangedSignature, int32, StepIndex, FName, StepName);

/**
 * 스테이지 진행도에 따라 판 전체의 교통 밀도를 옮긴다.
 *
 * 왜 필요한가
 *   스포너는 자기 차선만 알고 주기가 처음부터 끝까지 고정이다. 그래서 "후반으로 갈수록
 *   조여든다" 를 표현할 방법이 없고, 차선이 8개면 같은 숫자를 8번 넣어야 한다.
 *   이 액터는 판에 하나만 놓고 단계표를 적으면 나머지를 대신 해준다.
 *
 * 무엇을 정하지 않는가
 *   **어느 차선에서 언제 차가 나오는지는 정하지 않는다.** 그건 스포너의 난수 그대로다.
 *   외울 수 있는 배열이 생기면 몇 판 만에 학습되고 그때부터 재미가 떨어지기 때문이다.
 *   여기서 정하는 것은 난수의 성질(얼마나 자주 / 무엇이)뿐이다.
 *
 * 진행도의 기준
 *   ACWGameState 의 목표 달성률(완료 수 / 전체 수)이다. 경과 시간이 아니다 -
 *   제한 시간이 아직 기획 미정이라 시간축은 기준으로 쓸 수 없다 (2026-08-26).
 *   목표를 빨리 끝낼수록 빨리 어려워지므로 잘하는 판이 저절로 빡세진다.
 *
 * 네트워크 - 복제하지 않는다.
 *   스폰은 서버 단독이고 이 액터는 스포너 값만 바꾼다. 클라이언트는 이미 복제되는
 *   차량을 볼 뿐이라 난이도 값을 알 필요가 없다.
 */
UCLASS()
class CROSSWALK_API ACWTrafficDifficulty : public AActor
{
	GENERATED_BODY()

public:
	ACWTrafficDifficulty();

	/** 지금 적용 중인 단계. 아직 아무것도 적용하지 않았으면 INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Traffic|Difficulty")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

	/**
	 * 이 진행도에 해당하는 단계를 찾는다. 없으면 INDEX_NONE.
	 *
	 * 조건을 만족하는 것 중 StartProgress 가 가장 높은 것을 고른다. 배열을 정렬해
	 * 두지 않아도 되게 하기 위해서다 - 단계를 나중에 하나 끼워 넣을 때 순서를
	 * 맞추라고 요구하면 그 자체가 실수할 자리가 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic|Difficulty")
	int32 FindStepForProgress(float Progress) const;

	/** 지금 진행도에 맞는 단계를 다시 적용한다. 단계표를 고친 뒤 눌러 확인하는 용도. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Traffic|Difficulty")
	void RefreshNow();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	/** 단계표를 고치면 미리보기를 다시 만든다. PIE 를 돌리지 않고 결과를 보기 위해서다. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** 진행도가 바뀔 때마다 단계를 다시 고른다. */
	UFUNCTION()
	void HandleStageProgressChanged(ACWGameState* GameState, int32 CompletedCount, int32 TotalCount);

	/** 단계 값을 대상 스포너 전부에 밀어 넣는다. 같은 단계면 아무것도 하지 않는다. */
	void ApplyStep(int32 StepIndex);

	/** 대상 스포너 목록. Spawners 가 비어 있으면 레벨의 스포너를 전부 찾는다. */
	TArray<ACWTrafficVehicleSpawner*> ResolveSpawners() const;

	/** 게임 상태를 찾아 진행도 알림을 구독한다. */
	void BindStageProgress();

	/**
	 * 단계표를 사람이 읽을 수 있는 결과로 바꿔 패널에 뿌린다.
	 *
	 * 주기 6~9초가 실제로 어떤 판인지는 차 길이와 속도를 같이 계산해야 나오는데,
	 * 그 셋이 서로 다른 파일에 흩어져 있어서 머리로는 안 잡힌다. 값이 안 보이면
	 * 결국 PIE 를 돌려 맞추게 되고, 그건 절차가 아니라 삽질이다.
	 */
	void RefreshPreview();

	/** 이 단계에서 나올 수 있는 가장 긴 차가 한 지점을 지나가는 시간(초). 모르면 0. */
	float GetLongestPassTime(const FCWTrafficDifficultyStep& Step) const;

protected:
	/**
	 * 진행도 구간별 밀도. 진행도 0 을 담당하는 단계가 하나는 있어야 한다.
	 *
	 * 배열 순서는 상관없다. 조건을 만족하는 것 중 StartProgress 가 가장 높은 것이 이긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Difficulty")
	TArray<FCWTrafficDifficultyStep> Steps;

	/**
	 * 값을 밀어 넣을 스포너. **비워두면 레벨의 스포너를 전부 잡는다.**
	 *
	 * 판 전체를 한 곡선으로 굴리는 것이 기본이라 비워두는 쪽이 정상이다.
	 * 일부 차선만 따로 굴리고 싶을 때만 지정한다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic|Difficulty")
	TArray<TObjectPtr<ACWTrafficVehicleSpawner>> Spawners;

	/**
	 * 차를 보고 나서 반응할 수 있어야 하는 최소 시간(초). 검산에만 쓰고 게임 로직은 아니다.
	 *
	 * 기본값 1.8 은 카메라에서 나온 값이다 - 스프링암 1200cm / 피치 -55° 면 화면 위쪽
	 * 끝이 캐릭터 앞 1364cm 이고, 750cm/s 로 오는 차는 1.81초 뒤에 닿는다.
	 * 카메라를 바꾸면 이 값도 같이 바꿔야 검산이 맞는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Difficulty|Preview", meta = (ClampMin = "0.0"))
	float MinReactionTime = 1.8f;

	/**
	 * [검산] 단계표가 실제로 어떤 판이 되는지. 값을 고치면 바로 갱신된다.
	 *
	 * 주기가 하한 아래로 내려가면 경고가 붙는다. 그 아래에서는 앞차가 지나가기도 전에
	 * 다음 차가 시야에 들어와 피할 틈 자체가 사라지고, 밀도 레버가 반응 레버로 변질된다.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Traffic|Difficulty|Preview")
	TArray<FString> Preview;

private:
	/** 지금 적용 중인 단계. 같은 단계로는 다시 밀어 넣지 않는다. */
	int32 CurrentStepIndex = INDEX_NONE;

	/** 구독을 끊기 위해 들고 있는 게임 상태. */
	TWeakObjectPtr<ACWGameState> BoundGameState;

public:
	UPROPERTY(BlueprintAssignable, Category = "Traffic|Difficulty")
	FCWOnTrafficDifficultyStepChangedSignature OnStepChanged;
};
