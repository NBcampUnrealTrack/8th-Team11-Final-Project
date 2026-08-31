// CWSinkhole.cpp

#include "Sinkhole/CWSinkhole.h"

#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Net/UnrealNetwork.h"
#include "Sinkhole/Components/CWSinkholeFallComponent.h"

ACWSinkhole::ACWSinkhole()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;

	FallTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("FallTrigger"));
	SetRootComponent(FallTrigger);
	FallTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FallTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	FallTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	FallTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	FallTrigger->SetGenerateOverlapEvents(true);

	// 데칼은 바닥을 향해 투영해야 해서 아래를 보도록 돌려 둔다.
	const FRotator DecalProjection(-90.f, 0.f, 0.f);

	HoleDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("HoleDecal"));
	HoleDecal->SetupAttachment(FallTrigger);
	HoleDecal->SetRelativeRotation(DecalProjection);

	CrackDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("CrackDecal"));
	CrackDecal->SetupAttachment(FallTrigger);
	CrackDecal->SetRelativeRotation(DecalProjection);
}

void ACWSinkhole::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACWSinkhole, FillCount);
	DOREPLIFETIME(ACWSinkhole, State);
}

void ACWSinkhole::BeginPlay()
{
	Super::BeginPlay();

	// 균열만 랜덤하게 돌린다. 검은 원은 대칭이라 돌려도 티가 나지 않는다.
	//
	// 서버에서 굴려서 회전값 자체를 리플리케이트하지 않는 이유: 이건 순수 외형이라
	// 머신마다 달라도 게임에 영향이 없다. 다만 스크린샷 비교나 관전 시 어색할 수 있으니
	// 신경 쓰인다면 스폰 파라미터로 넘겨받는 쪽으로 바꾸면 된다.
	if (CrackDecal != nullptr)
	{
		CrackDecal->SetRelativeRotation(FRotator(-90.f, FMath::FRandRange(0.f, 360.f), 0.f));
	}

	FallTrigger->OnComponentBeginOverlap.AddDynamic(this, &ACWSinkhole::HandleFallOverlap);

	RefreshVisualAndCollision();

	if (HasAuthority())
	{
		if (WarningDuration > 0.f)
		{
			GetWorldTimerManager().SetTimer(
				WarningTimerHandle,
				this,
				&ACWSinkhole::OpenSinkhole,
				WarningDuration,
				false);
		}
		else
		{
			OpenSinkhole();
		}
	}
}

bool ACWSinkhole::ApplyAsphaltHit()
{
	if (!HasAuthority())
	{
		return false;
	}

	// 예고 중에는 아직 구멍이 아니다. 채울 대상도 아니다.
	if (State != ECWSinkholeState::Active)
	{
		return false;
	}

	FillCount = FMath::Min(FillCount + 1, RequiredFillCount);
	OnRep_FillCount();

	if (FillCount >= RequiredFillCount)
	{
		State = ECWSinkholeState::Filled;
		OnRep_State();

		OnSinkholeFilled();

		// 액터를 지우는 것으로 원상복구가 끝난다. 바닥에 흔적을 남기지 않는다.
		//
		// 즉시 Destroy 하지 않는 이유: 마지막 한 발의 진행도와 Filled 상태가 클라이언트에
		// 도달하기 전에 액터가 사라지면, 남의 화면에서는 구멍이 다 메꿔지는 그림 없이
		// 그냥 없어진다. 짧게 남겨 두어 마지막 연출이 전달될 시간을 준다.
		SetLifeSpan(FilledDestroyDelay);
	}

	return true;
}

bool ACWSinkhole::IsPointInHitRange(const FVector& WorldLocation) const
{
	// 높이를 무시하고 평면 거리만 본다. 도로는 평면이라 Z 차이는 판정에 의미가 없다.
	const FVector Delta = WorldLocation - GetActorLocation();
	const float PlanarDistanceSquared = Delta.X * Delta.X + Delta.Y * Delta.Y;
	const float HitRadius = GetVisualRadius();

	return PlanarDistanceSquared <= HitRadius * HitRadius;
}

float ACWSinkhole::GetVisualRadius() const
{
	// 명중 수에 선형으로 줄인다. 면적 기준(sqrt)으로 줄이면 초반 한두 발의 변화가
	// 눈에 띄지 않아 "맞았나?" 싶어진다. 읽히는 쪽을 택한다.
	//
	// 단계별 폭을 곡선으로 만지고 싶어지면 여기에 커브를 물리면 된다.
	const float RemainingRatio = 1.f - GetFillRatio();

	return InitialRadius * RemainingRatio;
}

float ACWSinkhole::GetFallRadius() const
{
	return GetVisualRadius() * FallRadiusRatio;
}

float ACWSinkhole::GetFillRatio() const
{
	if (RequiredFillCount <= 0)
	{
		return 1.f;
	}

	return static_cast<float>(FillCount) / static_cast<float>(RequiredFillCount);
}

void ACWSinkhole::OnSinkholeOpened_Implementation()
{
}

void ACWSinkhole::OnFillProgressed_Implementation(int32 NewFillCount, float NewFillRatio)
{
}

void ACWSinkhole::OnSinkholeFilled_Implementation()
{
}

void ACWSinkhole::OnRep_FillCount()
{
	RefreshVisualAndCollision();

	const float FillRatio = GetFillRatio();

	OnFillProgressed(FillCount, FillRatio);
	OnFillChanged.Broadcast(this, FillCount, FillRatio);
}

void ACWSinkhole::OnRep_State()
{
	RefreshVisualAndCollision();

	if (State == ECWSinkholeState::Active)
	{
		OnSinkholeOpened();
	}
}

void ACWSinkhole::OpenSinkhole()
{
	State = ECWSinkholeState::Active;
	OnRep_State();
}

void ACWSinkhole::RefreshVisualAndCollision()
{
	const float VisualRadius = GetVisualRadius();

	FallTrigger->SetSphereRadius(GetFallRadius());

	// 예고 중에는 낙하 판정을 꺼 둔다. 보이자마자 빠지면 예고의 의미가 없다.
	FallTrigger->SetCollisionEnabled(
		IsFallActive() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

	// TODO: 데칼 크기를 VisualRadius 에 맞춘다.
	//       DecalSize 는 반쪽 크기(half extent)라 X 는 투영 깊이, Y/Z 가 평면 크기다.
	//       투영 깊이는 도로 요철을 덮을 만큼만 있으면 되므로 반지름과 무관하게 둔다.
	if (HoleDecal != nullptr)
	{
		HoleDecal->DecalSize = FVector(HoleDecal->DecalSize.X, VisualRadius, VisualRadius);
		HoleDecal->MarkRenderStateDirty();
	}

	// 균열은 구멍이 줄어도 그대로 둔다. 실제로 갈라진 자국이 아스팔트로 덮인다고
	// 보기는 어렵고, 균열까지 같이 줄면 진행도가 이중으로 표현되어 산만해진다.
}

void ACWSinkhole::HandleFallOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// 빠뜨리는 것은 서버가 정한다. 클라이언트가 각자 판정하면 화면마다 다른 사람이 빠진다.
	if (!HasAuthority() || !IsFallActive())
	{
		return;
	}

	if (OtherActor == nullptr)
	{
		return;
	}

	UCWSinkholeFallComponent* FallComponent = OtherActor->FindComponentByClass<UCWSinkholeFallComponent>();
	if (FallComponent == nullptr)
	{
		return;
	}

	FallComponent->StartFall(this);
}
