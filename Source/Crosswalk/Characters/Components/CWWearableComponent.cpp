// CWWearableComponent.cpp

#include "Characters/Components/CWWearableComponent.h"

#include "Characters/Wearables/CWWearableCatalog.h"
#include "Characters/Wearables/CWWearableItem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CWDecalStatics.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

UCWWearableComponent::UCWWearableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWWearableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWWearableComponent, WornItems);
}

void UCWWearableComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() == nullptr)
	{
		return;
	}

	if (Catalog == nullptr)
	{
		/**
		 * 카탈로그가 없으면 슬롯도 아이템도 없다. 조용히 아무것도 안 하면 "J 를 눌러도
		 * 반응이 없다" 는 증상만 남고 어디를 봐야 하는지 알 수 없으므로 로그를 남긴다.
		 */
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] Catalog 가 비어 있어 착용물을 관리할 수 없습니다. BP 클래스 디폴트에서 지정하세요."),
			*GetNameSafe(GetOwner()));
		return;
	}

	PrepareSlots();

	if (HasAuthority())
	{
		// 서버가 시작 복장을 확정한다. 클라이언트는 복제로 받는다.
		for (int32 SlotIndex = 0; SlotIndex < Catalog->GetSlotCount(); ++SlotIndex)
		{
			const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
			WornItems[SlotIndex] = static_cast<int8>(Catalog->IndexOfItem(SlotDef->DefaultItem));
		}
	}

	/**
	 * 복제로 이미 받아 둔 값을 여기서 한 번 더 반영한다.
	 *
	 * 늦게 접속한 클라이언트는 액터가 스폰될 때 WornItems 를 함께 받는데, 그 시점의 OnRep 은
	 * BeginPlay 보다 먼저 돌 수 있다. 그때는 SlotComponents 가 아직 비어 있어서 반영이
	 * 조용히 실패한다.
	 */
	ApplyWornItems();
}

void UCWWearableComponent::PrepareSlots()
{
	const int32 SlotCount = Catalog->GetSlotCount();

	// 슬롯 컴포넌트 칸을 미리 확보한다. 실제 컴포넌트는 처음 입을 때 만든다.
	SlotComponents.Init(nullptr, SlotCount);

	/**
	 * 서버는 권위 있게 초기화하고, 클라이언트는 복제된 값을 건드리지 않는다.
	 *
	 * 클라이언트에서 이미 서버 값이 도착했다면 길이가 맞으므로 그대로 둔다. 여기서 Init 을
	 * 무조건 부르면 방금 받은 착용 상태를 스스로 지워 버린다.
	 */
	if (HasAuthority() || WornItems.Num() != SlotCount)
	{
		WornItems.Init(INDEX_NONE, SlotCount);
	}
}

void UCWWearableComponent::SetSlotItem(FName SlotName, UCWWearableItem* Item)
{
	const int32 SlotIndex = ResolveSlotIndex(SlotName);
	if (SlotIndex == INDEX_NONE)
	{
		return;
	}

	// nullptr 은 "벗기" 라서 정상적인 요청이다. IndexOfItem 이 INDEX_NONE 을 내 준다.
	const int32 ItemIndex = Catalog->IndexOfItem(Item);
	if (Item != nullptr && ItemIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] '%s' 는 카탈로그의 Items 목록에 없습니다. 카탈로그에 먼저 추가하세요."),
			*GetNameSafe(GetOwner()), *GetNameSafe(Item));
		return;
	}

	if (!HasAuthority())
	{
		Server_SetSlotItem(SlotIndex, ItemIndex);
		return;
	}

	SetSlotItemAuthority(SlotIndex, ItemIndex);
}

void UCWWearableComponent::CycleSlotItem(FName SlotName)
{
	const int32 SlotIndex = ResolveSlotIndex(SlotName);
	if (SlotIndex == INDEX_NONE)
	{
		return;
	}

	/**
	 * 다음 인덱스 계산을 클라이언트가 하지 않는다.
	 *
	 * 예전 CycleHelmet 은 클라이언트가 계산한 값을 보내고 서버가 그것을 검증했다. 서버가
	 * 직접 계산하면 검증할 값 자체가 없어지고, 두 머신의 순환 순서가 어긋날 여지도 없다.
	 */
	if (!HasAuthority())
	{
		Server_CycleSlotItem(SlotIndex);
		return;
	}

	CycleSlotItemAuthority(SlotIndex);
}

void UCWWearableComponent::ToggleAllSlots()
{
	if (Catalog == nullptr)
	{
		return;
	}

	if (!HasAuthority())
	{
		Server_ToggleAllSlots();
		return;
	}

	ToggleAllSlotsAuthority();
}

UCWWearableItem* UCWWearableComponent::GetSlotItem(FName SlotName) const
{
	if (Catalog == nullptr)
	{
		return nullptr;
	}

	// 여기서는 경고를 내지 않는다. 조회 함수라 매 프레임 불릴 수 있다.
	const int32 SlotIndex = Catalog->IndexOfSlot(SlotName);
	if (!WornItems.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}

	return Catalog->GetItem(WornItems[SlotIndex]);
}

void UCWWearableComponent::Server_SetSlotItem_Implementation(int32 SlotIndex, int32 ItemIndex)
{
	SetSlotItemAuthority(SlotIndex, ItemIndex);
}

void UCWWearableComponent::Server_CycleSlotItem_Implementation(int32 SlotIndex)
{
	CycleSlotItemAuthority(SlotIndex);
}

void UCWWearableComponent::Server_ToggleAllSlots_Implementation()
{
	ToggleAllSlotsAuthority();
}

void UCWWearableComponent::SetSlotItemAuthority(int32 SlotIndex, int32 ItemIndex)
{
	if (!HasAuthority() || Catalog == nullptr || !WornItems.IsValidIndex(SlotIndex))
	{
		return;
	}

	/**
	 * 클라이언트가 보낸 값을 그대로 믿지 않는다.
	 *
	 * 예전에는 인덱스 범위만 잘라냈다. 이제는 "이 아이템이 이 슬롯 것인가" 까지 본다.
	 * 그게 없으면 조작된 클라이언트가 머리 슬롯에 바지를 넣는 요청을 보낼 수 있다.
	 */
	if (ItemIndex != INDEX_NONE && !Catalog->IsItemAllowedInSlot(SlotIndex, ItemIndex))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] %d 번 슬롯에 %d 번 아이템을 넣을 수 없습니다. 요청을 무시합니다."),
			*GetNameSafe(GetOwner()), SlotIndex, ItemIndex);
		return;
	}

	const int8 NewItemIndex = static_cast<int8>(ItemIndex);
	if (WornItems[SlotIndex] == NewItemIndex)
	{
		return;
	}

	WornItems[SlotIndex] = NewItemIndex;

	// 서버(리슨 서버 호스트 화면 포함)에서는 OnRep 이 돌지 않으므로 직접 반영한다.
	ApplySlot(SlotIndex);
}

void UCWWearableComponent::CycleSlotItemAuthority(int32 SlotIndex)
{
	if (!HasAuthority() || Catalog == nullptr || !WornItems.IsValidIndex(SlotIndex))
	{
		return;
	}

	/**
	 * 순환 후보 목록을 만든다. 벗은 상태를 앞에 끼우면 맨머리도 한 칸이 된다.
	 *
	 * 예전 CycleHelmet 은 INDEX_NONE 을 0번으로 옮겨 계산하는 보정이 필요했다. 후보를
	 * 배열로 만들면 그 보정이 사라지고 "다음 칸으로" 라는 계산만 남는다.
	 */
	TArray<int32> Candidates;
	if (bIncludeEmptyInCycle)
	{
		Candidates.Add(INDEX_NONE);
	}
	Catalog->AppendItemIndicesForSlot(SlotIndex, Candidates);

	if (Candidates.Num() <= 1)
	{
		const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] '%s' 슬롯에 넘길 아이템이 없습니다. 카탈로그 Items 에 이 슬롯 아이템을 추가하세요."),
			*GetNameSafe(GetOwner()), SlotDef != nullptr ? *SlotDef->SlotName.ToString() : TEXT("?"));
		return;
	}

	const int32 CurrentIndex = static_cast<int32>(WornItems[SlotIndex]);
	const int32 CurrentPos = Candidates.IndexOfByKey(CurrentIndex);

	// 지금 값이 후보에 없으면(카탈로그에서 그 아이템이 빠진 경우) 처음부터 시작한다.
	const int32 NextPos = (CurrentPos == INDEX_NONE) ? 0 : (CurrentPos + 1) % Candidates.Num();

	SetSlotItemAuthority(SlotIndex, Candidates[NextPos]);
}

void UCWWearableComponent::ToggleAllSlotsAuthority()
{
	if (!HasAuthority() || Catalog == nullptr)
	{
		return;
	}

	// 대상 슬롯 중 하나라도 입고 있으면 전부 벗는 쪽으로 간다. 예전 ToggleAllOutfit 과 같은 규칙이다.
	bool bAnyWorn = false;
	for (int32 SlotIndex = 0; SlotIndex < WornItems.Num(); ++SlotIndex)
	{
		const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
		if (SlotDef != nullptr && SlotDef->bIncludeInToggleAll && WornItems[SlotIndex] != INDEX_NONE)
		{
			bAnyWorn = true;
			break;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < WornItems.Num(); ++SlotIndex)
	{
		const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
		if (SlotDef == nullptr || !SlotDef->bIncludeInToggleAll)
		{
			continue;
		}

		// 입는 쪽이면 슬롯의 기본 아이템으로 돌아간다. 기본 아이템이 없으면 그대로 벗은 상태다.
		const int32 NewItemIndex = bAnyWorn ? INDEX_NONE : Catalog->IndexOfItem(SlotDef->DefaultItem);
		SetSlotItemAuthority(SlotIndex, NewItemIndex);
	}
}

void UCWWearableComponent::OnRep_WornItems()
{
	// 배열 전체를 다시 반영한다. 슬롯 수가 한 자리라 어느 칸이 바뀐지 따질 이득이 없다.
	ApplyWornItems();
}

void UCWWearableComponent::ApplyWornItems()
{
	for (int32 SlotIndex = 0; SlotIndex < WornItems.Num(); ++SlotIndex)
	{
		ApplySlot(SlotIndex);
	}
}

void UCWWearableComponent::ApplySlot(int32 SlotIndex)
{
	if (Catalog == nullptr || !WornItems.IsValidIndex(SlotIndex) || !SlotComponents.IsValidIndex(SlotIndex))
	{
		return;
	}

	const UCWWearableItem* Item = Catalog->GetItem(WornItems[SlotIndex]);

	if (Item == nullptr)
	{
		/**
		 * 벗은 상태. 컴포넌트는 남겨 두고 숨긴다.
		 *
		 * 파괴하지 않는 이유: 헬멧 순환처럼 자주 오가는 조작에서 컴포넌트를 매번 만들고
		 * 버리면 렌더 상태를 그만큼 다시 만든다. 숨긴 컴포넌트는 그리지 않으니 사실상
		 * 공짜라, 남겨 두는 쪽이 싸다.
		 */
		if (UMeshComponent* Existing = SlotComponents[SlotIndex])
		{
			Existing->SetVisibility(false);
		}
		return;
	}

	UMeshComponent* SlotComponent = EnsureSlotComponent(SlotIndex, Item);
	if (SlotComponent == nullptr)
	{
		return;
	}

	if (Item->AttachMode == ECWWearAttachMode::Socket)
	{
		UStaticMesh* Mesh = Item->StaticMesh.LoadSynchronous();
		if (Mesh == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] '%s' 의 StaticMesh 를 로드할 수 없습니다."),
				*GetNameSafe(GetOwner()), *GetNameSafe(Item));
			SlotComponent->SetVisibility(false);
			return;
		}

		UStaticMeshComponent* StaticSlot = CastChecked<UStaticMeshComponent>(SlotComponent);
		StaticSlot->SetStaticMesh(Mesh);

		// 소켓에 붙인 뒤의 미세 조정. 아이템마다 다르므로 입을 때마다 다시 넣는다.
		StaticSlot->SetRelativeTransform(Item->RelativeTransform);
	}
	else
	{
		USkeletalMesh* Mesh = Item->SkeletalMesh.LoadSynchronous();
		if (Mesh == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] '%s' 의 SkeletalMesh 를 로드할 수 없습니다."),
				*GetNameSafe(GetOwner()), *GetNameSafe(Item));
			SlotComponent->SetVisibility(false);
			return;
		}

		/**
		 * 메시만 갈아 끼우면 된다. 리더 포즈를 다시 걸 필요는 없다.
		 *
		 * SetSkinnedAssetAndUpdate 가 등록된 컴포넌트에서 UpdateLeaderBoneMap 을 다시 부르기
		 * 때문이다. 반대로 등록 전에 메시를 꽂으면 그 갱신이 건너뛰어져 옷이 레퍼런스 포즈에
		 * 굳는다. 스켈레톤 불일치와 증상이 똑같아서 원인을 찾기 어렵다.
		 * EnsureSlotComponent 가 등록을 보장한다.
		 */
		CastChecked<USkeletalMeshComponent>(SlotComponent)->SetSkeletalMeshAsset(Mesh);
	}

	ApplyOverlayToSlot(SlotComponent, Item);
	SlotComponent->SetVisibility(true);
}

UMeshComponent* UCWWearableComponent::EnsureSlotComponent(int32 SlotIndex, const UCWWearableItem* Item)
{
	USkeletalMeshComponent* BodyMesh = GetBodyMesh();
	if (BodyMesh == nullptr)
	{
		return nullptr;
	}

	// 부착 방식이 컴포넌트 타입을 결정한다. 이것이 두 컴포넌트를 하나로 합칠 수 있는 근거다.
	UClass* WantedClass = (Item->AttachMode == ECWWearAttachMode::Socket)
		? UStaticMeshComponent::StaticClass()
		: USkeletalMeshComponent::StaticClass();

	UMeshComponent* Existing = SlotComponents[SlotIndex];

	// 타입이 다르면 재사용할 수 없다. 스태틱 컴포넌트에 스켈레탈 메시를 꽂을 방법은 없다.
	if (Existing != nullptr && Existing->GetClass() != WantedClass)
	{
		Existing->DestroyComponent();
		Existing = nullptr;
		SlotComponents[SlotIndex] = nullptr;
	}

	const FName SocketName = ResolveSocketName(SlotIndex, Item);

	if (Existing != nullptr)
	{
		/**
		 * 같은 타입이라 재사용한다. 붙는 자리만 달라질 수 있다.
		 *
		 * 등록이 끝난 컴포넌트에는 SetupAttachment 를 쓸 수 없다. 엔진이 ensure 로 막는다
		 * ("Once a component is registered you must use AttachToComponent" - SceneComponent.cpp).
		 * KeepRelativeTransform 을 쓰는 이유는 상대 트랜스폼을 ApplySlot 이 직접 넣기 때문이다.
		 */
		if (Existing->GetAttachSocketName() != SocketName)
		{
			Existing->AttachToComponent(BodyMesh, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
		}
		return Existing;
	}

	/**
	 * 이름을 MakeUniqueObjectName 으로 받는 이유.
	 *
	 * 슬롯 이름으로 고정하면, 타입이 바뀌어 방금 DestroyComponent 한 객체가 아직 GC 되지
	 * 않은 상태에서 같은 이름으로 새 객체를 만들게 된다. 이름 충돌은 조용히 넘어가지 않는다.
	 */
	const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
	const FName BaseName(*FString::Printf(TEXT("WearableSlot_%s"),
		SlotDef != nullptr ? *SlotDef->SlotName.ToString() : TEXT("Unknown")));
	const FName ComponentName = MakeUniqueObjectName(GetOwner(), WantedClass, BaseName);

	UMeshComponent* NewSlot = NewObject<UMeshComponent>(GetOwner(), WantedClass, ComponentName);
	if (NewSlot == nullptr)
	{
		return nullptr;
	}

	// 아직 등록 전이라 SetupAttachment 를 쓸 수 있다. 실제 부착은 RegisterComponent 가 수행한다.
	NewSlot->SetupAttachment(BodyMesh, SocketName);

	// 래그돌 중 몸의 물리 바디와 서로 밀어내는 것을 막는다. 착용물은 어떤 판정에도 관여하지 않는다.
	NewSlot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewSlot->SetGenerateOverlapEvents(false);

	// 시각 전용이라 복제하지 않는다. 모든 머신이 같은 데이터를 보고 각자 만든다.
	NewSlot->SetIsReplicated(false);

	// 몸과 같이 바닥 데칼을 받지 않는다. 캐릭터 BeginPlay 이후에 갈아입는 착용물은 여기서만 잡힌다.
	UCWDecalStatics::DisableDecalReceiveOnComponent(NewSlot);

	NewSlot->RegisterComponent();

	if (Item->AttachMode == ECWWearAttachMode::LeaderPose)
	{
		USkeletalMeshComponent* SkeletalSlot = CastChecked<USkeletalMeshComponent>(NewSlot);

		/**
		 * 팔로워는 자기 애니메이션을 절대 평가하지 않는다.
		 * AnimBP 가 붙어 있으면 CPU 를 두 배로 쓰면서 리더와 한 프레임씩 어긋난다.
		 */
		SkeletalSlot->SetAnimationMode(EAnimationMode::AnimationCustomMode);

		/**
		 * 바운드를 리더에서 가져온다.
		 *
		 * 이걸 빼면 래그돌로 몸이 멀리 날아갈 때 옷만 사라진다. 팔로워가 자기 바운드를
		 * 갱신하지 않아 화면 밖으로 컬링되기 때문이다.
		 */
		SkeletalSlot->bUseBoundsFromLeaderPoseComponent = true;

		/**
		 * 이 한 줄이 옷을 몸에 물리는 전부다.
		 *
		 * 리더의 컴포넌트 공간 본 트랜스폼을 그대로 복사해 스키닝한다. 그 값이 애니메이션에서
		 * 왔는지 SetSimulatePhysics 로 물리가 만들었는지 구분하지 않기 때문에, 래그돌·비행·
		 * 기상 몬타주가 전부 자동으로 따라온다. 래그돌을 위해 따로 할 일이 없다.
		 */
		SkeletalSlot->SetLeaderPoseComponent(BodyMesh);
	}

	SlotComponents[SlotIndex] = NewSlot;
	return NewSlot;
}

void UCWWearableComponent::ApplyOverlayToSlot(UMeshComponent* SlotComponent, const UCWWearableItem* Item) const
{
	if (SlotComponent == nullptr)
	{
		return;
	}

	const USkeletalMeshComponent* BodyMesh = GetBodyMesh();

	// 우선순위: 아이템 지정 > 컴포넌트 지정 > 몸에서 물려받기
	UMaterialInterface* NewOverlay = (Item != nullptr) ? Item->OverlayMaterialOverride.LoadSynchronous() : nullptr;

	if (NewOverlay == nullptr)
	{
		NewOverlay = OverlayMaterialOverride;
	}

	if (NewOverlay == nullptr && bInheritBodyOverlayMaterial && BodyMesh != nullptr)
	{
		/**
		 * GetOverlayMaterial 은 컴포넌트에 지정된 값이 없으면 스켈레탈 메시 애셋에 구워진 것을
		 * 대신 낸다. 그래서 외곽선을 BP 컴포넌트에 걸었든 메시 애셋에 걸었든 양쪽 다 잡힌다.
		 */
		NewOverlay = BodyMesh->GetOverlayMaterial();
	}

	if (NewOverlay == nullptr)
	{
		// 물려받을 것도 지정된 것도 없다. 이미 걸려 있는 것을 지우지는 않는다.
		return;
	}

	// SetOverlayMaterial 은 값이 같으면 아무것도 하지 않는다. 매번 불러도 렌더 상태가 더러워지지 않는다.
	SlotComponent->SetOverlayMaterial(NewOverlay);

	/**
	 * 컬링 거리도 몸과 맞춘다.
	 *
	 * 몸과 착용물이 서로 다른 거리에서 외곽선이 끊기면 그 구간에서 실루엣이 어긋난다.
	 * 4인 협동이라 남의 캐릭터를 멀리서 보는 시간이 길어서 눈에 띈다.
	 */
	SlotComponent->SetOverlayMaterialMaxDrawDistance(
		BodyMesh != nullptr ? BodyMesh->GetOverlayMaterialMaxDrawDistance() : 0.f);
}

void UCWWearableComponent::RefreshOverlayMaterial()
{
	if (Catalog == nullptr)
	{
		return;
	}

	for (int32 SlotIndex = 0; SlotIndex < SlotComponents.Num(); ++SlotIndex)
	{
		if (!WornItems.IsValidIndex(SlotIndex))
		{
			continue;
		}

		ApplyOverlayToSlot(SlotComponents[SlotIndex], Catalog->GetItem(WornItems[SlotIndex]));
	}
}

FName UCWWearableComponent::ResolveSocketName(int32 SlotIndex, const UCWWearableItem* Item) const
{
	/**
	 * 리더 포즈는 소켓에 붙이면 안 된다.
	 *
	 * 옷의 위치는 스키닝 웨이트가 정한다. 소켓 오프셋이 얹히면 스키닝 결과 위에 이동이 한 번
	 * 더 들어가서 몸에서 떠 버린다. 예전 CreateOutfitSlot 이 소켓 없이 몸 메시에 그대로 붙인
	 * 것과 같은 이유다.
	 */
	if (Item->AttachMode == ECWWearAttachMode::LeaderPose)
	{
		return NAME_None;
	}

	if (!Item->SocketName.IsNone())
	{
		return Item->SocketName;
	}

	const FCWWearableSlotDef* SlotDef = Catalog->GetSlotDef(SlotIndex);
	return (SlotDef != nullptr) ? SlotDef->DefaultSocketName : NAME_None;
}

int32 UCWWearableComponent::ResolveSlotIndex(FName SlotName) const
{
	if (Catalog == nullptr)
	{
		return INDEX_NONE;
	}

	const int32 SlotIndex = Catalog->IndexOfSlot(SlotName);
	if (SlotIndex == INDEX_NONE)
	{
		// 슬롯 이름 오타는 컴파일 에러가 아니라 "아무 일도 안 일어남" 으로 나타난다.
		UE_LOG(LogTemp, Warning, TEXT("[%s] '%s' 슬롯이 카탈로그에 없습니다."),
			*GetNameSafe(GetOwner()), *SlotName.ToString());
	}

	return SlotIndex;
}

USkeletalMeshComponent* UCWWearableComponent::GetBodyMesh() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter != nullptr ? OwnerCharacter->GetMesh() : nullptr;
}

bool UCWWearableComponent::HasAuthority() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor != nullptr && OwnerActor->HasAuthority();
}
