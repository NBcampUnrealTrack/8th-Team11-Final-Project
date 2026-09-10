// Copyright Crosswalk Team. All Rights Reserved.
// CWSessionTypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWSessionTypes.generated.h"

// 로그인 실패 시 UI가 표시할 실패 원인을 구분하기 위한 타입
UENUM(BlueprintType)
enum class ECWLoginFailure : uint8
{
	None UMETA(DisplayName = "None"),
	Cancelled UMETA(DisplayName = "Cancelled"),
	NoConnection UMETA(DisplayName = "No Connection"),
	ServiceUnavailable UMETA(DisplayName = "Service Unavailable"),
	NotConfigured UMETA(DisplayName = "Not Configured"),
	Unknown UMETA(DisplayName = "Unknown")
};

// 로그인에 사용한 온라인 플랫폼 - 어느 OnlineSubsystem 인스턴스를 쓸지 구분하기 위한 타입.
// Steam은 EOS를 거치지 않고 독립된 백엔드로 운영한다 -
// 그래서 에픽/DevAuth 유저와 Steam 유저는 서로 다른 방 목록을 본다.
UENUM(BlueprintType)
enum class ECWOnlinePlatform : uint8
{
	EOS UMETA(DisplayName = "EOS"),
	Steam UMETA(DisplayName = "Steam")
};

// 세션 관련 작업 완료 이벤트에서 어떤 작업의 결과인지 구분하기 위한 타입
UENUM(BlueprintType)
enum class ECWSessionOperation : uint8
{
	None UMETA(DisplayName = "None"),
	CreateRoom UMETA(DisplayName = "Create Room"),
	FindRooms UMETA(DisplayName = "Find Rooms"),
	JoinRoom UMETA(DisplayName = "Join Room"),
	DestroyRoom UMETA(DisplayName = "Destroy Room")
};

// 세션 작업 실패 시 UI가 표시할 실패 원인을 구분하기 위한 타입
UENUM(BlueprintType)
enum class ECWSessionFailure : uint8
{
	None UMETA(DisplayName = "None"),
	NotLoggedIn UMETA(DisplayName = "Not Logged In"),
	// 다른 세션 작업이 아직 끝나지 않아 요청이 거절됨. 진행 중이던 작업은 그대로 살아 있다
	Busy UMETA(DisplayName = "Busy"),
	RoomNotFound UMETA(DisplayName = "Room Not Found"),
	RoomFull UMETA(DisplayName = "Room Full"),
	AlreadyInSession UMETA(DisplayName = "Already In Session"),
	RoomCodeCollision UMETA(DisplayName = "Room Code Collision"),
	Timeout UMETA(DisplayName = "Timeout"),
	Unknown UMETA(DisplayName = "Unknown")
};