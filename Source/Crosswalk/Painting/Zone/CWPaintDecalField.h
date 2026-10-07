// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintDecalField.h

#pragma once

#include "CoreMinimal.h"
#include "Painting/Data/CWPaintDecal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "CWPaintDecalField.generated.h"

class UDecalComponent;
class UMaterialInterface;

/**
 * 터진 페인트 통이 남긴 자국을 모아 관리하는 존. (기획서 6-4)
 *
 * 스테이지에 하나만 배치한다. 자국이 생길 수 있는 범위를 Area 로 잡는다.
 *
 * 왜 오염 레이어에 얹지 않았나
 *   레이어의 칸을 채우면 사고 한 번이 붓질 수십 번을 요구한다. 자국 반경이 붓 반경의
 *   몇 배라서 그렇다. 게다가 레이어는 스트라이프 자리의 칸을 빼두기 때문에,
 *   도색 구역 위에서 터진 사고는 아무 흔적도 남기지 못한다.
 *
 *   자국을 칸이 아니라 별도 목록으로 두면 두 문제가 같이 풀린다. 도색 구역 위에도 남고,
 *   지우는 비용은 자국 개수로 직접 정할 수 있다.
 *
 * 체크리스트
 *   ObjectiveName 을 오염 레이어와 **같은 이름**("페인트 정리")으로 두면 한 줄로 묶인다.
 *   플레이어 입장에서는 삐져나간 페인트든 터진 자국이든 "치울 것" 하나다.
 *
 * 네트워크
 *   자국 목록을 서버가 만들고 배열째로 리플리케이트한다. 각 머신은 그 값으로 데칼을 다시 만든다.
 *
 *   지우는 판정도 서버만 한다. 붓질을 로컬에서 확정하는 칸 존과 다른 이유:
 *   자국은 칸처럼 촘촘하지 않아서 한두 프레임 늦게 반영돼도 그림이 어긋나지 않는다.
 *   클라이언트의 커서 좌표는 이미 작업 컴포넌트가 서버로 올려주고 있으므로 추가 RPC 도 필요 없다.
 */
UCLASS()
class CROSSWALK_API ACWPaintDecalField : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWPaintDecalField();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	/** 검은색 롤러로 자국을 문질러 지운다. 서버에서만 실제로 지워진다. */
	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

	/** 지울 자국이 남아 있을 때만 작업 대상이 된다. 완료 여부는 보지 않는다. */
	virtual bool CanWorkNow(ECWToolType ToolType) const override;

	/** 붓이 닿는 곳에 자국이 있으면 대상이다. Area 박스 거리로 재지 않는다. */
	virtual bool IsWorkerInRange(const UCWWorkerComponent* Worker, const FVector& WorkLocation) const override;

	/** 이 지점에 지울 자국이 있는가. 겹친 존 중 어느 쪽을 잡을지 가르는 데 쓴다. */
	virtual bool HasWorkAt(const UCWWorkerComponent* Worker, const FVector& WorldLocation) const override;

	/**
	 * 터진 통 하나가 남기는 자국들을 뿌린다. 서버에서만 유효하다.
	 *
	 * @param BurstLocation 피격 순간의 위치. 지면에 붙인 좌표를 넘긴다.
	 * @param BrokenCount   터진 통 개수. 자국 개수와 흩어지는 범위가 여기에 비례한다.
	 * @param IgnoredActor  지면을 찾을 때 뺄 액터. 치인 캐릭터를 빼지 않으면 캡슐 위에 자국이 뜬다.
	 */
	void AddBurst(const FVector& BurstLocation, int32 BrokenCount, const AActor* IgnoredActor = nullptr);

protected:
	/** 디버그 줄에 남은 자국 수를 덧붙인다. */
	virtual FString GetDebugDetailText() const override;

private:
	UFUNCTION()
	void OnRep_Decals();

	/** 자국 목록에 맞춰 데칼 컴포넌트를 다시 만든다. 모든 머신이 각자 부른다. */
	void RefreshDecalComponents();

	/** 남은 자국 수로 진행도를 다시 센다. 서버에서만 유효하다. */
	void RefreshProgress();

	/** 이 지점 반경 안에 자국이 하나라도 있는가. */
	bool HasDecalInRadius(const FVector& WorldCenter, float Radius) const;

	/**
	 * 이 자리에 겹치는 자국을 찾아 되살린다. 되살렸으면 true.
	 *
	 * 겹친 자리에 새 자국을 또 만들면 개수만 늘고 그림은 그대로다. 대신 이미 있던 자국의
	 * 남은 양을 되돌려서, 겨우 지워놓은 자리가 다시 더러워지는 것으로 보이게 한다.
	 *
	 * 되살릴 때 Id 를 새로 매긴다. 각 머신은 처음 보는 Id 를 지금 퍼지기 시작하는 자국으로
	 * 다루기 때문에, 번호를 갈아야 재오염이 퍼지는 연출을 다시 탄다.
	 */
	bool TryMergeIntoExisting(const FVector& Location, float SpawnDelay);

	/**
	 * 이 머신이 자국을 처음 본 시각(초). 기록이 없으면 지금 시각으로 새로 남긴다.
	 *
	 * 서버 시각을 그대로 쓰지 않는 이유: 머티리얼의 Time 노드는 각 머신의 로컬 월드 시간이라
	 * 기준이 다르다. 자국을 처음 본 순간부터 퍼지게 하면 지연만큼 늦게 시작할 뿐 그림은 같다.
	 */
	float GetLocalSpawnTime(int32 DecalId);

	/** 사라진 자국의 스폰 시각 기록을 버린다. 안 버리면 판이 길어질수록 맵만 쌓인다. */
	void PruneLocalSpawnTimes();

public:
	// 자국에 입힐 데칼 머티리얼. 비워두면 자국이 보이지 않는다. (판정은 정상 동작)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal")
	TObjectPtr<UMaterialInterface> DecalMaterial;

	/**
	 * 자국을 없애는 남은 양의 임계치. 이 값 이하로 내려가면 다 지운 것으로 처리한다.
	 *
	 * 0 까지 문지르게 두면 마지막 몇 %가 눈에 보이지 않을 만큼 작아진 상태로 남아,
	 * 플레이어는 다 치웠다고 보는데 진행도만 오르지 않는다. 그 구간을 잘라낸다.
	 *
	 * 임계치에서 끊어도 남은 몫을 소모한 것으로 세기 때문에 페인트 총 소모량은 변하지 않는다.
	 * 0 으로 두면 예전처럼 끝까지 문질러야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float RemoveRemainingThreshold = 0.15f;

	/**
	 * 자국이 그려지는 최소 크기 비율. 남은 양이 아무리 적어도 이 비율보다 작게는 안 그린다.
	 *
	 * 임계치와 짝이다. 임계치가 자국을 언제 없앨지 정하고, 이 값이 없어지기 직전까지
	 * 자국이 보이도록 유지한다. 그래서 "작아지다 툭 사라진다" 가 되고, 사라지는 순간이
	 * 자국 하나를 끝냈다는 신호로 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinVisibleScale = 0.35f;

	/**
	 * 자국 하나의 반경(cm).
	 *
	 * 붓 반경(ACWWorkZone_Cells::BrushRadius, 기본 20)과 비슷하게 잡는다.
	 * 훨씬 크면 한 번에 문질러도 잘 안 지워져 답답하고, 훨씬 작으면 자국이 점처럼 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "1.0"))
	float DecalRadius = 25.f;

	// 통 1개가 남기는 자국 개수. 통 3개면 이 값의 3배가 흩뿌려진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "1"))
	int32 DecalsPerCan = 3;

	// 자국이 흩어지는 반경(cm). 통 개수에 비례한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0"))
	float ScatterRadiusPerCan = 45.f;

	/**
	 * 새 자국을 기존 자국과 합칠 거리. 자국 반경에 대한 비율이다.
	 *
	 * 이 안에 이미 자국이 있으면 새로 만들지 않고 그 자국을 되살린다. 사고가 같은 자리에서
	 * 거듭돼도 개수가 늘지 않는다. 0 이면 합치지 않고 예전처럼 계속 쌓인다.
	 *
	 * 1 보다 크게 잡으면 자국이 서로 닿기 전에 합쳐져서 얼룩이 듬성해진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MergeRadiusScale = 0.6f;

	/**
	 * 자국 개수의 상한. 병합이 놓친 경우를 막는 안전망이다.
	 *
	 * 자국 하나가 데칼 컴포넌트 하나라 개수가 그대로 그리기 비용이다. 넓은 판에서 사고가
	 * 거듭되면 병합만으로는 안 잡힌다. 상한에 닿으면 새 자국을 더 만들지 않는다.
	 *
	 * 오래된 자국을 버리지 않는 이유: 치우지 않아도 사라지는 자국이 생기면
	 * 정리가 클리어 조건인 이 존에서 공짜로 넘어가는 길이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "1"))
	int32 MaxDecals = 128;

	/**
	 * 자국 하나가 다 퍼지는 데 걸리는 시간(초).
	 *
	 * 머티리얼이 Progress 파라미터로 마스크를 열어 쓴다. 0 이면 퍼지지 않고 바로 나타난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal|Grow", meta = (ClampMin = "0.0"))
	float GrowDuration = 0.35f;

	/**
	 * 한 사고에서 자국끼리 벌어지는 시간(초). 자국 순서에 비례해 늦게 나타난다.
	 *
	 * 전부 같이 퍼지면 덩어리 하나가 부풀어 오르는 것처럼 보인다. 조금씩 어긋나야
	 * 사방으로 튀어 박히는 것으로 읽힌다. 0 이면 동시에 퍼진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal|Grow", meta = (ClampMin = "0.0"))
	float SpawnDelayPerDecal = 0.045f;

	/**
	 * 자국 하나를 다 지우는 데 걸리는 시간(초). 붓을 계속 대고 있을 때 기준이다.
	 *
	 * 체온이 오르면 작업 컴포넌트가 넘겨주는 시간이 줄어들어 그만큼 느려진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.1"))
	float SecondsToScrub = 0.6f;

	/**
	 * 자국 하나를 다 지우는 데 드는 페인트. (기획서 5-2 "4단계 정리도 페인트를 소모")
	 *
	 * 통 3개가 터지면 자국이 9개라 45 가 든다. 충전량(50)의 대부분이다.
	 * 사고 한 번의 뒤처리 비용을 여기서 조절한다. 이게 "많이 들수록 실패 비용이 커진다" 를 만든다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "0.0"))
	float PaintCostPerDecal = 5.f;

	// 데칼을 지면에 투영하는 깊이(cm). 경사면에서 끊기면 늘린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (ClampMin = "1.0"))
	float DecalProjectionDepth = 64.f;

private:
	/**
	 * 남아 있는 자국들. 서버가 만들고 전원이 받는다.
	 *
	 * 배열째로 보내는 이유: 자국은 많아야 수십 개다. 개별 액터로 만들면 그만큼
	 * 스폰과 소유권이 생기는데, 자국은 위치와 남은 양 외에 아무 상태도 갖지 않는다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Decals)
	TArray<FCWPaintDecal> Decals;

	// 화면에 그리는 데칼들. 리플리케이트하지 않고 각 머신이 Decals 를 보고 다시 만든다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> DecalComponents;

	/**
	 * 각 머신이 자국을 처음 본 시각(초). 자국 Id 로 찾는다.
	 *
	 * 리플리케이트하지 않는다. 머티리얼의 Time 노드가 머신마다 다른 로컬 시간을 쓰기 때문에,
	 * 이 값도 각자 자기 시간축으로 갖고 있어야 퍼짐이 제대로 보인다.
	 */
	TMap<int32, float> LocalSpawnTimes;

	// 다음 자국에 매길 번호. 서버에서만 올라간다.
	int32 NextDecalId = 1;

	/**
	 * 지금까지 한 번에 가장 많이 쌓였던 자국의 남은 양 합계. 진행도의 분모다.
	 *
	 * 개수가 아니라 합계인 이유: 겹친 자국은 새로 만들지 않고 되살리기 때문에, 개수로 세면
	 * 같은 자리에 거듭난 사고가 진행도에 잡히지 않는다. 되살린 만큼 합계는 늘어난다.
	 *
	 * 자국이 모두 온전할 때는 합계가 곧 개수라 예전과 같은 값이 나온다.
	 * 사고가 나면 진행도가 뒤로 갔다가 지우면 다시 앞으로 가는 것이 의도다.
	 * (기획서 4-1 "전체 진행률이 뒤로 가는 것은 사고를 알리는 의도된 동작")
	 */
	float PeakRemainingTotal = 0.f;
};
