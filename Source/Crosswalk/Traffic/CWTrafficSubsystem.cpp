// CWTrafficSubsystem.cpp

#include "CWTrafficSubsystem.h"

#include "Engine/World.h"
#include "CWBaseVehicle.h"
#include "CWTrafficDirector.h"
#include "CWTrafficLane.h"
#include "CWTrafficStopLine.h"
#include "Interfaces/Interface_ParryReceiver.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Traffic/CWTrafficLog.h"

namespace
{
	/**
	 * 동기화 진단 로그. **기본값이 켜짐인 것은 의도다.**
	 *
	 * 이 로그는 다른 팀원과 2인 테스트를 할 때 쓰는데, 받는 사람이 설정을 먼저 해야 하면
	 * 그 단계에서 테스트가 끊긴다. 켜는 쪽이 아니라 끄는 쪽이 한 줄이어야 한다.
	 *
	 * 사건이 일어난 순간에만 찍으므로(정지·출발·색 변경) 켜 두어도 로그가 흐르지 않는다.
	 */
	TAutoConsoleVariable<bool> CVarTrafficLogSync(
		TEXT("cw.Traffic.LogSync"),
		true,
		TEXT("정지선·신호 동기화 진단 로그를 찍는다. 0 이면 끈다."),
		ECVF_Default);

	/**
	 * 차량 대수 대조 로그. LogSync 와 같은 이유로 기본값이 켜짐이다.
	 *
	 * 이쪽은 사건이 아니라 상태라서 1초에 한 줄씩 계속 흐른다. 그래도 켜 두는 것은,
	 * **어긋난 뒤에 켜서는 아무것도 못 보기 때문**이다. 이 버그는 언제 누구에게 터질지
	 * 모르는 채로 4인이 붙어야 재현되고, 그 판을 다시 잡는 비용이 로그 한 줄보다 비싸다.
	 *
	 * 조용한 구간이 길게 이어지는 것도 산출물이다. 어긋난 줄은 같은 숫자가 반복되던
	 * 흐름에서 튀기 때문에 오히려 눈에 잘 띈다.
	 */
	TAutoConsoleVariable<bool> CVarTrafficLogNetCensus(
		TEXT("cw.Traffic.LogNetCensus"),
		true,
		TEXT("차량 대수 대조 로그를 1초마다 찍는다. 0 이면 끈다."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarTrafficLogNetRelevancy(
		TEXT("cw.Traffic.LogNetRelevancy"),
		true,
		TEXT("스폰 순간의 커넥션별 관련성을 찍는다(서버 전용). 0 이면 끈다."),
		ECVF_Default);

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

bool UCWTrafficSubsystem::IsSyncLoggingEnabled()
{
	return CVarTrafficLogSync.GetValueOnAnyThread();
}

bool UCWTrafficSubsystem::IsNetRelevancyLoggingEnabled()
{
	return CVarTrafficLogNetRelevancy.GetValueOnAnyThread();
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
		FTimerDelegate::CreateUObject(this, &UCWTrafficSubsystem::LogVehicleCensus),
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

void UCWTrafficSubsystem::LogVehicleCensus()
{
	/**
	 * 순번과 간격은 콘솔 변수보다 먼저, 무조건 갱신한다.
	 *
	 * 켜짐 판정 뒤에 두면 로그를 껐다 켠 순간의 첫 줄이 "몇 분 만에 찍혔다" 로 나온다.
	 * 이 값이 재는 것은 로그가 나간 간격이 아니라 **타이머가 돈 간격**이다.
	 */
	const double Now = FPlatformTime::Seconds();
	const double Elapsed = (LastCensusRealTime < 0.0) ? -1.0 : (Now - LastCensusRealTime);
	LastCensusRealTime = Now;
	++CensusSequence;

	if (!CVarTrafficLogNetCensus.GetValueOnGameThread())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 VehicleCount = 0;
	int32 LanedCount = 0;
	int32 MeshedCount = 0;

	TArray<FString> VehicleIds;

	for (const TWeakObjectPtr<ACWBaseVehicle>& WeakVehicle : ActiveVehicles)
	{
		const ACWBaseVehicle* Vehicle = WeakVehicle.Get();
		if (!Vehicle)
		{
			continue;
		}

		++VehicleCount;
		VehicleIds.Add(GetNetIdTag(Vehicle));

		if (Vehicle->GetCurrentLane())
		{
			++LanedCount;
		}

		/**
		 * 차량에 메쉬 게터를 새로 열지 않고 컴포넌트로 집는다.
		 *
		 * 이 세 번째 숫자는 버그를 잡는 동안만 필요한 값이다. 그걸 위해 차량의 공개 면을
		 * 넓혀 두면, 진단이 끝난 뒤에도 아무나 메쉬를 만질 수 있는 통로가 남는다.
		 * C++ 이 만드는 메쉬 컴포넌트는 VehicleMesh 와 VehicleSkeletalMesh 둘뿐이다.
		 *
		 * 둘 다 봐야 한다. 차종이 스켈레탈 변형을 뽑으면 정적 메쉬 쪽은 비워지므로,
		 * 예전처럼 UStaticMeshComponent 만 세면 멀쩡히 보이는 차가 "메쉬 없음" 으로
		 * 잡혀 진단이 거짓 신호를 낸다.
		 */
		const UStaticMeshComponent* Mesh = Vehicle->FindComponentByClass<UStaticMeshComponent>();
		const USkeletalMeshComponent* SkelMesh = Vehicle->FindComponentByClass<USkeletalMeshComponent>();
		if ((Mesh && Mesh->GetStaticMesh()) || (SkelMesh && SkelMesh->GetSkeletalMeshAsset()))
		{
			++MeshedCount;
		}
	}

	/**
	 * 로컬 플레이어 이름을 같이 찍는다.
	 *
	 * 4대의 로그를 모아 놓으면 역할 표시(서버/클라)만으로는 클라 셋을 구분할 수 없다.
	 * 구분이 안 되면 "돌아가면서 한 명" 이라는 증상 자체를 로그로 확인할 수가 없다.
	 */
	FString LocalPlayerTag(TEXT("(이름없음)"));
	if (const APlayerController* LocalController = World->GetFirstPlayerController())
	{
		if (const APlayerState* LocalState = LocalController->PlayerState)
		{
			LocalPlayerTag = LocalState->GetPlayerName();
		}
	}

	/**
	 * 정렬해서 찍는다. 등록 순서는 서버와 클라이언트가 다르므로(스폰 순서 대 도착 순서)
	 * 정렬하지 않으면 같은 세 대를 들고 있어도 두 줄이 달라 보인다.
	 */
	VehicleIds.Sort();

	/**
	 * 한 줄이 로그를 덮지 않도록 상한을 둔다.
	 *
	 * 지금 도로 위 최대치는 4대라 걸릴 일이 없지만, 이 로그는 "예상보다 차가 많다" 는
	 * 상황에서도 살아 있어야 하는 물건이다. 잘렸다는 사실 자체가 그때의 관측값이라
	 * 조용히 자르지 않고 남은 수를 적는다.
	 */
	constexpr int32 MaxLoggedIds = 20;
	FString IdList;
	if (VehicleIds.Num() > MaxLoggedIds)
	{
		const int32 Remaining = VehicleIds.Num() - MaxLoggedIds;
		VehicleIds.SetNum(MaxLoggedIds);
		IdList = FString::Printf(TEXT("%s 외%d"), *FString::Join(VehicleIds, TEXT(" ")), Remaining);
	}
	else
	{
		IdList = VehicleIds.Num() > 0 ? FString::Join(VehicleIds, TEXT(" ")) : TEXT("(없음)");
	}

	// 첫 줄은 견줄 상대가 없어서 간격이 없다. 0.00 으로 적으면 "정상 간격" 으로 읽히므로 구분한다.
	const FString ElapsedTag = (Elapsed < 0.0) ? TEXT("첫줄") : FString::Printf(TEXT("+%.2f초"), Elapsed);

	UE_LOG(LogCWTraffic, Log, TEXT("[TrafficNet][%s][%s][#%d %s] 차량 %d / 차선연결 %d / 메쉬 %d / 목록 %s"),
		World->GetNetMode() == NM_Client ? TEXT("클라") : TEXT("서버"),
		*LocalPlayerTag, CensusSequence, *ElapsedTag,
		VehicleCount, LanedCount, MeshedCount, *IdList);
}

const TCHAR* UCWTrafficSubsystem::GetNetRoleTag(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("?");
	}

	return Actor->HasAuthority() ? TEXT("서버") : TEXT("클라");
}

FString UCWTrafficSubsystem::GetNetIdTag(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("(없음)");
	}

	/**
	 * 액터 이름이 아니라 네트워크 GUID 를 쓴다.
	 *
	 * 런타임에 스폰된 차량은 서버와 클라이언트에서 이름이 다르게 붙는다. 이름으로 찍으면
	 * 두 로그를 나란히 놓아도 **어느 줄이 같은 차인지 짚을 수가 없어서**, 이 로그의 목적
	 * 자체가 사라진다. GUID 는 양쪽이 같은 값을 갖는다.
	 */
	if (UNetDriver* NetDriver = Actor->GetNetDriver())
	{
		if (NetDriver->GuidCache.IsValid())
		{
			const FNetworkGUID NetGuid = NetDriver->GuidCache->GetNetGUID(Actor);
			if (NetGuid.IsValid())
			{
				return FString::Printf(TEXT("#%s"), *NetGuid.ToString());
			}
		}
	}

	// 네트워크가 없으면(싱글 PIE) 이름으로 충분하다. 맞춰 볼 상대 로그가 없다.
	return Actor->GetName();
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
	if (Vehicle)
	{
		ActiveVehicles.RemoveSingleSwap(Vehicle);
	}
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

	// 수신 측이 차종 분기를 할 수 있도록 서버가 확정한 차종을 채워서 넘긴다.
	FCWParryAttemptContext ResolvedContext = ParryAttemptContext;
	ResolvedContext.VehicleType = BestVehicle->GetVehicleType();

	if (!IInterface_ParryReceiver::Execute_ReceiveParryAttempt(BestVehicle, ResolvedContext))
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
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry]   %s: 패링 불가 차종"), *Vehicle.GetName());
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
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry]   %s: 반경 밖 (거리 %.0f > %.0f)"),
			*Vehicle.GetName(), SurfaceDistance, ParryAttemptContext.ParryRadius);
		return false;
	}

	// 커서가 향한 쪽만 친다. 쿼터뷰라 높이 차이는 보지 않는다.
	const FVector AttemptDirection = ParryAttemptContext.AttemptDirection.GetSafeNormal2D();
	const FVector ToVehicle = (Vehicle.GetActorLocation() - ParryAttemptContext.AttemptLocation).GetSafeNormal2D();
	if (!AttemptDirection.IsNearlyZero() && !ToVehicle.IsNearlyZero())
	{
		// ParryAngle 은 부채꼴 전체 각이므로 절반을 기준선으로 쓴다.
		const float HalfAngle = FMath::Clamp(ParryAttemptContext.ParryAngle, 0.f, 360.f) * 0.5f;
		const float Dot = FVector::DotProduct(AttemptDirection, ToVehicle);
		if (Dot < FMath::Cos(FMath::DegreesToRadians(HalfAngle)))
		{
			UE_LOG(LogCWTraffic, Log, TEXT("[Parry]   %s: 각도 밖 (커서와 %.0f도 차이, 허용 %.0f도)"),
				*Vehicle.GetName(), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f))), HalfAngle);
			return false;
		}
	}

	// 충돌 직전 윈도우 안이어야 한다. 윈도우 값은 차량이 단일 관리한다.
	// 시도한 사람이 화면에서 본 시점으로 되돌려 판정한다. (ViewRewindSeconds)
	float TimeToImpact = 0.f;
	if (!Vehicle.PredictTimeToImpact(ParryAttemptContext.Parrier, TimeToImpact, ParryAttemptContext.ViewRewindSeconds))
	{
		// 진행 경로 밖이거나 이미 지나친 차량이다. 도로 위에 서 있지 않으면 여기서 전부 걸린다.
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry]   %s: 진행 경로 밖 (또는 이미 지나침)"), *Vehicle.GetName());
		return false;
	}

	if (TimeToImpact > Vehicle.GetParryWindow())
	{
		UE_LOG(LogCWTraffic, Log, TEXT("[Parry]   %s: 타이밍 이릅 (충돌까지 %.2f초 > 윈도우 %.2f초, 보정 %.0fms)"),
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
			return false;
		}

		bLaneControlled = true;
	}

	// 신호가 없는 차선에 정지선만 놓아도 세우지 않는다. 세울 근거가 없다.
	if (!bLaneControlled)
	{
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

	return bFound;
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
