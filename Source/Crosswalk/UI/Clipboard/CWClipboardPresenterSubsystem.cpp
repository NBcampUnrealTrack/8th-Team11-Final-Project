// CWClipboardPresenterSubsystem.cpp

#include "UI/Clipboard/CWClipboardPresenterSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"

#pragma region SubsystemOverrides

void UCWClipboardPresenterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ClipboardState = ECWClipboardState::Hidden;
}

void UCWClipboardPresenterSubsystem::Deinitialize()
{
	if (IsValid(PropActor))
	{
		PropActor->OnRaiseFinished.RemoveAll(this);
		PropActor->OnLowerFinished.RemoveAll(this);
		PropActor->Destroy();
	}

	PropActor = nullptr;
	ClipboardState = ECWClipboardState::Hidden;

	Super::Deinitialize();
}

#pragma endregion

#pragma region PublicFunctions

ACWClipboardPropActor* UCWClipboardPresenterSubsystem::AttachPaperContent(
	UUserWidget* InOwner, UWidget* InContent)
{
	ReconcileLostProp();
	if (!IsValid(InOwner) || !IsValid(InContent) || !EnsurePropSpawned())
	{
		return nullptr;
	}

	InContent->RemoveFromParent();
	PropActor->SetPresentationScale(1.f);
	PropActor->SetPaperOnlyPresentation(false);
	PropActor->SetCenteredPresentation(false);
	PropActor->SetPaperContent(InOwner, InContent);
	return PropActor;
}

void UCWClipboardPresenterSubsystem::PlayRaise()
{
	ReconcileLostProp();

	// 이미 올라오는 중이면 진행 중인 연출이 곧 한 번 통보함
	// 여기서 또 쏘면 한 번의 열기에 완료가 두 번 전달됨
	if (ClipboardState == ECWClipboardState::Raising)
	{
		return;
	}

	// 이미 올라와 있으면 움직일 필요가 없음. 그래도 호출당 한 번은 통보해야 함
	// 안 쏘면 UI 가 입력을 끈 채로 완료를 기다려 스테이지 선택이 영영 잠김
	if (ClipboardState == ECWClipboardState::Raised)
	{
		OnRaiseFinished.Broadcast();
		return;
	}

	if (!EnsurePropSpawned())
	{
		// 계약: 연출이 실패해도 완료 통보는 반드시 전달됨
		// 안 쏘면 UI 가 TileView 를 영영 HitTestInvisible 로 두어 스테이지 선택이 잠김
		UE_LOG(
			LogCWClipboard,
			Warning,
			TEXT("[Clipboard] 프롭 준비 실패. 연출을 건너뛰고 즉시 Raise 완료를 통보한다")
		);

		ClipboardState = ECWClipboardState::Raised;
		OnRaiseFinished.Broadcast();
		return;
	}

	ClipboardState = ECWClipboardState::Raising;
	PropActor->StartRaise();
}

void UCWClipboardPresenterSubsystem::PlayLower()
{
	ReconcileLostProp();

	// 이미 내려가는 중이면 진행 중인 연출이 곧 한 번 통보함
	if (ClipboardState == ECWClipboardState::Lowering)
	{
		return;
	}

	// 이미 내려가 있어도 호출당 한 번은 통보함 (PlayRaise 와 대칭)
	// 안 쏘면 닫기 완료를 기다리는 UI 가 팝업을 영영 제거하지 못함
	if (ClipboardState == ECWClipboardState::Hidden)
	{
		OnLowerFinished.Broadcast();
		return;
	}

	if (!IsValid(PropActor))
	{
		// 프롭 없이 Raised 로 넘어와 있던 경우 (연출을 건너뛴 상태)
		UE_LOG(
			LogCWClipboard,
			Warning,
			TEXT("[Clipboard] 프롭이 없다. 연출을 건너뛰고 즉시 Lower 완료를 통보한다")
		);

		ClipboardState = ECWClipboardState::Hidden;
		OnLowerFinished.Broadcast();
		return;
	}

	ClipboardState = ECWClipboardState::Lowering;
	PropActor->StartLower();
}

#pragma endregion

#pragma region PrivateFunctions

void UCWClipboardPresenterSubsystem::ReconcileLostProp()
{
	// Subsystem 은 맵 이동을 견디지만 프롭 액터는 월드와 함께 사라짐
	// 그 경우 상태가 Raising 에 멈춰 다음 호출이 통째로 무시되는 걸 막음
	if (!IsValid(PropActor) && ClipboardState != ECWClipboardState::Hidden)
	{
		UE_LOG(
			LogCWClipboard,
			Verbose,
			TEXT("[Clipboard] 프롭이 사라져 상태를 Hidden 으로 되돌린다")
		);

		ClipboardState = ECWClipboardState::Hidden;
	}
}

bool UCWClipboardPresenterSubsystem::EnsurePropSpawned()
{
	if (IsValid(PropActor))
	{
		return true;
	}

	ULocalPlayer* LocalPlayer = ResolveLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	UWorld* World = LocalPlayer->GetWorld();
	if (!World)
	{
		return false;
	}

	APlayerController* PlayerController = LocalPlayer->GetPlayerController(World);
	if (!IsValid(PlayerController))
	{
		return false;
	}

	UClass* SpawnClass = ACWClipboardPropActor::StaticClass();
	if (!PropActorClass.IsNull())
	{
		if (UClass* LoadedClass = PropActorClass.LoadSynchronous())
		{
			SpawnClass = LoadedClass;
		}
		else
		{
			UE_LOG(
				LogCWClipboard,
				Warning,
				TEXT("[Clipboard] PropActorClass 로드 실패. 기본 클래스로 대체한다: %s"),
				*PropActorClass.ToString()
			);
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerController;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	PropActor = World->SpawnActor<ACWClipboardPropActor>(
		SpawnClass,
		FTransform::Identity,
		SpawnParameters
	);

	if (!IsValid(PropActor))
	{
		return false;
	}

	PropActor->SetOwningPlayerController(PlayerController);

	PropActor->OnRaiseFinished.AddUObject(
		this,
		&UCWClipboardPresenterSubsystem::HandleRaiseFinished
	);

	PropActor->OnLowerFinished.AddUObject(
		this,
		&UCWClipboardPresenterSubsystem::HandleLowerFinished
	);

	return true;
}

ULocalPlayer* UCWClipboardPresenterSubsystem::ResolveLocalPlayer() const
{
	return Cast<ULocalPlayer>(GetOuter());
}

void UCWClipboardPresenterSubsystem::HandleRaiseFinished()
{
	ClipboardState = ECWClipboardState::Raised;

	OnRaiseFinished.Broadcast();
}

void UCWClipboardPresenterSubsystem::HandleLowerFinished()
{
	ClipboardState = ECWClipboardState::Hidden;

	OnLowerFinished.Broadcast();
}

#pragma endregion
