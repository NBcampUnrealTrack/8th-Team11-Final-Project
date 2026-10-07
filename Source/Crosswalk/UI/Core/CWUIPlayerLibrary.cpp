#include "UI/Core/CWUIPlayerLibrary.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Painting/Components/CWSupplyCarrierComponent.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/Tools/CWBaseTool.h"

UCWWorkerComponent* UCWUIPlayerLibrary::GetWorkerComponent(const APawn* Pawn)
{
	return IsValid(Pawn) ? Pawn->FindComponentByClass<UCWWorkerComponent>() : nullptr;
}

UCWToolHolderComponent* UCWUIPlayerLibrary::GetToolHolderComponent(const APawn* Pawn)
{
	return IsValid(Pawn) ? Pawn->FindComponentByClass<UCWToolHolderComponent>() : nullptr;
}

UCWSupplyCarrierComponent* UCWUIPlayerLibrary::GetSupplyCarrierComponent(const APawn* Pawn)
{
	return IsValid(Pawn) ? Pawn->FindComponentByClass<UCWSupplyCarrierComponent>() : nullptr;
}

UCWTemperatureComponent* UCWUIPlayerLibrary::GetTemperatureComponent(const APawn* Pawn)
{
	return IsValid(Pawn) ? Pawn->FindComponentByClass<UCWTemperatureComponent>() : nullptr;
}

ACWBaseTool* UCWUIPlayerLibrary::GetHeldTool(const APawn* Pawn)
{
	const UCWToolHolderComponent* Holder = GetToolHolderComponent(Pawn);
	return Holder ? Holder->GetHeldTool() : nullptr;
}

UCWTemperatureComponent* UCWUIPlayerLibrary::GetTemperatureComponentOfPlayerState(const APlayerState* PlayerState)
{
	return IsValid(PlayerState) ? GetTemperatureComponent(PlayerState->GetPawn()) : nullptr;
}

ACWBaseTool* UCWUIPlayerLibrary::GetHeldToolOfPlayerState(const APlayerState* PlayerState)
{
	return IsValid(PlayerState) ? GetHeldTool(PlayerState->GetPawn()) : nullptr;
}

UCWWorkerComponent* UCWUIPlayerLibrary::GetLocalWorkerComponent(const UUserWidget* Widget)
{
	return IsValid(Widget) ? GetWorkerComponent(Widget->GetOwningPlayerPawn()) : nullptr;
}

UCWToolHolderComponent* UCWUIPlayerLibrary::GetLocalToolHolderComponent(const UUserWidget* Widget)
{
	return IsValid(Widget) ? GetToolHolderComponent(Widget->GetOwningPlayerPawn()) : nullptr;
}

UCWSupplyCarrierComponent* UCWUIPlayerLibrary::GetLocalSupplyCarrierComponent(const UUserWidget* Widget)
{
	return IsValid(Widget) ? GetSupplyCarrierComponent(Widget->GetOwningPlayerPawn()) : nullptr;
}

UCWTemperatureComponent* UCWUIPlayerLibrary::GetLocalTemperatureComponent(const UUserWidget* Widget)
{
	return IsValid(Widget) ? GetTemperatureComponent(Widget->GetOwningPlayerPawn()) : nullptr;
}

ACWBaseTool* UCWUIPlayerLibrary::GetLocalHeldTool(const UUserWidget* Widget)
{
	return IsValid(Widget) ? GetHeldTool(Widget->GetOwningPlayerPawn()) : nullptr;
}
