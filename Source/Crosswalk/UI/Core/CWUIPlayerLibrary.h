#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CWUIPlayerLibrary.generated.h"

class ACWBaseTool;
class APawn;
class APlayerState;
class UCWSupplyCarrierComponent;
class UCWTemperatureComponent;
class UCWToolHolderComponent;
class UCWWorkerComponent;
class UUserWidget;

// UI 가 플레이어의 컴포넌트를 읽는 공용 입구
// 대상 명시형이 기본이고 로컬 전용은 위젯의 소유 Pawn 을 넘기는 얇은 래퍼임
// 원격 플레이어를 보여주는 위젯이 로컬 전용을 쓰면 남의 상태창에 내 값이 나오므로 대상을 직접 넘길 것
UCLASS()
class CROSSWALK_API UCWUIPlayerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

#pragma region Explicit

public:
	// 대상 Pawn 의 컴포넌트를 반환. Pawn 이 없거나 컴포넌트가 없으면 null
	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static UCWWorkerComponent* GetWorkerComponent(const APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static UCWToolHolderComponent* GetToolHolderComponent(const APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static UCWSupplyCarrierComponent* GetSupplyCarrierComponent(const APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static UCWTemperatureComponent* GetTemperatureComponent(const APawn* Pawn);

	// 대상이 손에 든 도구. 들고 있지 않으면 null
	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static ACWBaseTool* GetHeldTool(const APawn* Pawn);

	// PlayerState 는 Pawn 보다 먼저 복제되어 Pawn 이 아직 없을 수 있음 - 그때는 null
	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static UCWTemperatureComponent* GetTemperatureComponentOfPlayerState(const APlayerState* PlayerState);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player")
	static ACWBaseTool* GetHeldToolOfPlayerState(const APlayerState* PlayerState);

#pragma endregion

#pragma region Local

public:
	// 위젯을 소유한 로컬 플레이어의 Pawn 기준
	UFUNCTION(BlueprintPure, Category = "CW|UI|Player", meta = (DefaultToSelf = "Widget"))
	static UCWWorkerComponent* GetLocalWorkerComponent(const UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player", meta = (DefaultToSelf = "Widget"))
	static UCWToolHolderComponent* GetLocalToolHolderComponent(const UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player", meta = (DefaultToSelf = "Widget"))
	static UCWSupplyCarrierComponent* GetLocalSupplyCarrierComponent(const UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player", meta = (DefaultToSelf = "Widget"))
	static UCWTemperatureComponent* GetLocalTemperatureComponent(const UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category = "CW|UI|Player", meta = (DefaultToSelf = "Widget"))
	static ACWBaseTool* GetLocalHeldTool(const UUserWidget* Widget);

#pragma endregion
};
