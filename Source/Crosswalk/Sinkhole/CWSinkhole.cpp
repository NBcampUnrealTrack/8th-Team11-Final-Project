// CWSinkhole.cpp

#include "Sinkhole/CWSinkhole.h"

#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Sinkhole/Components/CWSinkholeFallComponent.h"

namespace
{
	/**
	 * 바닥 데칼끼리의 층 순서. 값이 크면 위에 그려진다.
	 *
	 *   먼지 존 -10  →  페인트 자국 0(기본)  →  싱크홀 균열 10  →  구멍 테 20
	 *
	 * 싱크홀이 가장 위인 이유는 지면이 갈라진 사건이 도로 위 연출물보다 나중에, 그리고
	 * 더 근본적으로 일어난 일이기 때문이다. 균열이 먼지나 페인트에 가려지면 이미 갈라진
	 * 바닥이 멀쩡해 보인다.
	 */
	constexpr int32 CrackSortOrder = 10;
	constexpr int32 HoleRimSortOrder = 20;
}

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

	VoidPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VoidPlane"));
	VoidPlane->SetupAttachment(FallTrigger);
	VoidPlane->SetRelativeLocation(FVector(0.f, 0.f, VoidPlaneHeight));
	VoidPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VoidPlane->SetGenerateOverlapEvents(false);

	// 구멍은 빛을 받지도, 가리지도 않는다. 그림자를 던지면 바닥에 검은 원이 두 겹으로 생긴다.
	VoidPlane->SetCastShadow(false);

	// 도로 데칼(차선, 페인트 자국)이 구멍 안쪽에 얹히면 구멍이 아니라 스티커가 된다.
	VoidPlane->bReceivesDecals = false;

	// 데칼은 바닥을 향해 투영해야 해서 아래를 보도록 돌려 둔다.
	const FRotator DecalProjection(-90.f, 0.f, 0.f);

	HoleDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("HoleDecal"));
	HoleDecal->SetupAttachment(FallTrigger);
	HoleDecal->SetRelativeRotation(DecalProjection);
	HoleDecal->SortOrder = HoleRimSortOrder;

	CrackDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("CrackDecal"));
	CrackDecal->SetupAttachment(FallTrigger);
	CrackDecal->SetRelativeRotation(DecalProjection);
	CrackDecal->SortOrder = CrackSortOrder;
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

	// 층 순서를 런타임에 한 번 더 못 박는다. BP 에셋에 예전 값이 직렬화되어 있으면
	// 생성자에서 넣은 기본값이 덮인다. (#109 와 같은 함정)
	if (HoleDecal != nullptr)
	{
		HoleDecal->SetSortOrder(HoleRimSortOrder);
	}

	if (CrackDecal != nullptr)
	{
		CrackDecal->SetSortOrder(CrackSortOrder);
	}

	FallTrigger->OnComponentBeginOverlap.AddDynamic(this, &ACWSinkhole::HandleFallOverlap);

	// 평면 높이를 먼저 맞춘다. RefreshVisualAndCollision 이 스케일만 만지므로 순서는 상관없지만,
	// 첫 프레임에 한 번이라도 뜬 높이로 그려지면 그게 눈에 띈다.
	AlignVoidPlaneToVisibleGround();

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

	if (VoidPlane != nullptr)
	{
		// 기본 평면 메시는 한 변이 100 이라 반지름을 50 으로 나눈 값이 스케일이 된다.
		const float PlaneScale = VisualRadius / 50.f;
		VoidPlane->SetRelativeScale3D(FVector(PlaneScale, PlaneScale, 1.f));

		// 예고 중에는 아직 구멍이 아니다. 균열만 번지고 검은 구멍은 열리지 않았다. (기획서 3-2)
		// 여기서 검은 원이 미리 보이면 "밟으면 빠지겠다" 로 읽히는데 실제로는 낙하 판정이
		// 꺼져 있어서, 피했는데 아무 일도 없거나 밟았는데 멀쩡한 어긋남이 생긴다.
		VoidPlane->SetVisibility(IsFallActive());
	}

	// 데칼 크기를 VisualRadius 에 맞춘다.
	// DecalSize 는 반쪽 크기(half extent)라 X 는 투영 깊이, Y/Z 가 평면 크기다.
	// 투영 깊이는 도로 요철을 덮을 만큼만 있으면 되므로 반지름과 무관하게 둔다.
	if (HoleDecal != nullptr)
	{
		HoleDecal->DecalSize = FVector(HoleDecal->DecalSize.X, VisualRadius, VisualRadius);
		HoleDecal->MarkRenderStateDirty();

		// 가장자리 테는 검은 원과 한 몸이므로 같이 켜고 같이 끈다.
		//
		// 예고 중에 테만 먼저 깔리면, 이미 동그란 구멍 자국이 생긴 뒤 1초쯤 지나 검은 원이
		// 따로 얹히는 두 단계로 읽힌다. 예고는 "균열만 번지는" 한 단계여야 하고, 구멍이 열리는
		// 순간에 테와 검은 원이 같이 나타나야 한 사건으로 읽힌다. (기획서 3-2)
		HoleDecal->SetVisibility(IsFallActive());
	}

	// 균열은 구멍이 줄어도 그대로 둔다. 실제로 갈라진 자국이 아스팔트로 덮인다고
	// 보기는 어렵고, 균열까지 같이 줄면 진행도가 이중으로 표현되어 산만해진다.
}

void ACWSinkhole::AlignVoidPlaneToVisibleGround()
{
	if (VoidPlane == nullptr)
	{
		return;
	}

	const FVector ActorLocation = GetActorLocation();

	// 기본값: 액터 원점 바로 위. 보이는 지면을 못 찾으면 종전 동작을 그대로 유지한다.
	float PlaneOffset = VoidPlaneHeight;

	if (GroundProbeDistance > 0.f)
	{
		// 아래로만 훑는다. 위를 보면 구멍 위를 지나가는 차 밑면 같은 것을 지면으로 잡는다.
		// 시작점을 원점에서 살짝 띄우는 이유는, 액터가 이미 보이는 지면에 딱 붙어 스폰된
		// 경우에 표면과 겹쳐서 출발해 히트를 놓치는 것을 막기 위해서다.
		constexpr float ProbeStartOffset = 2.f;

		const FVector ProbeStart = ActorLocation + FVector(0.f, 0.f, ProbeStartOffset);
		const FVector ProbeEnd = ActorLocation - FVector(0.f, 0.f, GroundProbeDistance);

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		// 안 보이는 표면을 만나면 무시 목록에 넣고 같은 자리에서 다시 쏜다.
		//
		// Multi 트레이스로 한 번에 받을 수 없다. 첫 블로킹 히트에서 끊기기 때문에, 안 보이는
		// 블로킹 볼륨이 도로를 덮고 있으면 결과가 그 볼륨 하나로 끝난다. 히트 지점 아래에서
		// 다시 쏘는 방법도 안 된다. 볼륨 내부에서 출발하면 시작부터 파고든 상태로 잡힌다.
		//
		// 횟수를 넉넉히 잡아야 한다. 도로 위에는 안 보이면서 Visibility 를 막는 볼륨이 여러 겹
		// 깔려 있다. 도로 콜리전용 블로킹 브러시 하나에 작업 존마다 Area 박스가 하나씩 붙는데,
		// 작업 존이 겹친 자리에서는 이것만 네다섯 개다. 횟수가 모자라면 보이는 지면을 찾기 전에
		// 조용히 포기하고 fallback 으로 떨어져, 자리에 따라 평면 높이가 달라진다.
		constexpr int32 MaxProbeSteps = 16;

		for (int32 Step = 0; Step < MaxProbeSteps; ++Step)
		{
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, ProbeStart, ProbeEnd, ECC_Visibility, QueryParams))
			{
				break;
			}

			UPrimitiveComponent* HitComponent = Hit.GetComponent();
			if (HitComponent == nullptr)
			{
				break;
			}

			// 게임 화면에 안 그려지는 것에는 데칼도 안 찍힌다. 그런 표면에 평면을 얹으면
			// 검은 원만 공중에 뜬다.
			if (HitComponent->bHiddenInGame || !HitComponent->IsVisible())
			{
				QueryParams.AddIgnoredComponent(HitComponent);
				continue;
			}

			PlaneOffset = (Hit.ImpactPoint.Z - ActorLocation.Z) + VoidPlaneHeight;
			break;
		}
	}

	VoidPlane->SetRelativeLocation(FVector(0.f, 0.f, PlaneOffset));
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
