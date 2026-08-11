// CWUITypes.h

#pragma once

#include "CoreMinimal.h"
#include "CWUITypes.generated.h"

#pragma region Page

// Screen Layer에 배타적으로 표시할 페이지
UENUM(BlueprintType)
enum class ECWUIPage : uint8
{
    // 현재 표시할 페이지가 없는 상태
    None,
    // 게임 타이틀 화면
    Title,
    // 메인 메뉴
    MainMenu,
    // 방 생성 화면
    CreateRoom,
    // 참가 코드 입력 화면
    JoinRoom,
    // 방 대기 화면
    Room,
    // 플레이할 스테이지를 선택하는 화면
    StageSelect,
    // 레벨 이동 및 플레이어 대기 화면
    Loading,
    // 결과를 표시 화면
    Result,
    // 게임 설정 화면
    Settings,
    // 멀티플레이 기능 선택 화면
    MultiplayerMenu,
    // 현재 생성된 공개 방 목록 화면
    RoomList
};

#pragma endregion

#pragma region Input

// 로컬 플레이어에게 적용할 UI 입력 방식
UENUM(BlueprintType)
enum class ECWUIInputMode : uint8
{
    // 게임플레이 입력만 사용
    GameOnly,
    // UI 입력만 사용
    UIOnly,
    // 게임플레이와 UI 입력을 함께 사용
    GameAndUI
};

#pragma endregion