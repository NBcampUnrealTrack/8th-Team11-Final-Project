// CWStageSelectionGameStateBase.cpp

#include "Core/Stage/CWStageSelectionGameStateBase.h"
#include "Net/UnrealNetwork.h"

#pragma region GameStateLifecycle

void ACWStageSelectionGameStateBase::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	// AGameStateBase가 등록하는 기본 복제 프로퍼티를 먼저 유지함
	// RoomGameState와 CWGameState도 각자의 override에서 Super를 호출하므로
	// 공용 선택 상태와 파생 클래스 상태가 모두 복제됨
	Super::GetLifetimeReplicatedProps(
		OutLifetimeProps
	);

	// 서버가 결정한 선택창 열림 상태를 모든 클라이언트에 공유함
	DOREPLIFETIME(
		ACWStageSelectionGameStateBase,
		bIsStageSelectionOpen
	);

	// 호스트가 현재 선택한 StageId를 모든 클라이언트에 공유함
	DOREPLIFETIME(
		ACWStageSelectionGameStateBase,
		SelectedStageId
	);
}

#pragma endregion

#pragma region StageSelection

void ACWStageSelectionGameStateBase::SetStageSelectionOpen(bool bOpen)
{
	// 복제 상태의 최종 결정권은 서버에만 있음
	// 클라이언트가 직접 호출해도 상태를 변경할 수 없음
	if (!HasAuthority())
	{
		return;
	}

	// 같은 값이면 불필요한 델리게이트와 네트워크 갱신을 생략함
	if (bIsStageSelectionOpen == bOpen)
	{
		return;
	}

	bIsStageSelectionOpen = bOpen;

	// Listen Server 호스트는 자기 서버 값 변경 시 OnRep가 호출되지 않음
	// 따라서 서버 UI가 즉시 반응하도록 Setter에서 직접 알림
	NotifyStageSelectionStateChanged();

	// 선택창 열기·닫기는 즉시 UI에 반영되어야 하므로
	// 다음 일반 복제 주기를 기다리지 않고 빠른 갱신을 요청함
	ForceNetUpdate();
}

void ACWStageSelectionGameStateBase::SetSelectedStageId(FName InStageId)
{
	// 현재 선택값도 서버에서만 변경할 수 있음
	if (!HasAuthority())
	{
		return;
	}

	// 동일한 StageId를 반복 선택한 경우 중복 알림을 만들지 않음
	if (SelectedStageId == InStageId)
	{
		return;
	}

	SelectedStageId = InStageId;

	// Listen Server의 로컬 UI에 현재 선택을 즉시 반영함
	NotifyStageSelectionStateChanged();

	// 원격 클라이언트에도 현재 선택을 빠르게 전송함
	ForceNetUpdate();
}

void ACWStageSelectionGameStateBase::OnRep_IsStageSelectionOpen()
{
	// 원격 클라이언트가 서버의 열림 상태를 받으면
	// ViewModel이 두 선택 상태를 다시 읽도록 알림
	NotifyStageSelectionStateChanged();
}

void ACWStageSelectionGameStateBase::OnRep_SelectedStageId()
{
	// 원격 클라이언트가 호스트의 현재 선택을 받으면
	// 목록 하이라이트와 미리보기를 갱신하도록 알림
	NotifyStageSelectionStateChanged();
}

void ACWStageSelectionGameStateBase::NotifyStageSelectionStateChanged()
{
	// 두 프로퍼티가 각각 복제되어 알림이 두 번 발생할 수 있음
	// 구독자는 이벤트를 받으면 열림 여부와 StageId를 함께 재조회함
	OnStageSelectionStateChanged.Broadcast();
}

#pragma endregion
