#include "Painting/CWWorkTruck.h"

#include "Core/CWDecalStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "UObject/ConstructorHelpers.h"

ACWWorkTruck::ACWWorkTruck()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;

	TruckMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TruckMesh"));
	SetRootComponent(TruckMesh);

	// 플레이스홀더. 전용 차량 메쉬가 나오기 전에도 위치와 크기가 눈에 보이게 한다.
	// Cube 는 100x100x100 이라 배율이 그대로 cm 크기가 된다. 대략 트럭 한 대 크기로 잡는다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		TruckMesh->SetStaticMesh(CubeMesh.Object);
		TruckMesh->SetRelativeScale3D(FVector(5.f, 2.f, 2.f));
	}

	TrunkPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TrunkPoint"));
	TrunkPoint->SetupAttachment(TruckMesh);

	// 차량 뒤쪽(-X) 끝 바깥에 둔다. 스케일이 걸린 메쉬의 자식이라 상대 좌표도 같이 늘어나므로
	// 스케일 기준으로 계산한 값을 넣는다. 레벨에서 차량 메쉬를 갈면 여기도 다시 잡아야 한다.
	TrunkPoint->SetRelativeLocation(FVector(-70.f, 0.f, -25.f));
}

void ACWWorkTruck::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWWorkTruck, StoredTools);
}

void ACWWorkTruck::BeginPlay()
{
	Super::BeginPlay();

	// 외형은 모든 머신에서 맞춰야 하므로 권한 검사보다 먼저 끈다.
	// 바닥 데칼이 차체에 씌워지는 것을 막는다.
	UCWDecalStatics::DisableDecalReceiveOnActor(this);

	// 도구를 트렁크에 넣는 것은 서버만 한다. 클라이언트는 리플리케이트된 목록을 받는다.
	if (!HasAuthority())
	{
		return;
	}

	CollectStageTools();
}

void ACWWorkTruck::CollectStageTools()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 레벨에 이미 놓여 있는 도구를 걷어 온다.
	//
	// 여기만 TActorIterator 를 쓴다. 레벨 액터 전체를 훑는 비용이 있지만 BeginPlay 에서
	// 한 번만 도는 코드다. 매 프레임 도는 자리라면 UCWPaintingSubsystem 처럼 등록 목록이 필요하다.
	if (bCollectExistingTools)
	{
		for (TActorIterator<ACWBaseTool> ToolIter(World); ToolIter; ++ToolIter)
		{
			StoreTool(*ToolIter);
		}
	}

	// 꺼내는 순서를 도구 종류 순으로 고정한다. 액터 순회 순서는 보장되지 않아서
	// 그대로 두면 판마다 처음 꺼내지는 도구가 달라진다.
	StoredTools.Sort([](const ACWBaseTool& Lhs, const ACWBaseTool& Rhs)
	{
		return Lhs.GetToolType() < Rhs.GetToolType();
	});
}

void ACWWorkTruck::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || InstigatorPawn == nullptr)
	{
		return;
	}

	UCWToolHolderComponent* HolderComponent = InstigatorPawn->FindComponentByClass<UCWToolHolderComponent>();
	if (HolderComponent == nullptr)
	{
		return;
	}

	// 들고 있으면 반납이 먼저다. 1인 1개라 어차피 새로 꺼낼 수도 없다.
	if (ACWBaseTool* HeldTool = HolderComponent->GetHeldTool())
	{
		// 홀더를 거쳐서 내려놓아야 한다. 도구만 직접 건드리면 홀더의 HeldTool 이 남아
		// "손에는 없는데 들고 있는 것으로 아는" 상태가 된다.
		HolderComponent->DropHeldTool();
		StoreTool(HeldTool);
		return;
	}

	TakeOutTool(InstigatorPawn);
}

bool ACWWorkTruck::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	if (InstigatorPawn == nullptr)
	{
		return false;
	}

	const UCWToolHolderComponent* HolderComponent = InstigatorPawn->FindComponentByClass<UCWToolHolderComponent>();
	if (HolderComponent == nullptr)
	{
		return false;
	}

	// 거리는 트렁크 문 기준으로 잰다. 차가 길어서 액터 원점으로 재면 트렁크 앞에 서 있는데도 멀다고 나온다.
	// 서버가 다시 판정할 때를 대비해 여유를 준다. 클라이언트와 서버의 위치가 핑만큼 어긋나 있다.
	const float AllowedRadius = InteractRadius * ServerRangeTolerance;
	if (FVector::DistSquared(InstigatorPawn->GetActorLocation(), GetTrunkLocation()) > FMath::Square(AllowedRadius))
	{
		return false;
	}

	// 반납할 게 있거나 꺼낼 게 남아 있어야 한다.
	// 맨손인데 트렁크도 비었으면 상호작용 대상에서 빠져서, 바닥의 도구가 대신 잡힌다.
	return HolderComponent->GetHeldTool() != nullptr || StoredTools.Num() > 0;
}

FVector ACWWorkTruck::GetInteractionLocation_Implementation() const
{
	return GetTrunkLocation();
}

bool ACWWorkTruck::StoreTool(ACWBaseTool* Tool)
{
	if (!HasAuthority() || Tool == nullptr || StoredTools.Contains(Tool))
	{
		return false;
	}

	// 누가 들고 있는 도구는 넣지 않는다. 홀더에서 먼저 내려놓아야 상태가 갈라지지 않는다.
	if (Tool->IsHeld())
	{
		return false;
	}

	Tool->SetActorLocation(GetTrunkLocation());

	// 보관 상태는 도구가 복제 변수로 들고 있고, 숨김과 집기 차단도 도구가 알아서 한다.
	// (ACWBaseTool 의 bStored 주석 참고)
	Tool->SetStored(true);

	StoredTools.Add(Tool);

	return true;
}

bool ACWWorkTruck::TakeOutTool(APawn* InstigatorPawn)
{
	if (!HasAuthority() || StoredTools.Num() == 0)
	{
		return false;
	}

	return GiveToolToPawn(StoredTools[0], InstigatorPawn);
}

bool ACWWorkTruck::GiveToolToPawn(ACWBaseTool* Tool, APawn* InstigatorPawn)
{
	if (Tool == nullptr || InstigatorPawn == nullptr)
	{
		return false;
	}

	UCWToolHolderComponent* HolderComponent = InstigatorPawn->FindComponentByClass<UCWToolHolderComponent>();
	if (HolderComponent == nullptr)
	{
		return false;
	}

	// 트렁크 앞에 꺼내 놓고 홀더에 넘긴다. 홀더가 폰과의 거리를 다시 재기 때문에
	// 숨어 있던 자리가 아니라 실제로 꺼내진 자리에 있어야 승인된다.
	Tool->SetActorLocation(GetTrunkLocation());
	Tool->SetStored(false);

	if (!HolderComponent->PickUpTool(Tool))
	{
		// 이미 다른 도구를 들고 있거나 너무 멀다. 꺼내다 만 상태로 두지 않고 되돌린다.
		Tool->SetStored(true);
		return false;
	}

	StoredTools.Remove(Tool);

	return true;
}

FVector ACWWorkTruck::GetTrunkLocation() const
{
	return TrunkPoint != nullptr ? TrunkPoint->GetComponentLocation() : GetActorLocation();
}
