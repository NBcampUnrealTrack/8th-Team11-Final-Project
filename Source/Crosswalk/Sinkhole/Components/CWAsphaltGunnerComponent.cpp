// CWAsphaltGunnerComponent.cpp

#include "Sinkhole/Components/CWAsphaltGunnerComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Sinkhole/Tools/CWAsphaltGun.h"

UCWAsphaltGunnerComponent::UCWAsphaltGunnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
}

void UCWAsphaltGunnerComponent::Fire()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	ACWAsphaltGun* Gun = GetHeldAsphaltGun();
	if (Gun == nullptr)
	{
		return;
	}

	// 입력 하한. 연타나 입력 중복으로 같은 프레임에 여러 번 나가는 것을 막는다.
	// 밸런스용 연사 간격은 서버가 따로 본다.
	const float Now = World->GetTimeSeconds();
	if (Now - LastFireRequestTime < Gun->GetInputMinInterval())
	{
		return;
	}

	FVector AimLocation;
	if (!GetAimLocationUnderCursor(AimLocation))
	{
		return;
	}

	LastFireRequestTime = Now;

	// 총구 연출만 즉시 재생한다. 궤적은 서버가 착탄점을 확정한 뒤에 온다.
	OnLocalFireRequested();

	Server_Fire(AimLocation);
}

void UCWAsphaltGunnerComponent::Reload()
{
	Server_Reload();
}

ACWAsphaltGun* UCWAsphaltGunnerComponent::GetHeldAsphaltGun() const
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	const UCWToolHolderComponent* ToolHolder = Owner->FindComponentByClass<UCWToolHolderComponent>();
	if (ToolHolder == nullptr)
	{
		return nullptr;
	}

	return Cast<ACWAsphaltGun>(ToolHolder->GetHeldTool());
}

void UCWAsphaltGunnerComponent::Server_Fire_Implementation(const FVector& AimLocation)
{
	// 클라이언트가 조준 지점을 보내지만, 쏠 수 있는지와 어디에 떨어지는지는 서버가 다시 정한다.
	if (ACWAsphaltGun* Gun = GetHeldAsphaltGun())
	{
		Gun->TryFire(AimLocation);
	}
}

void UCWAsphaltGunnerComponent::Server_Reload_Implementation()
{
	if (ACWAsphaltGun* Gun = GetHeldAsphaltGun())
	{
		Gun->TryReload();
	}
}

bool UCWAsphaltGunnerComponent::GetAimLocationUnderCursor(FVector& OutAimLocation) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn == nullptr)
	{
		return false;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController());
	if (PlayerController == nullptr)
	{
		return false;
	}

	// 지면(Visibility)을 직접 맞힌다. 쿼터뷰라 커서 아래에 항상 도로가 있다.
	FHitResult HitResult;
	if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		OutAimLocation = HitResult.ImpactPoint;
		return true;
	}

	return false;
}
