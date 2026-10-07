// CWTrafficSignalState.h

#pragma once

#include "CoreMinimal.h"
#include "CWTrafficSignalState.generated.h"

class ACWTrafficLane;

/** 신호가 지금 무엇을 지시하고 있는가. */
UENUM(BlueprintType)
enum class ECWTrafficSignalState : uint8
{
	// 신호가 없는 상태. 차는 정지선을 무시하고 그냥 지나간다.
	Off UMETA(DisplayName = "꺼짐"),

	Red UMETA(DisplayName = "적색"),

	// 적색으로 넘어가기 직전. 차량 거동은 초록과 같고 색만 다르다.
	Yellow UMETA(DisplayName = "황색"),

	Green UMETA(DisplayName = "초록")
};

/** 교차로에 차가 들어오는 방향. 우측통행 기준. 경로 교차 검증에만 쓴다. */
UENUM(BlueprintType)
enum class ECWTrafficApproach : uint8
{
	North UMETA(DisplayName = "북쪽 진입"),
	East UMETA(DisplayName = "동쪽 진입"),
	South UMETA(DisplayName = "남쪽 진입"),
	West UMETA(DisplayName = "서쪽 진입")
};

/**
 * 진입한 차가 교차로에서 무엇을 하는지. 경로 교차 검증에만 쓴다.
 *
 * 차량의 행동이 아니라 차선의 모양이다. 좌회전 차선은 좌회전용으로 휘어 있는 스플라인
 * 하나이고, 차량은 지금과 똑같이 자기 차선을 따라 직진할 뿐이다.
 */
UENUM(BlueprintType)
enum class ECWTrafficTurn : uint8
{
	Through UMETA(DisplayName = "직진"),
	Left UMETA(DisplayName = "좌회전"),
	Right UMETA(DisplayName = "우회전")
};

/**
 * 같은 색을 함께 받는 차선 묶음. 신호등 하나가 담당하는 범위이기도 하다.
 *
 * 왜 묶는가
 *   교차로에서는 남북이 초록일 때 동서가 적색이다. 신호를 하나만 두면 그 상황을
 *   표현할 수 없다. 반대로 왕복 2차선처럼 신호 상태가 하나뿐인 레벨은 그룹을
 *   하나만 만들면 되므로, 그룹은 단순한 경우에 부담이 되지 않는다.
 *
 * 차선이 없는 그룹
 *   보행 신호처럼 차량과 무관한 그룹은 Lanes 를 비운다. 색만 들고 있고 아무 차량도
 *   통제하지 않으므로, 신호등 컴포넌트와 연출 이벤트만 이 그룹을 본다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficSignalGroup
{
	GENERATED_BODY()

	/** 시퀀스와 신호등이 이 그룹을 가리킬 이름. (예: 남북직진 / 보행) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FName GroupName;

	/** 이 그룹이 통제하는 차선. 보행 신호처럼 차량과 무관하면 비운다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic")
	TArray<TObjectPtr<ACWTrafficLane>> Lanes;

	/** 아무 규칙도 이 그룹을 건드리지 않을 때의 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	ECWTrafficSignalState DefaultState = ECWTrafficSignalState::Green;

	// [검증용] 이 그룹의 차가 어디서 들어와 무엇을 하는가. (아래 Approaches, Turn)
	//
	// 런타임 로직은 이 값을 보지 않는다. 경로가 서로 교차하는 그룹을 같은 단계에
	// 초록으로 두면 차들이 교차로에서 서로를 통과하는데(차간 거리 유지가 같은 차선
	// 안에서만 동작한다), 화면에서는 그냥 이상해 보일 뿐 원인이 안 보인다.
	// 그 배치 실수를 패널에서 잡으려면 경로 정보가 필요하다.

	/**
	 * 이 그룹으로 차가 들어오는 방향들. 비우면 이 그룹은 검증에서 제외된다.
	 *
	 * 배열인 이유는 한 그룹이 마주보는 두 방향을 함께 담기 때문이다 - "남북직진" 은
	 * 북쪽 진입과 남쪽 진입을 둘 다 포함한다. 하나만 적게 하면 그 그룹의 절반이
	 * 검증에서 빠지고, 빠진 쪽이 일으키는 교차를 못 잡는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Validation")
	TArray<ECWTrafficApproach> Approaches;

	/** 이 그룹의 차가 교차로에서 하는 동작(직진·좌회전·우회전). 검증에서 경로 교차를 가릴 때만 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Validation")
	ECWTrafficTurn Turn = ECWTrafficTurn::Through;
};

/** 그룹 하나에 색 하나를 지정한다. 단계와 규칙이 공통으로 쓴다. */
USTRUCT(BlueprintType)
struct FCWTrafficSignalAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FName GroupName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	ECWTrafficSignalState State = ECWTrafficSignalState::Green;
};

/**
 * 시퀀스의 한 단계. 여기 적힌 대로 그룹들의 색이 정해지고, 시간이 지나면 다음 단계로 간다.
 *
 * 8차선 교차로의 "좌회전 먼저, 그 다음 직진" 이 단계 4개다.
 *
 *   [0] 남북 좌회전   { 남북좌=초록, 나머지 적색 }
 *   [1] 남북 직진     { 남북직=초록, 나머지 적색 }
 *   [2] 동서 좌회전   { 동서좌=초록, 나머지 적색 }
 *   [3] 동서 직진     { 동서직=초록, 나머지 적색 }
 *
 * 단계 순서가 곧 교통 규칙이다. 같은 교차로라도 좌회전을 먼저 보내느냐 직진을 먼저
 * 보내느냐가 다른 판이 되고, 순서와 길이는 전부 배치값이라 C++ 0줄로 바꿀 수 있다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficSignalStep
{
	GENERATED_BODY()

	/** 로그와 패널에서 알아보기 위한 이름. (예: 남북좌회전) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FName StepName;

	/**
	 * 이 단계에서의 그룹별 색. 여기 없는 그룹은 적색이 된다.
	 *
	 * 적색을 일일이 적지 않아도 되게 한 것이다. 단계마다 모든 그룹을 나열하게 하면
	 * 그룹을 하나 추가할 때 모든 단계를 고쳐야 하고, 빠뜨린 곳이 조용히 초록으로 남는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FCWTrafficSignalAssignment> Assignments;

	/** 이 단계가 유지되는 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1"))
	float Duration = 8.f;

	/**
	 * 다음 단계로 넘어가기 전에 모두 적색으로 두는 시간(초). 황색 + 전적색이다.
	 *
	 * 0 으로 두면 이 단계의 마지막 차가 아직 교차로 안에 있는데 다음 단계가 시작되어
	 * 서로를 통과한다. 교차로를 빠져나가는 데 걸리는 시간보다 길어야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float ClearanceDuration = 2.f;
};

/** 무엇이 규칙을 발동시키는가. */
UENUM(BlueprintType)
enum class ECWTrafficSignalTrigger : uint8
{
	// 아무것도 안 한다. BP 에서 TriggerRule 을 부를 때만 발동한다.
	Manual UMETA(DisplayName = "직접 호출"),

	// 스테이지 목표 달성률이 기준을 넘으면.
	StageProgress UMETA(DisplayName = "진행도 도달"),

	// 스테이지가 클리어 상태가 되면.
	StageCleared UMETA(DisplayName = "스테이지 클리어")
};

/**
 * 게임에서 무슨 일이 생겼을 때 신호에 개입하는 규칙.
 *
 * 시퀀스가 "평소에 어떻게 도는가" 라면 이쪽은 "무슨 일이 생기면 어떻게 되는가" 다.
 *
 *   "작업을 다 끝내면 차가 멈추고 시민이 건너간다"
 *   -> 조건 = 스테이지 클리어 / 시퀀스 정지 / { 차량=적색, 보행=초록 }
 *
 * 규칙을 하나도 넣지 않으면 시퀀스만 계속 돈다. 시퀀스도 비어 있으면 그룹의 기본 색이
 * 유지되고, 신호는 아무 일도 하지 않는다 - 계속 초록인 평범한 도로가 그 경우다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficSignalRule
{
	GENERATED_BODY()

	/** 로그와 패널에서 알아보기 위한 이름. (예: 작업완료_보행) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FName RuleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	ECWTrafficSignalTrigger Trigger = ECWTrafficSignalTrigger::StageCleared;

	/** 진행도 도달의 기준값(0~1). 다른 조건에서는 쓰지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProgressThreshold = 1.f;

	/**
	 * 조건이 맞은 뒤 실제로 바뀌기까지의 지연(초).
	 *
	 * "작업을 다 끝냈다" 와 "차가 멈춘다" 사이에 한 박자를 두면 플레이어가 자기가 한
	 * 일과 화면의 변화를 이어서 본다. 동시에 일어나면 둘이 관련 있다는 것이 안 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	/**
	 * 발동하면 시퀀스를 멈출지.
	 *
	 * 끄면 색만 잠깐 덮어쓰고 시퀀스가 계속 돌아 다음 단계에서 원래대로 돌아간다.
	 * 켜면 이 규칙의 색이 그대로 남는다 - 클리어 연출은 이쪽이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bStopSequence = true;

	/** 발동했을 때의 그룹별 색. 여기 없는 그룹은 적색이 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FCWTrafficSignalAssignment> Assignments;
};
