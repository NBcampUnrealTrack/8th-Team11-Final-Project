// CWWearableComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "CWWearableComponent.generated.h"

class UCWWearableCatalog;
class UCWWearableItem;
class UMaterialInterface;
class UMeshComponent;
class USkeletalMeshComponent;

/**
 * 착용물 전체를 관리하는 컴포넌트. UCWHeadgearComponent 와 UCWOutfitComponent 를 합친 것이다.
 *
 * 두 컴포넌트가 실제로 달랐던 것은 "붙이는 방법" 뿐이었다. 헬멧은 소켓 부착, 옷은 리더 포즈.
 * 그 차이를 아이템 애셋의 AttachMode 로 옮기고 나면 나머지(슬롯 수집, 오버레이 반영, 서버
 * 검증, OnRep 반영)는 완전히 같은 코드였다. 그래서 하나로 합친다.
 *
 * 슬롯 컴포넌트를 런타임에 만든다
 *   예전에는 캐릭터 생성자가 HelmetSlot_00~03 / OutfitSlot_00~03 을 미리 만들어 두고
 *   컴포넌트가 태그로 찾았다. 이제는 카탈로그의 슬롯 수만큼 필요할 때 만든다.
 *   슬롯을 늘리는 일이 C++ 수정에서 데이터 수정으로 바뀐다.
 *
 * 복제 정책
 *   슬롯마다 "어느 아이템" 을 담은 int8 배열 하나만 복제한다. 예전의 HelmetIndex(N 중 하나)와
 *   WornMask(슬롯별 on/off)를 동시에 대체한다. 슬롯 컴포넌트와 메시는 복제하지 않고, 모든
 *   머신이 같은 카탈로그와 같은 배열을 보고 각자 만든다.
 *
 * 예전 코드에서 그대로 가져온 판단
 *   - 클라이언트 예측을 하지 않는다. 한 프레임 늦어도 체감되지 않는 조작이고, 예측을 넣으면
 *     서버가 거절했을 때 되돌리는 코드가 따로 필요해진다.
 *   - 서버(리슨 서버 호스트 화면 포함)에서는 OnRep 이 돌지 않으므로 직접 반영한다.
 *   - BeginPlay 끝에서 한 번 더 반영한다. 늦게 접속한 클라이언트는 OnRep 이 BeginPlay 보다
 *     먼저 돌 수 있고, 그때는 슬롯이 아직 없어서 반영이 조용히 실패한다.
 *
 * 예전 코드에서 바꾼 판단
 *   순환과 토글의 계산을 서버가 한다. 예전에는 클라이언트가 다음 인덱스를 계산해서 보내고
 *   서버가 그 값을 검증했다. 서버가 직접 계산하면 검증할 값 자체가 없어진다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWWearableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWWearableComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 슬롯에 아이템을 입힌다. Item 이 nullptr 이면 그 슬롯을 벗는다. */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Wearable")
	void SetSlotItem(FName SlotName, UCWWearableItem* Item);

	/** 슬롯의 아이템을 다음 것으로 넘긴다. H 키가 쓰는 함수다. */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Wearable")
	void CycleSlotItem(FName SlotName);

	/**
	 * bIncludeInToggleAll 이 켜진 슬롯을 한꺼번에 입거나 벗는다. J 키가 쓰는 함수다.
	 * 하나라도 입고 있으면 전부 벗고, 아무것도 안 입었으면 각 슬롯의 기본 아이템을 입는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Wearable")
	void ToggleAllSlots();

	/** 그 슬롯이 지금 입고 있는 아이템. 없으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Wearable")
	UCWWearableItem* GetSlotItem(FName SlotName) const;

	/** 오버레이 머티리얼을 모든 슬롯에 다시 반영한다. BeginPlay 에서 자동으로 불린다. */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Wearable")
	void RefreshOverlayMaterial();

protected:
	virtual void BeginPlay() override;

	/** 실제 반영은 서버가 한다. 클라이언트에서 부른 요청이 여기로 들어온다. */
	UFUNCTION(Server, Reliable)
	void Server_SetSlotItem(int32 SlotIndex, int32 ItemIndex);

	UFUNCTION(Server, Reliable)
	void Server_CycleSlotItem(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_ToggleAllSlots();

	UFUNCTION()
	void OnRep_WornItems();

private:
	/** 서버에서만 도는 실제 변경. RPC 와 서버의 직접 호출이 여기로 모인다. */
	void SetSlotItemAuthority(int32 SlotIndex, int32 ItemIndex);
	void CycleSlotItemAuthority(int32 SlotIndex);
	void ToggleAllSlotsAuthority();

	/** WornItems 와 SlotComponents 의 길이를 카탈로그에 맞춘다. */
	void PrepareSlots();

	/** WornItems 전체를 화면에 반영한다. */
	void ApplyWornItems();

	/** 슬롯 하나를 화면에 반영한다. */
	void ApplySlot(int32 SlotIndex);

	/**
	 * 슬롯의 메시 컴포넌트를 준비한다. 없으면 만들고, 타입이 안 맞으면 버리고 다시 만든다.
	 * 반환된 컴포넌트는 이미 등록(RegisterComponent)이 끝난 상태다.
	 */
	UMeshComponent* EnsureSlotComponent(int32 SlotIndex, const UCWWearableItem* Item);

	void ApplyOverlayToSlot(UMeshComponent* SlotComponent, const UCWWearableItem* Item) const;

	/** 아이템 지정 > 슬롯 기본값 순으로 붙일 소켓 이름을 정한다. */
	FName ResolveSocketName(int32 SlotIndex, const UCWWearableItem* Item) const;

	/** 슬롯 이름 -> 인덱스. 없으면 경고를 남기고 INDEX_NONE 을 낸다. */
	int32 ResolveSlotIndex(FName SlotName) const;

	/** 소유 캐릭터의 몸 메시. 부착 대상이자 리더 포즈의 원본이며 오버레이의 출처다. */
	USkeletalMeshComponent* GetBodyMesh() const;

	bool HasAuthority() const;

protected:
	/**
	 * 이 캐릭터가 쓸 카탈로그. 비어 있으면 컴포넌트가 아무것도 하지 않는다.
	 * BP 클래스 디폴트에서 지정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Wearable")
	TObjectPtr<UCWWearableCatalog> Catalog;

	/**
	 * 순환할 때 "벗은 상태" 를 한 칸으로 끼워 넣을지.
	 * 켜면 맨머리 -> 0번 -> 1번 -> 맨머리 순으로 돈다. 벗은 모습도 확인할 수 있어 편하다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Wearable")
	bool bIncludeEmptyInCycle = true;

	/**
	 * 몸 메시의 오버레이 머티리얼(외곽선)을 착용물에도 씌울지.
	 * 물려받는 쪽이 기본이다. 아트 스타일이 바뀔 때 몸 한 곳만 고치면 전부 따라온다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Wearable|Overlay")
	bool bInheritBodyOverlayMaterial = true;

	/**
	 * 착용물 전체에 다른 오버레이를 쓰고 싶을 때 지정한다.
	 * 우선순위: 아이템 지정 > 이 값 > 몸에서 물려받기
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Wearable|Overlay")
	TObjectPtr<UMaterialInterface> OverlayMaterialOverride;

private:
	/**
	 * 슬롯마다 입고 있는 아이템의 카탈로그 인덱스. 길이는 카탈로그의 슬롯 수와 같다.
	 * INDEX_NONE 이면 그 슬롯은 비어 있다.
	 *
	 * TArray 의 복제는 원소 단위로 diff 를 뜨므로, 헬멧 하나를 바꾸면 1바이트짜리 원소
	 * 하나만 나간다. 예전 비트마스크와 실질적으로 같은 비용이다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WornItems)
	TArray<int8> WornItems;

	/**
	 * 슬롯마다의 메시 컴포넌트. 인덱스가 WornItems 와 일대일로 맞는다.
	 * 아직 아무것도 입지 않은 슬롯은 nullptr 이다.
	 *
	 * UMeshComponent 로 들고 있는 이유: SetVisibility 와 SetOverlayMaterial 이 이 클래스
	 * 위쪽에 있어서, 스태틱이든 스켈레탈이든 캐스트 없이 부를 수 있다. 캐스트가 필요한 것은
	 * 메시를 꽂는 부분과 리더 포즈를 거는 부분뿐이다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> SlotComponents;
};
