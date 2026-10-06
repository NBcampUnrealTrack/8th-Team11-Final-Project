// CWAsphaltGunnerComponent.cpp

#include "Sinkhole/Components/CWAsphaltGunnerComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Painting/Components/CWToolHolderComponent.h"
#include "Sinkhole/Tools/CWAsphaltGun.h"

namespace
{
	/**
	 * 아스팔트건 조준 전용 트레이스 채널. DefaultEngine.ini 의 "AimGround" 와 같은 슬롯이다.
	 *
	 * 기본 응답이 Ignore 라서, 콜리전 프로필을 CWGround 로 둔 바닥 메시(도로·인도·바닥 타일)만 이 채널을 막는다.
	 * 슬롯 번호를 바꾸면 ini 의 Channel=ECC_GameTraceChannel1 도 같이 바꿔야 한다.
	 */
	constexpr ECollisionChannel AimGroundChannel = ECC_GameTraceChannel1;
}

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

	// 바닥만 막는 채널로 맞힌다. Visibility 로 맞히면 커서 아래 첫 물체(건물 벽·지붕, 가로등, 차)에 박혔다.
	// 건물 위를 가리켜도 그 너머의 바닥에 맞고, 경사로는 실제 바닥 표면을 따라간다.
	FHitResult HitResult;
	if (PlayerController->GetHitResultUnderCursor(AimGroundChannel, false, HitResult))
	{
		OutAimLocation = HitResult.ImpactPoint;
		return true;
	}

	// TODO(임시): 모든 스테이지의 바닥 메시를 CWGround 프로필로 바꾸면 이 대체 경로를 지운다.
	//
	// 바닥 표시는 레벨 담당과 맞춰 맵마다 순차로 진행한다. 그 전까지 AimGround 만 쓰면 표시 안 된 맵에서
	// 아스팔트건이 아예 발사되지 않는다. 그래서 바닥을 못 찾으면 예전처럼 커서 아래 첫 물체(Visibility)를 쓴다.
	// 표시가 끝난 맵에서는 바닥이 항상 먼저 잡히므로 이 경로로 내려오지 않는다.
	if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		OutAimLocation = HitResult.ImpactPoint;
		return true;
	}

	return false;
}
