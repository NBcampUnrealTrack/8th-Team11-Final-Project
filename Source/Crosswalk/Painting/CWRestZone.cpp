#include "Painting/CWRestZone.h"

#include "Characters/CWPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerState.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Net/UnrealNetwork.h"

ACWRestZone::ACWRestZone()
{
	// 점유가 클레임 기반이라 주기적으로 훑을 것이 없어졌다. 남은 Tick 은 디버그 표시 전용이고,
	// BeginPlay 에서 bShowDebugOccupant 가 아니면 꺼 버린다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	bReplicates = true;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	SetRootComponent(Area);

	// 프롬프트가 뜨는 범위다. 의자 주변으로 넉넉히 잡되 SeatInteractRadius 안쪽이어야 한다.
	// 1인 제한은 이 박스의 폭이 아니라 착석 판정이 담당하므로 좁게 만들 이유가 없다.
	Area->SetBoxExtent(FVector(120.f, 120.f, 110.f));

	// 막지 않고 겹침만 본다. 물리적으로 좁히는 것은 레벨의 차량과 구조물이 한다.
	Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Area->SetCollisionObjectType(ECC_WorldStatic);
	Area->SetCollisionResponseToAllChannels(ECR_Ignore);
	Area->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Area->SetGenerateOverlapEvents(true);

	Seat = CreateDefaultSubobject<USceneComponent>(TEXT("Seat"));
	Seat->SetupAttachment(Area);
}

void ACWRestZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWRestZone, Occupant);
}

void ACWRestZone::BeginPlay()
{
	Super::BeginPlay();

	if (bShowDebugArea)
	{
		Area->SetHiddenInGame(false);
	}

	// 겹침은 모든 머신에서 받는다. 서버가 쓰려는 게 아니라 각 화면의 프롬프트 때문이다.
	// 예전에는 이 겹침이 회복 판정이라 서버만 필요했다.
	Area->OnComponentBeginOverlap.AddDynamic(this, &ACWRestZone::HandleBeginOverlap);
	Area->OnComponentEndOverlap.AddDynamic(this, &ACWRestZone::HandleEndOverlap);

	SetActorTickEnabled(bShowDebugOccupant);
}

void ACWRestZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/**
	 * 앉아 있는 사람을 반드시 일으켜 세운 뒤에 사라진다.
	 *
	 * 안 풀면 이동 모드가 MOVE_None 에 남아서, 존만 사라지고 플레이어는 영영 못 움직인다.
	 * 레벨 전환과 스테이지 리셋이 전부 이 경로를 지난다.
	 */
	if (HasAuthority() && Occupant != nullptr)
	{
		Occupant->EndSitting();
	}

	Super::EndPlay(EndPlayReason);
}

void ACWRestZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ShowDebugOccupant();
}

void ACWRestZone::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority())
	{
		return;
	}

	ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(InstigatorPawn);
	if (Player == nullptr)
	{
		return;
	}

	// 눌린 순간의 상태로 한 번 더 본다. 클라이언트가 보낸 요청이라 여기까지 오는 사이에
	// 다른 사람이 먼저 앉았을 수 있다. 같은 프레임에 둘이 눌러도 여기서 갈린다.
	if (!CanInteract_Implementation(InstigatorPawn))
	{
		return;
	}

	SetOccupant(Player);
	ApplyRestingState(Player, true);

	// 앉히는 것은 캐릭터가 한다. 이동 잠금과 애니메이션 상태가 전부 그쪽에 있다.
	Player->BeginSitting(this);
}

bool ACWRestZone::CanInteract_Implementation(APawn* InstigatorPawn) const
{
	const ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(InstigatorPawn);
	if (Player == nullptr)
	{
		return false;
	}

	// 의자는 하나다. 누가 앉아 있으면 끝이다. 본인이 앉아 있는 경우도 여기서 걸린다.
	if (Occupant != nullptr)
	{
		return false;
	}

	// 다른 데 앉아 있는 사람이 이쪽으로 옮겨 앉는 것도 막는다.
	// 일어나는 것은 WASD 지, 다른 의자에 E 를 누르는 것이 아니다.
	if (Player->IsSitting())
	{
		return false;
	}

	// 거리는 의자 기준이다. 서버가 다시 판정할 때를 대비해 여유를 준다.
	const float AllowedRadius = SeatInteractRadius * ServerRangeTolerance;
	if (FVector::DistSquared(Player->GetActorLocation(), GetInteractionLocation_Implementation()) > FMath::Square(AllowedRadius))
	{
		return false;
	}

	return true;
}

FVector ACWRestZone::GetInteractionLocation_Implementation() const
{
	return Seat != nullptr ? Seat->GetComponentLocation() : GetActorLocation();
}

FTransform ACWRestZone::GetSeatTransform() const
{
	if (Seat == nullptr)
	{
		// 컴포넌트가 통째로 없는 경우는 사실상 없지만, 조용히 바닥 원점에 박히는 것보다 낫다.
		UE_LOG(LogTemp, Warning, TEXT("[%s] Seat 컴포넌트가 없어 액터 원점에 앉힙니다."), *GetName());
		return GetActorTransform();
	}

	return Seat->GetComponentTransform();
}

void ACWRestZone::ReleaseOccupant(ACWPlayerCharacter* Player)
{
	if (!HasAuthority() || Occupant == nullptr || Occupant != Player)
	{
		return;
	}

	ApplyRestingState(Occupant, false);
	SetOccupant(nullptr);
}

void ACWRestZone::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	const ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(OtherActor);

	// 내 화면의 플레이어만 본다. 프롬프트는 남에게 보여줄 것이 아니다.
	if (Player == nullptr || !Player->IsLocallyControlled())
	{
		return;
	}

	bLocalPlayerInside = true;
	RefreshPromptState();
}

void ACWRestZone::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	const ACWPlayerCharacter* Player = Cast<ACWPlayerCharacter>(OtherActor);
	if (Player == nullptr || !Player->IsLocallyControlled())
	{
		return;
	}

	// 차에 치이면 캡슐 콜리전이 꺼지면서 여기로 들어온다. 그래도 프롬프트는 숨기는 게 맞다.
	// 쓰러져 있는 동안에는 어차피 E 가 막혀 있다.
	bLocalPlayerInside = false;
	RefreshPromptState();
}

void ACWRestZone::RefreshPromptState()
{
	ECWRestPromptState NewState = ECWRestPromptState::Hidden;

	if (bLocalPlayerInside)
	{
		if (Occupant == nullptr)
		{
			NewState = ECWRestPromptState::Available;
		}
		else if (!Occupant->IsLocallyControlled())
		{
			NewState = ECWRestPromptState::Occupied;
		}
		// 본인이 앉아 있으면 Hidden 그대로다. 앉아 있는 사람에게 "앉기" 를 띄울 이유가 없다.
	}

	if (PromptState == NewState)
	{
		return;
	}

	PromptState = NewState;

	OnPromptStateChanged(PromptState);
	OnPromptChangedDelegate.Broadcast(this, PromptState);
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
	// 누가 앉고 일어나면 내 화면의 프롬프트도 같이 바뀐다. (비어있음 <-> 사용 중)
	RefreshPromptState();

	OnOccupantChanged(Occupant);
	OnOccupantChangedDelegate.Broadcast(this, Occupant);
}

void ACWRestZone::ShowDebugOccupant() const
{
	if (!bShowDebugOccupant || GEngine == nullptr)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()),
		2.f,
		Occupant != nullptr ? FColor::Cyan : FColor::Silver,
		FString::Printf(TEXT("휴식구역: %s"),
			Occupant != nullptr ? *ResolvePlayerLabel(Occupant) : TEXT("비어있음")));
}

FString ACWRestZone::ResolvePlayerLabel(const APawn* Player)
{
	if (!IsValid(Player))
	{
		return TEXT("없음");
	}

	if (const APlayerState* PlayerState = Player->GetPlayerState())
	{
		return PlayerState->GetPlayerName();
	}

	return Player->GetName();
}

UCWTemperatureComponent* ACWRestZone::FindTemperatureComponent(ACWPlayerCharacter* Player)
{
	if (!IsValid(Player))
	{
		return nullptr;
	}

	return Player->FindComponentByClass<UCWTemperatureComponent>();
}
