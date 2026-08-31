#include "Painting/Components/CWToolHolderComponent.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Net/UnrealNetwork.h"
#include "Painting/Components/CWSupplyCarrierComponent.h"
#include "Painting/Components/CWWorkerComponent.h"

UCWToolHolderComponent::UCWToolHolderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicatedByDefault(true);
}

void UCWToolHolderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWToolHolderComponent, HeldTool);
}

void UCWToolHolderComponent::BeginPlay()
{
	Super::BeginPlay();

	WorkerComponent = GetOwner()->FindComponentByClass<UCWWorkerComponent>();
	SupplyCarrierComponent = GetOwner()->FindComponentByClass<UCWSupplyCarrierComponent>();
}

void UCWToolHolderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 캐릭터가 사라질 때 도구가 같이 묻히면 그 단계를 아무도 끝낼 수 없게 된다.
	// 도구 상태는 서버만 바꿀 수 있으므로 서버에서만 처리한다.
	if (EndPlayReason == EEndPlayReason::Destroyed && GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		DropHeldTool();
	}

	SetFocusedTool(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UCWToolHolderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 포커스는 화면에 프롬프트를 띄우기 위한 것이라 조작 중인 본인만 계산하면 된다.
	// 다른 클라이언트의 캐릭터까지 매 프레임 오버랩을 돌릴 이유가 없다.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	// 이미 들고 있으면 E 는 "내려놓기" 라서 집을 후보를 찾을 이유가 없다.
	SetFocusedTool(HeldTool != nullptr ? nullptr : FindNearestPickableTool());
}

void UCWToolHolderComponent::Interact()
{
	// 통을 들고 있으면 여기서 먼저 자른다. 서버도 PickUpTool 에서 다시 보지만,
	// 데디케이티드 서버에서는 거절 피드백이 이 클라이언트까지 오지 않는다.
	// (NotifyActionRejected 는 조종 중인 본인 기계에서만 발행된다)
	// 그러면 "양손이 찼다" 가 영영 안 뜨고 입력이 조용히 씹힌 것처럼 보인다.
	if (HeldTool == nullptr && IsBlockedByCarriedCans())
	{
		SupplyCarrierComponent->NotifyActionRejected(ECWSupplyRejectReason::CansInHand);
		return;
	}

	// 클라이언트는 판정하지 않고 요청만 보낸다.
	// FocusedTool 을 같이 보내는 이유: 서버는 이 클라이언트의 화면에서 무엇이 선택돼 있었는지 모른다.
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_Interact(FocusedTool);
		return;
	}

	PerformInteract(FocusedTool);
}

void UCWToolHolderComponent::Server_Interact_Implementation(ACWBaseTool* RequestedTool)
{
	PerformInteract(RequestedTool);
}

void UCWToolHolderComponent::PerformInteract(ACWBaseTool* RequestedTool)
{
	if (HeldTool != nullptr)
	{
		DropHeldTool();
		return;
	}

	PickUpTool(RequestedTool);
}

bool UCWToolHolderComponent::IsBlockedByCarriedCans() const
{
	return SupplyCarrierComponent != nullptr && SupplyCarrierComponent->IsCarrying();
}

bool UCWToolHolderComponent::PickUpTool(ACWBaseTool* Tool)
{
	// 1인 1개. 들고 있는 게 있으면 먼저 내려놓아야 한다.
	if (Tool == nullptr || HeldTool != nullptr)
	{
		return false;
	}

	// 페인트 통을 들고 있으면 도구를 집을 수 없다. (기획서 6-6)
	// 통은 박스 근처에서만 내려놓을 수 있으므로, 여기서 막히면 박스까지 돌아가야 한다.
	// 그 마찰이 "혼자 보급할 때는 통을 1개만 들고 오는 게 유리하다" 를 만든다.
	if (IsBlockedByCarriedCans())
	{
		SupplyCarrierComponent->NotifyActionRejected(ECWSupplyRejectReason::CansInHand);
		return false;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr || !OwnerPawn->HasAuthority())
	{
		return false;
	}

	// 클라이언트가 보낸 도구를 그대로 믿지 않는다. 서버 기준으로 거리를 다시 잰다.
	// (핑 때문에 위치가 어긋나므로 ServerRangeTolerance 만큼 여유를 둔다)
	const float AllowedRadius = PickupRadius * ServerRangeTolerance;
	if (FVector::DistSquared(OwnerPawn->GetActorLocation(), Tool->GetActorLocation()) > FMath::Square(AllowedRadius))
	{
		return false;
	}

	if (!Tool->TryPickUp(OwnerPawn))
	{
		return false;
	}

	HeldTool = Tool;
	SetFocusedTool(nullptr);
	NotifyWorkerComponent();

	OnHeldToolChanged.Broadcast(this, HeldTool);

	return true;
}

void UCWToolHolderComponent::DropHeldTool()
{
	if (HeldTool == nullptr || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 내려놓을 위치는 아직 HeldTool 이 살아 있는 동안 계산해야 한다. (트레이스에서 제외해야 하므로)
	DropHeldToolAt(CalculateDropLocation());
}

void UCWToolHolderComponent::DropHeldToolAt(const FVector& DropLocation)
{
	if (HeldTool == nullptr || GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return;
	}

	ACWBaseTool* DroppedTool = HeldTool;
	HeldTool = nullptr;

	DroppedTool->Drop(DropLocation);
	NotifyWorkerComponent();

	OnHeldToolChanged.Broadcast(this, nullptr);
}

void UCWToolHolderComponent::OnRep_HeldTool()
{
	// 소지 도구가 바뀐 걸 클라이언트에도 반영한다.
	// 작업 컴포넌트의 HeldTool 은 따로 리플리케이트되지만, HUD 델리게이트는 여기서 쏴야 한다.
	NotifyWorkerComponent();

	OnHeldToolChanged.Broadcast(this, HeldTool);
}

ECWToolType UCWToolHolderComponent::GetHeldToolType() const
{
	return HeldTool != nullptr ? HeldTool->GetToolType() : ECWToolType::None;
}

ACWBaseTool* UCWToolHolderComponent::FindNearestPickableTool() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr)
	{
		return nullptr;
	}

	// 도구 메쉬는 WorldDynamic 오브젝트 타입이다. 반경 오버랩으로 찾으면
	// 레벨 액터를 전부 훑지 않아도 되고 전용 트레이스 채널도 필요 없다.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Owner->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(PickupRadius),
		QueryParams);

	ACWBaseTool* NearestTool = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACWBaseTool* Tool = Cast<ACWBaseTool>(Overlap.GetActor());
		if (Tool == nullptr || Tool->IsHeld())
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

FVector UCWToolHolderComponent::CalculateDropLocation() const
{
	// DropHeldTool 에서만 부르고, 그때는 Owner 와 HeldTool 이 모두 유효하다.
	const AActor* Owner = GetOwner();
	const FVector AheadLocation = Owner->GetActorLocation() + Owner->GetActorForwardVector() * DropForwardDistance;

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return AheadLocation;
	}

	// 캐릭터 키 정도만 위아래로 훑으면 인도든 도로든 잡힌다.
	constexpr float GroundTraceHalfHeight = 200.f;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.AddIgnoredActor(HeldTool);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(
		Hit,
		AheadLocation + FVector(0.f, 0.f, GroundTraceHalfHeight),
		AheadLocation - FVector(0.f, 0.f, GroundTraceHalfHeight),
		ECC_Visibility,
		QueryParams))
	{
		return Hit.ImpactPoint;
	}

	// 앞이 허공이면(난간 밖 등) 그냥 지금 높이에 둔다.
	return AheadLocation;
}

void UCWToolHolderComponent::SetFocusedTool(ACWBaseTool* NewTool)
{
	if (FocusedTool == NewTool)
	{
		return;
	}

	// 하이라이트는 도구 자신이 켜고 끈다. 그래서 두 플레이어가 같은 도구를 번갈아
	// 바라보면 나중에 떠난 쪽 기준으로 꺼진다. 프로토타입 검증에는 문제되지 않는다.
	if (FocusedTool != nullptr)
	{
		FocusedTool->SetFocused(false);
	}

	FocusedTool = NewTool;

	if (FocusedTool != nullptr)
	{
		FocusedTool->SetFocused(true);
	}

	OnFocusedToolChanged.Broadcast(this, FocusedTool);
}

void UCWToolHolderComponent::NotifyWorkerComponent()
{
	if (WorkerComponent != nullptr)
	{
		// 종류만이 아니라 액터도 함께 넘긴다. 페인트 잔량은 도구가 들고 있어서
		// 실물 포인터가 없으면 물어볼 수가 없다. (기획서 5-1)
		WorkerComponent->SetHeldTool(GetHeldToolType(), HeldTool);
	}
}
