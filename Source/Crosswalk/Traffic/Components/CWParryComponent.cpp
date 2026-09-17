// CWParryComponent.cpp

#include "Traffic/Components/CWParryComponent.h"

#include "Characters/Components/CWVehicleHitReactionComponent.h"
#include "Data/Assets/CWParryPropCatalog.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficSubsystem.h"
#include "Traffic/CWTrafficLog.h"

/**
 * 패링 서버 판정을 한 구간으로 잰다.
 *
 * 프레임 예산표의 복제·판정 배정(0.5ms)에 패링이 빠져 있었다. 그 칸의 실측은 래그돌
 * 동기화만 잡은 값이라, 패링이 더해지면 배정이 다시 잡혀야 한다.
 *
 * 쪼개지 않고 HandleServerParryRequest 전체를 한 구간으로 두는 이유는, 그 표가 파트
 * 단위 합계를 쓰기 때문이다. 후보 탐색·방향 계산·멀티캐스트 중 어디가 비싼지는 이 값이
 * 배정을 위협할 때 쪼개서 보면 된다. 지금 쪼개면 표에 넣을 수 없는 숫자가 세 개 늘어난다.
 *
 * stat 과 CSV 를 같이 내보낸다. 표의 다른 수치가 CSV 캡처로 나온 값이라 CSV 컬럼이
 * 없으면 같은 표에 넣을 수 없고, 에디터에서 바로 보려면 stat 이 필요하다.
 * 둘 다 Shipping 에서는 컴파일되지 않으므로 최종 빌드 비용은 0 이다.
 */
DECLARE_STATS_GROUP(TEXT("CWTraffic"), STATGROUP_CWTraffic, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Parry ServerJudge"), STAT_CWParryServerJudge, STATGROUP_CWTraffic);
CSV_DEFINE_CATEGORY(CWTraffic, true);

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
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 로컬 쿨타임에 막힘. 남은 시간 %.2f초"),
			ParryCooldown - (GetWorld()->GetTimeSeconds() - LastLocalParryRequestTime));
		return;
	}

	UpdateLocalCooldown();

	if (OwnerActor->HasAuthority())
	{
		HandleServerParryRequest(CursorWorldLocation);
		return;
	}

	Server_RequestParry(CursorWorldLocation);
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

void UCWParryComponent::Server_RequestParry_Implementation(FVector_NetQuantize CursorWorldLocation)
{
	HandleServerParryRequest(CursorWorldLocation);
}

void UCWParryComponent::HandleServerParryRequest(const FVector& CursorWorldLocation)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	// 권한 확인 뒤에 잰다. 클라이언트에서 불려 바로 빠지는 호출까지 표본에 넣으면
	// 프레임 합계는 그대로인데 "판정 한 번의 비용" 으로 읽을 때 값이 희석된다.
	SCOPE_CYCLE_COUNTER(STAT_CWParryServerJudge);
	CSV_SCOPED_TIMING_STAT(CWTraffic, ParryServerJudge);

	/**
	 * 서버 쿨타임에 걸린 것은 환불하지 않는다.
	 *
	 * 되돌려주면 클라이언트가 곧바로 다시 보내고 다시 걸리는 것을 반복해서 요청만 계속 오간다.
	 * 그리고 이 경우는 실제로 쿨타임이 남아 있는 것이 맞아서, 되돌려줄 것 자체가 없다.
	 */
	if (!CanUseServerCooldown())
	{
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 서버 쿨타임에 막힘."));
		return;
	}

	// 다운(래그돌) 상태에서는 패링을 받지 않는다.
	// 클라이언트 쪽 IsHitLocked() 체크만으로는, 피격과 패링 입력이 거의 동시에 도착했을 때
	// 서버가 이미 히트락을 건 뒤에도 이 요청을 그대로 승인해버리는 문제가 있었다.
	if (const UCWVehicleHitReactionComponent* HitReactionComponent = OwnerActor->FindComponentByClass<UCWVehicleHitReactionComponent>())
	{
		if (HitReactionComponent->IsHitLocked())
		{
			// 타이밍을 놓친 것이 아니라 서버 상태 때문에 못 받은 것이므로 쿨타임을 되돌려준다.
			UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 히트락 상태라 서버 판정을 거부합니다. 쿨타임을 되돌려줍니다."));
			RejectParry();
			return;
		}
	}

	UCWTrafficSubsystem* TrafficSubsystem = UCWTrafficSubsystem::Get(OwnerActor);
	if (TrafficSubsystem == nullptr)
	{
		UE_LOG(LogCWTraffic, Warning, TEXT("[Parry] TrafficSubsystem 을 찾지 못했습니다. 쿨타임을 되돌려줍니다."));
		RejectParry();
		return;
	}

	// 어느 차를 칠지 고르고 판정하는 건 트래픽 쪽이다. 여기서는 시도 정보만 만들어 넘긴다.
	const FCWParryAttemptContext ParryAttemptContext = BuildParryAttemptContext(*OwnerActor, CursorWorldLocation);

	// 보정량은 컨텍스트에 들어간 값을 그대로 찍는다. 따로 다시 구하면 두 값이 갈릴 수 있다.
	UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 서버 판정 시작. 활성 차량 %d대, 반경 %.0f, 각도 %.0f, 랙 보정 %.0fms"),
		TrafficSubsystem->GetActiveVehicleCount(), ParryRadius, ParryAngle,
		ParryAttemptContext.ViewRewindSeconds * 1000.f);

	ACWBaseVehicle* ParriedVehicle = TrafficSubsystem->TryParryWithAttempt(ParryAttemptContext);

	/**
	 * 쿨타임은 소모가 확정된 뒤에 갱신한다.
	 *
	 * 실패를 환불하는 설정에서 미리 갱신해 두면, 환불받은 클라이언트가 곧바로 다시 보내도
	 * 서버 쿨타임에 걸려 거절되고 그때는 환불도 되지 않는다. 그러면 환불 자체가 무의미해진다.
	 */
	if (ParriedVehicle != nullptr || !bRefundCooldownOnMiss)
	{
		UpdateServerCooldown();
	}

	if (ParriedVehicle == nullptr)
	{
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 판정 실패. 조건을 통과한 차량이 없습니다.%s"),
			bRefundCooldownOnMiss ? TEXT(" 쿨타임을 되돌려줍니다.") : TEXT(""));

		if (bRefundCooldownOnMiss)
		{
			RejectParry();
		}

		return;
	}

	UE_LOG(LogCWTraffic, Log, TEXT("[Parry] 판정 성공. 대상=%s"), *ParriedVehicle->GetName());

	// 차량을 커서 방향으로 튕겨내고 2초 뒤 제거하는 것까지 ACWBaseVehicle::TryParry가 끝냈다.
	// 여기서는 서버가 확정한 결과만 연출용으로 뿌린다. 실패는 보내지 않는다.
	Multicast_ParrySucceeded(BuildParrySyncData(ParryAttemptContext, *ParriedVehicle));
}

FCWParryAttemptContext UCWParryComponent::BuildParryAttemptContext(AActor& Parrier, const FVector& CursorWorldLocation) const
{
	FCWParryAttemptContext ParryAttemptContext;
	ParryAttemptContext.Parrier = &Parrier;
	ParryAttemptContext.AttemptLocation = Parrier.GetActorLocation();
	ParryAttemptContext.ParryRadius = ParryRadius;
	ParryAttemptContext.ParryAngle = ParryAngle;

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
	// 계산과 상한은 피격 판정과 공유한다. 두 판정이 같은 차를 다른 만큼 되돌리면 안 된다.
	ParryAttemptContext.ViewRewindSeconds = UCWTrafficSubsystem::ResolveViewRewindSeconds(&Parrier);

	// 대상 차종은 서버가 선별을 끝낸 뒤 채운다. 여기서는 비워 둔다.

	return ParryAttemptContext;
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

	/**
	 * 이번 스윙에 쓸 소품을 여기서 한 번만 뽑는다.
	 *
	 * 이 함수는 서버에서만 불린다(HandleServerParryRequest 안). 뽑은 값이 아래 멀티캐스트에
	 * 그대로 실려 나가므로 모든 머신이 같은 소품을 본다. 클라이언트가 각자 뽑으면 화면마다
	 * 다른 소품이 나오고 사운드까지 어긋난다.
	 *
	 * 카탈로그가 비었으면 INDEX_NONE 이 들어가고, 받는 쪽은 소품 없이 스윙만 재생한다.
	 */
	ParrySyncData.PropIndex = (PropCatalog != nullptr) ? PropCatalog->PickRandomPropIndex() : INDEX_NONE;

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

void UCWParryComponent::RejectParry()
{
	// 호스트가 조종하는 캐릭터면 이 Client RPC 가 그 자리에서 바로 실행된다.
	// 원격 클라이언트면 소유 연결로만 나간다.
	Client_ParryRejected();
}

void UCWParryComponent::Client_ParryRejected_Implementation()
{
	ResetLocalCooldown();
}

void UCWParryComponent::ResetLocalCooldown()
{
	// 초기값과 같은 의미다. 월드 시간은 항상 이보다 크므로 곧바로 사용 가능 상태가 된다.
	// GetCooldownReadyRatio 도 이 값을 보므로 UI 가 함께 되돌아간다.
	LastLocalParryRequestTime = -999.f;
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
