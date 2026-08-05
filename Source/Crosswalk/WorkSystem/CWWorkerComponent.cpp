#include "WorkSystem/CWWorkerComponent.h"

#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "WorkSystem/CWBaseWorkZone.h"

UCWWorkerComponent::UCWWorkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicatedByDefault(true);
}

void UCWWorkerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWWorkerComponent, HeldTool);
	DOREPLIFETIME(UCWWorkerComponent, bWorking);
	DOREPLIFETIME(UCWWorkerComponent, bWorkBlocked);
}

void UCWWorkerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	const bool bIsServer = Owner->HasAuthority();
	const bool bIsLocalWorker = IsLocallyControlledWorker();

	// 다른 사람의 캐릭터에서는 계산할 이유가 없다. 서버 판정도, 내 HUD 도 아니다.
	if (!bIsServer && !bIsLocalWorker)
	{
		return;
	}

	// HUD에 "지금 작업할 수 있는 대상"을 보여주기 위해 항상 갱신한다.
	FVector WorkLocation = FVector::ZeroVector;
	ACWBaseWorkZone* FoundZone = FindTargetZone(WorkLocation);
	SetTargetZone(FoundZone);

	// 커서 지점은 소유 클라이언트만 알 수 있으므로 작업 중에는 서버로 올려보낸다.
	// (게이지/미니게임 존은 이 값을 쓰지 않지만, 칸을 칠하는 존은 이게 붓 중심이 된다)
	if (bIsLocalWorker && !bIsServer && bWorking)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastWorkLocationSendTime >= WorkLocationSendInterval)
		{
			LastWorkLocationSendTime = Now;
			Server_UpdateWorkLocation(WorkLocation);
		}
	}

	// 진행도를 실제로 올리는 것은 서버뿐이다.
	if (!bIsServer || !bWorking || bWorkBlocked || FoundZone == nullptr)
	{
		return;
	}

	// 존이 게이지 방식이든 칸 방식이든 미니게임이든 이 한 줄로 끝난다.
	FoundZone->ApplyWork(this, WorkLocation, DeltaTime);
}

void UCWWorkerComponent::StartWork()
{
	// 소유 클라이언트에서는 즉시 반영한다. 미니게임 선의 로컬 가속이 이 값을 본다.
	bWorking = true;

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_SetWorking(true, ReceivedWorkLocation);
	}
}

void UCWWorkerComponent::StopWork()
{
	bWorking = false;

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_SetWorking(false, ReceivedWorkLocation);
	}
}

void UCWWorkerComponent::Server_SetWorking_Implementation(bool bNewWorking, FVector_NetQuantize WorkLocation)
{
	bWorking = bNewWorking;
	ReceivedWorkLocation = WorkLocation;
}

void UCWWorkerComponent::Server_UpdateWorkLocation_Implementation(FVector_NetQuantize WorkLocation)
{
	ReceivedWorkLocation = WorkLocation;
}

bool UCWWorkerComponent::ConsumeDirectionInput(const FVector2D& MoveInput)
{
	if (TargetZone == nullptr)
	{
		return false;
	}

	// 소유 클라이언트에서는 먼저 로컬로 적용해 방향 전환이 즉시 보이게 한다.
	// 서버 값이 리플리케이트되면 그쪽으로 정정된다.
	const bool bConsumed = TargetZone->ConsumeDirectionInput(MoveInput);

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_SubmitDirectionInput(TargetZone, MoveInput);
	}

	return bConsumed;
}

void UCWWorkerComponent::Server_SubmitDirectionInput_Implementation(ACWBaseWorkZone* Zone, FVector2D MoveInput)
{
	if (Zone != nullptr)
	{
		Zone->ConsumeDirectionInput(MoveInput);
	}
}

void UCWWorkerComponent::SetHeldTool(ECWToolType NewTool)
{
	HeldTool = NewTool;

	// 도구가 바뀌면 진행 중이던 작업은 끊는다.
	bWorking = false;
}

void UCWWorkerComponent::SetWorkBlocked(bool bNewBlocked)
{
	bWorkBlocked = bNewBlocked;

	if (bWorkBlocked)
	{
		bWorking = false;
	}
}

ACWBaseWorkZone* UCWWorkerComponent::FindTargetZone(FVector& OutWorkLocation) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();
	if (OwnerPawn == nullptr || World == nullptr)
	{
		return nullptr;
	}

	const FVector PawnLocation = OwnerPawn->GetActorLocation();

	// 작업 지점의 기본값은 캐릭터 위치다.
	//
	// 존을 "고르는" 기준과 작업 "지점" 을 분리한 이유:
	//   - 게이지가 차오르는 존(2단계 테두리)은 지점이 필요 없다. 존 옆에 서서 누르면 된다.
	//   - 칸을 칠하는 존(1/3단계)은 커서가 가리키는 지점을 붓 중심으로 써야 한다.
	OutWorkLocation = PawnLocation;

	if (IsLocallyControlledWorker())
	{
		// 커서 트레이스는 로컬 플레이어 컨트롤러가 있어야 가능하다.
		if (const APlayerController* PlayerController = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			FHitResult Hit;
			if (PlayerController->GetHitResultUnderCursorByChannel(
				UEngineTypes::ConvertToTraceType(WorkTraceChannel), false, Hit) && Hit.bBlockingHit)
			{
				OutWorkLocation = Hit.ImpactPoint;
			}
		}
	}
	else if (!ReceivedWorkLocation.IsZero())
	{
		// 서버에서 원격 클라이언트를 처리하는 경우. 커서를 볼 수 없으므로 올라온 값을 쓴다.
		OutWorkLocation = ReceivedWorkLocation;
	}

	// 캐릭터에서 가장 가까운, 지금 작업할 수 있는 존을 고른다.
	// 레벨의 존은 많아도 5~6개 수준이라 전부 훑어도 부담이 없다.
	ACWBaseWorkZone* NearestZone = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	for (TActorIterator<ACWBaseWorkZone> ZoneIter(World); ZoneIter; ++ZoneIter)
	{
		ACWBaseWorkZone* Zone = *ZoneIter;
		if (Zone == nullptr || !Zone->CanWorkNow(HeldTool))
		{
			continue;
		}

		const float AllowedRange = FMath::Min(MaxWorkDistance, Zone->GetWorkRange());
		const float DistanceSquared = Zone->GetSquaredDistanceToPoint(PawnLocation);
		if (DistanceSquared > FMath::Square(AllowedRange) || DistanceSquared >= NearestDistanceSquared)
		{
			continue;
		}

		NearestDistanceSquared = DistanceSquared;
		NearestZone = Zone;
	}

	return NearestZone;
}

void UCWWorkerComponent::SetTargetZone(ACWBaseWorkZone* NewZone)
{
	if (TargetZone == NewZone)
	{
		return;
	}

	TargetZone = NewZone;
	OnTargetZoneChanged.Broadcast(this, TargetZone);
}

bool UCWWorkerComponent::IsLocallyControlledWorker() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	return OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();
}
