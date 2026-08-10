#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CWPlayerController.generated.h"

/**
 * 쿼터뷰 플레이어 컨트롤러.
 *
 * 마우스 커서를 켜는 것과 오디오 리스너 위치를 잡는 것, 두 가지를 한다.
 *
 * 커서는 반드시 켜져 있어야 한다. UCWWorkerComponent 가 커서 아래를 트레이스해서
 * 작업 지점을 잡기 때문에, 꺼져 있으면 도구를 들고 있어도 게이지가 전혀 오르지 않는다.
 */
UCLASS()
class CROSSWALK_API ACWPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACWPlayerController();

protected:
	/** 서버가 폰을 붙이는 시점. 리슨 서버 호스트는 이 경로로 리스너가 잡힌다. */
	virtual void OnPossess(APawn* InPawn) override;

	/**
	 * 클라이언트가 폰 소유를 확인하는 시점.
	 * OnPossess 는 서버에서만 호출되므로 원격 클라이언트는 여기서 잡아야 한다.
	 */
	virtual void AcknowledgePossession(APawn* InPawn) override;

	/** 폰을 놓을 때 오버라이드를 되돌린다. 안 그러면 사라진 컴포넌트를 계속 가리킨다. */
	virtual void OnUnPossess() override;

private:
	/**
	 * 소리를 듣는 지점을 카메라에서 캐릭터 위치로 옮긴다. 화면에 보이는 그림은 건드리지 않는다.
	 *
	 * 쿼터뷰라 카메라가 캐릭터 뒤 1200cm 상공에 있는데, 리스너는 기본적으로 카메라를 따라간다.
	 * 그대로 두면 내 발밑에서 난 소리가 12m 밖 소리로 계산돼서 감쇠를 걸 수가 없다.
	 * 그렇다고 감쇠 거리를 카메라 거리만큼 늘리면 화면 끝의 소리까지 다 들려 감쇠가 무의미해진다.
	 *
	 * 부착 대상으로 CameraBoom 을 쓴다. 붐의 원점은 캐릭터 위치에 있고 회전은
	 * SetUsingAbsoluteRotation 으로 고정돼 있어서, 위치는 캐릭터를 따라가면서
	 * 좌우 정위 기준은 화면과 계속 일치한다.
	 *
	 * 캐릭터 루트나 메시에 붙이면 안 된다. bOrientRotationToMovement 로 캐릭터가
	 * 이동 방향마다 회전하기 때문에, 방향을 틀 때마다 좌우 채널이 통째로 뒤집힌다.
	 */
	void ApplyAudioListenerOverride(APawn* InPawn);
};
