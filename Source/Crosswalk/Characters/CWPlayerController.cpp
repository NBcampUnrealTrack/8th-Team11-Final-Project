#include "Characters/CWPlayerController.h"

#include "Characters/CWPlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"

ACWPlayerController::ACWPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
}

void ACWPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ApplyAudioListenerOverride(InPawn);
}

void ACWPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);

	ApplyAudioListenerOverride(InPawn);
}

void ACWPlayerController::OnUnPossess()
{
	// 폰이 사라진 뒤에도 오버라이드가 남아 있으면 파괴된 컴포넌트를 가리키게 된다.
	if (IsLocalController())
	{
		ClearAudioListenerOverride();
	}

	Super::OnUnPossess();
}

void ACWPlayerController::ApplyAudioListenerOverride(APawn* InPawn)
{
	// 소리를 듣는 지점은 로컬 플레이어에게만 의미가 있다. 데디 서버와 원격 프록시는 건너뛴다.
	if (!IsLocalController())
	{
		return;
	}

	const ACWPlayerCharacter* PlayerCharacter = Cast<ACWPlayerCharacter>(InPawn);
	if (PlayerCharacter == nullptr || PlayerCharacter->CameraBoom == nullptr)
	{
		return;
	}

	// 붐이 아래를 내려다보는 만큼 반대로 돌려 리스너를 수평으로 되돌린다.
	// 카메라 각도를 나중에 바꿔도 여기서 다시 계산되므로 값을 따로 맞출 필요가 없다.
	const FRotator BoomRotation = PlayerCharacter->CameraBoom->GetRelativeRotation();
	const FRotator ListenerRotation(-BoomRotation.Pitch, 0.f, 0.f);

	SetAudioListenerOverride(PlayerCharacter->CameraBoom, FVector::ZeroVector, ListenerRotation);
}
