#include "Painting/CWRestZone.h"

#include "Characters/CWPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
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

	// 상호작용 후보로 잡히는 범위다. 의자 주변으로 넉넉히 잡되 SeatRange 반지름 안쪽이어야 한다.
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

	// 두 범위는 의자를 기준으로 잰다. Seat 를 옮기면 같이 따라간다.
	// 둘 다 콜리전이 없다. 뷰포트에서 보여주고 반지름 값을 들고 있는 것이 전부다.
	SeatRange = CreateDefaultSubobject<USphereComponent>(TEXT("SeatRange"));
	SeatRange->SetupAttachment(Seat);
	SeatRange->SetSphereRadius(200.f);
	SeatRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SeatRange->SetGenerateOverlapEvents(false);
	SeatRange->SetHiddenInGame(true);
	SeatRange->ShapeColor = FColor(80, 200, 120);

	// SeatRange 보다 조금 넓게 잡아 둔다. 거절 구간까지 덮어야 도구가 안 쌓인다.
	ToolDropBlockRange = CreateDefaultSubobject<USphereComponent>(TEXT("ToolDropBlockRange"));
	ToolDropBlockRange->SetupAttachment(Seat);
	ToolDropBlockRange->SetSphereRadius(250.f);
	ToolDropBlockRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ToolDropBlockRange->SetGenerateOverlapEvents(false);
	ToolDropBlockRange->SetHiddenInGame(true);
	ToolDropBlockRange->ShapeColor = FColor(235, 150, 60);
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
		SeatRange->SetHiddenInGame(false);
		ToolDropBlockRange->SetHiddenInGame(false);
	}

	/**
	 * 도구 차단 범위가 착석 범위보다 좁으면, 앉을 수는 있는데 도구는 내려놓이는 구간이 생긴다.
	 * 바로 그 구간이 도구가 쌓이던 자리라 거꾸로 잡으면 고친 의미가 없다.
	 */
	if (ToolDropBlockRange->GetScaledSphereRadius() < SeatRange->GetScaledSphereRadius())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] ToolDropBlockRange(%.0f)가 SeatRange(%.0f)보다 좁습니다. 앉을 수 있는데 도구가 내려놓이는 구간이 생깁니다."),
			*GetName(), ToolDropBlockRange->GetScaledSphereRadius(), SeatRange->GetScaledSphereRadius());
	}

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
	const float SeatRadius = SeatRange->GetScaledSphereRadius();
	const float AllowedRadius = SeatRadius * ServerRangeTolerance;
	if (FVector::DistSquared(Player->GetActorLocation(), GetInteractionLocation_Implementation()) > FMath::Square(AllowedRadius))
	{
		return false;
	}

	return true;
}

FVector ACWRestZone::GetInteractionLocation_Implementation() const
{
	return Seat->GetComponentLocation();
}

FTransform ACWRestZone::GetSeatTransform() const
{
	return Seat->GetComponentTransform();
}

bool ACWRestZone::IsWithinToolDropBlockRange(const FVector& Location) const
{
	const float Radius = ToolDropBlockRange->GetScaledSphereRadius();

	// 반지름을 0 으로 내리면 이 기능을 끈 것으로 본다. 레벨마다 다르게 쓸 수 있어야 한다.
	if (Radius <= 0.f)
	{
		return false;
	}

	return FVector::DistSquared(Location, ToolDropBlockRange->GetComponentLocation()) <= FMath::Square(Radius);
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
