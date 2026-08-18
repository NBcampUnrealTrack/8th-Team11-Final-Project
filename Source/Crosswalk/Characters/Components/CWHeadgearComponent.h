// Copyright Crosswalk Team. All Rights Reserved.
// CWHeadgearComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CWHeadgearComponent.generated.h"

class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * 머리 장비(헬멧/모자)를 하나만 켜 두고 갈아 끼우는 컴포넌트.
 *
 * 헬멧 메시 컴포넌트는 이 컴포넌트가 만들지 않는다. ACWPlayerCharacter 생성자가 슬롯
 * (HelmetSlot_00 …)을 만들어 머리에 붙이고 태그를 달아 두고, 이 컴포넌트는 BeginPlay 에서
 * 태그로 모아 "어느 하나만 보이게" 한다. 태그만 맞으면 BP 에서 손으로 추가한 컴포넌트도 잡힌다.
 *
 * 메시를 런타임에 만들지 않는 이유
 *   런타임에 만든 컴포넌트는 에디터 뷰포트에 없어서 기즈모로 정렬할 수 없다. 부착물
 *   애셋은 원점과 단위가 제작 도구마다 달라서, 숫자만 보고 맞추면 PIE 왕복이 계속 필요하다.
 *   생성자에서 만들어 두면 BP 뷰포트에서 위치·크기를 기즈모로 맞출 수 있다.
 *
 * 그래도 컴포넌트가 필요한 이유
 *   BP 에서 Set Visibility 를 그냥 토글하면 그 머신에서만 바뀐다. 4인 협동이라 남의
 *   화면에도 같은 헬멧이 보여야 하고, 그러려면 서버가 인덱스를 확정해 모두에게 뿌려야
 *   한다. 그 복제와 순환 상태를 여기서 들고 있는다.
 *
 * 복제 정책
 *   "몇 번 헬멧을 쓰고 있나"라는 인덱스 하나만 복제한다. 메시 컴포넌트는 BP 에 있으니
 *   모든 머신에 이미 똑같이 존재한다. 각자 그 인덱스만 보고 보이기/숨기기를 정하면 된다.
 *
 * 태그가 붙은 컴포넌트가 하나도 없어도 정상 동작한다. 그 경우 H 를 눌러도 아무 일이 없다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CROSSWALK_API UCWHeadgearComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCWHeadgearComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 다음 헬멧으로 넘긴다. 마지막까지 가면 처음으로 돌아온다.
	 * 어느 머신에서 불러도 되고, 실제 반영은 항상 서버를 거쳐 모두에게 복제된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Headgear")
	void CycleHelmet();

	/**
	 * 특정 헬멧을 지정해서 쓴다. INDEX_NONE(-1) 이면 전부 숨긴다.
	 * 범위를 벗어난 값은 서버에서 걸러진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Headgear")
	void SetHelmet(int32 NewHelmetIndex);

	/** 지금 켜져 있는 헬멧 인덱스. 아무것도 안 썼으면 INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Headgear")
	int32 GetHelmetIndex() const;

	/** 모아 둔 헬멧 개수. 태그가 붙은 컴포넌트의 수다. */
	UFUNCTION(BlueprintPure, Category = "Crosswalk|Headgear")
	int32 GetHelmetCount() const;

	/**
	 * 오버레이 머티리얼을 모든 헬멧에 다시 반영한다.
	 *
	 * BeginPlay 에서 자동으로 불리므로 평소에 부를 일은 없다. 몸 메시의 오버레이를
	 * 런타임에 바꾸는 연출이 생기면(외곽선을 켜고 끄는 식) 그쪽에서 같이 불러야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Headgear")
	void RefreshOverlayMaterial();

protected:
	virtual void BeginPlay() override;

	/** 실제 반영은 서버가 한다. 클라이언트에서 부른 CycleHelmet/SetHelmet 이 여기로 들어온다. */
	UFUNCTION(Server, Reliable)
	void Server_SetHelmet(int32 NewHelmetIndex);

	UFUNCTION()
	void OnRep_HelmetIndex();

private:
	/**
	 * 태그가 붙은 스태틱 메시 컴포넌트를 모아 이름순으로 정렬한다.
	 *
	 * 정렬이 필요한 이유: AActor 의 컴포넌트 목록은 TSet 이라 순회 순서가 보장되지 않는다.
	 * 그런데 복제되는 것은 인덱스뿐이라, 머신마다 순서가 다르면 같은 인덱스가 서로 다른
	 * 헬멧을 가리켜 화면마다 다른 모자를 쓰게 된다. 이름순으로 고정하면 순서가 결정적이 되고,
	 * 동시에 컴포넌트 이름(Helmet_01, Helmet_02 …)으로 순서를 직접 지정할 수 있게 된다.
	 */
	void CollectHelmetComponents();

	/** 소유 캐릭터의 몸 메시. 오버레이 머티리얼을 물려받는 원본이다. */
	USkeletalMeshComponent* GetBodyMesh() const;

	/** HelmetIndex 를 화면에 반영한다. 서버·클라이언트 모두 자기 머신에서 부른다. */
	void ApplyHelmet();

protected:
	/**
	 * 헬멧 후보를 찾을 때 쓰는 컴포넌트 태그.
	 *
	 * ACWPlayerCharacter 생성자가 슬롯에 붙여 두는 태그와 같아야 한다. 여기만 바꾸면
	 * 슬롯을 못 찾아 H 를 눌러도 아무 일이 없다.
	 *
	 * 태그로 찾는 이유: BP 클래스 디폴트에서는 형제 컴포넌트를 프로퍼티로 직접 지정할 수
	 * 없다(컴포넌트는 인스턴스마다 새로 만들어진다). 덕분에 BP 에서 손으로 추가한
	 * 컴포넌트에 같은 태그를 달아도 그대로 헬멧으로 잡힌다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear")
	FName HelmetComponentTag = TEXT("Helmet");

	/**
	 * 시작할 때 켜 둘 헬멧. -1 이면 전부 숨긴 채로 시작한다.
	 * 서버에서만 적용되고 클라이언트에는 복제로 전달된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear")
	int32 InitialHelmetIndex = INDEX_NONE;

	/**
	 * 순환할 때 "안 쓴 상태"를 한 칸으로 끼워 넣을지.
	 *
	 * true 면 맨머리 → 0번 → 1번 → 맨머리 순으로 돈다. 벗은 모습도 확인할 수 있어
	 * 프로토타입 단계에서 편하다. false 면 헬멧들 사이에서만 돈다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear")
	bool bIncludeNoHelmetInCycle = true;

	/**
	 * 헬멧의 콜리전을 코드가 강제로 끌지.
	 *
	 * 래그돌 중에는 몸 메시가 물리 시뮬레이션을 돌리고 있어서, 헬멧에 콜리전이 남아
	 * 있으면 머리 바디와 서로 밀어내며 몸이 튀거나 헬멧이 목을 파고든다. BP 에서
	 * 컴포넌트를 추가할 때 빠뜨리기 쉬운 설정이라 기본으로 켜 둔다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear")
	bool bForceNoCollision = true;

	/**
	 * 몸 메시의 오버레이 머티리얼을 헬멧에도 씌울지.
	 *
	 * 캐릭터에 외곽선 같은 오버레이가 걸려 있으면 헬멧에도 같은 것이 필요하다. 안 씌우면
	 * 몸만 외곽선이 있고 헬멧은 매끈해서, 붙어 있는 물건인데 다른 스타일로 떠 보인다.
	 *
	 * 물려받는 쪽을 기본값으로 둔 이유: 아트 스타일이 바뀔 때 몸 메시 한 곳만 고치면
	 * 모든 부착물이 따라온다. 부착물마다 같은 머티리얼을 꽂아두면 반드시 하나가 빠진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear|Overlay")
	bool bInheritBodyOverlayMaterial = true;

	/**
	 * 헬멧에만 다른 오버레이를 쓰고 싶을 때 지정한다. 비어 있으면 위 설정에 따라 몸에서 물려받는다.
	 * 지정하면 물려받기보다 이쪽이 우선한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crosswalk|Headgear|Overlay")
	TObjectPtr<UMaterialInterface> OverlayMaterialOverride;

private:
	/**
	 * 지금 켜져 있는 헬멧 인덱스. 서버가 정하고 모두에게 복제된다.
	 *
	 * 기본값을 0 이 아니라 INDEX_NONE 으로 두는 이유: 0 이 기본값이면 서버가 0번 헬멧을
	 * 지정했을 때 값이 바뀌지 않아 OnRep 이 안 돈다. "아무것도 안 씀"을 별도 값으로
	 * 빼 두면 0번도 정상적인 변경으로 잡힌다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_HelmetIndex)
	int32 HelmetIndex = INDEX_NONE;

	// BeginPlay 에서 태그로 모아 이름순으로 정렬해 둔 목록. 이 순서가 인덱스가 된다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HelmetComponents;
};
