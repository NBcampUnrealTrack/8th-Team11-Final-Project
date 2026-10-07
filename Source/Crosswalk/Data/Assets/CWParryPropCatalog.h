// CWParryPropCatalog.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "Data/Structs/CWParryProp.h"

#include "CWParryPropCatalog.generated.h"

/**
 * 패링 스윙에 쓸 소품 목록.
 *
 * 배열 인덱스가 곧 네트워크로 오가는 값이다. 패링을 누른 사람이 인덱스 하나를 뽑아
 * 서버로 보내고, 서버가 FCWParrySyncData 에 실어 돌려주면 모든 클라이언트가 같은 배열의 같은 칸을 본다.
 * 그래서 메시도 사운드도 어긋나지 않는다.
 *
 * 중간 항목을 지우면 뒤쪽 인덱스가 한 칸씩 밀린다. 게임 중에는 문제가 없지만
 * (패링마다 새로 뽑으므로) 소품을 세이브에 기록하게 되면 인덱스 대신 이름을 써야 한다.
 */
UCLASS(BlueprintType)
class CROSSWALK_API UCWParryPropCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/**
	 * 애셋을 저장할 때 엔진이 불러 준다. 빠진 칸을 여기서 잡는다.
	 *
	 * 런타임 폴백을 두지 않는 대신 이걸 둔다. 사운드 칸이 빈 소품은 그 소품이 뽑힌
	 * 패링만 조용히 무음이 되는데, 차는 날아가고 소리만 없는 상태라 바쁜 화면에서
	 * 눈치채기 어렵다. 게임을 켜기 전에 막는 편이 낫다.
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	/** 범위를 벗어난 인덱스면 nullptr. 반환값을 쓰기 전에 항상 확인한다. */
	const FCWParryProp* GetProp(int32 PropIndex) const;

	/**
	 * 쓸 수 있는 소품 하나를 무작위로 고른다. 비어 있으면 INDEX_NONE.
	 *
	 * 패링을 누른 사람만 부른다(UCWParryComponent::RequestParry). 다른 머신이 각자 부르면
	 * 화면마다 다른 소품이 나온다. 뽑은 값은 서버를 거쳐 FCWParrySyncData 로 모두에게 간다.
	 */
	int32 PickRandomPropIndex() const;

public:
	/** 소품 목록. 배열 순서가 곧 복제되는 인덱스다. */
	UPROPERTY(EditAnywhere, Category = "Catalog")
	TArray<FCWParryProp> Props;
};
