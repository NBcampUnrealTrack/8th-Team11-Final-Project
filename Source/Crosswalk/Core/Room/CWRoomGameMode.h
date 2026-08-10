// CWRoomGameMode.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CWRoomGameMode.generated.h"

UCLASS()
class CROSSWALK_API ACWRoomGameMode : public AGameModeBase
{
	GENERATED_BODY()

#pragma region GameModeLifecycle

public:
	// 방에서 사용할 GameState와 PlayerState 클래스를 지정
	ACWRoomGameMode();

	// GameState 생성 직후 방 설정값을 주입
	virtual void InitGameState() override;

#pragma endregion

#pragma region GameStart

public:
	// 시작 조건을 만족하면 연결된 플레이어와 함께 인게임 맵으로 이동
	UFUNCTION(BlueprintCallable, Category = "CW|Room")
	bool TryStartGame();

#pragma endregion

#pragma region GameConfiguration

protected:
	// 준비 완료 후 이동할 실제 인게임 맵
	UPROPERTY(EditDefaultsOnly, Category = "CW|Room")
	TSoftObjectPtr<UWorld> GameplayMap;

	// 게임 시작에 필요한 최소 방 인원
	UPROPERTY(
		EditDefaultsOnly,
		Category = "CW|Room",
		meta = (ClampMin = "1", UIMin = "1")
	)
	int32 MinimumPlayersToStart = 1;

#pragma endregion
};
