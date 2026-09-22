// CWInteractionPromptDisplayTest.cpp

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/Widget/CWInteractionPromptWidget.h"
#include "UI/Widget/CWInteractionPromptEntryWidget.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/Components/CWInteractionPromptComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWInteractionPromptDisplayTest,
	"Crosswalk.UI.InteractionPromptDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWInteractionPromptDisplayTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	APawn* Pawn = World->SpawnActor<APawn>();
	UCWInteractionPromptComponent* Source = NewObject<UCWInteractionPromptComponent>(Pawn);
	Pawn->AddInstanceComponent(Source);
	Source->RegisterComponent();
	AActor* Box = World->SpawnActor<AActor>();
	AActor* Tool = World->SpawnActor<AActor>();
	UCWInteractionPromptWidget* Widget = NewObject<UCWInteractionPromptWidget>(World);

	// 디자인은 WBP 가 가지므로 여기서는 표시 결정만 검사하도록 베이스 클래스를 꽂음
	Widget->EntryWidgetClass = UCWInteractionPromptEntryWidget::StaticClass();
	Widget->Initialize();
	Widget->TakeWidget();

	// 대상 하나의 안내판에 담긴 줄 목록. 위젯 트리 모양에 기대지 않고 결정 결과만 봄
	auto LinesOf = [](const UCWInteractionPromptWidget* Owner, int32 GroupIndex)
		-> const TArray<FCWInteractionPromptEntryLine>*
	{
		if (!Owner->Groups.IsValidIndex(GroupIndex) || !IsValid(Owner->Groups[GroupIndex].Entry))
		{
			return nullptr;
		}
		return &Owner->Groups[GroupIndex].Entry->Lines;
	};

	auto GroupIndexOf = [](const UCWInteractionPromptWidget* Owner, const AActor* Target)
	{
		return Owner->Groups.IndexOfByPredicate([Target](const FCWInteractionPromptGroup& Group)
		{
			return Group.Target.Get() == Target;
		});
	};

	FCWInteractionPrompt TakeCan;
	TakeCan.TargetActor = Box;
	TakeCan.Key = EKeys::E;
	TakeCan.Action = ECWInteractionPromptAction::PickUpCan;
	FCWInteractionPrompt ReturnCan = TakeCan;
	ReturnCan.Key = EKeys::R;
	ReturnCan.Action = ECWInteractionPromptAction::ReturnCan;

	// UI 생성 전에 판정이 끝난 상황을 만들어 초기 조회 누락을 검증함
	const FArrayProperty* ActiveProperty = FindFProperty<FArrayProperty>(Source->GetClass(), TEXT("ActivePrompts"));
	if (!TestNotNull(TEXT("Prompt snapshot exists"), ActiveProperty))
	{
		World->DestroyWorld(false);
		return false;
	}
	TArray<FCWInteractionPrompt>* Snapshot = ActiveProperty->ContainerPtrToValuePtr<TArray<FCWInteractionPrompt>>(Source);
	*Snapshot = { TakeCan, ReturnCan };

	Widget->HandlePawnChanged(nullptr, Pawn);
	TestEqual(TEXT("Late HUD subscription immediately shows the current target"), Widget->Groups.Num(), 1);
	if (const TArray<FCWInteractionPromptEntryLine>* Lines = LinesOf(Widget, 0))
	{
		TestEqual(TEXT("Two prompts on one target stack inside a single entry"), Lines->Num(), 2);
	}
	else
	{
		AddError(TEXT("The first target has no entry widget"));
	}
	TestTrue(TEXT("Change delegate is bound"), Source->OnInteractionPromptsChanged.IsAlreadyBound(
		Widget, &UCWInteractionPromptWidget::HandlePromptsChanged));

	FCWInteractionPrompt ChargeTool = ReturnCan;
	ChargeTool.TargetActor = Tool;
	ChargeTool.Action = ECWInteractionPromptAction::ChargeTool;
	ChargeTool.Key = EKeys::Q;
	Source->OnInteractionPromptsChanged.Broadcast(Source, { TakeCan, ChargeTool });
	TestEqual(TEXT("Different targets get separate entries"), Widget->Groups.Num(), 2);

	const int32 ToolIndex = GroupIndexOf(Widget, Tool);
	if (const TArray<FCWInteractionPromptEntryLine>* Lines = ToolIndex != INDEX_NONE ? LinesOf(Widget, ToolIndex) : nullptr;
		Lines && Lines->Num() == 1)
	{
		const FCWInteractionPromptEntryLine& Line = (*Lines)[0];
		TestTrue(TEXT("UI draws the supplied mapping rather than a hardcoded R key"), Line.Key == EKeys::Q);
		TestTrue(TEXT("Mapped key is shown"), Line.bKeyShown);
		TestTrue(TEXT("Charge action uses its UI label"), Line.Label.EqualTo(
			Widget->Groups[ToolIndex].Entry->ActionLabels[ECWInteractionPromptAction::ChargeTool]));
	}
	else
	{
		AddError(TEXT("The tool target does not hold exactly one line"));
	}

	ChargeTool.Key = EKeys::Invalid;
	Source->OnInteractionPromptsChanged.Broadcast(Source, { ChargeTool });
	if (const TArray<FCWInteractionPromptEntryLine>* Lines = LinesOf(Widget, 0); Lines && Lines->Num() == 1)
	{
		TestFalse(TEXT("Unmapped key hides the key slot instead of inventing one"), (*Lines)[0].bKeyShown);
		TestFalse(TEXT("Unmapped key keeps its action label"), (*Lines)[0].Label.IsEmpty());
	}
	else
	{
		AddError(TEXT("An unmapped key should still draw its action line"));
	}

	Source->OnInteractionPromptsChanged.Broadcast(Source, {});
	TestTrue(TEXT("Empty prompts remove every entry"), Widget->Groups.IsEmpty());

	// 코드로 스폰한 액터에는 기본 루트가 없어 어태치가 조용히 실패함
	auto EnsureRoot = [](AActor* Actor)
	{
		if (Actor->GetRootComponent() == nullptr)
		{
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		return Actor;
	};

	// 집는 순간 손에 붙은 도구. 문구 갱신을 기다리면 안내판이 손으로 끌려감
	AActor* HeldProp = EnsureRoot(World->SpawnActor<AActor>());
	HeldProp->AttachToActor(EnsureRoot(Pawn), FAttachmentTransformRules::KeepRelativeTransform);
	TestTrue(TEXT("A target in my own hand stops being drawn"), Widget->IsHeldByOwner(HeldProp));

	// 팀원이 든 도구는 충전(R)의 정당한 대상이라 계속 그려야 함
	AActor* TeammateTool = EnsureRoot(World->SpawnActor<AActor>());
	TeammateTool->AttachToActor(EnsureRoot(World->SpawnActor<APawn>()), FAttachmentTransformRules::KeepRelativeTransform);
	TestFalse(TEXT("A tool held by a teammate keeps its prompt"), Widget->IsHeldByOwner(TeammateTool));

	// 바닥에 놓인 도구는 그대로 그림
	TestFalse(TEXT("A tool on the ground keeps its prompt"), Widget->IsHeldByOwner(Tool));

	Widget->HandlePawnChanged(Pawn, nullptr);
	TestFalse(TEXT("Pawn replacement unbinds the old source"), Source->OnInteractionPromptsChanged.IsAlreadyBound(
		Widget, &UCWInteractionPromptWidget::HandlePromptsChanged));
	Source->OnInteractionPromptsChanged.Broadcast(Source, { TakeCan });
	TestTrue(TEXT("Previous pawn cannot restore stale prompts"), Widget->Groups.IsEmpty());

	Widget->HandlePawnChanged(nullptr, Pawn);
	TestEqual(TEXT("Rebinding restores the latest snapshot"), Widget->Groups.Num(), 1);
	Widget->NativeDestruct();
	TestFalse(TEXT("Widget destruction releases the subscription"), Source->OnInteractionPromptsChanged.IsAlreadyBound(
		Widget, &UCWInteractionPromptWidget::HandlePromptsChanged));
	TestTrue(TEXT("Widget destruction clears target references"), Widget->Groups.IsEmpty());

	World->DestroyWorld(false);
	return true;
}

#endif
