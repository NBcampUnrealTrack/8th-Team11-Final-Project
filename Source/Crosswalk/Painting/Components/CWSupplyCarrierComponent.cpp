// Copyright Crosswalk Team. All Rights Reserved.
// CWSupplyCarrierComponent.cpp

#include "Painting/Components/CWSupplyCarrierComponent.h"

#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Painting/CWPaintSupplyBox.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Zone/CWPaintDecalField.h"

#include "Painting/Components/CWToolHolderComponent.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/Tools/CWBaseTool.h"

namespace
{
	/** 반경 안에서 지정한 오브젝트 타입 액터를 모은다. 도구와 박스가 모두 WorldDynamic 이다. */
	void OverlapNearbyActors(const AActor& Owner, float Radius, TArray<FOverlapResult>& OutOverlaps)
	{
		const UWorld* World = Owner.GetWorld();
		if (World == nullptr)
		{
			return;
		}

		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(&Owner);

		World->OverlapMultiByObjectType(
			OutOverlaps,
			Owner.GetActorLocation(),
			FQuat::Identity,
			ObjectParams,
			FCollisionShape::MakeSphere(Radius),
			QueryParams);
	}
}

UCWSupplyCarrierComponent::UCWSupplyCarrierComponent()
{
	// 매 프레임 할 일이 없다. 대상 탐색은 키를 누른 순간에만 돈다.
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
}

void UCWSupplyCarrierComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWSupplyCarrierComponent, CarriedCount);
}

void UCWSupplyCarrierComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		ToolHolderComponent = Owner->FindComponentByClass<UCWToolHolderComponent>();
		WorkerComponent = Owner->FindComponentByClass<UCWWorkerComponent>();
	}
}

void UCWSupplyCarrierComponent::PickUpCan()
{
	if (IsSupplyInputBlocked())
	{
		NotifyActionRejected(ECWSupplyRejectReason::Busy);
		return;
	}

	// 도구를 든 채로는 통을 들 수 없다. (기획서 6-6)
	// 이 한 줄이 "자기가 든 도구는 자기가 충전할 수 없다" 까지 같이 만들어낸다.
	if (ToolHolderComponent != nullptr && ToolHolderComponent->GetHeldTool() != nullptr)
	{
		NotifyActionRejected(ECWSupplyRejectReason::ToolInHand);
		return;
	}

	if (CarriedCount >= MaxCarriedCount)
	{
		NotifyActionRejected(ECWSupplyRejectReason::CarryLimit);
		return;
	}

	// 어느 박스를 골랐는지는 이 기계만 안다. 서버가 자기 기준으로 다시 고르면
	// 박스가 두 개 붙어 있을 때 화면에서 본 것과 다른 쪽이 흔들린다.
	ACWPaintSupplyBox* NearestBox = FindNearestBox();
	if (NearestBox == nullptr)
	{
		NotifyActionRejected(ECWSupplyRejectReason::NoBoxNearby);
		return;
	}

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_PickUpCan(NearestBox);
		return;
	}

	PerformPickUp(NearestBox);
}

void UCWSupplyCarrierComponent::DropCan()
{
	if (IsSupplyInputBlocked())
	{
		NotifyActionRejected(ECWSupplyRejectReason::Busy);
		return;
	}

	if (CarriedCount <= 0)
	{
		NotifyActionRejected(ECWSupplyRejectReason::NoCans);
		return;
	}

	// 박스 근처에서만 반납할 수 있다. (기획서 6-2)
	// 이 제약이 "혼자 보급할 때는 1개만 들고 오는 게 유리하다" 를 만든다.
	if (FindNearestBox() == nullptr)
	{
		NotifyActionRejected(ECWSupplyRejectReason::NoBoxNearby);
		return;
	}

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_DropCan();
		return;
	}

	PerformDrop();
}

void UCWSupplyCarrierComponent::ChargeNearestTool()
{
	if (IsSupplyInputBlocked())
	{
		NotifyActionRejected(ECWSupplyRejectReason::Busy);
		return;
	}

	if (CarriedCount <= 0)
	{
		NotifyActionRejected(ECWSupplyRejectReason::NoCans);
		return;
	}

	ACWBaseTool* TargetTool = FindNearestChargeableTool();
	if (TargetTool == nullptr)
	{
		NotifyActionRejected(ECWSupplyRejectReason::NoTargetTool);
		return;
	}

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_ChargeTool(TargetTool);
		return;
	}

	PerformCharge(TargetTool);
}

void UCWSupplyCarrierComponent::Server_PickUpCan_Implementation(ACWPaintSupplyBox* RequestedBox)
{
	PerformPickUp(RequestedBox);
}

void UCWSupplyCarrierComponent::Server_DropCan_Implementation()
{
	PerformDrop();
}

void UCWSupplyCarrierComponent::Server_ChargeTool_Implementation(ACWBaseTool* RequestedTool)
{
	PerformCharge(RequestedTool);
}

void UCWSupplyCarrierComponent::PerformPickUp(ACWPaintSupplyBox* RequestedBox)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || RequestedBox == nullptr)
	{
		return;
	}

	// 클라이언트가 보낸 값을 그대로 믿지 않는다. 서버 기준으로 전부 다시 본다.
	if (IsSupplyInputBlocked() || CarriedCount >= MaxCarriedCount)
	{
		return;
	}

	if (ToolHolderComponent != nullptr && ToolHolderComponent->GetHeldTool() != nullptr)
	{
		return;
	}

	// 핑 때문에 위치가 어긋나므로 여유를 두고 잰다. (도구 집기와 같은 이유, 같은 값)
	const float AllowedRadius = RequestedBox->GetInteractRadius() * ServerRangeTolerance;
	if (FVector::DistSquared(Owner->GetActorLocation(), RequestedBox->GetActorLocation()) > FMath::Square(AllowedRadius))
	{
		return;
	}

	SetCarriedCount(CarriedCount + 1);

	RequestedBox->NotifyCanDispensed();
}

void UCWSupplyCarrierComponent::PerformDrop()
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || CarriedCount <= 0)
	{
		return;
	}

	if (IsSupplyInputBlocked())
	{
		return;
	}

	// 박스 재고는 무한이라 반납한 통을 어디에 넣을지는 신경 쓰지 않는다. (기획서 8절)
	// 서버에서도 박스가 근처인지는 다시 본다.
	if (FindNearestBox(ServerRangeTolerance) == nullptr)
	{
		return;
	}

	SetCarriedCount(CarriedCount - 1);
}

void UCWSupplyCarrierComponent::PerformCharge(ACWBaseTool* RequestedTool)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority() || CarriedCount <= 0)
	{
		return;
	}

	if (IsSupplyInputBlocked() || !CanChargeTool(RequestedTool))
	{
		return;
	}

	const float AllowedRadius = ChargeRadius * ServerRangeTolerance;
	if (FVector::DistSquared(Owner->GetActorLocation(), RequestedTool->GetActorLocation()) > FMath::Square(AllowedRadius))
	{
		return;
	}

	// 통은 부은 자리에서 사라진다. (기획서 6-3 처리 2항)
	// 마지막 통을 쓰면 손이 비어 곧바로 도구를 집을 수 있다.
	SetCarriedCount(CarriedCount - 1);

	RequestedTool->AddPaint(ChargeAmountPerCan);
}

ACWPaintSupplyBox* UCWSupplyCarrierComponent::FindNearestBox(float RadiusScale) const
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	// 박스마다 반경이 다를 수 있어서, 넉넉히 훑은 뒤 각 박스의 반경으로 다시 거른다.
	//
	// 훑는 반경을 ChargeRadius 로 잡으면 안 된다. 그건 도구 충전 사거리라 박스와 무관하고,
	// 레벨에서 박스의 InteractRadius 를 더 크게 잡는 순간 그 박스는 아예 안 잡힌다.
	// 실제 판정은 아래 루프가 박스 자신의 반경으로 하므로, 여기서는 넉넉하기만 하면 된다.
	// (키를 누른 순간에만 도는 쿼리라 반경을 크게 잡아도 비용이 문제되지 않는다)
	constexpr float BoxSearchRadius = 600.f;

	TArray<FOverlapResult> Overlaps;
	OverlapNearbyActors(*Owner, BoxSearchRadius * FMath::Max(RadiusScale, 1.f), Overlaps);

	ACWPaintSupplyBox* NearestBox = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACWPaintSupplyBox* Box = Cast<ACWPaintSupplyBox>(Overlap.GetActor());
		if (Box == nullptr)
		{
			continue;
		}

		const float AllowedRadius = Box->GetInteractRadius() * RadiusScale;
		const float DistanceSquared = FVector::DistSquared(Owner->GetActorLocation(), Box->GetActorLocation());
		if (DistanceSquared > FMath::Square(AllowedRadius) || DistanceSquared >= NearestDistanceSquared)
		{
			continue;
		}

		NearestDistanceSquared = DistanceSquared;
		NearestBox = Box;
	}

	return NearestBox;
}

ACWBaseTool* UCWSupplyCarrierComponent::FindNearestChargeableTool(float RadiusScale) const
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	TArray<FOverlapResult> Overlaps;
	OverlapNearbyActors(*Owner, ChargeRadius * RadiusScale, Overlaps);

	ACWBaseTool* NearestTool = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACWBaseTool* Tool = Cast<ACWBaseTool>(Overlap.GetActor());
		if (!CanChargeTool(Tool))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Owner->GetActorLocation(), Tool->GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			NearestTool = Tool;
		}
	}

	return NearestTool;
}

bool UCWSupplyCarrierComponent::CanChargeTool(const ACWBaseTool* Tool) const
{
	// 빗자루는 잔량 개념이 없어 대상에서 빠진다. (기획서 6-3 예외 처리)
	if (Tool == nullptr || !Tool->UsesPaint())
	{
		return false;
	}

	// 트렁크에 보관 중인 도구는 충전 대상이 아니다. 콜리전이 살아 있어 차량 옆에서는
	// 오버랩에 잡힌다. 보급은 작업 중인 사람에게 하는 것이고, 안 보이는 도구가 조용히
	// 통을 먹으면 왜 통이 사라졌는지 알 수가 없다.
	if (Tool->IsStored())
	{
		return false;
	}

	// 내가 든 도구는 대상이 아니다. 통을 들고 있으면 도구를 들 수 없어 실제로는 생기지 않는 경우지만,
	// 규칙이 바뀌었을 때 조용히 자기 충전이 열리는 것을 막아 둔다. (기획서 6-6)
	return Tool->GetHolder() != Cast<APawn>(GetOwner());
}

bool UCWSupplyCarrierComponent::IsSupplyInputBlocked() const
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return true;
	}

	// 다운/기상 중에는 아무 보급 입력도 받지 않는다. (기획서 6-7)
	if (const UCWVehicleHitReactionComponent* HitReaction = Owner->FindComponentByClass<UCWVehicleHitReactionComponent>())
	{
		if (HitReaction->IsHitLocked())
		{
			return true;
		}
	}

	// 미니게임처럼 입력을 붙잡는 작업 중에도 받지 않는다. (기획서 6-7)
	// 패링을 막는 조건과 같다. 둘 다 "지금은 이 작업이 화면과 입력을 쥐고 있다" 는 뜻이다.
	return WorkerComponent != nullptr && WorkerComponent->IsParryBlocked();
}

void UCWSupplyCarrierComponent::BreakCansAndSpill(const FVector& SpillLocation)
{
	const int32 BrokenCount = BreakAllCans();
	if (BrokenCount <= 0)
	{
		return;
	}

	UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return;
	}

	// 자기 캡슐을 빼고 지면을 찾는다. 넘어온 좌표가 캡슐 중심이라 그냥 쏘면 자기 몸을 맞는다.
	// (작업 존을 빼는 것은 서브시스템이 알아서 한다)
	const FVector GroundLocation = PaintingSubsystem->ProjectToGround(SpillLocation, GetOwner());

	// 오염 레이어의 칸을 채우지 않는다. 터진 통은 자국(데칼)을 남긴다.
	//
	// 레이어를 채우면 사고 한 번이 붓질 수십 번을 요구한다. 오염 반경이 붓 반경의 몇 배라서다.
	// 게다가 레이어는 스트라이프 자리의 칸을 빼두기 때문에 도색 구역 위 사고는 흔적도 안 남는다.
	// 자국으로 두면 도색 구역 위에도 남고, 지우는 비용을 자국 개수로 직접 정할 수 있다.
	if (ACWPaintDecalField* DecalField = FindDecalField())
	{
		DecalField->AddBurst(GroundLocation, BrokenCount, GetOwner());
	}

	// 파티클처럼 한 번 터지고 마는 연출은 따로 뿌린다. 자국은 위에서 이미 전파됐다.
	Multicast_PlayCanBurst(GroundLocation, BrokenCount);
}

void UCWSupplyCarrierComponent::Multicast_PlayCanBurst_Implementation(FVector_NetQuantize BurstLocation, int32 BrokenCount)
{
	OnCansBurst.Broadcast(this, BurstLocation, BrokenCount);
}

ACWPaintDecalField* UCWSupplyCarrierComponent::FindDecalField() const
{
	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return nullptr;
	}

	// 스테이지에 하나만 배치하는 존이라 먼저 찾은 것을 그대로 쓴다.
	// 배치하지 않았으면 자국이 남지 않는다. 통 손실과 다운만으로 사고가 끝난다.
	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		if (ACWPaintDecalField* DecalField = Cast<ACWPaintDecalField>(WeakZone.Get()))
		{
			return DecalField;
		}
	}

	return nullptr;
}

int32 UCWSupplyCarrierComponent::BreakAllCans()
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || CarriedCount <= 0)
	{
		return 0;
	}

	const int32 BrokenCount = CarriedCount;

	SetCarriedCount(0);

	return BrokenCount;
}

void UCWSupplyCarrierComponent::NotifyActionRejected(ECWSupplyRejectReason Reason)
{
	// 입력을 넣은 본인에게만 보여주면 된다. 남의 화면에 띄울 이유가 없다.
	if (!IsLocallyControlled())
	{
		return;
	}

	// UI 가 붙기 전까지는 이 로그가 유일한 확인 수단이다.
	// 거절은 정상 동작이라 Verbose 로 둔다. 확인하려면 `Log LogTemp Verbose` 로 켠다.
	UE_LOG(LogTemp, Verbose, TEXT("[Supply] 입력이 무시되었습니다. 이유=%s"),
		*StaticEnum<ECWSupplyRejectReason>()->GetDisplayNameTextByValue(static_cast<int64>(Reason)).ToString());

	OnSupplyActionRejected.Broadcast(this, Reason);
}

void UCWSupplyCarrierComponent::SetCarriedCount(int32 NewCount)
{
	// 개수는 서버만 정한다. 클라이언트에서 불러도 조용히 무시한다.
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 Clamped = FMath::Clamp(NewCount, 0, MaxCarriedCount);
	if (Clamped == CarriedCount)
	{
		return;
	}

	CarriedCount = Clamped;

	HandleCarriedCountChanged();
}

void UCWSupplyCarrierComponent::OnRep_CarriedCount()
{
	HandleCarriedCountChanged();
}

void UCWSupplyCarrierComponent::HandleCarriedCountChanged()
{
	if (LastBroadcastCount == CarriedCount)
	{
		return;
	}

	LastBroadcastCount = CarriedCount;

	if (bShowDebugCarriedCount && IsLocallyControlled() && GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(reinterpret_cast<UPTRINT>(this)),
			3.f,
			FColor::Yellow,
			FString::Printf(TEXT("Cans %d / %d"), CarriedCount, MaxCarriedCount));
	}

	OnCarriedCanCountChanged.Broadcast(this, CarriedCount);
}

bool UCWSupplyCarrierComponent::IsLocallyControlled() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	return OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();
}
