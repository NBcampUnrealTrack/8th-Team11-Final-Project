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

	/** 남아 있는 자국 수. 0 이면 정리가 끝난 상태다. */
	UFUNCTION(BlueprintPure, Category = "Work")
	int32 GetDecalCount() const { return Decals.Num(); }

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

	/** 이 작업자가 자국을 지울 수 있는 도구를 들고 있는가. */
	bool IsCleanupTool(ECWToolType ToolType) const;

public:
	// 자국을 지우는 도구. (검은색 롤러)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal")
	TArray<ECWToolType> CleanupTools;

	// 자국에 입힐 데칼 머티리얼. 비워두면 자국이 보이지 않는다. (판정은 정상 동작)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal")
	TObjectPtr<UMaterialInterface> DecalMaterial;

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
	 * 지금까지 한 번에 가장 많이 쌓였던 자국 수. 진행도의 분모다.
	 *
	 * 전체 개수로 나누면 사고가 날 때마다 진행도가 뒤로 갔다가, 지우면 다시 앞으로 간다.
	 * 그게 의도다. (기획서 4-1 "전체 진행률이 뒤로 가는 것은 사고를 알리는 의도된 동작")
	 */
	int32 PeakDecalCount = 0;
};
