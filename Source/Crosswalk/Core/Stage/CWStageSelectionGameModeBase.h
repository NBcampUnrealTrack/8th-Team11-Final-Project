// CWStageSelectionGameModeBase.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CWStageSelectionGameModeBase.generated.h"

class ACWStageSelectionGameStateBase;
class UCWStageCatalog;

UCLASS(Abstract)
class CROSSWALK_API ACWStageSelectionGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

#pragma region StageSelectionRequests

public:
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	// 선택 조건을 만족하면 전원에게 스테이지 선택창을 표시함
	UFUNCTION(BlueprintCallable, Category = "CW|StageSelection")
	bool BeginStageSelection();

	// 현재 선택 스테이지를 서버에서 변경함
	UFUNCTION(BlueprintCallable, Category = "CW|StageSelection")
	bool SelectStage(FName StageId);

	// 선택을 확정하지 않고 전원의 선택창을 닫음
	UFUNCTION(BlueprintCallable, Category = "CW|StageSelection")
	bool CancelStageSelection();

	// 선택한 StageId를 검증하고 해당 맵으로 전원을 이동함
	UFUNCTION(BlueprintCallable, Category = "CW|StageSelection")
	bool TryStartGame(FName StageId);

#pragma endregion

#pragma region StageSelectionValidation

protected:
	// 현재 화면에서 선택창을 열 수 있는지 파생 GameMode가 판정함
	// Room은 인원·준비 상태, 인게임은 클리어 상태를 검사함
	virtual bool CanOpenStageSelection() const
	{
		return false;
	}

	// 현재 선택으로 다음 스테이지를 시작할 수 있는지 파생 GameMode가 재검증함
	virtual bool CanStartSelectedStage() const
	{
		return false;
	}

private:
	// 현재 월드의 공용 스테이지 선택 GameState를 반환함
	ACWStageSelectionGameStateBase* GetStageSelectionGameState() const;

#pragma endregion

#pragma region StageSelectionConfiguration

protected:
	// 서버가 StageId와 이동 맵을 검증할 때 사용하는 스테이지 목록임
	UPROPERTY(
		EditDefaultsOnly,
		Category = "CW|StageSelection"
	)
	TObjectPtr<UCWStageCatalog> StageCatalog;

#pragma endregion
};
