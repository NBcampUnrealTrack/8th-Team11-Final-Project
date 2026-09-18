// CWTrafficSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/Structs/CWParryAttemptContext.h"
#include "Traffic/CWTrafficDiagnostics.h"
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

	// 진단 로그(콘솔 변수·역할 태그·대수 대조)는 여기 없다. CWTrafficDiagnostics 참고.
	// 판정을 바꾸면 안 되는 코드라 판정 코드와 파일을 나눠 둔다.

	/**
	 * 이 사람의 화면이 서버보다 얼마나 뒤처져 있는가(초). 판정 직전에 차량을 되돌릴 양이다.
	 *
	 * 패링(쳐내는 사람)과 피격(맞는 사람) 두 판정이 같은 차량을 같은 근거(그 사람 핑)로
	 * 되돌린다. 계산과 상한이 한 곳이어야 "패링은 됐는데 그대로 맞는다" 처럼 같은 순간을
	 * 두 판정이 다르게 보는 일이 없다. 그래서 어느 한쪽 소유가 아니라 여기 둔다.
	 *
	 * 되돌릴 시간은 왕복 시간(ping) 전체다. 절반이 아니다. 그 사람 화면의 차량은 서버가
	 * 보낸 값을 받아 그린 것이라 편도만큼 뒤처져 있고, 그 화면을 보고 한 행동이 서버에
	 * 닿는 데 다시 편도가 걸린다.
	 *
	 * 서버에서만 의미가 있다. 클라이언트나 호스트가 직접 조종하는 폰은 0 이다 -
	 * 자기가 보는 것이 곧 서버 상황이라 되돌릴 것이 없다.
	 *
	 * @param Viewer  화면을 보고 행동한 사람의 폰. 폰이 아니면 0.
	 */
	static float ResolveViewRewindSeconds(const AActor* Viewer);

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

	/**
	 * 지금 경적을 울려도 되는지 묻고, 되면 그 자리를 잡는다. (월드 전체에서 한 대만)
	 *
	 * 차량 개별 쿨다운과 별개다. 차마다 따로 세면 "각자 12초에 한 번" 은 지켜지지만
	 * 서른 대가 각자의 타이밍으로 울어서 결과적으로 계속 시끄럽다. 총량은 여기서 막는다.
	 *
	 * 서버·클라이언트가 각자 자기 월드에서 쓴다. 누가 울릴지 맞출 필요가 없다 -
	 * 경적은 판정이 아니라 연출이고, 들리는 범위는 화면마다 다르기 때문이다.
	 *
	 * @param MinIntervalSeconds  직전 경적으로부터 비어 있어야 하는 시간(초)
	 * @return 통과하면 true. 이때 내부 시각이 갱신되므로 부른 쪽은 반드시 울려야 한다.
	 */
	bool TryConsumeHornBudget(float MinIntervalSeconds);

private:
	/**
	 * 시도 정보 기준으로 이 차량이 패링 후보인지 본다.
	 * 통과하면 OutTimeToImpact 에 충돌까지 남은 시간을 낸다.
	 */
	bool PassesAttemptFilter(ACWBaseVehicle& Vehicle, const FCWParryAttemptContext& ParryAttemptContext, float& OutTimeToImpact) const;

	/** FindActiveStopLine 이 "세울 정지선 없음" 으로 빠져나간 이유를 진단 로그에 넘긴다. */
	void LogStopLineMiss(const ACWTrafficLane* Lane, const TCHAR* Reason) const;

private:
	TArray<TWeakObjectPtr<ACWBaseVehicle>> ActiveVehicles;

	/**
	 * 직전 줄을 기억하는 진단 로그(대수 대조 순번, 차선별 미발견 사유).
	 *
	 * mutable 인 이유: FindActiveStopLine 이 const 인데 그 안에서 사유를 남긴다.
	 * 진단 로그가 판정을 못 바꾸게 하려면 판정 함수가 const 로 남아 있어야 하고,
	 * 그러면서도 "같은 말 반복" 을 거르려면 마지막 사유는 갱신돼야 한다.
	 */
	mutable FCWTrafficDiagnostics Diagnostics;

	/** 대수 대조 로그의 타이머. 주기는 1초 고정이고, 켜고 끄는 것은 콘솔 변수가 한다. */
	FTimerHandle CensusTimerHandle;

	/** 레벨에 배치된 신호 제어기들. 클라이언트에도 등록된다 - 정지선 판정을 양쪽이 하기 때문이다. */
	TArray<TWeakObjectPtr<ACWTrafficDirector>> SignalDirectors;

	/** 레벨에 배치된 정지선들. 같은 이유로 클라이언트에도 등록된다. */
	TArray<TWeakObjectPtr<ACWTrafficStopLine>> StopLines;

	/**
	 * 이 월드에서 마지막으로 경적이 난 시각(초). TryConsumeHornBudget 만 만진다.
	 * 한 번도 안 났으면 음수로 남아 첫 요청이 그대로 통과한다.
	 */
	float LastHornTime = -UE_BIG_NUMBER;
};
