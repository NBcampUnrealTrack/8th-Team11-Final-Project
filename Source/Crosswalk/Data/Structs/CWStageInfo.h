// CWStageInfo.h

#pragma once

#include "CoreMinimal.h"
#include "CWStageInfo.generated.h"

class UTexture2D;
class UWorld;

// 스테이지 선택 화면에 표시하고 실제 맵 이동에 사용할 스테이지 정의 StageId를 안정적인 식별자로 사용
// 카탈로그 배열 순서가 바뀌거나 네트워크로 선택값을 전달할 때도 StageId로 스테이지를 찾고 GameplayMap을 조회

USTRUCT(BlueprintType)
struct CROSSWALK_API FCWStageInfo
{
	GENERATED_BODY()
public:
	// 카탈로그 내에서 비어 있지 않고 유일해야 하는 안정 식별자
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	FName StageId = NAME_None;

	// 이 스테이지를 해금하려면 먼저 클리어해야 하는 스테이지
	// NAME_None 이면 처음부터 해금 (해금 "조건"은 기획 데이터, "결과"는 진행 저장 데이터)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	FName RequiredStageId = NAME_None;

	// 카드와 상세 영역에 표시할 스테이지 이름
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	FText DisplayName;

	// 스테이지 설명. 비어 있으면 UI에서 설명 영역을 숨김
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	FText Description;

	// 카드 썸네일. 비어 있으면 UI에서 기본 이미지로 대체
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	// 서버가 해당 스테이지 시작 시 이동할 인게임 맵
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crosswalk|Stage")
	TSoftObjectPtr<UWorld> GameplayMap;
};
