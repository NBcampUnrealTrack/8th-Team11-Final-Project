// CWTrafficDifficultyStep.h

#pragma once

#include "CoreMinimal.h"
#include "CWTrafficVehicleEntry.h"
#include "CWTrafficDifficultyStep.generated.h"

/**
 * 스테이지 진행도 한 구간의 교통 밀도.
 *
 * 여기 적는 것은 "언제 어느 차선에서 차가 나온다" 가 아니라 **얼마나 자주 나오는가**다.
 * 배열을 적게 하면 몇 판 만에 외워지고, 외운 뒤로는 패턴대로 움직이는 게임이 된다.
 * 순서는 매번 난수로 뽑고, 사람은 그 난수의 성질만 정한다.
 *
 * 난이도를 올릴 때 건드릴 수 있는 것과 없는 것이 갈린다.
 *   올려도 되는 것 - 얼마나 자주 오는가 (주기), 무엇이 오는가 (차종)
 *   건드리면 안 되는 것 - 차량 속도. 그것은 예고 시간을 직접 깎는다.
 *
 * 탑뷰 카메라가 캐릭터 앞 1364cm 까지 보여주므로 750cm/s 기준 예고가 1.81초뿐이다.
 * 속도를 올리면 그 1.81초가 그대로 줄어들고, 플레이어는 "봤는데 못 피했다" 가 아니라
 * "보이지도 않았다" 를 겪는다. 그래서 이 구조체에 속도가 없다.
 */
USTRUCT(BlueprintType)
struct FCWTrafficDifficultyStep
{
	GENERATED_BODY()

	/** 로그와 디테일 패널에서 이 단계를 알아보기 위한 이름. (예: 여유 / 보통 / 빡셈) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FName StepName;

	/**
	 * 이 단계가 시작되는 스테이지 진행도. 0 = 시작, 1 = 목표 전부 달성.
	 *
	 * 진행도가 이 값을 넘으면 이 단계로 갈아탄다. 배열 순서는 상관없다 -
	 * 조건을 만족하는 것 중 가장 높은 값을 고르므로 아무 데나 끼워 넣어도 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartProgress = 0.f;

	/** 이 단계의 스폰 간격 하한(초). 이 값이 곧 그 차선의 최소 틈을 정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1"))
	float SpawnIntervalMin = 8.f;

	/** 이 단계의 스폰 간격 상한(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1"))
	float SpawnIntervalMax = 12.f;

	/**
	 * 이 단계의 차종 구성. **비워두면 스포너가 자기 값을 그대로 쓴다.**
	 *
	 * 주기를 안 건드리고도 난이도를 올릴 수 있는 자리다. 트럭은 길어서 한 지점을
	 * 지나가는 시간이 길고, 커서 화면에서 더 잘 보인다 - 즉 어려워지는데 예고는
	 * 오히려 강해진다. 밀도를 조이는 것보다 안전한 레버다.
	 *
	 * 차선마다 다른 차종을 굴리고 싶으면 여기를 비우고 스포너에서 정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FCWTrafficVehicleEntry> VehicleMix;
};
