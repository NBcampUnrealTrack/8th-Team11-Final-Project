// Copyright Crosswalk Team. All Rights Reserved.
// CWHeadgearComponent.cpp

#include "Characters/Components/CWHeadgearComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

UCWHeadgearComponent::UCWHeadgearComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWHeadgearComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWHeadgearComponent, HelmetIndex);
}

void UCWHeadgearComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	CollectHelmetComponents();
	RefreshOverlayMaterial();

	if (OwnerActor->HasAuthority())
	{
		// 서버가 시작값을 확정한다. 클라이언트는 복제로 받는다.
		SetHelmet(InitialHelmetIndex);
	}

	/**
	 * 복제로 이미 받아 둔 값을 여기서 한 번 더 반영한다.
	 *
	 * 늦게 접속한 클라이언트는 액터가 스폰될 때 HelmetIndex 를 초기값으로 함께 받는데,
	 * 그 시점의 OnRep 은 BeginPlay 보다 먼저 돌 수 있다. 그때는 HelmetComponents 가
	 * 아직 비어 있어서 반영이 조용히 실패한다.
	 */
	ApplyHelmet();
}

void UCWHeadgearComponent::CycleHelmet()
{
	if (HelmetComponents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 바꿀 헬멧이 없습니다. HelmetSlot_XX 컴포넌트에 Static Mesh 를 꽂았는지 확인하세요."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 순환 길이. 맨머리를 끼우면 헬멧 개수보다 한 칸 길다.
	const int32 SlotCount = HelmetComponents.Num() + (bIncludeNoHelmetInCycle ? 1 : 0);

	// INDEX_NONE 을 0번 슬롯으로 옮겨 놓고 계산해야 음수가 섞이지 않는다.
	const int32 CurrentSlot = bIncludeNoHelmetInCycle ? (HelmetIndex + 1) : FMath::Max(HelmetIndex, 0);
	const int32 NextSlot = (CurrentSlot + 1) % SlotCount;

	SetHelmet(bIncludeNoHelmetInCycle ? (NextSlot - 1) : NextSlot);
}

void UCWHeadgearComponent::SetHelmet(int32 NewHelmetIndex)
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	if (!OwnerActor->HasAuthority())
	{
		// 클라이언트가 자기 화면만 먼저 바꾸고 서버에 요청하지는 않는다.
		// 헬멧은 즉각적인 조작감이 필요한 입력이 아니라, 한 프레임 늦어도 체감되지 않는다.
		// 예측을 넣으면 서버가 거절했을 때 되돌리는 코드가 따로 필요해진다.
		Server_SetHelmet(NewHelmetIndex);
		return;
	}

	// 범위를 벗어난 값은 "안 씀"으로 눌러 둔다. 클라이언트가 보낸 값을 그대로 믿지 않는다.
	const int32 ClampedIndex = HelmetComponents.IsValidIndex(NewHelmetIndex) ? NewHelmetIndex : INDEX_NONE;
	if (HelmetIndex == ClampedIndex)
	{
		return;
	}

	HelmetIndex = ClampedIndex;

	// 서버(리슨 서버 호스트 화면 포함)에서는 OnRep 이 돌지 않으므로 직접 반영한다.
	ApplyHelmet();
}

int32 UCWHeadgearComponent::GetHelmetIndex() const
{
	return HelmetIndex;
}

int32 UCWHeadgearComponent::GetHelmetCount() const
{
	return HelmetComponents.Num();
}

void UCWHeadgearComponent::RefreshOverlayMaterial()
{
	const USkeletalMeshComponent* BodyMesh = GetBodyMesh();

	UMaterialInterface* NewOverlay = OverlayMaterialOverride;
	if (NewOverlay == nullptr && bInheritBodyOverlayMaterial && BodyMesh != nullptr)
	{
		/**
		 * GetOverlayMaterial 은 컴포넌트에 지정된 값이 없으면 스켈레탈 메시 애셋에 구워진 것을
		 * 대신 낸다 (UMeshComponent::GetOverlayMaterial → GetDefaultOverlayMaterial).
		 * 그래서 외곽선을 BP 컴포넌트에 걸었든 메시 애셋에 걸었든 양쪽 다 여기서 잡힌다.
		 */
		NewOverlay = BodyMesh->GetOverlayMaterial();
	}

	if (NewOverlay == nullptr)
	{
		// 물려받을 것도, 지정된 것도 없다. BP 에서 컴포넌트마다 직접 걸었을 수 있으니 덮어쓰지 않는다.
		return;
	}

	const float MaxDrawDistance = BodyMesh != nullptr ? BodyMesh->GetOverlayMaterialMaxDrawDistance() : 0.f;

	for (const TObjectPtr<UStaticMeshComponent>& HelmetComponent : HelmetComponents)
	{
		if (HelmetComponent == nullptr)
		{
			continue;
		}

		// SetOverlayMaterial 은 값이 같으면 아무것도 하지 않는다. 매번 불러도 렌더 상태가 더러워지지 않는다.
		HelmetComponent->SetOverlayMaterial(NewOverlay);

		/**
		 * 컬링 거리도 몸과 맞춘다.
		 *
		 * OverlayMaterialMaxDrawDistance 는 이 거리를 넘으면 오버레이만 그리지 않게 하는 값이다.
		 * 몸과 헬멧이 서로 다른 거리에서 끊기면, 그 사이 구간에서 몸에는 외곽선이 있고 헬멧에는
		 * 없는(혹은 반대) 상태가 되어 실루엣이 어긋난다. 4인 협동이라 남의 캐릭터를 멀리서
		 * 보는 시간이 길어서 눈에 띈다.
		 */
		HelmetComponent->SetOverlayMaterialMaxDrawDistance(MaxDrawDistance);
	}
}

void UCWHeadgearComponent::CollectHelmetComponents()
{
	HelmetComponents.Reset();

	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	TInlineComponentArray<UStaticMeshComponent*> MeshComponents(OwnerActor);

	TArray<UStaticMeshComponent*> Tagged;
	Tagged.Reserve(MeshComponents.Num());
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent == nullptr || !MeshComponent->ComponentHasTag(HelmetComponentTag))
		{
			continue;
		}

		/**
		 * 메시가 꽂히지 않은 슬롯은 뺀다.
		 *
		 * 캐릭터가 슬롯을 넉넉하게 만들어 두기 때문에(HelmetSlotCount) 이 걸러내기가 없으면
		 * H 를 눌렀을 때 아무것도 안 보이는 빈 슬롯을 몇 번씩 지나가게 된다.
		 * BP 에 꽂힌 메시는 모든 머신에서 같으므로, 이렇게 걸러도 인덱스는 여전히 결정적이다.
		 */
		if (MeshComponent->GetStaticMesh() == nullptr)
		{
			continue;
		}

		Tagged.Add(MeshComponent);
	}

	/**
	 * 이름순으로 정렬해 인덱스를 결정적으로 만든다 (헤더의 CollectHelmetComponents 주석 참고).
	 * 포인터 배열의 Sort 는 술어에 역참조된 객체를 넘긴다.
	 */
	Tagged.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
	{
		return A.GetName() < B.GetName();
	});

	HelmetComponents.Reserve(Tagged.Num());
	for (UStaticMeshComponent* MeshComponent : Tagged)
	{
		if (bForceNoCollision)
		{
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComponent->SetGenerateOverlapEvents(false);
		}

		HelmetComponents.Add(MeshComponent);
	}

	if (HelmetComponents.Num() == 0)
	{
		/**
		 * 태그를 안 달았거나 오타가 난 경우다. 이 로그가 없으면 "H 를 눌러도 아무 일이
		 * 없다"는 증상만 남고, 코드와 BP 중 어디를 봐야 하는지 알 수 없다.
		 */
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] 쓸 수 있는 헬멧이 없습니다. '%s' 태그가 붙은 스태틱 메시 컴포넌트에 Static Mesh 를 꽂았는지 확인하세요."),
			*GetNameSafe(OwnerActor), *HelmetComponentTag.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] 헬멧 %d개를 찾았습니다 (이름순)."),
		*GetNameSafe(OwnerActor), HelmetComponents.Num());
}

USkeletalMeshComponent* UCWHeadgearComponent::GetBodyMesh() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter != nullptr ? OwnerCharacter->GetMesh() : nullptr;
}

void UCWHeadgearComponent::ApplyHelmet()
{
	/**
	 * 켜야 할 하나만 보이게 하고 나머지는 숨긴다.
	 *
	 * 인덱스가 INDEX_NONE 이면 전부 숨는다. BP 에서 편집 중에는 모든 헬멧이 겹쳐 보이는데,
	 * 게임이 시작되면 이 함수가 정리하므로 에디터 뷰포트 상태는 신경 쓰지 않아도 된다.
	 */
	for (int32 Index = 0; Index < HelmetComponents.Num(); ++Index)
	{
		UStaticMeshComponent* HelmetComponent = HelmetComponents[Index];
		if (HelmetComponent == nullptr)
		{
			continue;
		}

		HelmetComponent->SetVisibility(Index == HelmetIndex);
	}
}

void UCWHeadgearComponent::Server_SetHelmet_Implementation(int32 NewHelmetIndex)
{
	SetHelmet(NewHelmetIndex);
}

void UCWHeadgearComponent::OnRep_HelmetIndex()
{
	ApplyHelmet();
}
