// CWWearableItem.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "Data/Enums/CWWearableTypes.h"

#include "CWWearableItem.generated.h"

class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

/**
 * 코드가 이름으로 지목하는 슬롯들.
 *
 * 슬롯을 늘리는 것은 카탈로그 애셋에 항목을 더하는 일이고 C++ 을 고치지 않는다.
 * 다만 "H 키는 머리 슬롯을 순환한다" 처럼 코드가 특정 슬롯을 알아야 하는 곳이 있어서,
 * 그런 슬롯만 여기에 상수로 둔다. 코드에 TEXT("Head") 를 직접 쓰면 오타가 컴파일
 * 에러가 아니라 "아무 일도 안 일어남" 으로 나타나 원인을 찾기 어렵다.
 */
namespace CWWearableSlots
{
	extern CROSSWALK_API const FName Head;
	extern CROSSWALK_API const FName Torso;
	extern CROSSWALK_API const FName Legs;
	extern CROSSWALK_API const FName BodyColor;
}

/**
 * 착용물 한 종류. 헬멧 하나, 상의 한 벌이 각각 이 애셋 하나가 된다.
 *
 * 이 애셋이 하는 일은 "무엇을, 어디에, 어떻게 붙이는가" 를 적어 두는 것뿐이다.
 * 실제로 붙이는 것은 UCWWearableComponent 가 하고, 이 애셋은 그 지시서다.
 * 헬멧을 한 종류 늘리는 작업이 C++ 수정에서 애셋 하나 추가로 바뀌는 것이 목적이다.
 */
UCLASS(BlueprintType)
class CROSSWALK_API UCWWearableItem : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** 애셋을 저장할 때 엔진이 불러 준다. 잘못된 데이터를 런타임 전에 잡는다. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	/** 부착 방식에 맞는 메시가 채워져 있는지. 보는 칸이 모드에 따라 다르다. */
	bool HasMesh() const;

	/** UI 에 띄울 이름. DisplayName 이 비어 있으면 애셋 이름을 대신 낸다. */
	FText GetDisplayNameSafe() const;

public:
	/** 어느 슬롯에 들어가는가. CWWearableSlots 의 값 중 하나를 적는다. */
	UPROPERTY(EditAnywhere, Category = "Wearable")
	FName SlotName;

	UPROPERTY(EditAnywhere, Category = "Wearable")
	ECWWearAttachMode AttachMode = ECWWearAttachMode::Socket;

	/** 몸 색상 프리셋. 선택 인덱스는 기존 WornItems로 함께 복제된다. */
	UPROPERTY(EditAnywhere, Category = "Wearable|Color",
		meta = (EditCondition = "AttachMode == ECWWearAttachMode::BodyMaterial", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> BodyMaterial;

	UPROPERTY(EditAnywhere, Category = "Wearable|Color",
		meta = (ClampMin = "0", EditCondition = "AttachMode == ECWWearAttachMode::BodyMaterial", EditConditionHides))
	int32 BodyMaterialIndex = 0;

	/** Socket 모드에서 쓸 메시. */
	UPROPERTY(EditAnywhere, Category = "Wearable",
		meta = (EditCondition = "AttachMode == ECWWearAttachMode::Socket", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** LeaderPose 모드에서 쓸 메시. 몸과 같은 스켈레톤이어야 한다. */
	UPROPERTY(EditAnywhere, Category = "Wearable",
		meta = (EditCondition = "AttachMode == ECWWearAttachMode::LeaderPose", EditConditionHides))
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** 붙일 본/소켓 이름. 비어 있으면 몸 메시의 원점에 붙는다. */
	UPROPERTY(EditAnywhere, Category = "Wearable",
		meta = (EditCondition = "AttachMode == ECWWearAttachMode::Socket", EditConditionHides))
	FName SocketName;

	/** 소켓에 붙인 뒤의 미세 조정. 헬멧이 머리에 파묻히거나 뜨는 것을 여기서 맞춘다. */
	UPROPERTY(EditAnywhere, Category = "Wearable",
		meta = (EditCondition = "AttachMode == ECWWearAttachMode::Socket", EditConditionHides))
	FTransform RelativeTransform;

	/**
	 * 이 아이템만 다른 외곽선을 쓸 때 지정한다.
	 * 비어 있으면 컴포넌트가 몸 메시의 오버레이를 물려준다. 비어 있는 것이 기본이다.
	 */
	UPROPERTY(EditAnywhere, Category = "Wearable|Overlay")
	TSoftObjectPtr<UMaterialInterface> OverlayMaterialOverride;

	/** UI 목록에 띄울 이름. 비우면 애셋 이름이 그대로 나온다. */
	UPROPERTY(EditAnywhere, Category = "Wearable|UI")
	FText DisplayName;
};
