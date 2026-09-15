#include "Painting/Components/CWWorkerComponent.h"

#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Painting/Tools/CWBaseTool.h"
#include "Painting/Zone/CWBaseWorkZone.h"

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

	// 남의 머리 위 알림에도 필요하다. 소유자에게만 보내면 보급자가 볼 수 없다. (기획서 5-4)
	DOREPLIFETIME(UCWWorkerComponent, bPaintDepleted);

	// 남의 화면에서도 작업 애니메이션이 나와야 한다. 판정에 쓰이는 TargetZone 은 복제되지 않는다.
	DOREPLIFETIME(UCWWorkerComponent, bWorkEffective);
}

void UCWWorkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		TemperatureComponent = Owner->FindComponentByClass<UCWTemperatureComponent>();
	}

	// 등록은 권한과 무관하게 모든 머신에서 한다.
	// 먼지 존이 "지금 쓸고 있는 사람" 을 각 머신에서 찾아야 원격 화면에도 붓 자취가 남는다.
	if (UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this))
	{
		PaintingSubsystem->RegisterWorker(this);
	}
}

void UCWWorkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this))
	{
		PaintingSubsystem->UnregisterWorker(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UCWWorkerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr)
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

	// 조준 표시기가 읽어갈 수 있게 남겨 둔다. 표시기는 이 값을 그대로 원의 중심으로 쓴다.
	CurrentWorkLocation = WorkLocation;

	// 커서 지점은 소유 클라이언트만 알 수 있으므로 작업 중에는 서버로 올려보낸다.
	// (게이지/미니게임 존은 이 값을 쓰지 않지만, 칸을 칠하는 존은 이게 붓 중심이 된다)
	if (bIsLocalWorker && !bIsServer && bWorking)
	{
		const float Now = World->GetTimeSeconds();
		if (Now - LastWorkLocationSendTime >= WorkLocationSendInterval)
		{
			LastWorkLocationSendTime = Now;
			Server_UpdateWorkLocation(WorkLocation);
		}
	}

	// 존이 스스로 보고한 체온 상승분을 흘려보낸다.
	// 작업 키를 누르고 있지 않아도(미니게임이 혼자 도는 동안) 나가야 하므로 아래 조기 반환보다 위에 둔다.
	FlushPendingWorkHeat();

	// 페인트가 없으면 작업이 진행되지 않는다. (기획서 5-1)
	//
	// 작업하려 했는데 페인트가 없는 경우만 잡는다. 작업 키를 놓거나 다운되면 저절로 풀린다.
	// 대상 존을 찾은 뒤에 보는 이유: 존도 없는 허공에 대고 키를 누를 때마다
	// 머리 위에 "페인트가 부족합니다!" 가 뜨면 안 된다.
	const bool bWantsToWork = bWorking && !bWorkBlocked && FoundZone != nullptr;

	// 미니게임 존은 작업 키를 떼도 선이 계속 나아가므로, 소진으로 멈춘 것을 존이 따로 알려준다.
	// (NotifyZonePaintDepleted) 그 보고는 여기서 푼다. 페인트가 채워졌거나, 존을 벗어났거나,
	// 다운됐으면 더 이상 "페인트가 없어 막혀 있다" 가 아니다.
	// 도구를 내려놓은 경우도 HasPaintForWork 가 true 라 여기서 같이 풀린다. (맨손은 언제나 true)
	if (bZoneReportedDepleted && (FoundZone == nullptr || bWorkBlocked || HasPaintForWork()))
	{
		bZoneReportedDepleted = false;
	}

	SetPaintDepleted((bWantsToWork || bZoneReportedDepleted) && !HasPaintForWork());

	// 작업 애니메이션이 보는 값. 아래 조기 반환 앞에 둬야 작업이 끝난 프레임에도 false 로 내려간다.
	// 여기까지 오는 것은 서버와 소유 클라이언트뿐이다. (위쪽 조기 반환)
	bWorkEffective = bWantsToWork && !bPaintDepleted;

	ShowDebugPaint();

	// 진행도를 실제로 올리는 것은 서버뿐이다.
	if (!bWantsToWork || bPaintDepleted)
	{
		return;
	}

	// 여기서 서버/클라이언트를 가르지 않는다. 권위 모델은 존마다 다르기 때문이다.
	//   게이지·칸·먼지 존 : 서버가 진행도를 올린다 (다 같이 작업하는 대상)
	//   테두리 미니게임   : 그리는 본인 기계에서만 돌고 결과만 서버에 올린다
	// 어느 쪽인지는 각 존의 ApplyWork 가 스스로 판단한다.

	// 체온이 높을수록 작업이 느려진다. (체온 기획서 4.2)
	//
	// 존마다 따로 처리하지 않고 넘겨주는 시간을 줄여서 한 번에 적용한다. 게이지 존이든
	// 테두리 미니게임이든 "흐른 시간" 으로 진행하므로, 시간을 줄이면 그대로 느려진다.
	const float WorkSpeedFactor = GetWorkSpeedFactor();
	const float EffectiveDeltaTime = DeltaTime * WorkSpeedFactor;

	// 체온 상승은 흐른 시간이 아니라 "실제로 진행된 작업량" 에 비례한다. (체온 기획서 3.3)
	//
	// 이게 최대 체온을 만든다. 뜨거워지면 작업이 느려지고, 느려지면 체온도 덜 오르므로
	// 상승량과 하락량이 같아지는 지점에서 저절로 멈춘다. 상한값을 박아둘 필요가 없다.
	// 여기에 DeltaTime 을 그대로 넣으면 균형점이 사라져 체온이 무한정 올라간다.
	//
	// 진행이 자기 Tick 에서 도는 존은 제외한다. 그런 존은 작업 키를 누른 프레임에만 여기로 오므로
	// 여기서 올리면 부스트할 때만 더워진다. 대신 존이 SubmitWorkHeat 로 직접 보고한다.
	//
	// 작업이 실제로 진행된 프레임에만, 그때까지 쌓인 작업 시간을 체온으로 넘긴다.
	//
	// 존을 찾았다는 것만으로 올리면 이미 칠한 자리에 붓을 대고 있어도 체온만 오른다.
	// 칸을 칠하는 존은 HasWorkAt 으로 "지금 이 붓질이 뒤집을 칸이 있는가" 를 답한다.
	// 지점 개념이 없는 존(게이지, 먼지)은 기본값 true 라 매 프레임 그대로 넘어간다.
	//
	// 쌓았다가 넘기는 이유는 MaxUnheatedWorkSeconds 주석 참고. 진행이 이어지는 한 흐른 시간이
	// 전부 반영되므로 상승량은 예전과 같고, 덧칠만 할 때만 멈춘다.
	UnheatedWorkSeconds = FMath::Min(UnheatedWorkSeconds + EffectiveDeltaTime, MaxUnheatedWorkSeconds);

	if (bIsServer && TemperatureComponent != nullptr && !FoundZone->DrivesOwnWorkHeat()
		&& FoundZone->HasWorkAt(this, WorkLocation))
	{
		TemperatureComponent->AddWorkHeat(UnheatedWorkSeconds);
		UnheatedWorkSeconds = 0.f;
	}

	// 존이 게이지 방식이든 칸 방식이든 미니게임이든 이 한 줄로 끝난다.
	FoundZone->ApplyWork(this, WorkLocation, EffectiveDeltaTime);
}

void UCWWorkerComponent::SubmitWorkHeat(float EffectiveWorkSeconds)
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr || EffectiveWorkSeconds <= 0.f)
	{
		return;
	}

	// 리슨 서버의 호스트. 자기 체온이므로 바로 올린다.
	if (Owner->HasAuthority())
	{
		if (TemperatureComponent != nullptr)
		{
			TemperatureComponent->AddWorkHeat(EffectiveWorkSeconds);
		}

		return;
	}

	// 클라이언트. 모아 뒀다가 다음 Tick 에서 한 번에 올린다.
	PendingWorkHeat += EffectiveWorkSeconds;
}

void UCWWorkerComponent::NotifyZonePaintDepleted()
{
	bZoneReportedDepleted = true;

	// 알림을 남에게도 띄우려면 서버가 알아야 한다. 미니게임은 이 기계에서만 돌기 때문에
	// 서버가 스스로는 소진 시점을 잡을 수 없다. (기획서 5-4)
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_NotifyZonePaintDepleted();
	}
}

void UCWWorkerComponent::Server_NotifyZonePaintDepleted_Implementation()
{
	bZoneReportedDepleted = true;
}

void UCWWorkerComponent::FlushPendingWorkHeat()
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr || Owner->HasAuthority() || PendingWorkHeat <= 0.f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastWorkHeatSendTime < WorkHeatSendInterval)
	{
		return;
	}

	LastWorkHeatSendTime = Now;
	Server_SubmitWorkHeat(PendingWorkHeat);
	PendingWorkHeat = 0.f;
}

void UCWWorkerComponent::Server_SubmitWorkHeat_Implementation(float EffectiveWorkSeconds)
{
	if (TemperatureComponent != nullptr)
	{
		TemperatureComponent->AddWorkHeat(EffectiveWorkSeconds);
	}
}

void UCWWorkerComponent::ConsumePaint(float Amount)
{
	if (HeldToolActor != nullptr)
	{
		HeldToolActor->ConsumePaint(Amount);
	}
}

bool UCWWorkerComponent::HasPaintForWork() const
{
	// 맨손이면 잔량을 볼 것이 없다. 도구가 없어서 작업이 안 되는 것은 존의 CanUseTool 이 거른다.
	return HeldToolActor == nullptr || HeldToolActor->HasPaint();
}

void UCWWorkerComponent::ShowDebugPaint() const
{
	// 남의 캐릭터까지 그리면 같은 자리에 여러 줄이 겹친다. 내 것만 띄운다.
	if (!bShowDebugPaint || !IsLocallyControlledWorker() || GEngine == nullptr)
	{
		return;
	}

	if (HeldToolActor == nullptr || !HeldToolActor->UsesPaint())
	{
		return;
	}

	// 키를 컴포넌트 주소로 잡아 매 프레임 같은 줄을 덮어쓴다. 화면이 밀려 올라가지 않는다.
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(reinterpret_cast<UPTRINT>(this)),
		0.f,
		bPaintDepleted ? FColor::Red : FColor::Cyan,
		FString::Printf(TEXT("Paint %.0f / %.0f (%.0f%%)"),
			HeldToolActor->GetPaintAmount(),
			HeldToolActor->GetMaxPaintAmount(),
			HeldToolActor->GetPaintRatio() * 100.f));
}

void UCWWorkerComponent::SetPaintDepleted(bool bNewDepleted)
{
	if (bPaintDepleted == bNewDepleted)
	{
		return;
	}

	bPaintDepleted = bNewDepleted;

	HandlePaintDepletedChanged();
}

void UCWWorkerComponent::OnRep_PaintDepleted()
{
	// 원격 캐릭터는 Tick 이 조기 반환하므로 이 경로로만 상태가 들어온다.
	// 소유 클라이언트가 미리 같은 값으로 맞춰 둔 경우에는 값이 바뀌지 않아 여기까지 오지 않는다.
	HandlePaintDepletedChanged();
}

void UCWWorkerComponent::HandlePaintDepletedChanged()
{
	OnPaintDepletedChanged.Broadcast(this, bPaintDepleted);
}

float UCWWorkerComponent::GetWorkSpeedFactor() const
{
	return TemperatureComponent != nullptr ? TemperatureComponent->GetWorkSpeedFactor() : 1.f;
}

void UCWWorkerComponent::StartWork()
{
	// 소유 클라이언트에서는 즉시 반영한다. 미니게임 선의 로컬 가속이 이 값을 본다.
	bWorking = true;
	bCancelRequested = false;

	// 다음 틱에 커서 위치가 바로 올라가게 한다.
	// 여기서 좌표를 실어 보내지 않는 이유: ReceivedWorkLocation 은 서버가 쓰는 값이라
	// 클라이언트에서는 언제나 0 이다. 그대로 보내면 서버가 붓질 시작 지점을 0 으로 잡는다.
	LastWorkLocationSendTime = -1000.f;

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

void UCWWorkerComponent::CancelWork()
{
	// 이동 잠금은 로컬에서 바로 푼다. 서버 왕복을 기다리면 그 사이 차를 못 피한다.
	bCancelRequested = true;
	bWorking = false;

	// 미니게임은 이 기계에서 돌고 있으므로 여기서 바로 끈다.
	// 서버에 취소를 요청하는 게 아니다. 서버에는 돌고 있는 미니게임이 없다.
	if (TargetZone != nullptr)
	{
		TargetZone->CancelWork();
	}

	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		Server_SetWorking(false, ReceivedWorkLocation);
	}
}

bool UCWWorkerComponent::ConsumeDirectionInput(const FVector2D& MoveInput)
{
	// 취소를 눌렀으면 존이 정리될 때까지 입력을 붙잡지 않는다.
	if (TargetZone == nullptr || bCancelRequested)
	{
		return false;
	}

	// 이동 잠금은 본인 기계에서만 판단한다. 서버로 보내지 않는다.
	//
	// 예전에는 매 프레임 Server_SubmitDirectionInput 을 보냈다. 미니게임이 꼭짓점에서
	// 방향키를 받아야 했기 때문인데, 그 미니게임이 없어지면서 서버가 이 입력으로 할 일이
	// 사라졌다. 그대로 두면 차징 중 WASD 를 누르는 내내 신뢰 RPC 가 초당 수십 개씩 나간다.
	return TargetZone->ConsumeDirectionInput(MoveInput);
}

void UCWWorkerComponent::SetHeldTool(ECWToolType NewTool, ACWBaseTool* NewToolActor)
{
	HeldTool = NewTool;
	HeldToolActor = NewToolActor;

	// 도구가 바뀌면 진행 중이던 작업은 끊는다.
	bWorking = false;

	// 새 도구는 잔량이 다르다. 이전 도구 기준의 부족 표시를 그대로 두면 안 된다.
	SetPaintDepleted(false);
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
				OutWorkLocation = ClampWorkLocationToPawn(PawnLocation, Hit.ImpactPoint);
			}
		}
	}
	else if (!ReceivedWorkLocation.IsZero())
	{
		// 서버에서 원격 클라이언트를 처리하는 경우. 커서를 볼 수 없으므로 올라온 값을 쓴다.
		OutWorkLocation = ClampWorkLocationToPawn(PawnLocation, ReceivedWorkLocation);
	}

	const UCWPaintingSubsystem* PaintingSubsystem = UCWPaintingSubsystem::Get(this);
	if (PaintingSubsystem == nullptr)
	{
		return nullptr;
	}

	// 캐릭터에서 가장 가까운, 지금 작업할 수 있는 존을 고른다.
	//
	// 등록된 존만 돈다. 이 함수는 매 프레임, 서버에서는 플레이어 수만큼 돌기 때문에
	// TActorIterator 로 레벨 액터 전체를 훑으면 맵이 커질수록 그대로 비용이 된다.
	// (스캔 비용은 존 개수가 아니라 레벨 액터 총 개수에 비례한다)
	ACWBaseWorkZone* NearestZone = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();

	// 고른 존이 "지금 이 지점에 할 일이 있는" 존인가.
	//
	// 이게 1순위이고 거리가 2순위다. 존이 겹쳐 있으면 박스 거리가 둘 다 0 이라 거리만으로는
	// 갈리지 않는다. 도로 전체를 덮는 정리 존과 도색 존이 겹친 자리에서, 지울 페인트가 없는
	// 쪽이 붓을 붙잡으면 발밑의 페인트가 안 지워지고 체온만 오른다.
	bool bNearestHasWorkHere = false;

	for (const TWeakObjectPtr<ACWBaseWorkZone>& WeakZone : PaintingSubsystem->GetZones())
	{
		ACWBaseWorkZone* Zone = WeakZone.Get();
		if (Zone == nullptr || !Zone->CanWorkNow(HeldTool))
		{
			continue;
		}

		// 작업할 수 있는 위치인지는 존이 판단한다.
		//
		// 존마다 기준이 다르다. 게이지 존이나 미니게임은 "존 옆에 서 있어야 한다" 가 맞지만,
		// 칸을 칠하는 존은 "붓이 닿는 곳에 칸이 있으면 된다" 가 맞다. 후자를 박스 거리로
		// 판단하면 격자가 박스보다 넓게 뻗은 만큼(바깥 고리) 손이 닿는데도 작업이 막힌다.
		if (!Zone->IsWorkerInRange(this, OutWorkLocation))
		{
			continue;
		}

		const float DistanceSquared = Zone->GetSquaredDistanceToPoint(PawnLocation);

		const bool bHasWorkHere = Zone->HasWorkAt(this, OutWorkLocation);

		// 할 일이 있는 존을 이미 잡았으면 없는 존으로 바꾸지 않는다.
		if (bNearestHasWorkHere && !bHasWorkHere)
		{
			continue;
		}

		// 등급이 같을 때만 거리로 가른다. 등급이 올라가면 거리를 따지지 않고 바꿔 잡는다.
		if (bNearestHasWorkHere == bHasWorkHere && DistanceSquared >= NearestDistanceSquared)
		{
			continue;
		}

		NearestDistanceSquared = DistanceSquared;
		NearestZone = Zone;
		bNearestHasWorkHere = bHasWorkHere;
	}

	return NearestZone;
}

FVector UCWWorkerComponent::ClampWorkLocationToPawn(const FVector& PawnLocation, const FVector& CursorLocation) const
{
	if (MaxBrushDistance <= 0.f)
	{
		return CursorLocation;
	}

	// 평면 거리로만 자른다. 높이까지 같이 줄이면 붓이 도로 위가 아니라 공중에 뜬다.
	FVector Offset = CursorLocation - PawnLocation;
	Offset.Z = 0.f;

	const float DistanceSquared = Offset.SizeSquared();
	if (DistanceSquared <= FMath::Square(MaxBrushDistance))
	{
		return CursorLocation;
	}

	// 커서 방향은 살리고 거리만 줄인다. 커서를 멀리 두면 팔이 닿는 데까지만 칠해진다.
	const FVector ClampedOffset = Offset * (MaxBrushDistance / FMath::Sqrt(DistanceSquared));

	return FVector(PawnLocation.X + ClampedOffset.X, PawnLocation.Y + ClampedOffset.Y, CursorLocation.Z);
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

void UCWWorkerComponent::ReportWorkProgress(ACWBaseWorkZone* Zone, float NewProgress)
{
	if (Zone == nullptr)
	{
		return;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		Zone->ReceiveReportedProgress(this, NewProgress);
		return;
	}

	// 완주는 놓치면 작업이 끝나지 않으므로 신뢰 채널로 따로 보낸다.
	if (NewProgress >= 1.f)
	{
		Server_ReportWorkCompleted(Zone);
		return;
	}

	Server_ReportWorkProgress(Zone, NewProgress);
}

void UCWWorkerComponent::Server_ReportWorkProgress_Implementation(ACWBaseWorkZone* Zone, float NewProgress)
{
	if (Zone != nullptr)
	{
		Zone->ReceiveReportedProgress(this, NewProgress);
	}
}

void UCWWorkerComponent::Server_ReportWorkCompleted_Implementation(ACWBaseWorkZone* Zone)
{
	if (Zone != nullptr)
	{
		Zone->ReceiveReportedProgress(this, 1.f);
	}
}

void UCWWorkerComponent::SubmitBrushStrokes(ACWBaseWorkZone* Zone, const TArray<FCWBrushStroke>& Strokes)
{
	if (Zone == nullptr || Strokes.Num() == 0)
	{
		return;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		Zone->ReceiveBrushStrokes(this, Strokes);
		return;
	}

	Server_SubmitBrushStrokes(Zone, Strokes);
}

void UCWWorkerComponent::Server_SubmitBrushStrokes_Implementation(ACWBaseWorkZone* Zone, const TArray<FCWBrushStroke>& Strokes)
{
	if (Zone != nullptr)
	{
		Zone->ReceiveBrushStrokes(this, Strokes);
	}
}

void UCWWorkerComponent::SubmitBorderCells(ACWBaseWorkZone* Zone, const TArray<int32>& CellIndices)
{
	if (Zone == nullptr || CellIndices.Num() == 0)
	{
		return;
	}

	// 리슨 서버 호스트는 자기 자신이 서버다. RPC 를 태우면 한 프레임을 돌아가기만 한다.
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		Zone->ReceiveBorderCells(this, CellIndices);
		return;
	}

	Server_SubmitBorderCells(Zone, CellIndices);
}

void UCWWorkerComponent::Server_SubmitBorderCells_Implementation(ACWBaseWorkZone* Zone, const TArray<int32>& CellIndices)
{
	if (Zone != nullptr)
	{
		Zone->ReceiveBorderCells(this, CellIndices);
	}
}

bool UCWWorkerComponent::IsParryBlocked() const
{
	// bWorking 을 같이 본다. 대상 존이 잡혔다는 것만으로 막으면, 차징 존 옆에 서 있기만 해도
	// 패링이 안 된다. 대상 존은 근처에 있기만 해도 잡히기 때문이다.
	//
	// bWorking 은 리플리케이트되는 값이라 서버와 클라이언트의 판단이 갈리지 않는다.
	// 차징 누적 시간은 로컬 값이므로 그걸로 판단하면 안 된다.
	return bWorking && TargetZone != nullptr && TargetZone->BlocksParry();
}

bool UCWWorkerComponent::IsAimingBlocked() const
{
	return bWorking && TargetZone != nullptr && TargetZone->BlocksAiming();
}

int32 UCWWorkerComponent::GetChargeLevel() const
{
	return (TargetZone != nullptr) ? TargetZone->GetChargeLevel() : 0;
}

float UCWWorkerComponent::GetChargeRatio() const
{
	return (TargetZone != nullptr) ? TargetZone->GetChargeRatio() : 0.f;
}

bool UCWWorkerComponent::IsLocalWorker() const
{
	return IsLocallyControlledWorker();
}

bool UCWWorkerComponent::IsLocallyControlledWorker() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	return OwnerPawn != nullptr && OwnerPawn->IsLocallyControlled();
}
