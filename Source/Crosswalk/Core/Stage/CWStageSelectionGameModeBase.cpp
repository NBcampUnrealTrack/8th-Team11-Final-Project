// CWStageSelectionGameModeBase.cpp

#include "Core/Stage/CWStageSelectionGameModeBase.h"
#include "Core/Stage/CWStageSelectionGameStateBase.h"
#include "Data/Assets/CWStageCatalog.h"
#include "Data/Structs/CWStageInfo.h"
#include "Engine/World.h"

#pragma region StageSelectionRequests

bool ACWStageSelectionGameModeBase::BeginStageSelection()
{
	// 선택창 상태는 서버에서만 변경할 수 있음
	if (!HasAuthority())
	{
		return false;
	}

	ACWStageSelectionGameStateBase* StageSelectionGameState =
		GetStageSelectionGameState();

	if (!IsValid(StageSelectionGameState))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"StageSelectionGameMode: "
				"StageSelectionGameState is invalid."
			)
		);

		return false;
	}

	// 파생 GameMode가 정의한 현재 화면의 시작 조건을 검사함
	if (!CanOpenStageSelection())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"stage selection conditions are not satisfied."
			)
		);

		return false;
	}

	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// 이미 다른 맵으로 이동 중이면 새로운 선택창을 열지 않음
	if (!World->NextURL.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"travel is already pending."
			)
		);

		return false;
	}

	// 이미 열린 상태에서 다시 요청한 경우 현재 선택을 초기화하지 않음
	if (StageSelectionGameState->IsStageSelectionOpen())
	{
		return true;
	}

	// 이전 선택값을 지우고 전원에게 선택창을 표시함
	StageSelectionGameState->SetSelectedStageId(NAME_None);
	StageSelectionGameState->SetStageSelectionOpen(true);

	return true;
}

bool ACWStageSelectionGameModeBase::SelectStage(FName StageId)
{
	// 현재 선택은 서버에서만 변경할 수 있음
	if (!HasAuthority())
	{
		return false;
	}

	ACWStageSelectionGameStateBase* StageSelectionGameState =
		GetStageSelectionGameState();

	if (!IsValid(StageSelectionGameState))
	{
		return false;
	}

	// 선택창이 열린 동안에만 선택 변경을 허용함
	if (!StageSelectionGameState->IsStageSelectionOpen())
	{
		return false;
	}

	// None은 실제 선택값으로 허용하지 않음
	if (StageId.IsNone())
	{
		return false;
	}

	// 서버가 보유한 Catalog에서 StageId를 다시 검증함
	if (!IsValid(StageCatalog))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"StageSelectionGameMode: "
				"StageCatalog is not configured."
			)
		);

		return false;
	}

	const FCWStageInfo* StageInfo =
		StageCatalog->FindStageById(StageId);

	if (StageInfo == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"StageId '%s' was not found."
			),
			*StageId.ToString()
		);

		return false;
	}

	// 검증된 현재 선택을 모든 클라이언트에 복제함
	StageSelectionGameState->SetSelectedStageId(StageId);

	return true;
}

bool ACWStageSelectionGameModeBase::CancelStageSelection()
{
	// 선택창 닫기도 서버에서만 처리함
	if (!HasAuthority())
	{
		return false;
	}

	ACWStageSelectionGameStateBase* StageSelectionGameState =
		GetStageSelectionGameState();

	if (!IsValid(StageSelectionGameState))
	{
		return false;
	}

	// 현재 선택을 지우고 전원의 선택창을 닫음
	StageSelectionGameState->SetSelectedStageId(NAME_None);
	StageSelectionGameState->SetStageSelectionOpen(false);

	return true;
}

bool ACWStageSelectionGameModeBase::TryStartGame(FName StageId)
{
	// 실제 맵 이동은 서버에서만 실행할 수 있음
	if (!HasAuthority())
	{
		return false;
	}

	ACWStageSelectionGameStateBase* StageSelectionGameState =
		GetStageSelectionGameState();

	if (!IsValid(StageSelectionGameState))
	{
		return false;
	}

	// 선택창이 열린 상태에서만 확정 요청을 허용함
	if (!StageSelectionGameState->IsStageSelectionOpen())
	{
		return false;
	}

	// 파생 GameMode가 현재 화면의 시작 조건을 다시 검사함
	// 선택창이 열린 사이 준비 해제·클리어 상태 변경 등이 생길 수 있음
	if (!CanStartSelectedStage())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"start conditions are not satisfied."
			)
		);

		return false;
	}

	if (StageId.IsNone())
	{
		return false;
	}

	// 화면에 복제된 현재 선택과 확정 요청값이 일치해야 함
	if (StageSelectionGameState->GetSelectedStageId() != StageId)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"requested StageId '%s' does not match "
				"the replicated selection '%s'."
			),
			*StageId.ToString(),
			*StageSelectionGameState
				->GetSelectedStageId()
				.ToString()
		);

		return false;
	}

	if (!IsValid(StageCatalog))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"StageSelectionGameMode: "
				"StageCatalog is not configured."
			)
		);

		return false;
	}

	// 클라이언트 UI가 전달한 StageId를 그대로 신뢰하지 않고
	// 서버 Catalog에서 다시 검색함
	const FCWStageInfo* StageInfo =
		StageCatalog->FindStageById(StageId);

	if (StageInfo == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"StageSelectionGameMode: "
				"StageId '%s' was not found."
			),
			*StageId.ToString()
		);

		return false;
	}

	// TODO(진행 저장 계약): 잠금·해금 조건을 서버에서 추가 검증해야 함

	if (StageInfo->GameplayMap.IsNull())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"StageSelectionGameMode: "
				"GameplayMap is not configured for '%s'."
			),
			*StageId.ToString()
		);

		return false;
	}

	UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return false;
	}

	// 중복 확정으로 ServerTravel이 여러 번 호출되는 것을 방지함
	if (!World->NextURL.IsEmpty())
	{
		return false;
	}

	const FString GameplayMapPath =
		StageInfo
		->GameplayMap
		.ToSoftObjectPath()
		.GetLongPackageName();

	if (GameplayMapPath.IsEmpty())
	{
		return false;
	}

	// RoomGameState와 인게임 GameState는 Travel 후 교체됨
	// 다음 인게임 GameMode가 현재 StageId를 복구할 수 있도록 URL에 전달함
	const FString TravelURL =
		FString::Printf(
			TEXT("%s?StageId=%s"),
			*GameplayMapPath,
			*StageId.ToString()
		);

	const bool bTravelStarted =
		World->ServerTravel(TravelURL);

	if (!bTravelStarted)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"StageSelectionGameMode: "
				"ServerTravel failed. URL=%s"
			),
			*TravelURL
		);

		return false;
	}

	// Travel 요청에 성공한 경우에만 확정값과 닫힘 상태를 반영함
	StageSelectionGameState->SetSelectedStageId(StageId);
	StageSelectionGameState->SetStageSelectionOpen(false);

	UE_LOG(
		LogTemp,
		Log,
		TEXT(
			"StageSelectionGameMode: "
			"ServerTravel started. URL=%s"
		),
		*TravelURL
	);

	return true;
}

#pragma endregion

#pragma region StageSelectionValidation

ACWStageSelectionGameStateBase*
ACWStageSelectionGameModeBase::
GetStageSelectionGameState() const
{
	return GetGameState<ACWStageSelectionGameStateBase>();
}

#pragma endregion
