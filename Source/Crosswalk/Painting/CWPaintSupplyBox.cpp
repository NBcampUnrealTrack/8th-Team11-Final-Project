// Copyright Crosswalk Team. All Rights Reserved.
// CWPaintSupplyBox.cpp

#include "Painting/CWPaintSupplyBox.h"

#include "Components/StaticMeshComponent.h"

ACWPaintSupplyBox::ACWPaintSupplyBox()
{
	PrimaryActorTick.bCanEverTick = false;

	// 리플리케이트할 상태는 없지만 멀티캐스트 RPC 를 받으려면 켜야 한다.
	bReplicates = true;

	BoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoxMesh"));
	SetRootComponent(BoxMesh);

	// 바닥 데칼(싱크홀 균열, 아스팔트 끈끈이, 페인트 자국)이 통에 씌워지지 않게 한다.
	BoxMesh->bReceivesDecals = false;

	// 거리 판정은 UCWSupplyCarrierComponent 가 오버랩으로 한다. 도구와 같은 방식이다.
	// 커서 트레이스에 걸리지 않게 모든 채널을 Overlap 으로 둔다. 블로킹이면 박스 위에
	// 커서를 올렸을 때 아래 작업 존이 안 찍혀 작업이 끊긴다.
	BoxMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BoxMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	BoxMesh->SetGenerateOverlapEvents(false);
}

void ACWPaintSupplyBox::NotifyCanDispensed()
{
	if (!HasAuthority())
	{
		return;
	}

	Multicast_PlayDispense();
}

void ACWPaintSupplyBox::Multicast_PlayDispense_Implementation()
{
	OnCanDispensed(ShakeDuration);
}
