// CWWearableCatalog.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "CWWearableCatalog.generated.h"

class UCWWearableItem;
class USkeleton;

/**
 * 슬롯 하나의 정의.
 *
 * 배열에 항목을 더하는 것이 곧 슬롯을 늘리는 것이다. 예전에는 캐릭터 생성자에
 * CreateHelmetSlot 을 한 줄 더 쓰고, 헤더에 UPROPERTY 멤버를 하나 더 선언하고,
 * 에디터를 닫고 다시 빌드해야 했다.
 */
USTRUCT(BlueprintType)
struct FCWWearableSlotDef
{
	GENERATED_BODY()

public:
	/** 슬롯 이름. 아이템의 SlotName 과 이 값이 같아야 그 슬롯에 들어간다. */
	UPROPERTY(EditAnywhere, Category = "Slot")
	FName SlotName;

	/**
	 * 아이템의 SocketName 이 비어 있을 때 대신 쓸 본/소켓 이름.
	 *
	 * 헬멧을 여러 개 만들면 전부 같은 본("head")에 붙는다. 아이템마다 같은 값을 적는 것은
	 * 하나를 빠뜨리기 좋으니, 슬롯에 기본값을 두고 예외인 아이템만 자기 값을 적는다.
	 */
	UPROPERTY(EditAnywhere, Category = "Slot")
	FName DefaultSocketName;

	/** 게임 시작 시 이 슬롯에 입혀 둘 아이템. 비우면 벗은 채로 시작한다. */
	UPROPERTY(EditAnywhere, Category = "Slot")
	TObjectPtr<UCWWearableItem> DefaultItem;

	/**
	 * 한꺼번에 입고 벗는(J 키) 대상인가.
	 *
	 * 작업복은 상의와 하의를 같이 입고 벗지만 헬멧은 따로 순환한다(H 키).
	 * 머리 슬롯만 이 값을 끄면 J 를 눌러도 헬멧은 그대로 남는다. 이 판단을 데이터로 두면
	 * 장갑이나 신발이 추가될 때 캐릭터 C++ 을 고치지 않아도 된다.
	 */
	UPROPERTY(EditAnywhere, Category = "Slot")
	bool bIncludeInToggleAll = true;
};

/**
 * 한 캐릭터가 쓸 수 있는 슬롯과 아이템 전체 목록.
 *
 * 이 애셋의 존재 이유는 두 가지다.
 *   1) 슬롯 개수를 컴파일 타임에서 데이터로 옮긴다.
 *   2) 아이템에 "모든 머신이 같게 보는 순서" 를 준다.
 *
 * 2번이 특히 중요하다. 예전에는 컴포넌트를 태그로 모아 이름순으로 정렬해서 순서를 맞췄다.
 * 액터의 컴포넌트 목록이 TSet 이라 순회 순서가 보장되지 않기 때문이었다. 이제는 Items
 * 배열의 순서가 곧 순서라서, 복제할 값이 배열 인덱스 하나로 끝난다.
 */
UCLASS(BlueprintType)
class CROSSWALK_API UCWWearableCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** 애셋을 저장할 때 엔진이 불러 준다. 슬롯과 아이템의 관계가 어긋난 것을 여기서 잡는다. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	int32 GetSlotCount() const;

	/** 없는 인덱스면 nullptr. 반환값을 쓰기 전에 항상 확인한다. */
	const FCWWearableSlotDef* GetSlotDef(int32 SlotIndex) const;

	/** 슬롯 이름 -> 인덱스. 없으면 INDEX_NONE. */
	int32 IndexOfSlot(FName SlotName) const;

	/** 인덱스 -> 아이템. INDEX_NONE 이나 범위를 벗어난 값이면 nullptr("벗은 상태"). */
	UCWWearableItem* GetItem(int32 ItemIndex) const;

	/** 아이템 -> 인덱스. nullptr 이거나 목록에 없으면 INDEX_NONE. */
	int32 IndexOfItem(const UCWWearableItem* Item) const;

	/**
	 * 그 아이템을 그 슬롯에 넣을 수 있는가.
	 *
	 * 서버가 클라이언트 요청을 검증할 때 쓴다. 예전에는 인덱스 범위만 봤기 때문에,
	 * 조작된 클라이언트가 머리 슬롯에 바지를 넣는 요청을 보내도 통과했다.
	 */
	bool IsItemAllowedInSlot(int32 SlotIndex, int32 ItemIndex) const;

	/**
	 * 그 슬롯에 들어갈 수 있는 아이템 인덱스들을 배열 뒤에 덧붙인다.
	 *
	 * 지우지 않고 덧붙이는 이유: 순환 목록을 만들 때 맨 앞에 "벗은 상태"(INDEX_NONE)를
	 * 먼저 넣어 두고 그 뒤에 아이템을 붙이는 식으로 쓴다.
	 */
	void AppendItemIndicesForSlot(int32 SlotIndex, TArray<int32>& OutItemIndices) const;

public:
	/** 이 캐릭터가 가진 슬롯 목록. 배열 순서가 곧 슬롯 인덱스다. */
	UPROPERTY(EditAnywhere, Category = "Catalog")
	TArray<FCWWearableSlotDef> Slots;

	/**
	 * 쓸 수 있는 아이템 전체. 배열 인덱스가 복제되는 값이다.
	 *
	 * 소프트가 아니라 하드 참조인 이유: 아이템 애셋 자체는 소프트 포인터 몇 개가 든 아주
	 * 작은 객체다. 무거운 메시는 그 소프트 포인터 뒤에 있어서 입을 때까지 로드되지 않는다.
	 * 대신 "인덱스로 즉시 찾을 수 있음" 을 이 하드 참조가 준다.
	 *
	 * 중간 항목을 지우면 뒤쪽 인덱스가 전부 한 칸 밀린다. 게임 중에는 문제가 없지만
	 * (세션마다 새로 고르므로) 나중에 착용 상태를 세이브에 저장하게 되면 인덱스 대신
	 * 이름으로 바꿔야 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Catalog")
	TArray<TObjectPtr<UCWWearableItem>> Items;

	/**
	 * 리더 포즈 아이템이 써야 하는 스켈레톤. 검증에만 쓰고 런타임에는 보지 않는다.
	 *
	 * 스켈레톤이 다른 옷은 리더 포즈 본 매핑이 거의 비어서 레퍼런스 포즈에 굳은 채로
	 * 남는다. 원인이 눈에 보이지 않는 종류의 버그라, 게임을 실행하기 전에 잡는다.
	 * 몸 메시가 쓰는 스켈레톤을 지정한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Catalog|Validation")
	TSoftObjectPtr<USkeleton> BodySkeleton;
};
