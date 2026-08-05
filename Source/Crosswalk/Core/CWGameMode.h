#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CWGameMode.generated.h"

/**
 * 프로토타입 게임모드.
 *
 * 기본값으로 C++ 캐릭터/컨트롤러를 물려 둔다.
 * 메쉬와 입력 애셋을 붙인 BP_PlayerCharacter 를 만들었으면
 * 이 클래스를 상속한 BP_GameMode 에서 DefaultPawnClass 만 갈아 끼우면 된다.
 */
UCLASS()
class CROSSWALK_API ACWGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACWGameMode();
};
