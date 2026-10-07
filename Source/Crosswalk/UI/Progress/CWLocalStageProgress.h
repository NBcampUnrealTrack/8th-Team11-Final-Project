#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CWLocalStageProgress.generated.h"

class UCWStageCatalog;
class UWorld;

UCLASS()
class CROSSWALK_API UCWLocalStageProgressSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 Version = 1;

	UPROPERTY(SaveGame)
	TArray<FName> ClearedStages;
};

// UI 선택 제한만 저장하여 서버의 입장 조건과 분리하기 위함
class CROSSWALK_API FCWLocalStageProgress
{
public:
	static UCWLocalStageProgressSave* Load();
	static UCWStageCatalog* GetCatalog();
	static bool IsUnlocked(const UCWStageCatalog* Catalog, int32 Index, const UCWLocalStageProgressSave* Save);
	static bool CanSelect(FName StageId);
	static void RecordClear(UWorld* World);
};
