// CWTrafficSubsystem.cpp

#include "CWTrafficSubsystem.h"

#include "Engine/World.h"
#include "CWBaseVehicle.h"
#include "CWTrafficDirector.h"
#include "CWTrafficLane.h"
#include "CWTrafficMath.h"
#include "CWTrafficStopLine.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Traffic/CWTrafficLog.h"

namespace
{
	/**
	 * 대수 대조 주기. 값을 노출하지 않는다.
	 *
	 * 4대의 로그를 시각으로 맞춰 읽는 것이 이 로그의 용도라, 주기가 사람마다 다르면
	 * 맞춰 볼 수가 없다. 조절할 수 있어야 하는 것은 주기가 아니라 켜짐 여부다.
	 */
	constexpr float TrafficNetCensusInterval = 1.f;

	/**
	 * 랙 보정으로 차량을 되돌릴 수 있는 상한(초). 패링과 피격이 같이 쓴다.
	 *
	 * 보정을 하지 않으면 핑이 있는 사람은 패링 윈도우(0.3초)의 뒷부분을 통째로 잃고,
	 * 화면에서 분명히 피한 피격을 서버에서는 맞는다. 그렇다고 무제한으로 되돌리면
	 * 다른 사람 화면에서 한참 전에 지나간 차가 갑자기 튕겨나가거나 뒤늦게 누굴 친다.
	 * 되돌린 만큼은 남들이 보기에 과거를 고치는 것이다.
	 *
	 * 0.2 인 이유: 기본 속도 750cm/s 로 150cm, 차 길이(440cm)의 3분의 1 정도다.
	 * 그 정도면 쳐낸 사람과 차가 아직 겹쳐 보이는 범위라 남들 화면에서도 납득이 된다.
	 * 랜 환경 핑은 대부분 이 밑이라 실제로는 거의 잘리지 않는다.
	 *
	 * BP 프로퍼티로 두지 않는다. 예전에는 차량과 패링 컴포넌트가 각자 들고 "같게 맞춰라" 는
	 * 주석으로 버텼는데, BP 에서 한쪽만 바꾸면 컴파일도 로그도 잡지 못한 채 두 판정이 갈린다.
	 * 값이 둘일 수 없는 물건이라 코드에 하나만 둔다.
	 */
	constexpr float TrafficMaxViewRewindSeconds = 0.2f;
}

float UCWTrafficSubsystem::ResolveViewRewindSeconds(const AActor* Viewer)
{
	// 핑은 서버에서만 정확하다. 클라이언트에서 부르면 되돌릴 근거 자체가 없다.
	if (Viewer == nullptr || !Viewer->HasAuthority())
	{
		return 0.f;
	}

	/**
	 * 호스트가 직접 조종하는 폰은 되돌리지 않는다. 자기가 보는 것이 곧 서버 상황이라
	 * 어긋남 자체가 없고, 되돌리면 오히려 맞아야 할 피격을 흘려보낸다.
	 */
	const APawn* ViewerPawn = Cast<APawn>(Viewer);
	if (ViewerPawn == nullptr || ViewerPawn->IsLocallyControlled())
	{
		return 0.f;
	}

	const APlayerState* ViewerPlayerState = ViewerPawn->GetPlayerState();
	if (ViewerPlayerState == nullptr)
	{
		return 0.f;
	}

	// ExactPing 은 서버에서만 정확하다. GetPingInMilliseconds 가 서버에서 그 값을 돌려준다.
	const float PingSeconds = ViewerPlayerState->GetPingInMilliseconds() * 0.001f;

	return FMath::Clamp(PingSeconds, 0.f, TrafficMaxViewRewindSeconds);
}

void UCWTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	/**
	 * 콘솔 변수를 여기서 읽지 않고 타이머는 무조건 건다.
	 *
	 * 시작할 때 한 번 읽어 버리면 판이 도는 도중에 끌 수가 없다. 진단 로그는 필요할 때
	 * 켜고 시끄러우면 끄는 물건이라, 켜짐 판정은 매번 안에서 한다. 꺼져 있을 때 드는
	 * 비용은 1초에 콘솔 변수 한 번 읽는 것이 전부다.
	 */
	InWorld.GetTimerManager().SetTimer(
		CensusTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Diagnostics.LogVehicleCensus(GetWorld(), ActiveVehicles);
		}),
		TrafficNetCensusInterval,
		true);
}

void UCWTrafficSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CensusTimerHandle);
	}

	Super::Deinitialize();
}

UCWTrafficSubsystem* UCWTrafficSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UCWTrafficSubsystem>() : nullptr;
}

void UCWTrafficSubsystem::RegisterVehicle(ACWBaseVehicle* Vehicle)
{
	if (Vehicle)
	{
		ActiveVehicles.AddUnique(Vehicle);
	}
}

void UCWTrafficSubsystem::UnregisterVehicle(ACWBaseVehicle* Vehicle)
{
	// 죽은 약참조도 같이 걷어낸다. 제어기·정지선 등록부와 같은 규칙이다 - 세 등록부가
	// 서로 다르게 정리하면 "왜 이 목록만 빈 칸이 남나" 를 따로 따져야 한다.
	ActiveVehicles.RemoveAllSwap([Vehicle](const TWeakObjectPtr<ACWBaseVehicle>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Vehicle;
	});
}

ACWBaseVehicle* UCWTrafficSubsystem::FindVehicleAhead(const ACWBaseVehicle* Vehicle) const
{
	const ACWTrafficLane* Lane = Vehicle ? Vehicle->GetCurrentLane() : nullptr;
	if (!Lane)
	{
		return nullptr;
	}

	const float MyDistance = Vehicle->GetDistanceAlongLane();

	ACWBaseVehicle* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Other = VehiclePtr.Get();
		if (!Other || Other == Vehicle || Other->GetCurrentLane() != Lane)
		{
			continue;
		}

		// 나보다 앞서 있는 차량 중 가장 가까운 것.
		const float OtherDistance = Other->GetDistanceAlongLane();
		if (OtherDistance > MyDistance && OtherDistance < NearestDistance)
		{
			NearestDistance = OtherDistance;
			Nearest = Other;
		}
	}

	return Nearest;
}

ACWBaseVehicle* UCWTrafficSubsystem::TryParryWithAttempt(const FCWParryAttemptContext& ParryAttemptContext)
{
	// 시도한 액터가 없으면 충돌 예측 기준점이 없어 판정 자체가 성립하지 않는다.
	if (ParryAttemptContext.Parrier == nullptr)
	{
		return nullptr;
	}

	// 대상은 항상 서버가 고른다. 클라이언트가 차량을 지정해 보내는 경로는 없다 -
	// 그러면 선별을 클라가 대신하는 셈이고, 어느 차가 맞았는지를 서버가 보증할 수 없다.
	ACWBaseVehicle* BestVehicle = nullptr;
	float BestTimeToImpact = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Vehicle = VehiclePtr.Get();

		float TimeToImpact = 0.f;
		if (!Vehicle || !PassesAttemptFilter(*Vehicle, ParryAttemptContext, TimeToImpact))
		{
			continue;
		}

		// 여러 대가 조건에 걸리면 가장 먼저 닿을 차를 친다.
		if (TimeToImpact < BestTimeToImpact)
		{
			BestTimeToImpact = TimeToImpact;
			BestVehicle = Vehicle;
		}
	}

	if (BestVehicle == nullptr)
	{
		return nullptr;
	}

	// 최종 판정은 고른 차량이 내린다. 인터페이스를 거치지 않는다 - 구현체가 차량 하나뿐이다.
	if (!BestVehicle->ReceiveParryAttempt(ParryAttemptContext))
	{
		return nullptr;
	}

	return BestVehicle;
}

bool UCWTrafficSubsystem::PassesAttemptFilter(ACWBaseVehicle& Vehicle, const FCWParryAttemptContext& ParryAttemptContext, float& OutTimeToImpact) const
{
	OutTimeToImpact = 0.f;

	if (!Vehicle.CanBeParried())
	{
		UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 패링 불가 차종"), *Vehicle.GetName());
		return false;
	}

	// 손이 닿는 거리. 차체 표면 기준이라 차 길이가 길어도 판정이 불리해지지 않는다.
	//
	// 이 거리는 랙 보정을 하지 않고 서버의 지금 위치로 잰다. 기본값이 0(제한 없음)이라
	// 판정에 참여하지 않고, 되돌릴 거리도 윈도우 판정보다 훨씬 작다. 반경을 실제로
	// 쓰기로 하면 여기도 되돌린 위치로 재야 한다.
	const float SurfaceDistance = Vehicle.GetDistanceToPoint(ParryAttemptContext.AttemptLocation);
	if (ParryAttemptContext.ParryRadius > 0.f && SurfaceDistance > ParryAttemptContext.ParryRadius)
	{
		UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 반경 밖 (거리 %.0f > %.0f)"),
			*Vehicle.GetName(), SurfaceDistance, ParryAttemptContext.ParryRadius);
		return false;
	}

	// 커서가 향한 쪽만 친다. 쿼터뷰라 높이 차이는 보지 않는다.
	const FVector AttemptDirection = ParryAttemptContext.AttemptDirection.GetSafeNormal2D();
	const FVector ToVehicle = (Vehicle.GetActorLocation() - ParryAttemptContext.AttemptLocation).GetSafeNormal2D();
	if (!AttemptDirection.IsNearlyZero() && !ToVehicle.IsNearlyZero()
		&& !CWTrafficMath::IsInsideCone(ToVehicle, AttemptDirection, ParryAttemptContext.ParryAngle))
	{
		UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 각도 밖 (커서와 %.0f도 차이, 허용 %.0f도)"),
			*Vehicle.GetName(), CWTrafficMath::AngleBetweenDegrees(AttemptDirection, ToVehicle),
			FMath::Clamp(ParryAttemptContext.ParryAngle, 0.f, 360.f) * 0.5f);
		return false;
	}

	// 충돌 직전 윈도우 안이어야 한다. 윈도우 값은 차량이 단일 관리한다.
	// 시도한 사람이 화면에서 본 시점으로 되돌려 판정한다. (ViewRewindSeconds)
	float TimeToImpact = 0.f;
	if (!Vehicle.PredictTimeToImpact(ParryAttemptContext.Parrier, TimeToImpact, ParryAttemptContext.ViewRewindSeconds))
	{
		// 진행 경로 밖이거나 이미 지나친 차량이다. 도로 위에 서 있지 않으면 여기서 전부 걸린다.
		UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 진행 경로 밖 (또는 이미 지나침)"), *Vehicle.GetName());
		return false;
	}

	if (TimeToImpact > Vehicle.GetParryWindow())
	{
		UE_LOG(LogCWTraffic, Verbose, TEXT("[Parry]   %s: 타이밍 이릅 (충돌까지 %.2f초 > 윈도우 %.2f초, 보정 %.0fms)"),
			*Vehicle.GetName(), TimeToImpact, Vehicle.GetParryWindow(), ParryAttemptContext.ViewRewindSeconds * 1000.f);
		return false;
	}

	OutTimeToImpact = TimeToImpact;

	return true;
}

void UCWTrafficSubsystem::GetVehiclesNearPoint(const FVector& Point, float Radius, TArray<ACWBaseVehicle*>& OutVehicles) const
{
	OutVehicles.Reset();

	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		ACWBaseVehicle* Vehicle = VehiclePtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		if (Vehicle->GetDistanceToPoint(Point) <= Radius)
		{
			OutVehicles.Add(Vehicle);
		}
	}
}

void UCWTrafficSubsystem::RegisterSignalDirector(ACWTrafficDirector* Director)
{
	if (Director)
	{
		SignalDirectors.AddUnique(Director);
	}
}

void UCWTrafficSubsystem::UnregisterSignalDirector(ACWTrafficDirector* Director)
{
	SignalDirectors.RemoveAllSwap([Director](const TWeakObjectPtr<ACWTrafficDirector>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Director;
	});
}

void UCWTrafficSubsystem::RegisterStopLine(ACWTrafficStopLine* StopLine)
{
	if (StopLine)
	{
		StopLines.AddUnique(StopLine);
	}
}

void UCWTrafficSubsystem::UnregisterStopLine(ACWTrafficStopLine* StopLine)
{
	StopLines.RemoveAllSwap([StopLine](const TWeakObjectPtr<ACWTrafficStopLine>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == StopLine;
	});
}

bool UCWTrafficSubsystem::FindActiveStopLine(const ACWTrafficLane* Lane, float FromDistance, float& OutStopLineDistance) const
{
	if (!Lane)
	{
		return false;
	}

	/**
	 * 두 가지를 각자 다른 곳에 물어본다.
	 *
	 *   "여기가 정지선이다"      -> 정지선 액터 (차선에 붙는다)
	 *   "지금 빨간불인가"        -> 신호 제어기 (페이즈를 돌린다)
	 *
	 * 정지선은 차선의 속성이고 신호는 페이즈의 속성이라 소유자가 다르다.
	 * 둘을 한 곳에 묶으면 같은 차선이 여러 페이즈에 걸릴 때 정지선이 중복된다.
	 */
	bool bLaneControlled = false;
	for (const TWeakObjectPtr<ACWTrafficDirector>& DirectorPtr : SignalDirectors)
	{
		const ACWTrafficDirector* Director = DirectorPtr.Get();
		if (!Director || !Director->ControlsLane(Lane))
		{
			continue;
		}

		// 통행 신호(초록·황색)면 세우지 않는다. 제어기가 둘이면 하나라도 통행이면 지나간다.
		if (Director->IsLanePassable(Lane))
		{
			LogStopLineMiss(Lane, TEXT("제어기가 통행 신호라고 답함"));
			return false;
		}

		bLaneControlled = true;
	}

	// 신호가 없는 차선에 정지선만 놓아도 세우지 않는다. 세울 근거가 없다.
	if (!bLaneControlled)
	{
		LogStopLineMiss(Lane, TEXT("이 차선을 통제하는 제어기 없음"));
		return false;
	}

	bool bFound = false;
	float Nearest = 0.f;

	for (const TWeakObjectPtr<ACWTrafficStopLine>& StopLinePtr : StopLines)
	{
		const ACWTrafficStopLine* StopLine = StopLinePtr.Get();
		if (!StopLine || !StopLine->HasLane(Lane))
		{
			continue;
		}

		const float Distance = StopLine->GetDistanceAlongLane(Lane);

		// 이미 지난 정지선은 뒤로 끌어당기지 않는다. 지나갔으면 그냥 가는 것이 맞다.
		if (Distance <= FromDistance)
		{
			continue;
		}

		if (!bFound || Distance < Nearest)
		{
			Nearest = Distance;
			bFound = true;
		}
	}

	OutStopLineDistance = Nearest;

	if (!bFound)
	{
		LogStopLineMiss(Lane, TEXT("앞쪽에 이 차선의 정지선 없음"));
	}

	return bFound;
}

void UCWTrafficSubsystem::LogStopLineMiss(const ACWTrafficLane* Lane, const TCHAR* Reason) const
{
	// 목록 개수를 같이 넘긴다. 이유 문자열만으로는 "제어기가 통행이라 답했다" 와
	// "제어기 자체가 없다" 가 같은 줄로 보일 수 있고, 둘은 고칠 곳이 다르다.
	Diagnostics.LogStopLineMiss(GetWorld(), Lane, Reason, SignalDirectors.Num(), StopLines.Num());
}

int32 UCWTrafficSubsystem::GetActiveVehicleCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ACWBaseVehicle>& VehiclePtr : ActiveVehicles)
	{
		if (VehiclePtr.IsValid())
		{
			++Count;
		}
	}

	return Count;
}
