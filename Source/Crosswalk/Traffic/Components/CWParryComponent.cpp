// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.cpp

#include "Traffic/Components/CWParryComponent.h"

#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficSubsystem.h"

UCWParryComponent::UCWParryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWParryComponent::RequestParry(const FVector& CursorWorldLocation)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	if (!CanUseLocalCooldown())
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry] 로컬 쿨타임에 막힘. 남은 시간 %.2f초"),
			ParryCooldown - (GetWorld()->GetTimeSeconds() - LastLocalParryRequestTime));
		return;
	}

	UpdateLocalCooldown();

	// 서버 판정에는 쓰지 않고 기록만 한다. 클라 월드 시간이라 서버 시간과 비교할 수 없다.
	const UWorld* World = GetWorld();
	const float ClientInputTime = World ? World->GetTimeSeconds() : 0.f;

	if (OwnerActor->HasAuthority())
	{
		HandleServerParryRequest(CursorWorldLocation, ClientInputTime);
		return;
	}

	Server_RequestParry(CursorWorldLocation, ClientInputTime);
}

float UCWParryComponent::GetCooldownReadyRatio() const
{
	// 쿨타임을 사용하지 않으면 항상 패링 가능 상태로 처리
	if (ParryCooldown <= 0.f)
	{
		return 1.f;
	}

	const UWorld* World = GetWorld();

	// 월드 시간을 읽을 수 없는 초기화 구간에서는 사용 가능 상태로 처리
	if (!IsValid(World))
	{
		return 1.f;
	}

	// UI는 로컬 입력 쿨타임을 표시해야 하므로 서버 기록이 아닌
	// LastLocalParryRequestTime을 기준으로 준비 비율을 계산
	const float Elapsed =
		World->GetTimeSeconds() - LastLocalParryRequestTime;

	return FMath::Clamp(
		Elapsed / ParryCooldown,
		0.f,
		1.f
	);
}

void UCWParryComponent::Server_RequestParry_Implementation(FVector_NetQuantize CursorWorldLocation, float ClientInputTime)
{
	HandleServerParryRequest(CursorWorldLocation, ClientInputTime);
}

void UCWParryComponent::HandleServerParryRequest(const FVector& CursorWorldLocation, float ClientInputTime)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || !CanUseServerCooldown())
	{
		return;
	}

	// 다운(래그돌) 상태에서는 패링을 받지 않는다.
	// 클라이언트 쪽 IsHitLocked() 체크만으로는, 피격과 패링 입력이 거의 동시에 도착했을 때
	// 서버가 이미 히트락을 건 뒤에도 이 요청을 그대로 승인해버리는 문제가 있었다.
	if (const UCWVehicleHitReactionComponent* HitReactionComponent = OwnerActor->FindComponentByClass<UCWVehicleHitReactionComponent>())
	{
		if (HitReactionComponent->IsHitLocked())
		{
			UE_LOG(LogTemp, Log, TEXT("[Parry] 히트락 상태라 서버 판정을 거부합니다."));
			return;
		}
	}

	UpdateServerCooldown();

	UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(OwnerActor);
	if (TrafficSubsystem == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Parry] TrafficSubsystem 을 찾지 못했습니다."));
		return;
	}

	// 어느 차를 칠지 고르고 판정하는 건 트래픽 쪽이다. 여기서는 시도 정보만 만들어 넘긴다.
	const FCWParryAttemptContext ParryAttemptContext = BuildParryAttemptContext(*OwnerActor, CursorWorldLocation, ClientInputTime);

	// 보정량은 컨텍스트에 들어간 값을 그대로 찍는다. 따로 다시 구하면 두 값이 갈릴 수 있다.
	UE_LOG(LogTemp, Log, TEXT("[Parry] 서버 판정 시작. 활성 차량 %d대, 반경 %.0f, 각도 %.0f, 랙 보정 %.0fms"),
		TrafficSubsystem->GetActiveVehicleCount(), ParryRadius, ParryAngle,
		ParryAttemptContext.ViewRewindSeconds * 1000.f);

	ACWBaseVehicle* ParriedVehicle = TrafficSubsystem->TryParryWithAttempt(ParryAttemptContext);
	if (ParriedVehicle == nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("[Parry] 판정 실패. 조건을 통과한 차량이 없습니다."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Parry] 판정 성공. 대상=%s"), *ParriedVehicle->GetName());

	// 차량을 커서 방향으로 튕겨내고 2초 뒤 제거하는 것까지 ACWBaseVehicle::TryParry가 끝냈다.
	// 여기서는 서버가 확정한 결과만 연출용으로 뿌린다. 실패는 보내지 않는다.
	Multicast_ParrySucceeded(BuildParrySyncData(ParryAttemptContext, *ParriedVehicle));
}

FCWParryAttemptContext UCWParryComponent::BuildParryAttemptContext(AActor& Parrier, const FVector& CursorWorldLocation, float ClientInputTime) const
{
	FCWParryAttemptContext ParryAttemptContext;
	ParryAttemptContext.Parrier = &Parrier;
	ParryAttemptContext.AttemptLocation = Parrier.GetActorLocation();
	ParryAttemptContext.ParryRadius = ParryRadius;
	ParryAttemptContext.ParryAngle = ParryAngle;
	ParryAttemptContext.ClientInputTime = ClientInputTime;

	// 쿼터뷰라 커서는 바닥 평면 위에 찍힌다. 높이 차를 빼야 방향 판정이 흔들리지 않는다.
	const FVector ToCursor = (CursorWorldLocation - ParryAttemptContext.AttemptLocation).GetSafeNormal2D();

	/**
	 * 커서가 플레이어 위치와 사실상 같으면 트레이스가 빗나간 것이다.
	 * (ResolveCursorWorldLocation 이 실패 시 플레이어 위치를 그대로 돌려준다)
	 *
	 * 이 값을 목표 지점으로 쓰면 차가 플레이어를 향해 날아오므로 구분해 둔다.
	 */
	ParryAttemptContext.bHasCursorTarget = !ToCursor.IsNearlyZero();
	ParryAttemptContext.CursorWorldLocation = CursorWorldLocation;

	ParryAttemptContext.AttemptDirection = ParryAttemptContext.bHasCursorTarget
		? ToCursor
		: Parrier.GetActorForwardVector().GetSafeNormal2D();

	// 이 사람이 화면에서 본 상황으로 되돌려 판정하도록 보정량을 실어 보낸다.
	ParryAttemptContext.ViewRewindSeconds = ResolveViewRewindSeconds();

	// 대상 차량과 차종은 서버가 골라 채운다. 클라가 지정해 보내면 서버가 다시 검증한다.

	return ParryAttemptContext;
}

float UCWParryComponent::ResolveViewRewindSeconds() const
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return 0.f;
	}

	// 호스트가 직접 조종하는 캐릭터는 되돌릴 것이 없다. 자기가 보는 것이 곧 서버 상황이다.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (OwnerPawn == nullptr || OwnerPawn->IsLocallyControlled())
	{
		return 0.f;
	}

	const APlayerState* OwnerPlayerState = OwnerPawn->GetPlayerState();
	if (OwnerPlayerState == nullptr)
	{
		return 0.f;
	}

	/**
	 * 되돌릴 시간은 왕복 시간(ping) 전체다. 절반이 아니다.
	 *
	 * 클라이언트 화면의 차량 위치는 서버가 보낸 값을 이어받아 그린 것이라 편도만큼 뒤처져 있다.
	 * 그 화면을 보고 누른 입력이 서버에 닿는 데 다시 편도가 걸린다.
	 * 그래서 서버의 지금 상황과 그 사람이 본 상황의 차이는 두 편도를 합한 값, 즉 왕복 시간이다.
	 *
	 * ExactPing 은 서버에서만 정확하다. GetPingInMilliseconds 가 서버에서 그 값을 돌려준다.
	 */
	const float PingSeconds = OwnerPlayerState->GetPingInMilliseconds() * 0.001f;

	return FMath::Clamp(PingSeconds, 0.f, MaxViewRewindSeconds);
}

void UCWParryComponent::Multicast_ParrySucceeded_Implementation(const FCWParrySyncData& ParrySyncData)
{
	OnParrySucceeded.Broadcast(ParrySyncData);
}

FCWParrySyncData UCWParryComponent::BuildParrySyncData(const FCWParryAttemptContext& ParryAttemptContext, ACWBaseVehicle& ParriedVehicle) const
{
	FCWParrySyncData ParrySyncData;
	ParrySyncData.TargetVehicle = &ParriedVehicle;
	ParrySyncData.VehicleType = ParriedVehicle.GetVehicleType();
	ParrySyncData.bParrySucceeded = true;

	// 연출 기준점은 클라이언트가 보낸 커서가 아니라 서버가 확정한 차량 위치다.
	ParrySyncData.ParryLocation = ParriedVehicle.GetActorLocation();

	/**
	 * 차량이 실제로 날아가는 방향. 회전이 이미 끝난 뒤라 차량의 정면이 곧 진행 방향이다.
	 *
	 * AttemptDirection(플레이어 → 커서)을 쓰면 안 된다. 차량은 자기 위치에서 커서 지점을
	 * 향하므로 두 값이 다르다. 연출은 차가 실제로 가는 쪽을 따라가야 한다.
	 */
	ParrySyncData.ParryDirection = ParriedVehicle.GetActorForwardVector().GetSafeNormal2D();

	// 연출 강도는 차량이 튕겨나가는 속도를 그대로 넘긴다. 세기 환산은 수신 측이 정한다.
	ParrySyncData.VehicleReactionStrength = ParriedVehicle.GetEffectiveSpeed();

	return ParrySyncData;
}

bool UCWParryComponent::CanUseLocalCooldown() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastLocalParryRequestTime >= ParryCooldown;
}

bool UCWParryComponent::CanUseServerCooldown() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastServerParryRequestTime >= ParryCooldown;
}

void UCWParryComponent::UpdateLocalCooldown()
{
	if (const UWorld* World = GetWorld())
	{
		LastLocalParryRequestTime = World->GetTimeSeconds();
	}
}

void UCWParryComponent::UpdateServerCooldown()
{
	if (const UWorld* World = GetWorld())
	{
		LastServerParryRequestTime = World->GetTimeSeconds();
	}
}
