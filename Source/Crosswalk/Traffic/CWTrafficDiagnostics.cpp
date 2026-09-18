// CWTrafficDiagnostics.cpp

#include "CWTrafficDiagnostics.h"

#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficLane.h"
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
}

bool CWTrafficDiagnostics::IsSyncLoggingEnabled()
{
	return CVarTrafficLogSync.GetValueOnAnyThread();
}

bool CWTrafficDiagnostics::IsNetCensusLoggingEnabled()
{
	return CVarTrafficLogNetCensus.GetValueOnGameThread();
}

bool CWTrafficDiagnostics::IsNetRelevancyLoggingEnabled()
{
	return CVarTrafficLogNetRelevancy.GetValueOnAnyThread();
}

const TCHAR* CWTrafficDiagnostics::GetNetRoleTag(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return TEXT("?");
	}

	return World->GetNetMode() == NM_Client ? TEXT("클라") : TEXT("서버");
}

FString CWTrafficDiagnostics::GetNetIdTag(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("(없음)");
	}

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

void CWTrafficDiagnostics::LogSpawnRelevancy(const ACWBaseVehicle& Vehicle)
{
	if (!IsNetRelevancyLoggingEnabled())
	{
		return;
	}

	UNetDriver* NetDriver = Vehicle.GetNetDriver();
	if (!NetDriver || NetDriver->ClientConnections.Num() == 0)
	{
		return;
	}

	const FVector SpawnLocation = Vehicle.GetActorLocation();
	const float CullDistance = FMath::Sqrt(Vehicle.GetNetCullDistanceSquared());

	for (UNetConnection* Connection : NetDriver->ClientConnections)
	{
		if (!Connection)
		{
			continue;
		}

		FString PlayerTag = GetNameSafe(Connection->OwningActor);
		if (const APlayerController* Controller = Cast<APlayerController>(Connection->OwningActor))
		{
			if (const APlayerState* State = Controller->PlayerState)
			{
				PlayerTag = State->GetPlayerName();
			}
		}

		/**
		 * 뷰타깃이 없는 커넥션은 건너뛰지 않고 그 사실을 남긴다.
		 *
		 * 아직 폰을 못 받은 상태(로딩·리스폰 중)라는 뜻인데, 그 구간에 스폰된 차량이
		 * 통째로 빠지는 것이 바로 의심하는 그림 중 하나다. 줄이 없으면 "그 시각에
		 * 스폰이 없었다" 와 구분이 안 된다.
		 *
		 * FNetViewer 는 뷰타깃을 확인하지 않고 바로 위치를 읽으므로 여기서 걸러야 한다.
		 */
		if (!Connection->OwningActor || !Connection->ViewTarget)
		{
			UE_LOG(LogCWTraffic, Log, TEXT("[TrafficNet] 스폰 %s / %s 시점없음(로딩·리스폰 중)"),
				*Vehicle.GetName(), *PlayerTag);
			continue;
		}

		const FNetViewer Viewer(Connection, 0.f);
		const float Distance = FVector::Dist(SpawnLocation, Viewer.ViewLocation);
		const bool bRelevant = Vehicle.IsNetRelevantFor(Viewer.InViewer, Viewer.ViewTarget, Viewer.ViewLocation);

		UE_LOG(LogCWTraffic, Log, TEXT("[TrafficNet] 스폰 %s / 컬거리 %.0fcm / %s 거리 %.0fcm / 관련 %s"),
			*Vehicle.GetName(), CullDistance, *PlayerTag, Distance, bRelevant ? TEXT("O") : TEXT("X"));
	}
}

void FCWTrafficDiagnostics::LogVehicleCensus(const UWorld* World, TConstArrayView<TWeakObjectPtr<ACWBaseVehicle>> Vehicles)
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

	if (!CWTrafficDiagnostics::IsNetCensusLoggingEnabled() || !World)
	{
		return;
	}

	int32 VehicleCount = 0;
	int32 LanedCount = 0;
	int32 MeshedCount = 0;

	TArray<FString> VehicleIds;

	for (const TWeakObjectPtr<ACWBaseVehicle>& WeakVehicle : Vehicles)
	{
		const ACWBaseVehicle* Vehicle = WeakVehicle.Get();
		if (!Vehicle)
		{
			continue;
		}

		++VehicleCount;
		VehicleIds.Add(CWTrafficDiagnostics::GetNetIdTag(Vehicle));

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
		 *
		 * 스켈레탈 쪽을 USkinnedMeshComponent 로 집는 이유는 차량이 쓰는 것이
		 * UPoseableMeshComponent 라 USkeletalMeshComponent 가 아니기 때문이다. 둘의
		 * 공통 부모가 여기이고, 그래서 메쉬 게터도 GetSkinnedAsset 쪽을 쓴다.
		 */
		const UStaticMeshComponent* Mesh = Vehicle->FindComponentByClass<UStaticMeshComponent>();
		const USkinnedMeshComponent* SkelMesh = Vehicle->FindComponentByClass<USkinnedMeshComponent>();
		if ((Mesh && Mesh->GetStaticMesh()) || (SkelMesh && SkelMesh->GetSkinnedAsset()))
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
		CWTrafficDiagnostics::GetNetRoleTag(World),
		*LocalPlayerTag, CensusSequence, *ElapsedTag,
		VehicleCount, LanedCount, MeshedCount, *IdList);
}

void FCWTrafficDiagnostics::LogStopLineMiss(const UWorld* World, const ACWTrafficLane* Lane, const TCHAR* Reason, int32 DirectorCount, int32 StopLineCount)
{
	if (!CWTrafficDiagnostics::IsSyncLoggingEnabled() || !World)
	{
		return;
	}

	// 되돌아오는 전이도 찍힌다 - 파란불 -> 빨간불 -> 파란불 이면 세 줄이 남는다.
	// 조여야 할 것은 "몇 번 찍히나" 가 아니라 "같은 말을 반복하나" 다. (헤더 주석 참고)
	const FName LaneName = Lane ? Lane->GetFName() : NAME_None;
	if (const FString* LastReason = LastStopLineMissReasonByLane.Find(LaneName))
	{
		if (*LastReason == Reason)
		{
			return;
		}
	}

	LastStopLineMissReasonByLane.Add(LaneName, Reason);

	UE_LOG(LogCWTraffic, Log, TEXT("[TrafficSync][%s] 정지선 미발견 - 차선 %s / 이유: %s / 제어기 %d개 / 정지선 %d개"),
		CWTrafficDiagnostics::GetNetRoleTag(World),
		Lane ? *Lane->GetName() : TEXT("(없음)"),
		Reason,
		DirectorCount,
		StopLineCount);
}
