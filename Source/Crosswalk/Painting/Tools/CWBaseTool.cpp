#include "Painting/Tools/CWBaseTool.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Painting/Tools/CWToolHolderComponent.h"
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

void ACWBaseTool::OnRep_Holder()
{
	// 손에 붙거나 떨어지는 것 자체는 어태치먼트 리플리케이션이 이미 처리했다.
	// 여기서는 전파되지 않는 나머지를 맞춘다.
	HandleHeldStateChanged();
}

void ACWBaseTool::HandleHeldStateChanged()
{
	const bool bNowHeld = IsHeld();

	// 들고 있는 동안은 다른 플레이어의 탐색 결과에서 빠진다.
	ToolMesh->SetCollisionEnabled(bNowHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);

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
