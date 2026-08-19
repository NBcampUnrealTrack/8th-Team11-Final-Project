#include "Painting/CWRestZone.h"

#include "Characters/CWPlayerCharacter.h"
#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerState.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Net/UnrealNetwork.h"

ACWRestZone::ACWRestZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	bReplicates = true;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	SetRootComponent(Area);

	// 캡슐 하나가 겨우 들어가는 크기. 한 변 60cm 로, 마네킹 캡슐 지름(68)과 비슷하다.
	// 두 명이 나란히 설 수 없는 폭이라 1인 제한이 눈으로도 읽힌다.
	Area->SetBoxExtent(FVector(30.f, 30.f, 90.f));

	// 막지 않고 겹침만 본다. 물리적으로 좁히는 것은 레벨의 차량과 구조물이 한다.
	Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Area->SetCollisionObjectType(ECC_WorldStatic);
	Area->SetCollisionResponseToAllChannels(ECR_Ignore);
	Area->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Area->SetGenerateOverlapEvents(true);
}

void ACWRestZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWRestZone, Occupant);
}

void ACWRestZone::BeginPlay()
{
	Super::BeginPlay();

	PrimaryActorTick.TickInterval = CandidateCheckInterval;

	// 바닥 데칼이 붙기 전까지는 박스가 보여야 위치를 확인할 수 있다.
	if (bShowDebugArea)
	{
		Area->SetHiddenInGame(false);
	}

	// 겹침 판정은 서버만 한다. 클라이언트는 Occupant 를 받아 연출만 맞춘다.
	if (!HasAuthority())
	{
		SetActorTickEnabled(bShowDebugOccupant);
		return;
	}

	Area->OnComponentBeginOverlap.AddDynamic(this, &ACWRestZone::HandleBeginOverlap);
	Area->OnComponentEndOverlap.AddDynamic(this, &ACWRestZone::HandleEndOverlap);
}

void ACWRestZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		// 나가는 것은 EndOverlap 이 알려주지만, 파괴나 접속 종료는 알려주지 않는다.
		PruneCandidates();
		RefreshDownedCandidates();
		RefreshOccupant();
	}

	ShowDebugOccupant();
}

void ACWRestZone::ShowDebugOccupant() const
{
	if (!bShowDebugOccupant || GEngine == nullptr)
	{
		return;
	}

	// 대기 인원은 겹침 목록에서 나오므로 서버에서만 정확하다. 클라이언트는 점유자만 안다.
	const FString WaitingText = HasAuthority()
		? FString::Printf(TEXT(" | 대기 %d명"), GetWaitingCount())
		: FString();

	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()),
		2.f,
		Occupant != nullptr ? FColor::Cyan : FColor::Silver,
		FString::Printf(TEXT("휴식구역: %s%s"),
			Occupant != nullptr ? *ResolvePlayerLabel(Occupant) : TEXT("비어있음"),
			*WaitingText));
}

FString ACWRestZone::ResolvePlayerLabel(const APawn* Player)
{
	if (!IsValid(Player))
	{
		return TEXT("?");
	}

	// 액터 이름(GetName)을 쓰면 안 된다. 서버 월드와 클라이언트 월드는 스폰 순서가 달라서
	// 같은 플레이어가 머신마다 다른 이름(_C_0 / _C_1)을 갖는다. 화면마다 엉뚱한 사람이 찍힌다.
	// PlayerState 의 이름은 서버가 정해 전원에게 복제되므로 모든 머신에서 같다.
	const APlayerState* PlayerState = Player->GetPlayerState();
	const FString Name = PlayerState != nullptr ? PlayerState->GetPlayerName() : FString();

	// 이 화면의 주인인지 같이 표시한다. 4인 테스트에서 이름만으로는 헷갈린다.
	const TCHAR* MineTag = Player->IsLocallyControlled() ? TEXT(" [나]") : TEXT("");

	return FString::Printf(TEXT("%s%s"), Name.IsEmpty() ? TEXT("이름없음") : *Name, MineTag);
}

void ACWRestZone::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(OtherActor);
	if (Player == nullptr)
	{
		return;
	}

	Candidates.AddUnique(Player);
	RefreshOccupant();
}

void ACWRestZone::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(OtherActor);
	if (Player == nullptr)
	{
		return;
	}

	// 차에 치인 순간 캡슐 콜리전이 꺼지면서 여기로 들어온다. 몸은 아직 존 안에 있을 수 있으므로
	// 여기서 빼지 않고 RefreshDownedCandidates 의 위치 판정에 맡긴다.
	// (쓰러진 사람도 자리를 유지한다. 다운 시간이 짧아 오래 붙잡지 못한다)
	if (IsPlayerDowned(Player))
	{
		return;
	}

	Candidates.Remove(Player);

	if (Occupant == Player)
	{
		ApplyRestingState(Player, false);
		SetOccupant(nullptr);
	}

	RefreshOccupant();
}

void ACWRestZone::RefreshOccupant()
{
	if (!HasAuthority())
	{
		return;
	}

	// 점유자는 스스로 나가기 전까지 유지된다. 안에 있는 동안 자리를 뺏기지 않는다.
	if (Occupant != nullptr && Candidates.Contains(Occupant))
	{
		return;
	}

	if (Occupant != nullptr)
	{
		ApplyRestingState(Occupant, false);
		SetOccupant(nullptr);
	}

	ACWPlayerCharacter* NextOccupant = FindHottestCandidate();
	if (NextOccupant == nullptr)
	{
		return;
	}

	SetOccupant(NextOccupant);
	ApplyRestingState(NextOccupant, true);
}

ACWPlayerCharacter* ACWRestZone::FindHottestCandidate() const
{
	ACWPlayerCharacter* Hottest = nullptr;
	float HighestTemperature = -FLT_MAX;

	for (ACWPlayerCharacter* Candidate : Candidates)
	{
		const UCWTemperatureComponent* Temperature = FindTemperatureComponent(Candidate);
		if (Temperature == nullptr)
		{
			continue;
		}

		const float CandidateTemperature = Temperature->GetTemperature();
		if (CandidateTemperature <= HighestTemperature)
		{
			continue;
		}

		HighestTemperature = CandidateTemperature;
		Hottest = Candidate;
	}

	return Hottest;
}

void ACWRestZone::RefreshDownedCandidates()
{
	// 쓰러진 사람은 겹침 이벤트가 오지 않으므로 몸이 아직 존 안에 있는지 직접 본다.
	// 패링으로 차를 틀어 점유자를 날려버리면 여기서 걸러져 자리가 넘어간다.
	for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
	{
		ACWPlayerCharacter* Candidate = Candidates[Index];
		if (!IsPlayerDowned(Candidate) || IsPointInsideArea(ResolveProbeLocation(Candidate)))
		{
			continue;
		}

		Candidates.RemoveAt(Index);

		if (Occupant == Candidate)
		{
			ApplyRestingState(Candidate, false);
			SetOccupant(nullptr);
		}
	}
}

bool ACWRestZone::IsPointInsideArea(const FVector& Point) const
{
	if (Area == nullptr)
	{
		return false;
	}

	// 박스가 회전/스케일되어 있어도 맞도록 로컬 공간으로 옮겨서 잰다.
	// InverseTransformPosition 이 스케일을 이미 풀어주므로 스케일 전 크기와 비교한다.
	const FVector LocalPoint = Area->GetComponentTransform().InverseTransformPosition(Point);
	const FVector Extent = Area->GetUnscaledBoxExtent();

	return FMath::Abs(LocalPoint.X) <= Extent.X
		&& FMath::Abs(LocalPoint.Y) <= Extent.Y
		&& FMath::Abs(LocalPoint.Z) <= Extent.Z;
}

FVector ACWRestZone::ResolveProbeLocation(const ACWPlayerCharacter* Player)
{
	if (!IsValid(Player))
	{
		return FVector::ZeroVector;
	}

	if (!IsPlayerDowned(Player))
	{
		return Player->GetActorLocation();
	}

	const USkeletalMeshComponent* MeshComponent = Player->GetMesh();
	if (MeshComponent == nullptr)
	{
		return Player->GetActorLocation();
	}

	// 쓰러진 몸은 바닥에 붙어 있어서 그대로 재면 박스 아래로 빠진다.
	// 서 있을 때의 캡슐 중심 높이만큼 올려서 같은 기준으로 비교한다.
	const float CapsuleHalfHeight = Player->GetCapsuleComponent() != nullptr
		? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.f;

	return MeshComponent->GetComponentLocation() + FVector(0.f, 0.f, CapsuleHalfHeight);
}

bool ACWRestZone::IsPlayerDowned(const ACWPlayerCharacter* Player)
{
	if (!IsValid(Player) || Player->HitReactionComponent == nullptr)
	{
		return false;
	}

	return Player->HitReactionComponent->IsHitLocked();
}

void ACWRestZone::PruneCandidates()
{
	Candidates.RemoveAll([](const TObjectPtr<ACWPlayerCharacter>& Candidate)
	{
		return !IsValid(Candidate);
	});
}

void ACWRestZone::SetOccupant(ACWPlayerCharacter* NewOccupant)
{
	if (Occupant == NewOccupant)
	{
		return;
	}

	Occupant = NewOccupant;

	// 서버에서는 리플리케이션 콜백이 오지 않으므로 직접 부른다. 연출 경로를 양쪽에서 하나로 둔다.
	OnRep_Occupant();
}

void ACWRestZone::ApplyRestingState(ACWPlayerCharacter* Player, bool bNewResting)
{
	if (UCWTemperatureComponent* Temperature = FindTemperatureComponent(Player))
	{
		Temperature->SetResting(bNewResting);
	}
}

void ACWRestZone::OnRep_Occupant()
{
	OnOccupantChanged(Occupant);
	OnOccupantChangedDelegate.Broadcast(this, Occupant);
}

int32 ACWRestZone::GetWaitingCount() const
{
	return FMath::Max(0, Candidates.Num() - (Occupant != nullptr ? 1 : 0));
}

UCWTemperatureComponent* ACWRestZone::FindTemperatureComponent(ACWPlayerCharacter* Player)
{
	if (!IsValid(Player))
	{
		return nullptr;
	}

	return Player->FindComponentByClass<UCWTemperatureComponent>();
}
