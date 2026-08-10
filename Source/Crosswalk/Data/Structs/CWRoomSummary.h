// Copyright Crosswalk Team. All Rights Reserved.
// CWRoomSummary.h

#pragma once

#include "CoreMinimal.h"
#include "CWRoomSummary.generated.h"

// 방 목록 UI에 표시할 세션 요약 정보
USTRUCT(BlueprintType)
struct CROSSWALK_API FCWRoomSummary
{
	GENERATED_BODY()

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
};