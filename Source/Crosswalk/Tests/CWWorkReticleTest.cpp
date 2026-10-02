#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Painting/Components/CWTemperatureComponent.h"
#include "Painting/Components/CWWorkerComponent.h"
#include "Painting/Components/CWWorkReticleComponent.h"
#include "Painting/Zone/CWWorkZone_Cells.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWWorkReticleTest,
	"Crosswalk.Painting.ReticleMatchesPaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWWorkReticleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	APawn* Pawn = World->SpawnActor<APawn>();
	USceneComponent* Root = NewObject<USceneComponent>(Pawn);
	Pawn->SetRootComponent(Root);
	Root->RegisterComponent();
	Pawn->SetActorScale3D(FVector(2.f, 1.5f, 3.f));
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->Possess(Pawn);
	Controller->SetAsLocalPlayerController();
	UCWWorkerComponent* Worker = NewObject<UCWWorkerComponent>(Pawn);
	UCWTemperatureComponent* Temperature = NewObject<UCWTemperatureComponent>(Pawn);
	UCWWorkReticleComponent* Reticle = NewObject<UCWWorkReticleComponent>(Pawn);
	Worker->RegisterComponent();
	Reticle->RegisterComponent();
	Worker->SetHeldTool(ECWToolType::WhiteRoller);
	auto SetObject = [](UObject* Object, const TCHAR* Name, UObject* Value)
	{
		FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name)->SetObjectPropertyValue_InContainer(Object, Value);
	};
	auto SetFloat = [](UObject* Object, const TCHAR* Name, float Value)
	{
		FindFProperty<FFloatProperty>(Object->GetClass(), Name)->SetPropertyValue_InContainer(Object, Value);
	};
	SetObject(Worker, TEXT("TemperatureComponent"), Temperature);
	SetObject(Reticle, TEXT("WorkerComponent"), Worker);
	SetObject(Reticle, TEXT("ReticleMaterial"), LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/CrossWalk/Painting/Zone/Materials/M_WorkReticle.M_WorkReticle")));
	TestTrue(TEXT("Reticle runs for local worker"), Worker->IsLocalWorker());

	const FVector Scales[] = { FVector::OneVector, FVector(0.5f), FVector(2.f), FVector(0.5f, 1.7f, 1.f) };
	for (const FVector& Scale : Scales)
	{
		for (const float Heat : { 36.5f, 70.f })
		{
			ACWWorkZone_Cells* Zone = World->SpawnActor<ACWWorkZone_Cells>();
			// 홀수 격자로 원점을 칸 중심에 맞춰, 가장 작은 붓도 비교할 칸이 있게 한다.
			Zone->Area->SetBoxExtent(FVector(102.5f, 102.5f, 20.f));
			Zone->SetActorScale3D(Scale);
			Zone->SetActorRotation(FRotator(0.f, 37.f, 0.f));
			*FindFProperty<FStructProperty>(Zone->GetClass(), TEXT("GridSize"))
				->ContainerPtrToValuePtr<FIntPoint>(Zone) = FIntPoint(41, 41);
			SetFloat(Zone, TEXT("OverspillMargin"), 0.f);
			SetFloat(Zone, TEXT("BrushRadius"), 6.f);
			Zone->OnConstruction(Zone->GetActorTransform());
			SetFloat(Temperature, TEXT("CurrentTemperature"), Heat);
			SetObject(Worker, TEXT("TargetZone"), Zone);
			const float PreviewRadius = Zone->GetPaintedWorldRadius(Worker);
			TestTrue(TEXT("Small brush preview can shrink below the old 20cm minimum"), PreviewRadius < 20.f);
			TestTrue(TEXT("Stroke paints at least one cell"), Zone->AddCellsAt(FVector::ZeroVector, Zone->GetEffectiveBrushRadius(Worker)) > 0);

			// 프리뷰 식을 복사하지 않고 실제 칠해진 모든 칸을 원 안/밖 판정과 비교한다.
			int32 Mismatches = 0;
			for (int32 Y = 0; Y < 41; ++Y)
			{
				for (int32 X = 0; X < 41; ++X)
				{
					const FVector Center = Zone->Area->GetComponentTransform().TransformPosition(
						FVector(-100.f + X * 5.f, -100.f + Y * 5.f, 0.f));
					const bool bInsidePreview = Center.SizeSquared2D() <= FMath::Square(PreviewRadius);
					Mismatches += Zone->IsFilledAt(Center) != bInsidePreview ? 1 : 0;
				}
			}
			TestEqual(FString::Printf(TEXT("Paint matches preview at scale %s and temperature %.1f"), *Scale.ToString(), Heat), Mismatches, 0);

			Reticle->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
			UDecalComponent* Decal = Pawn->FindComponentByClass<UDecalComponent>();
			if (TestNotNull(TEXT("Reticle creates its decal"), Decal))
			{
				TestEqual(TEXT("Displayed radius matches painted cells"), Decal->DecalSize.Y, static_cast<double>(PreviewRadius));
				TestEqual(TEXT("Displayed circle has equal axes"), Decal->DecalSize.Z, static_cast<double>(PreviewRadius));
				TestTrue(TEXT("Pawn scale does not enlarge preview"), Decal->GetComponentScale().Equals(FVector::OneVector));
			}
			Zone->Destroy();
		}
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
