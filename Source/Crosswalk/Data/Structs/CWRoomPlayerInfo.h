// CWRoomPlayerInfo.h

#pragma once

#include "CoreMinimal.h"
#include "CWRoomPlayerInfo.generated.h"

class APlayerState;

USTRUCT(BlueprintType)
struct CROSSWALK_API FCWRoomPlayerInfo
{
	GENERATED_BODY()

#pragma region PlayerInformation

public:
	// 서버가 부여한 플레이어 번호로 화면마다 동일한 목록 순서를 유지
	UPROPERTY(BlueprintReadOnly, Category = "CW|Room")
	int32 PlayerId = 0;

	// 방 화면에 표시할 플레이어 이름
	UPROPERTY(BlueprintReadOnly, Category = "CW|Room")
	FText PlayerName;

	// 해당 플레이어가 게임 시작 준비를 완료했는지 나타냄
	UPROPERTY(BlueprintReadOnly, Category = "CW|Room")
	bool bIsReady = false;

	// 현재 로컬 플레이어 자신인지 나타냄
	UPROPERTY(BlueprintReadOnly, Category = "CW|Room")
	bool bIsLocalPlayer = false;

	// 체온처럼 계속 바뀌는 값을 위젯이 직접 읽기 위한 참조
	// 인게임 목록에서만 채움
	UPROPERTY(BlueprintReadOnly, Category = "CW|Room")
	TObjectPtr<APlayerState> PlayerState;

#pragma endregion

#pragma region Comparison

public:
	// MVVM 배열 변경 검사에서 플레이어 표시 정보가 같은지 비교
	bool operator==(
		const FCWRoomPlayerInfo& Other
		) const
	{
		return
			PlayerId == Other.PlayerId &&
			PlayerName.EqualTo(Other.PlayerName) &&
			bIsReady == Other.bIsReady &&
			bIsLocalPlayer == Other.bIsLocalPlayer;
	}

	// 플레이어 표시 정보가 다른지 비교
	bool operator!=(
		const FCWRoomPlayerInfo& Other
		) const
	{
		return !(*this == Other);
	}

#pragma endregion
};
