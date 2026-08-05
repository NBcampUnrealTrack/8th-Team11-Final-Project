#include "Core/CWGameMode.h"

#include "Player/CWPlayerCharacter.h"
#include "Player/CWPlayerController.h"

ACWGameMode::ACWGameMode()
{
	DefaultPawnClass = ACWPlayerCharacter::StaticClass();
	PlayerControllerClass = ACWPlayerController::StaticClass();
}
