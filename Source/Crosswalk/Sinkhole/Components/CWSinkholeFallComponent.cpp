// CWSinkholeFallComponent.cpp

#include "Sinkhole/Components/CWSinkholeFallComponent.h"

#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Sinkhole/CWSinkhole.h"

UCWSinkholeFallComponent::UCWSinkholeFallComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 빠져 있는 동안에만 돈다. 평소에는 꺼 둔다.
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

void UCWSinkholeFallComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCWSinkholeFallComponent, bFallLocked);
	DOREPLIFETIME(UCWSinkholeFallComponent, bInvincible);
	DOREPLIFETIME(UCWSinkholeFallComponent, FallTargetLocation);
}

void UCWSinkholeFallComponent::BeginPlay()
{
	Super::BeginPlay();

	// 원래 메쉬 상태를 기억해 둔다. 리스폰 때 이 값으로 정확히 되돌린다.
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			DefaultMeshRelativeLocation = Mesh->GetRelativeLocation();
			DefaultMeshRelativeScale = Mesh->GetRelativeScale3D();
		}
	}
}

void UCWSinkholeFallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bFallLocked)
	{
		return;
	}

	TickFallSequence(DeltaTime);
}

void UCWSinkholeFallComponent::StartFall(ACWSinkhole* Sinkhole)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	// 이미 빠져 있으면 무시한다. 구멍 두 개가 겹쳐 있어도 한 번만 처리된다.
	if (bFallLocked)
	{
		return;
	}

	if (Sinkhole == nullptr)
	{
		return;
	}

	// 도구를 먼저 떨어뜨린다. 흡입으로 위치가 바뀐 뒤에 떨어뜨리면 구멍 한가운데 놓여 회수할 수 없다.
	EntryLocation = Owner->GetActorLocation();
	DropHeldToolAtEntry();

	FallTargetLocation = Sinkhole->GetActorLocation();
	ElapsedFallTime = 0.f;

	bFallLocked = true;
	OnRep_FallLocked();

	// 완전히 잠긴 뒤부터 리스폰 대기를 센다.
	GetWorld()->GetTimerManager().SetTimer(
		RespawnTimerHandle,
		this,
		&UCWSinkholeFallComponent::Respawn,
		SuctionDuration + SinkDuration + RespawnDelay,
		false);
}

void UCWSinkholeFallComponent::OnRep_FallLocked()
{
	SetComponentTickEnabled(bFallLocked);

	if (bFallLocked)
	{
		ElapsedFallTime = 0.f;

		// 지면 아래로 내려가도 그림자가 도로 위에 남아 있으면 트릭이 바로 드러난다.
		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			if (USkeletalMeshComponent* Mesh = Character->GetMesh())
			{
				Mesh->SetCastShadow(false);
			}
		}

		OnFallStarted();
	}
	else
	{
		// 연출 원복은 모든 머신이 각자 해야 한다.
		//
		// 가라앉히는 것은 각 머신이 자기 시계로 돌린 결과라, 서버에서만 되돌리면
		// 클라이언트의 메쉬는 잠긴 위치(스케일 축소 + Z -SunkMeshDepth)에 그대로 남는다.
		// 리스폰은 됐는데 캐릭터가 도로 아래에 있어 보이지 않게 된다.
		ResetSinkVisual();
	}

	OnFallLockChanged.Broadcast(this, bFallLocked);
}

void UCWSinkholeFallComponent::TickFallSequence(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	ElapsedFallTime += DeltaTime;

	// 1) 흡입 - 구멍 중앙으로 부드럽게 끌어당긴다.
	if (ElapsedFallTime < SuctionDuration)
	{
		const float SuctionAlpha = FMath::Clamp(ElapsedFallTime / FMath::Max(SuctionDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);

		// Z 는 건드리지 않는다. 캐릭터를 띄우거나 묻으면 이동 컴포넌트와 싸우게 된다.
		FVector NewLocation = FMath::Lerp(Owner->GetActorLocation(), FallTargetLocation, SuctionAlpha);
		NewLocation.Z = Owner->GetActorLocation().Z;

		Owner->SetActorLocation(NewLocation);
		return;
	}

	// 2) 가라앉음 - 스케일과 Z 오프셋을 같은 알파로 민다.
	const float SinkElapsed = ElapsedFallTime - SuctionDuration;
	const float RawAlpha = FMath::Clamp(SinkElapsed / FMath::Max(SinkDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);
	const float SinkAlpha = SinkCurve != nullptr ? SinkCurve->GetFloatValue(RawAlpha) : RawAlpha;

	ApplySinkAlpha(SinkAlpha);
}

void UCWSinkholeFallComponent::ApplySinkAlpha(float Alpha)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	const float Scale = FMath::Lerp(1.f, SunkMeshScale, Alpha);
	const float Depth = FMath::Lerp(0.f, SunkMeshDepth, Alpha);

	Mesh->SetRelativeScale3D(DefaultMeshRelativeScale * Scale);
	Mesh->SetRelativeLocation(DefaultMeshRelativeLocation - FVector(0.f, 0.f, Depth));
}

void UCWSinkholeFallComponent::ResetSinkVisual()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr)
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		Mesh->SetRelativeScale3D(DefaultMeshRelativeScale);
		Mesh->SetRelativeLocation(DefaultMeshRelativeLocation);
		Mesh->SetCastShadow(true);
	}
}

void UCWSinkholeFallComponent::Respawn()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(Owner);
	AGameModeBase* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode() : nullptr;

	// 스테이지가 넓지 않아 스타트 지점 복귀를 시간 손해로 쓴다. (기획서 6-1)
	if (Pawn != nullptr && GameMode != nullptr)
	{
		if (const AActor* StartSpot = GameMode->FindPlayerStart(Pawn->GetController()))
		{
			Owner->SetActorLocationAndRotation(StartSpot->GetActorLocation(), StartSpot->GetActorRotation());
		}
	}

	// 연출 원복은 OnRep_FallLocked 가 맡는다. 서버는 OnRep 이 자동으로 오지 않으므로 직접 부른다.
	bFallLocked = false;
	OnRep_FallLocked();

	bInvincible = true;
	OnRespawned();

	GetWorld()->GetTimerManager().SetTimer(
		InvincibleTimerHandle,
		this,
		&UCWSinkholeFallComponent::ClearInvincibility,
		InvincibleDuration,
		false);
}

void UCWSinkholeFallComponent::ClearInvincibility()
{
	bInvincible = false;
	OnInvincibilityFinished();
}

void UCWSinkholeFallComponent::DropHeldToolAtEntry()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	UCWToolHolderComponent* ToolHolder = Owner->FindComponentByClass<UCWToolHolderComponent>();
	if (ToolHolder == nullptr)
	{
		return;
	}

	// 흡입으로 중앙에 끌려가기 전 위치에 떨어뜨린다.
	// 중앙에 놓이면 구멍 안이라 회수할 수 없다.
	ToolHolder->DropHeldToolAt(EntryLocation);
}
