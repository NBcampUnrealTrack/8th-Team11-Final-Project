// Copyright Crosswalk Team. All Rights Reserved.
// CWParryComponent.cpp

#include "Traffic/Components/CWParryComponent.h"

#include "GameFramework/Actor.h"
#include "Traffic/BaseVehicle.h"
#include "Traffic/TrafficSubsystem.h"

UCWParryComponent::UCWParryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCWParryComponent::RequestParry(const FVector& CursorWorldLocation)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !CanUseLocalCooldown())
	{
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

	UpdateServerCooldown();

	UTrafficSubsystem* TrafficSubsystem = UTrafficSubsystem::Get(OwnerActor);
	if (TrafficSubsystem == nullptr)
	{
		return;
	}

	// 어느 차를 칠지 고르고 판정하는 건 트래픽 쪽이다. 여기서는 시도 정보만 만들어 넘긴다.
	const FCWParryAttemptContext ParryAttemptContext = BuildParryAttemptContext(*OwnerActor, CursorWorldLocation, ClientInputTime);

	ABaseVehicle* ParriedVehicle = TrafficSubsystem->TryParryWithAttempt(ParryAttemptContext);
	if (ParriedVehicle == nullptr)
	{
		return;
	}

	// 차량을 커서 방향으로 튕겨내고 2초 뒤 제거하는 것까지 ABaseVehicle::TryParry가 끝냈다.
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
	ParryAttemptContext.AttemptDirection = ToCursor.IsNearlyZero()
		? Parrier.GetActorForwardVector().GetSafeNormal2D()
		: ToCursor;

	// 대상 차량과 차종은 서버가 골라 채운다. 클라가 지정해 보내면 서버가 다시 검증한다.

	return ParryAttemptContext;
}

void UCWParryComponent::Multicast_ParrySucceeded_Implementation(const FCWParrySyncData& ParrySyncData)
{
	OnParrySucceeded.Broadcast(ParrySyncData);
}

FCWParrySyncData UCWParryComponent::BuildParrySyncData(const FCWParryAttemptContext& ParryAttemptContext, ABaseVehicle& ParriedVehicle) const
{
	FCWParrySyncData ParrySyncData;
	ParrySyncData.TargetVehicle = &ParriedVehicle;
	ParrySyncData.VehicleType = ParriedVehicle.GetVehicleType();
	ParrySyncData.bParrySucceeded = true;

	// 연출 기준점은 클라이언트가 보낸 커서가 아니라 서버가 확정한 차량 위치다.
	ParrySyncData.ParryLocation = ParriedVehicle.GetActorLocation();

	// 차량이 튕겨나가는 방향 = 커서 방향. (기획서 8.1)
	// 차량은 이미 이 방향으로 회전한 뒤이므로 실제 진행 방향과 같은 값이다.
	ParrySyncData.ParryDirection = ParryAttemptContext.AttemptDirection;

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