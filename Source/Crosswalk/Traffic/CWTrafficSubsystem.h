// Copyright Crosswalk Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "CWTrafficSubsystem.generated.h"

class ACWBaseVehicle;
class ACWTrafficDirector;
class ACWTrafficLane;
class ACWTrafficStopLine;

/**
 * 활성 차량 목록을 들고 있는 조회 창구.
 *
 * 패링은 플레이어가 "지금 나를 칠 차가 있냐"를 물어야 성립한다.
 * 플레이어 파트가 차량을 직접 순회하지 않도록 트래픽 쪽에서 이 진입점 하나만 노출한다.
 */
UCLASS()
class CROSSWALK_API UCWTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** WorldContext로부터 서브시스템을 얻는다. 월드가 없으면 nullptr. */
	static UCWTrafficSubsystem* Get(const UObject* WorldContextObject);

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/**
	 * 정지선·신호 동기화 진단 로그를 찍을지. 콘솔 변수 `cw.Traffic.LogSync 0` 으로 끈다.
	 *
	 * 서버와 클라이언트가 같은 사건을 각자 찍으므로, 두 로그를 나란히 놓으면
	 * "서버는 세웠는데 클라는 그냥 지나갔다" 가 줄 단위로 드러난다.
	 * 두 화면을 눈으로 비교해서는 이 판정을 낼 수 없다 - 카메라와 시점이 다르기 때문이다.
	 */
	static bool IsSyncLoggingEnabled();

	/**
	 * 스폰 순간의 커넥션별 관련성 로그를 찍을지. 콘솔 변수 `cw.Traffic.LogNetRelevancy 0` 으로 끈다.
	 *
	 * 서버에서만 찍는다. 클라이언트는 자기가 왜 그 차를 못 받았는지 알 방법이 없다 -
	 * 관련성 판정은 서버가 커넥션마다 따로 내리고, 탈락한 액터는 아예 보내지지 않는다.
	 */
	static bool IsNetRelevancyLoggingEnabled();

	/** 진단 로그의 역할 표시. 두 로그를 섞어 놓아도 어느 쪽 것인지 알아야 한다. */
	static const TCHAR* GetNetRoleTag(const AActor* Actor);

	/** 진단 로그의 액터 식별자. 서버와 클라이언트에서 같은 값이 나오는 것을 고른다. */
	static FString GetNetIdTag(const AActor* Actor);

	void RegisterVehicle(ACWBaseVehicle* Vehicle);
	void UnregisterVehicle(ACWBaseVehicle* Vehicle);

	/**
	 * 같은 차선에서 Vehicle 바로 앞에 있는 차량을 찾는다. 없으면 nullptr.
	 * 차간 거리 유지에 쓰이므로 정지한 차량도 후보에 포함한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Traffic")
	ACWBaseVehicle* FindVehicleAhead(const ACWBaseVehicle* Vehicle) const;

	/**
	 * 패링 입력 한 번을 처리한다. 성공 시 패링당한 차량을 반환.
	 *
	 * 대상 선별(반경·각도·임박도)까지만 여기서 하고, 최종 판정은 고른 차량의
	 * ReceiveParryAttempt 가 내린다. 차종별 예외는 차량이 스스로 결정해야 하기 때문이다.
	 * 쿨타임(1초)과 입력 바인딩은 플레이어 파트 담당이므로 여기서 관리하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic|Parry")
	ACWBaseVehicle* TryParryWithAttempt(const FCWParryAttemptContext& ParryAttemptContext);

	/**
	 * 지정 지점에서 Radius 안에 있는 차량을 모은다. 차체 표면 기준으로 잰다.
	 *
	 * 도로 위에서 차량과 상호작용하는 다른 파트를 위한 조회 창구다.
	 * (젖은 페인트를 밟고 지나가는 차량을 찾는 데 쓴다)
	 */
	UFUNCTION(BlueprintCallable, Category = "Traffic")
	void GetVehiclesNearPoint(const FVector& Point, float Radius, TArray<ACWBaseVehicle*>& OutVehicles) const;

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetActiveVehicleCount() const;

	void RegisterSignalDirector(ACWTrafficDirector* Director);
	void UnregisterSignalDirector(ACWTrafficDirector* Director);

	void RegisterStopLine(ACWTrafficStopLine* StopLine);
	void UnregisterStopLine(ACWTrafficStopLine* StopLine);

	/**
	 * 이 차선에서 지금 차를 세워야 하는 가장 가까운 정지선을 찾는다.
	 *
	 * 차량이 "내 차선에 지금 정지선이 있나" 를 묻는 창구다. 차량이 신호 액터를 직접
	 * 순회하지 않도록 앞차 조회(FindVehicleAhead)와 같은 자리에 둔다.
	 *
	 * 서버와 클라이언트 양쪽에서 같은 답이 나온다 - 신호가 읽는 값이 복제 프로퍼티와
	 * 레벨 배치값뿐이기 때문이다. 그래서 차량이 각자 감속을 계산해도 어긋나지 않는다.
	 *
	 * @param FromDistance  차량의 현재 진행 거리. 이미 지난 정지선은 무시한다
	 */
	bool FindActiveStopLine(const ACWTrafficLane* Lane, float FromDistance, float& OutStopLineDistance) const;

private:
	/**
	 * 시도 정보 기준으로 이 차량이 패링 후보인지 본다.
	 * 통과하면 OutTimeToImpact 에 충돌까지 남은 시간을 낸다.
	 */
	bool PassesAttemptFilter(ACWBaseVehicle& Vehicle, const FCWParryAttemptContext& ParryAttemptContext, float& OutTimeToImpact) const;

	/**
	 * 이 화면의 차량 대수를 세 갈래로 나눠 찍는다. 서버·클라이언트 양쪽에서 돈다.
	 *
	 * 트랜스폼을 복제하지 않는 구조라(ACWBaseVehicle 의 네트워크 주석 참고) 클라이언트 화면에
	 * 차가 보이려면 **세 가지가 전부** 성립해야 하고, 셋 중 무엇이 깨져도 증상은 똑같이
	 * "차가 안 보인다" 로 나온다. 눈으로는 갈래를 가를 수 없어서 숫자로 가른다.
	 *
	 *   차량      액터 채널이 이 커넥션에 열렸는가. 서버보다 적으면 관련성이나 대역폭이다
	 *   차선연결  CurrentLane 이 해결됐는가. 적으면 Tick 이 매 프레임 빠져나가 차가 얼어붙는다
	 *   메쉬      MeshVariantIndex 의 OnRep 이 돌았는가. 적으면 차는 있는데 투명하다
	 *
	 * 세 숫자가 서버와 모두 같은데도 안 보인다면 네트워크 문제가 아니다. 그 판정 자체가
	 * 이 로그의 산출물이다 - 아니라는 것을 확인하는 데 드는 시간이 제일 아깝다.
	 *
	 * 숫자 뒤에 차량 목록을 네트워크 GUID 로 붙인다. **개수만으로는 갈래가 하나 더 남는다** -
	 * "호스트 2대, 클라 1대" 가 그 차가 아예 안 온 것인지, 늦게 온 것인지, 먼저 사라진
	 * 것인지 구분되지 않는다. 셋은 원인이 다르다(전달 실패 / 채널 개설 지연 / 소멸 전파).
	 * GUID 는 서버와 클라이언트가 같은 값을 가지므로(GetNetIdTag 주석 참고) 두 로그의
	 * 차량을 한 대씩 짝지어 생존 구간을 비교할 수 있다.
	 */
	void LogVehicleCensus() const;

private:
	TArray<TWeakObjectPtr<ACWBaseVehicle>> ActiveVehicles;

	/** 대수 대조 로그의 타이머. 주기는 1초 고정이고, 켜고 끄는 것은 콘솔 변수가 한다. */
	FTimerHandle CensusTimerHandle;

	/** 레벨에 배치된 신호 제어기들. 클라이언트에도 등록된다 - 정지선 판정을 양쪽이 하기 때문이다. */
	TArray<TWeakObjectPtr<ACWTrafficDirector>> SignalDirectors;

	/** 레벨에 배치된 정지선들. 같은 이유로 클라이언트에도 등록된다. */
	TArray<TWeakObjectPtr<ACWTrafficStopLine>> StopLines;
};
