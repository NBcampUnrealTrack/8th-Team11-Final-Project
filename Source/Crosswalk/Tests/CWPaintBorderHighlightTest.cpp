#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Painting/CWPaintingSubsystem.h"
#include "Painting/Zone/CWWorkZone_BorderSketch.h"
#include "Painting/Zone/CWWorkZone_Cells.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWPaintBorderHighlightTest,
	"Crosswalk.Painting.HighlightOnExistingBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWPaintBorderHighlightTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ACWWorkZone_BorderSketch* Border = World->SpawnActor<ACWWorkZone_BorderSketch>();
	ACWWorkZone_Cells* Fill = World->SpawnActor<ACWWorkZone_Cells>();
	UCWPaintingSubsystem* Painting = World->GetSubsystem<UCWPaintingSubsystem>();
	Painting->RegisterZone(Border);
	Painting->RegisterZone(Fill);

	// 복제되는 완료/잠금 값의 조합을 직접 구성해 표시 판정을 검증한다.
	auto SetFlag = [](UObject* Object, const TCHAR* Name, bool Value)
	{
		FindFProperty<FBoolProperty>(Object->GetClass(), Name)->SetPropertyValue_InContainer(Object, Value);
	};
	auto SetGroup = [](UObject* Object, FName Group)
	{
		FindFProperty<FNameProperty>(Object->GetClass(), TEXT("WorkGroup"))->SetPropertyValue_InContainer(Object, Group);
	};
	SetGroup(Border, TEXT("A"));
	SetGroup(Fill, TEXT("A"));
	auto ToolList = [Fill](const TCHAR* Name) -> TArray<ECWToolType>&
	{
		return *FindFProperty<FArrayProperty>(Fill->GetClass(), Name)
			->ContainerPtrToValuePtr<TArray<ECWToolType>>(Fill);
	};
	// 실제 스테이지는 FillTools 대신 AcceptedTools에 흰 롤러를 등록한다.
	ToolList(TEXT("FillTools")).Reset();
	ToolList(TEXT("CleanupTools")).Reset();
	ToolList(TEXT("AcceptedTools")) = { ECWToolType::WhiteRoller };
	Border->BorderMeshes->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/CrossWalk/Painting/Zone/Materials/M_BorderCell.M_BorderCell")));
	Border->SetActorScale3D(FVector(0.5f, 1.7f, 1.f));
	Border->SetActorRotation(FRotator(0.f, 37.f, 0.f));
	TArray<FTransform> Before;
	for (int32 Index = 0; Index < Border->BorderMeshes->GetInstanceCount(); ++Index)
	{
		FTransform Transform;
		Border->BorderMeshes->GetInstanceTransform(Index, Transform, true);
		Before.Add(Transform);
	}
	TestTrue(TEXT("Existing border has geometry"), Before.Num() > 0);
	const auto State = [&]() { return Border->GetHighlightStateFor(ECWToolType::WhiteRoller); };
	TestEqual(TEXT("Incomplete border stays unhighlighted"), State(), ECWZoneHighlightState::None);
	SetFlag(Border, TEXT("bCompleted"), true);
	SetFlag(Fill, TEXT("bLocked"), true);
	TestEqual(TEXT("Locked painting stays unhighlighted"), State(), ECWZoneHighlightState::None);
	SetFlag(Fill, TEXT("bLocked"), false);
	TestEqual(TEXT("Ready roller highlights completed border"), State(), ECWZoneHighlightState::Active);
	ToolList(TEXT("AcceptedTools")).Reset();
	TestEqual(TEXT("Unconfigured tool cannot highlight border"), State(), ECWZoneHighlightState::None);
	ToolList(TEXT("FillTools")).Add(ECWToolType::WhiteRoller);
	TestEqual(TEXT("Explicit fill tool also highlights border"), State(), ECWZoneHighlightState::Active);
	ToolList(TEXT("CleanupTools")).Add(ECWToolType::WhiteRoller);
	TestEqual(TEXT("Cleanup takes priority over fill styling"), State(), ECWZoneHighlightState::None);
	ToolList(TEXT("CleanupTools")).Reset();
	ToolList(TEXT("FillTools")).Reset();
	ToolList(TEXT("AcceptedTools")).Add(ECWToolType::WhiteRoller);
	TestEqual(TEXT("Liner does not enable roller styling"), Border->GetHighlightStateFor(ECWToolType::Liner), ECWZoneHighlightState::None);
	TestEqual(TEXT("Cleanup roller does not enable paint styling"), Border->GetHighlightStateFor(ECWToolType::BlackRoller), ECWZoneHighlightState::None);
	Border->SetHighlightState(State());
	Fill->SetHighlightState(Fill->GetHighlightStateFor(ECWToolType::WhiteRoller));
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Border->BorderMeshes->GetMaterial(0));
	if (TestNotNull(TEXT("Border owns its local highlight material"), MID))
	{
		TestEqual(TEXT("Highlight enabled"), MID->K2_GetScalarParameterValue(TEXT("PaintHighlight")), 1.f);
		TestEqual(TEXT("Paint uses teal"), MID->K2_GetVectorParameterValue(TEXT("PaintHighlightColor")), FLinearColor(0.02f, 0.8f, 0.65f, 1.f));
		TestTrue(TEXT("Stripes move"), MID->K2_GetScalarParameterValue(TEXT("PaintHighlightScrollSpeed")) > 0.f);
		Border->SetHighlightState(Border->GetHighlightStateFor(ECWToolType::None));
		TestEqual(TEXT("Dropping tool restores original appearance"), MID->K2_GetScalarParameterValue(TEXT("PaintHighlight")), 0.f);
		Border->SetHighlightState(State());
		SetFlag(Fill, TEXT("bCompleted"), true);
		TestEqual(TEXT("Completed paint stops guidance"), State(), ECWZoneHighlightState::None);
		Border->SetHighlightState(State());
		TestEqual(TEXT("Completed paint restores original appearance"), MID->K2_GetScalarParameterValue(TEXT("PaintHighlight")), 0.f);
	}
	TArray<UDecalComponent*> Decals;
	Fill->GetComponents(Decals);
	TestEqual(TEXT("No inner painting decal is created"), Decals.Num(), 0);
	Border->GetComponents(Decals);
	TestEqual(TEXT("No second border decal is created"), Decals.Num(), 0);
	TestFalse(TEXT("No debug box over paint"), Fill->ShouldDrawDebugHighlightBox());
	TestFalse(TEXT("No debug box over border"), Border->ShouldDrawDebugHighlightBox());
	TestEqual(TEXT("Border instance count unchanged"), Border->BorderMeshes->GetInstanceCount(), Before.Num());
	for (int32 Index = 0; Index < Before.Num(); ++Index)
	{
		FTransform After;
		Border->BorderMeshes->GetInstanceTransform(Index, After, true);
		TestTrue(TEXT("Border position, rotation, width and corners unchanged"), Before[Index].Equals(After));
	}
	SetFlag(Fill, TEXT("bCompleted"), false);
	SetGroup(Fill, TEXT("B"));
	TestEqual(TEXT("Another group cannot highlight this border"), State(), ECWZoneHighlightState::None);
	SetGroup(Fill, TEXT("A"));
	ACWWorkZone_Cells* Duplicate = World->SpawnActor<ACWWorkZone_Cells>();
	SetGroup(Duplicate, TEXT("A"));
	Painting->RegisterZone(Duplicate);
	TestEqual(TEXT("Ambiguous group does not highlight an arbitrary border"), State(), ECWZoneHighlightState::None);
	World->DestroyWorld(false);
	return true;
}

#endif
