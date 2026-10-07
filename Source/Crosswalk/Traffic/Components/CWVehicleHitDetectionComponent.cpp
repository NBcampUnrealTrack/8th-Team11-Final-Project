// CWVehicleHitDetectionComponent.cpp

#include "Traffic/Components/CWVehicleHitDetectionComponent.h"

#include "Components/BoxComponent.h"
#include "Data/Structs/CWVehicleHitContext.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/Interfaces/Interface_VehicleHitReceiver.h"
#include "Traffic/CWTrafficSubsystem.h"

UCWVehicleHitDetectionComponent::UCWVehicleHitDetectionComponent()
{
	// 재판정은 차량 Tick 이 부른다. 자기 틱은 없다. (헤더 주석 참고)
	PrimaryComponentTick.bCanEverTick = false;
}

void UCWVehicleHitDetectionComponent::BeginPlay()
{
	Super::BeginPlay();

	// 겹침 자체는 서버·클라 양쪽에서 일어나지만 판정은 서버만 한다. 바인딩은 양쪽에서 하고
	// 핸들러 안에서 권한을 본다 - 여기서 갈라 두면 나중에 클라 연출을 붙일 자리가 없다.
	const ACWBaseVehicle* Vehicle = GetVehicle();
	UBoxComponent* CollisionBox = Vehicle ? Vehicle->GetCollisionBox() : nullptr;
	if (!CollisionBox)
	{
		return;
	}

	CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &UCWVehicleHitDetectionComponent::HandleBeginOverlap);
	CollisionBox->OnComponentEndOverlap.AddDynamic(this, &UCWVehicleHitDetectionComponent::HandleEndOverlap);
}

ACWBaseVehicle* UCWVehicleHitDetectionComponent::GetVehicle() const
{
	return Cast<ACWBaseVehicle>(GetOwner());
}

void UCWVehicleHitDetectionComponent::ResetHitRecords()
{
	HitActors.Reset();
	OverlappingReceivers.Reset();
}

void UCWVehicleHitDetectionComponent::IgnoreWhileOverlapping(AActor* Actor)
{
	const ACWBaseVehicle* Vehicle = GetVehicle();
	const UBoxComponent* CollisionBox = Vehicle ? Vehicle->GetCollisionBox() : nullptr;

	if (Actor && CollisionBox && CollisionBox->IsOverlappingActor(Actor))
	{
		HitActors.Add(Actor);
	}
}

void UCWVehicleHitDetectionComponent::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool bFromSweep, const FHitResult& SweepResult)
{
	const ACWBaseVehicle* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		return;
	}

	// 겹치는 동안 다시 볼 수 있게 기록해 둔다. 인터페이스를 구현한 대상만 담는다.
	// (재검사는 서버만 하므로 서버에서만 채운다)
	if (Vehicle->HasAuthority() && OtherActor && OtherActor != Vehicle
		&& OtherActor->Implements<UInterface_VehicleHitReceiver>())
	{
		OverlappingReceivers.Add(OtherActor);
	}

	TryApplyHit(OtherActor, bFromSweep, SweepResult);
}

void UCWVehicleHitDetectionComponent::RetryPendingHits()
{
	const ACWBaseVehicle* Vehicle = GetVehicle();

	// 피격은 서버만 확정한다.
	if (!Vehicle || !Vehicle->HasAuthority() || OverlappingReceivers.IsEmpty())
	{
		return;
	}

	/**
	 * 순회 중에 TryApplyHit 가 피격을 넣고, 그 수신 측이 넉백으로 대상을 차 밖으로 밀어내
	 * HandleEndOverlap 이 이 집합을 건드릴 수 있다. 사본으로 돌아야 안전하다.
	 */
	TArray<TWeakObjectPtr<AActor>> PendingReceivers = OverlappingReceivers.Array();

	for (const TWeakObjectPtr<AActor>& ReceiverPtr : PendingReceivers)
	{
		AActor* Receiver = ReceiverPtr.Get();
		if (!Receiver)
		{
			// 겹친 채로 사라진 대상. EndOverlap 이 오지 않으므로 여기서 지운다.
			OverlappingReceivers.Remove(ReceiverPtr);
			continue;
		}

		// 이미 친 대상은 TryApplyHit 이 기록으로 걸러낸다. 여기서 다시 볼 것은
		// 아직 못 친 대상뿐이라, 조건이 갖춰지는 순간 그 프레임에 성립한다.
		//
		// 스윕 정보가 없으므로 피격 지점은 최근접점으로 대체된다. (ResolveHitLocation)
		TryApplyHit(Receiver, false, FHitResult());
	}
}

bool UCWVehicleHitDetectionComponent::PassesHitLagCompensation(const AActor* Target) const
{
	const ACWBaseVehicle* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		return false;
	}

	// 판정 주체가 쳐내는 사람이 아니라 맞는 사람이라 대상 폰의 핑으로 되돌린다.
	// 계산과 상한은 패링과 공유한다. 두 판정이 같은 차를 다른 만큼 되돌리면 안 된다.
	const float RewindSeconds = UCWTrafficSubsystem::ResolveViewRewindSeconds(Target);
	if (RewindSeconds <= 0.f)
	{
		return true;
	}

	/**
	 * 되돌릴 거리. 차량은 진행 방향으로 등속 직진하므로, RewindSeconds 전의 차량은
	 * 지금 위치에서 정확히 이만큼 뒤에 있었다. (PredictTimeToImpact 의 랙 보정과 같은 가정)
	 *
	 * 희망 속도가 아니라 실제 속도를 쓴다. 앞차에 막혀 느리게 굴러온 차를 제 속도로
	 * 되돌리면 있지도 않았던 위치까지 물러나 맞아야 할 피격을 흘려보낸다.
	 */
	const float RewindDistance = Vehicle->GetCurrentSpeed() * RewindSeconds;
	if (RewindDistance <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	/**
	 * 차가 대상을 얼마나 파고들었는가. 앞면이 대상에 막 닿은 순간이 0 이고,
	 * 더 지나갈수록 커진다. 기준은 GetVehicleLength 와 같은 스케일 적용 후 크기다.
	 */
	const FVector ToTarget = Target->GetActorLocation() - Vehicle->GetActorLocation();
	const float ForwardDistance = FVector::DotProduct(ToTarget, Vehicle->GetActorForwardVector());
	const float PenetrationDepth = Vehicle->GetVehicleHalfLength() - ForwardDistance;

	/**
	 * 파고든 깊이가 되돌릴 거리보다 얕으면, 되돌린 차의 앞면은 아직 대상 뒤에 있다.
	 * 그 사람 화면에서는 차가 안 왔다는 뜻이라 이번 판정은 흘려보낸다.
	 *
	 * 몸을 빼면 겹침이 끝나면서 영영 안 맞고(피한 것이 맞다), 가만히 있으면 차가 계속
	 * 파고들어 이 조건을 넘는 순간 맞는다. 그 순간이 곧 그 사람 화면에서 차가 닿는 때다.
	 */
	return PenetrationDepth >= RewindDistance;
}

void UCWVehicleHitDetectionComponent::TryApplyHit(AActor* OtherActor, bool bFromSweep, const FHitResult& SweepResult)
{
	const ACWBaseVehicle* Vehicle = GetVehicle();

	// 튕겨나가는 중에도 피격 판정은 살아 있다. 날아간 차가 다른 플레이어를 치는 게
	// 패링이 노리는 그림이다. 쳐낸 사람도 차에서 떨어졌다가 다시 닿으면 똑같이 맞는다.
	if (!Vehicle || !Vehicle->HasAuthority() || !OtherActor || OtherActor == Vehicle)
	{
		return;
	}

	/**
	 * 너무 느린 차는 치지 않는다. 앞차에 막혀 서 있는 차에 걸어 들어간 경우다.
	 *
	 * 여기서 기록(HitActors)을 남기지 않는 것이 중요하다. 남기면 그 차가 다시 출발해도
	 * 이미 판정한 대상으로 걸러져서 영영 맞지 않는다. 대신 RetryPendingHits 가
	 * 겹쳐 있는 동안 매 틱 다시 보므로, 속도가 하한을 넘는 프레임에 그대로 성립한다.
	 */
	if (Vehicle->GetCurrentSpeed() < Vehicle->GetMinHitSpeed())
	{
		return;
	}

	// 피격 인터페이스를 구현한 액터만 대상으로 본다.
	// ACWPlayerCharacter 가 이 인터페이스를 구현하므로 폰을 따로 받아줄 이유가 없다.
	if (!OtherActor->Implements<UInterface_VehicleHitReceiver>())
	{
		return;
	}

	/**
	 * 이 사람의 화면에서도 실제로 닿았는지 본다. 아직이면 이번 판정은 흘려보낸다.
	 *
	 * 기록(HitActors)을 남기지 않는 것이 핵심이다. 남기면 화면에서 곧 닿을 사람도
	 * 이미 판정한 대상으로 걸러져 영영 안 맞는다. 대신 겹쳐 있는 동안 RetryPendingHits 가
	 * 매 틱 다시 보므로, 파고든 깊이가 보정 거리를 넘는 순간에 정상 피격이 성립한다.
	 */
	if (!PassesHitLagCompensation(OtherActor))
	{
		return;
	}

	// 한 번의 접촉이 여러 프레임에 걸쳐 중복으로 들어가지 않게 막는다.
	// 차에서 떨어지면 기록이 지워지므로, 떨어졌다 다시 닿는 건 새로운 피격이다.
	if (HitActors.Contains(OtherActor))
	{
		return;
	}
	HitActors.Add(OtherActor);

	FCWVehicleHitContext HitContext;
	HitContext.HitLocation = ResolveHitLocation(OtherActor, bFromSweep, SweepResult);
	HitContext.HitDirection = Vehicle->GetActorForwardVector();
	// 실제로 내고 있는 속도를 넘긴다. 앞차에 막혀 서 있는 차에 걸어 들어간 경우
	// 희망 속도를 넘기면 전속력으로 치인 것과 같은 값이 된다.
	HitContext.VehicleSpeed = Vehicle->GetCurrentSpeed();

	// 넉백 강도·스턴 시간 산출과 상태 전이는 수신 측(플레이어/물리 파트)이 결정한다.
	IInterface_VehicleHitReceiver::Execute_ReceiveVehicleHit(OtherActor, HitContext);
}

void UCWVehicleHitDetectionComponent::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	const ACWBaseVehicle* Vehicle = GetVehicle();

	// 피격 기록은 서버만 들고 있다.
	if (!Vehicle || !Vehicle->HasAuthority() || !OtherActor)
	{
		return;
	}

	HitActors.Remove(OtherActor);

	// 겹침이 끝났으니 재검사 대상에서도 뺀다. 랙 보정으로 보류된 채 몸을 뺀 대상은
	// 여기서 빠지면서 영영 안 맞게 된다 - 화면에서 피한 것이므로 그게 맞다.
	OverlappingReceivers.Remove(OtherActor);
}

FVector UCWVehicleHitDetectionComponent::ResolveHitLocation(const AActor* HitActor, bool bFromSweep, const FHitResult& SweepResult) const
{
	// 스윕으로 발생한 오버랩이 아니면 SweepResult는 비어 있다.
	// 차량은 SetActorTransform으로 위치를 덮어쓰므로 대부분 이 경우다.
	if (bFromSweep)
	{
		return SweepResult.ImpactPoint;
	}

	const ACWBaseVehicle* Vehicle = GetVehicle();
	const FVector VehicleLocation = Vehicle ? Vehicle->GetActorLocation() : FVector::ZeroVector;
	const FVector TargetLocation = HitActor ? HitActor->GetActorLocation() : VehicleLocation;

	const UBoxComponent* CollisionBox = Vehicle ? Vehicle->GetCollisionBox() : nullptr;
	FVector ClosestPoint = FVector::ZeroVector;
	if (CollisionBox && CollisionBox->GetClosestPointOnCollision(TargetLocation, ClosestPoint) >= 0.f)
	{
		return ClosestPoint;
	}

	// 대상이 이미 박스 안쪽이면 최근접점을 못 구한다. 그때는 대상 위치가 곧 접점에 가깝다.
	return TargetLocation;
}
