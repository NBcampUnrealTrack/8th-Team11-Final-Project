#include "Core/CWGameMode.h"

#include "Characters/CWPlayerCharacter.h"
#include "Characters/CWPlayerController.h"

ACWGameMode::ACWGameMode()
{
	DefaultPawnClass = ACWPlayerCharacter::StaticClass();
	PlayerControllerClass = ACWPlayerController::StaticClass();
}
