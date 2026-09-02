#include "Painting/Tools/CWBaseTool.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CWDecalStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Net/UnrealNetwork.h"

ACWBaseTool::ACWBaseTool()
{
	PrimaryActorTick.bCanEverTick = false;

	// 손에 붙는 것과 내려놓은 위치를 모두 전파해야 한다.
	// 어태치먼트는 무브먼트 리플리케이션에 얹혀 가므로 둘 다 켠다.
	bReplicates = true;
	SetReplicateMovement(true);

	ToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolMesh"));
	SetRootComponent(ToolMesh);

	// 홀더 컴포넌트가 WorldDynamic 오브젝트 타입 오버랩으로 도구를 찾는다.
	// 새 트레이스 채널을 프로젝트 세팅에 추가하지 않아도 되고, 레벨 전체를 훑지도 않는다.
	//
	// 모든 채널 응답을 Overlap 으로 두는 이유:
	// 커서 트레이스(ECC_Visibility)는 블로킹 히트만 받으므로 도구를 그냥 통과한다.
	// 즉 도구 위에 커서가 겹쳐도 아래 작업 존이 계속 찍혀서 작업이 끊기지 않는다.
	ToolMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ToolMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ToolMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	ToolMesh->SetGenerateOverlapEvents(false);
}

void ACWBaseTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWBaseTool, Holder);
	DOREPLIFETIME(ACWBaseTool, PaintAmount);
}

void ACWBaseTool::BeginPlay()
{
	Super::BeginPlay();

	// 도구는 바닥 데칼을 받지 않는다. 데칼 위에 내려놨거나 그 위에서 들고 있어도 무늬가 옮지 않는다.
	UCWDecalStatics::DisableDecalReceiveOnActor(this);

	// 스테이지 시작 시 모든 도구는 가득 찬 상태다. (기획서 8절)
	// 서버에서만 채우고 클라이언트는 리플리케이션으로 받는다.
	if (HasAuthority() && bUsesPaint)
	{
		SetPaintAmount(MaxPaintAmount);
	}

	// 시작값이 0 인 도구(빗자루)도 게이지 초기 상태를 한 번은 알려야 한다.
	HandlePaintAmountChanged();
}

void ACWBaseTool::Interact_Implementation(APawn* InstigatorPawn)
{
	if (InstigatorPawn == nullptr)
	{
		return;
	}

	// 집기 판정과 1인 1개 제한은 홀더가 갖고 있다. 여기서 직접 TryPickUp 을 부르면 상태가 갈라진다.
	if (UCWToolHolderComponent* HolderComponent = InstigatorPawn->FindComponentByClass<UCWToolHolderComponent>())
	{
		HolderComponent->PickUpTool(this);
	}
}

bool ACWBaseTool::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	return !IsHeld();
}

bool ACWBaseTool::TryPickUp(APawn* NewHolder)
{
	// 경합 판정은 서버에서만 한다. 두 명이 같은 프레임에 집으려 해도 먼저 도착한 쪽만 성공한다.
	if (!HasAuthority())
	{
		return false;
	}

	// 1인 1개 제한은 홀더가 본다. 여기서는 "한 도구를 두 명이 동시에 집는" 경우만 막는다.
	if (NewHolder == nullptr || IsHeld())
	{
		return false;
	}

	Holder = NewHolder;

	// 스켈레탈 메쉬가 있으면 손 소켓에, 없으면(박스 더미 캐릭터 등) 루트에 붙인다.
	USceneComponent* AttachParent = NewHolder->GetRootComponent();
	if (const ACharacter* HolderCharacter = Cast<ACharacter>(NewHolder))
	{
		if (USkeletalMeshComponent* HolderMesh = HolderCharacter->GetMesh())
		{
			AttachParent = HolderMesh;
		}
	}

	AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachSocketName);

	// 스케일은 도구가 원래 갖고 있던 값을 유지해야 하므로 위치/회전만 보정한다.
	SetActorRelativeLocation(AttachRelativeLocation);
	SetActorRelativeRotation(AttachRelativeRotation);

	HandleHeldStateChanged();

	return true;
}

void ACWBaseTool::Drop(const FVector& DropLocation)
{
	if (!HasAuthority() || !IsHeld())
	{
		return;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Holder = nullptr;

	// 손에서 기울어져 있던 각도를 그대로 두면 바닥에 박혀 보인다. Yaw 만 남긴다.
	SetActorLocation(DropLocation + FVector(0.f, 0.f, DropHeightOffset));
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));

	HandleHeldStateChanged();
}

float ACWBaseTool::ConsumePaint(float Amount)
{
	// 페인트를 쓰지 않는 도구(빗자루)는 아무리 작업해도 줄어들 것이 없다.
	if (!bUsesPaint || Amount <= 0.f)
	{
		return 0.f;
	}

	// 권위를 보지 않는다. 부른 기계에서 바로 깎는다. (헤더 주석 참고)
	// 잔량보다 많이 요청해도 남아 있던 만큼만 깎이고, 그 값이 반환된다.
	return -SetPaintAmount(PaintAmount - Amount);
}

float ACWBaseTool::AddPaint(float Amount)
{
	// 충전은 통을 소비하는 1회성 행동이라 서버만 확정한다.
	if (!HasAuthority() || !bUsesPaint || Amount <= 0.f)
	{
		return 0.f;
	}

	return SetPaintAmount(PaintAmount + Amount);
}

float ACWBaseTool::GetPaintRatio() const
{
	if (!bUsesPaint || MaxPaintAmount <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(PaintAmount / MaxPaintAmount, 0.f, 1.f);
}

float ACWBaseTool::SetPaintAmount(float NewAmount)
{
	const float Clamped = FMath::Clamp(NewAmount, 0.f, MaxPaintAmount);
	const float Delta = Clamped - PaintAmount;

	if (FMath::IsNearlyZero(Delta))
	{
		return 0.f;
	}

	PaintAmount = Clamped;

	// 서버는 여기서 바로 알린다. 클라이언트는 OnRep_PaintAmount 로도 들어오는데,
	// 로컬 예측으로 먼저 깎은 값과 같으면 HandlePaintAmountChanged 가 알아서 걸러낸다.
	HandlePaintAmountChanged();

	return Delta;
}

void ACWBaseTool::OnRep_PaintAmount()
{
	HandlePaintAmountChanged();
}

void ACWBaseTool::HandlePaintAmountChanged()
{
	if (FMath::IsNearlyEqual(PaintAmount, LastBroadcastPaintAmount))
	{
		return;
	}

	LastBroadcastPaintAmount = PaintAmount;

	const float Ratio = GetPaintRatio();

	OnPaintAmountChanged(PaintAmount, Ratio);
	OnPaintChanged.Broadcast(this, PaintAmount, Ratio);
}

void ACWBaseTool::OnRep_Holder()
{
	// 손에 붙거나 떨어지는 것 자체는 어태치먼트 리플리케이션이 이미 처리했다.
	// 여기서는 전파되지 않는 나머지를 맞춘다.
	HandleHeldStateChanged();
}

void ACWBaseTool::HandleHeldStateChanged()
{
	const bool bNowHeld = IsHeld();

	// 들고 있어도 콜리전을 끄지 않는다.
	//
	// 예전에는 껐다. 다른 플레이어의 집기 탐색에서 빼려는 목적이었는데, 그 필터는
	// UCWToolHolderComponent::FindNearestPickableTool 이 IsHeld 로 이미 하고 있어서 필요가 없었다.
	//
	// 지금은 켜 두어야만 한다. 페인트 충전 대상에는 "남이 들고 있는 도구" 가 포함되는데
	// (기획서 6-3) 콜리전이 꺼져 있으면 반경 오버랩에 아예 안 잡혀서, 작업자가 도구를 든 채
	// 보급받는 협동 경로가 통째로 막힌다.
	//
	// 켜 두어도 해가 없는 이유: 모든 채널 응답이 Overlap 이라 무엇도 막지 않는다.
	// 커서 트레이스(블로킹 히트만 받음)는 그대로 통과하고, 캐릭터 이동에도 걸리지 않는다.
	if (bNowHeld)
	{
		SetFocused(false);
		OnPickedUp();
	}
	else
	{
		OnDropped();
	}
}

void ACWBaseTool::SetFocused(bool bNewFocused)
{
	if (bFocused == bNewFocused)
	{
		return;
	}

	bFocused = bNewFocused;

	OnFocusChanged(bFocused);
}

void ACWBaseTool::OnPickedUp_Implementation()
{
	// BP 자식에서 연출을 넣는 자리.
}

void ACWBaseTool::OnDropped_Implementation()
{
	// BP 자식에서 연출을 넣는 자리.
}

void ACWBaseTool::OnFocusChanged_Implementation(bool bNewFocused)
{
	// 외곽선 포스트프로세스가 아직 없어도 해가 없고, 생기면 바로 하이라이트가 된다.
	ToolMesh->SetRenderCustomDepth(bNewFocused);
}

void ACWBaseTool::OnPaintAmountChanged_Implementation(float NewAmount, float NewRatio)
{
	// BP 자식에서 도구 위 게이지를 갱신하는 자리.
}
