#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/Components/CWWearableComponent.h"
#include "Characters/Wearables/CWWearableItem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CWPlayerState.h"
#include "Core/Room/CWRoomGameState.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWWearableLifecycleTest,
	"Crosswalk.Wearable.LobbyTravelAndLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWWearableLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ACWRoomGameState* Room = World->SpawnActor<ACWRoomGameState>();
	World->SetGameState(Room);
	ACWPlayerState* Original = World->SpawnActor<ACWPlayerState>();
	Original->SetPlayerId(17);
	Original->DispatchBeginPlay();
	UCWWearableComponent* Wearable = Original->GetWearableComponent();
	UCWWearableItem* Cone = LoadObject<UCWWearableItem>(nullptr,
		TEXT("/Game/CrossWalk/Characters/Player/Data_Asset/DA_ConeHat.DA_ConeHat"));
	if (!TestNotNull(TEXT("Cone asset loads"), Cone))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	Wearable->SetSlotItem(TEXT("Head"), Cone);
	TestEqual(TEXT("Lobby accepts cone selection"), Wearable->GetSlotItem(TEXT("Head")), Cone);

	UCWWearableItem* InitialColor = Wearable->GetSlotItem(CWWearableSlots::BodyColor);
	if (!TestNotNull(TEXT("Color slot has a default preset"), InitialColor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	TSet<UCWWearableItem*> Colors;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UCWWearableItem* Color = Wearable->GetSlotItem(CWWearableSlots::BodyColor);
		TestNotNull(TEXT("Color cycle never selects an empty slot"), Color);
		Colors.Add(Color);
		Wearable->CycleSlotItem(CWWearableSlots::BodyColor);
	}
	TestEqual(TEXT("Eight distinct color presets"), Colors.Num(), 8);
	TestEqual(TEXT("Eight clicks return to initial color"), Wearable->GetSlotItem(CWWearableSlots::BodyColor), InitialColor);
	TestEqual(TEXT("Color cycle preserves hat selection"), Wearable->GetSlotItem(TEXT("Head")), Cone);
	Wearable->CycleSlotItem(CWWearableSlots::BodyColor);
	UCWWearableItem* SelectedColor = Wearable->GetSlotItem(CWWearableSlots::BodyColor);
	Wearable->ToggleAllSlots();
	Wearable->SetSlotItem(CWWearableSlots::BodyColor, nullptr);
	TestEqual(TEXT("Outfit toggle and empty requests preserve color"), Wearable->GetSlotItem(CWWearableSlots::BodyColor), SelectedColor);

	// CopyProperties가 BeginPlay보다 먼저 또는 나중에 호출되는 두 순서를 검사한다.
	ACWPlayerState* Stage = World->SpawnActor<ACWPlayerState>();
	Original->SeamlessTravelTo(Stage);
	Stage->DispatchBeginPlay();
	TestEqual(TEXT("Travel preserves selected color"), Stage->GetWearableComponent()->GetSlotItem(CWWearableSlots::BodyColor), SelectedColor);
	TestEqual(TEXT("Travel copy survives destination BeginPlay"),
		Stage->GetWearableComponent()->GetSlotItem(TEXT("Head")), Cone);
	World->SetGameState(World->SpawnActor<AGameStateBase>());
	UCWWearableComponent* StageWearable = Stage->GetWearableComponent();
	StageWearable->CycleSlotItem(TEXT("Head"));
	StageWearable->CycleSlotItem(CWWearableSlots::BodyColor);
	TestEqual(TEXT("Stage locks color changes"), StageWearable->GetSlotItem(CWWearableSlots::BodyColor), SelectedColor);
	StageWearable->SetSlotItem(TEXT("Head"), nullptr);
	StageWearable->ToggleAllSlots();
	// 공개 요청을 우회해 기존 서버 RPC로 직접 들어와도 단계 제한을 적용해야 한다.
	struct { int32 SlotIndex; int32 ItemIndex; } SetParams{0, INDEX_NONE};
	StageWearable->ProcessEvent(StageWearable->FindFunctionChecked(TEXT("Server_SetSlotItem")), &SetParams);
	struct { int32 SlotIndex; } CycleParams{0};
	StageWearable->ProcessEvent(StageWearable->FindFunctionChecked(TEXT("Server_CycleSlotItem")), &CycleParams);
	StageWearable->ProcessEvent(StageWearable->FindFunctionChecked(TEXT("Server_ToggleAllSlots")), nullptr);
	TestEqual(TEXT("Stage rejects all change entry points"), StageWearable->GetSlotItem(TEXT("Head")), Cone);

	ACWPlayerState* Returned = World->SpawnActor<ACWPlayerState>();
	Returned->DispatchBeginPlay();
	Stage->SeamlessTravelTo(Returned);
	TestEqual(TEXT("Return travel restores selection after BeginPlay"),
		Returned->GetWearableComponent()->GetSlotItem(TEXT("Head")), Cone);
	World->SetGameState(Room);
	World->NextURL = TEXT("/Game/NextMap");
	Returned->GetWearableComponent()->CycleSlotItem(TEXT("Head"));
	TestEqual(TEXT("Pending travel also locks selection"),
		Returned->GetWearableComponent()->GetSlotItem(TEXT("Head")), Cone);
	World->NextURL.Reset();
	Returned->GetWearableComponent()->CycleSlotItem(TEXT("Head"));
	TestTrue(TEXT("Returning to lobby unlocks selection"),
		Returned->GetWearableComponent()->GetSlotItem(TEXT("Head")) != Cone);

	// 선택 정보가 먼저 도착하고 표시 메시가 나중에 생기는 순서를 검사한다.
	AActor* Display = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Display);
	Body->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/CrossWalk/Characters/Player/long_character/CWCharacter_new.CWCharacter_new")));
	Body->RegisterComponent();
	UMaterialInterface* OriginalMaterial = Body->GetMaterial(0);
	Wearable->SetBodyMesh(Body);
	TestEqual(TEXT("Late body binding applies selected color"), Body->GetMaterial(0), SelectedColor->BodyMaterial.LoadSynchronous());
	TArray<UStaticMeshComponent*> Visuals;
	Display->GetComponents(Visuals);
	TestEqual(TEXT("Late visual binding creates exactly one head mesh"), Visuals.Num(), 1);
	if (Visuals.Num() == 1)
	{
		TestTrue(TEXT("Bound head uses selected cone"), Visuals[0]->GetStaticMesh() == Cone->StaticMesh.Get());
		TestFalse(TEXT("Visual component is not replicated"), Visuals[0]->GetIsReplicated());
	}
	Wearable->SetBodyMesh(Body);
	Display->GetComponents(Visuals);
	TestEqual(TEXT("Repeated seat refresh does not duplicate visuals"), Visuals.Num(), 1);
	Wearable->ReleaseBodyMesh(Body);
	TestEqual(TEXT("Releasing seat restores its original material"), Body->GetMaterial(0), OriginalMaterial);
	Display->GetComponents(Visuals);
	TestEqual(TEXT("Releasing seat removes old visuals"), Visuals.Num(), 0);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
