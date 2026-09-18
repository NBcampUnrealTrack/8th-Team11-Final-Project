// Copyright Crosswalk Team. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"
#include "UI/Clipboard/CWClipboardPropActor.h"
#include "UI/Core/CWUISubsystem.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "UI/ViewModel/CWStageSelectViewModel.h"
#include "Data/Assets/CWStageCatalog.h"

namespace
{
	bool IsPaperPixel(const FColor& Color)
	{
		return Color.R > 120 && Color.G > 120 && Color.B > 120;
	}

	struct FPaperScreenCheck
	{
		FDelegateHandle Handle;
		int32 Samples = 0;
		int32 BrightSamples = 0;
		bool bCaptured = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWStageSelectionDisplayTest,
	"Crosswalk.UI.StageSelectionDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWStageSelectionDisplayTest::RunTest(const FString& Parameters)
{
	UCWStageSelectViewModel* ViewModel = NewObject<UCWStageSelectViewModel>();
	const FTextProperty* Display = FindFProperty<FTextProperty>(
		UCWStageSelectViewModel::StaticClass(), TEXT("SelectedDisplayName"));
	UCWStageCatalog* Catalog = LoadObject<UCWStageCatalog>(nullptr,
		TEXT("/Game/CrossWalk/Maps/Stages/DA_StageCatalog.DA_StageCatalog"));
	if (!TestNotNull(TEXT("Display field exists"), Display) ||
		!TestNotNull(TEXT("Stage catalog loads"), Catalog))
	{
		return false;
	}
	if (!TestTrue(TEXT("Stage catalog contains selection cases"), Catalog->GetStages().Num() > 0))
	{
		return false;
	}
	TestTrue(TEXT("Display is empty before initialization"),
		Display->GetPropertyValue_InContainer(ViewModel).IsEmpty());
	ViewModel->Initialize(Catalog);
	for (int32 Index = 0; Index < Catalog->GetStages().Num(); ++Index)
	{
		TestTrue(TEXT("Stage can be selected"), ViewModel->RequestSelectByIndex(Index));
		TestEqual(TEXT("Selected name follows selection"),
			Display->GetPropertyValue_InContainer(ViewModel).ToString(),
			Catalog->GetStages()[Index].DisplayName.ToString());
	}
	ViewModel->Initialize(nullptr);
	TestTrue(TEXT("Clearing catalog clears the old display"),
		Display->GetPropertyValue_InContainer(ViewModel).IsEmpty());
	TestFalse(TEXT("Empty catalog has no selection"), ViewModel->HasSelection());
	TestNull(TEXT("Empty selection remains null"), ViewModel->GetSelectedEntry());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWClipboardPaperTest,
	"Crosswalk.UI.ClipboardPaper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCWClipboardPaperTest::RunTest(const FString& Parameters)
{
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr,
		TEXT("/Game/CrossWalk/UI/Screens/StageSelect/WBP_StageSelect.WBP_StageSelect_C"));
	UClass* PropClass = LoadClass<ACWClipboardPropActor>(nullptr,
		TEXT("/Game/CrossWalk/UI/Clipboard/BP_ClipboardProp.BP_ClipboardProp_C"));
	if (!TestNotNull(TEXT("Stage select class loads"), WidgetClass) ||
		!TestNotNull(TEXT("Clipboard class loads"), PropClass))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ACWClipboardPropActor* Prop = World->SpawnActor<ACWClipboardPropActor>(PropClass);
	UUserWidget* FirstOwner = NewObject<UUserWidget>(World, WidgetClass);
	UUserWidget* SecondOwner = NewObject<UUserWidget>(World, WidgetClass);
	FirstOwner->Initialize();
	UBorder* Paper = Cast<UBorder>(FirstOwner->GetWidgetFromName(TEXT("Panel_ClipboardPaper")));
	UWidgetComponent* Surface = Prop ? Prop->FindComponentByClass<UWidgetComponent>() : nullptr;
	if (!TestNotNull(TEXT("Runtime paper is constructed"), Paper) ||
		!TestNotNull(TEXT("Paper component exists"), Surface))
	{
		World->DestroyWorld(false);
		return false;
	}
	for (const FName FieldName : {FName(TEXT("Panel_ClipboardPaper")), FName(TEXT("SizeBox_PaperHost"))})
	{
		const FObjectPropertyBase* Field = FindFProperty<FObjectPropertyBase>(WidgetClass, FieldName);
		TestTrue(*FString::Printf(TEXT("Native widget binding: %s"), *FieldName.ToString()),
			Field && Field->GetObjectPropertyValue_InContainer(FirstOwner) != nullptr);
	}
	Paper->RemoveFromParent();
	Prop->SetPaperContent(FirstOwner, Paper);
	TestTrue(TEXT("Only content is rendered in world space"), Surface->GetSlateWidget().IsValid());
	TestNull(TEXT("No duplicate popup instance"), Surface->GetUserWidgetObject());
	TestTrue(TEXT("Paper follows prop root"), Surface->GetAttachParent() == Prop->GetRootComponent());
	TestEqual(TEXT("Paper draw size"), Surface->GetDrawSize(), FVector2D(800.f, 1040.f));
	TestFalse(TEXT("Input is blocked while hidden"), Prop->PressPaperPointer(EKeys::LeftMouseButton));
	if (!GUsingNullRHI)
	{
		Surface->TickComponent(0.016f, LEVELTICK_All, nullptr);
		if (TestNotNull(TEXT("Paper render target exists"), Surface->GetRenderTarget()))
		{
			TArray<FColor> Pixels;
			UKismetRenderingLibrary::ReadRenderTarget(World, Surface->GetRenderTarget(), Pixels);
			int32 BrightPixels = 0;
			for (const FColor& Pixel : Pixels)
			{
				BrightPixels += IsPaperPixel(Pixel) ? 1 : 0;
			}
			TestTrue(TEXT("Paper contains visible white pixels"), BrightPixels > Pixels.Num() / 4);
			if (TestNotNull(TEXT("Paper material instance exists"), Surface->GetMaterialInstance()))
			{
				UTexture* BoundTexture = nullptr;
				Surface->GetMaterialInstance()->GetTextureParameterValue(FMaterialParameterInfo(TEXT("SlateUI")), BoundTexture);
				TestTrue(TEXT("Material uses paper render target"), BoundTexture == Surface->GetRenderTarget());
			}
		}
	}
	Prop->StartRaise();
	Prop->Tick(1.f);
	TestTrue(TEXT("Raise completes"), Prop->GetClipboardState() == ECWClipboardState::Raised);
	Prop->StartLower();
	TestTrue(TEXT("Paper remains during lowering"), Surface->GetSlateWidget().IsValid());
	Prop->StartRaise();
	Prop->Tick(1.f);
	TestTrue(TEXT("Reopen interrupts lowering"), Prop->GetClipboardState() == ECWClipboardState::Raised);
	Prop->SetPaperContent(SecondOwner, Paper);
	Prop->ClearPaperContent(FirstOwner);
	TestTrue(TEXT("Old popup cannot clear new paper"), Prop->IsPaperOwner(SecondOwner));
	Prop->ClearPaperContent(SecondOwner);
	Prop->ClearPaperContent(SecondOwner);
	TestFalse(TEXT("Repeated cleanup is harmless"), Surface->GetSlateWidget().IsValid());
	World->DestroyWorld(false);
	return true;
}

// Title을 -game으로 실행함. 별도 SceneCapture는 실제 카메라의 노출을 재현하지 못함
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCWClipboardPaperRuntimeTest,
	"Crosswalk.UI.ClipboardPaperRuntime",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCWClipboardPaperRuntimeTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game)
		{
			World = Context.World();
			break;
		}
	}
	if (!World || !World->GetOutermost()->GetName().EndsWith(TEXT("/Title")) || GUsingNullRHI)
	{
		AddError(TEXT("Run on the Title map with a rendered game window."));
		return false;
	}
	APlayerController* Player = World->GetFirstPlayerController();
	ULocalPlayer* LocalPlayer = Player ? Player->GetLocalPlayer() : nullptr;
	UCWUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCWUISubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Local UI subsystem exists"), UI))
	{
		return false;
	}
	const ECWUIPage PreviousPage = UI->GetCurrentPage();
	UI->OpenPage(ECWUIPage::Room);
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr,
		TEXT("/Game/CrossWalk/UI/Screens/StageSelect/WBP_StageSelect.WBP_StageSelect_C"));
	UUserWidget* Popup = WidgetClass ? CreateWidget<UUserWidget>(Player, WidgetClass) : nullptr;
	if (!TestTrue(TEXT("Stage popup opens"), Popup && UI->OpenPopup(Popup)))
	{
		UI->OpenPage(PreviousPage);
		return false;
	}
	const TSharedRef<FPaperScreenCheck> Check = MakeShared<FPaperScreenCheck>();
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, World, Player, Popup, Check]()
	{
		UWidgetComponent* Surface = nullptr;
		for (TActorIterator<ACWClipboardPropActor> It(World); It; ++It)
		{
			if (It->IsPaperOwner(Popup))
			{
				Surface = It->FindComponentByClass<UWidgetComponent>();
				break;
			}
		}
		if (!TestNotNull(TEXT("Runtime paper component exists"), Surface))
		{
			return;
		}
		Check->Handle = UGameViewportClient::OnScreenshotCaptured().AddLambda(
			[Player = TWeakObjectPtr<APlayerController>(Player), Surface = TWeakObjectPtr<UWidgetComponent>(Surface), Check]
			(int32 Width, int32 Height, const TArray<FColor>& Pixels)
		{
			Check->bCaptured = true;
			if (!Player.IsValid() || !Surface.IsValid())
			{
				return;
			}
			// 방 정보 패널과 손을 피한 종이 안쪽을 실제 화면 좌표로 검사함
			const FVector2D Size = Surface->GetDrawSize();
			for (int32 Y = 4; Y <= 6; ++Y)
			{
				for (int32 X = 3; X <= 5; ++X)
				{
					const FVector Local(0.0, (0.5 - X * 0.1) * Size.X, (0.5 - Y * 0.1) * Size.Y);
					FVector2D Screen;
					if (Player->ProjectWorldLocationToScreen(Surface->GetComponentTransform().TransformPosition(Local), Screen))
					{
						const int32 PixelX = FMath::RoundToInt(Screen.X);
						const int32 PixelY = FMath::RoundToInt(Screen.Y);
						if (PixelX >= 0 && PixelX < Width && PixelY >= 0 && PixelY < Height)
						{
							++Check->Samples;
							Check->BrightSamples += IsPaperPixel(Pixels[PixelY * Width + PixelX]) ? 1 : 0;
						}
					}
				}
			}
		});
		// 콜백에서 픽셀만 검사하며 이미지 파일은 생성하지 않음
		FScreenshotRequest::RequestScreenshot(true);
	}, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, UI, Popup, PreviousPage, Check]()
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(Check->Handle);
		TestTrue(TEXT("Final game frame captured"), Check->bCaptured);
		AddInfo(FString::Printf(TEXT("Visible paper samples: %d / %d"), Check->BrightSamples, Check->Samples));
		TestTrue(TEXT("Paper remains bright under game camera exposure"),
			Check->Samples >= 6 && Check->BrightSamples > Check->Samples / 2);
		UI->ClosePopup(Popup);
		UI->OpenPage(PreviousPage);
	}, 2.f));
	return true;
}

#endif
