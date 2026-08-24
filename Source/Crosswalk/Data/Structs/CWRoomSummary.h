// CWRoomSummary.h

#pragma once

#include "CoreMinimal.h"
#include "CWRoomSummary.generated.h"

// 방 목록 UI에 표시할 세션 요약 정보
USTRUCT(BlueprintType)
struct CROSSWALK_API FCWRoomSummary
{
	GENERATED_BODY()

#pragma region Comparison

public:
	// MVVM 배열 변경 검사에서 방 표시 정보가 같은지 비교
	bool operator==(
		const FCWRoomSummary& Other
		) const
	{
		return
			RoomCode == Other.RoomCode &&
			HostName == Other.HostName &&
			CurrentPlayers == Other.CurrentPlayers &&
			MaxPlayers == Other.MaxPlayers;
	}

	// 방 표시 정보가 다른지 비교
	bool operator!=(
		const FCWRoomSummary& Other
		) const
	{
		return !(*this == Other);
	}

#pragma endregion

#pragma region RoomInformation

public:
	// 플레이어가 입력해서 참가할 방 코드
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Session")
	FString RoomCode;

	// 방을 생성한 호스트 이름
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Session")
	FString HostName;

	// 현재 방에 참가 중인 플레이어 수
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Session")
	int32 CurrentPlayers = 0;

	// 해당 방에 참가 가능한 최대 플레이어 수
	UPROPERTY(BlueprintReadOnly, Category = "Crosswalk|Session")
	int32 MaxPlayers = 0;

#pragma endregion
};
