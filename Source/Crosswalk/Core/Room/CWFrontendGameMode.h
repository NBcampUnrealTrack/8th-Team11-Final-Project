// CWFrontendGameMode.h

#pragma once

#include "CoreMinimal.h"
#include "Core/Stage/CWStageSelectionGameModeBase.h"
#include "CWFrontendGameMode.generated.h"

// 게임 플레이 전 단계를 맡는 GameMode
// Title 레벨 하나에 메인 메뉴·방 목록·방 대기실·스테이지 선택이 모두 들어감
UCLASS()
class CROSSWALK_API ACWFrontendGameMode : public ACWStageSelectionGameModeBase
{
	GENERATED_BODY()

#pragma region GameModeLifecycle

public:
	// 방에서 사용할 GameState와 PlayerState 클래스를 지정
	ACWFrontendGameMode();

	// GameState 생성 직후 방 설정값을 주입
	virtual void InitGameState() override;

#pragma endregion

#pragma region StageSelectionValidation

protected:
	// 최소 인원과 전원 준비 조건을 만족해야 선택창을 열 수 있음
	virtual bool CanOpenStageSelection() const override;

	// 선택 확정 순간에도 인원과 준비 조건을 다시 검사함
	virtual bool CanStartSelectedStage() const override;

#pragma endregion

#pragma region GameConfiguration

protected:
	// 게임 시작에 필요한 최소 방 인원
	UPROPERTY(
		EditDefaultsOnly,
		Category = "CW|Room",
		meta = (ClampMin = "1", UIMin = "1")
	)
	int32 MinimumPlayersToStart = 1;

#pragma endregion
};
