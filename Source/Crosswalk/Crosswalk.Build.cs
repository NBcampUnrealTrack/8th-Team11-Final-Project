// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Crosswalk : ModuleRules
{
    public Crosswalk(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Lets code include headers by subfolder-qualified path (e.g. "Characters/CWPlayerCharacter.h")
        // from anywhere else in this module, instead of only from the same folder.
        PublicIncludePaths.AddRange(new string[]
        {
            ModuleDirectory
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",

            // 테두리 스케치 미니게임 위젯이 Slate 로 선을 직접 그린다.
            "OnlineBase",
            "OnlineSubsystem",
            "OnlineSubsystemUtils",

            // UI
            "UMG",
            "Slate",
            "SlateCore",
            "ModelViewViewModel",
            "FieldNotification",

            // 피격 이펙트를 코드에서 스폰한다.
            "Niagara",

            // ACWWorkZone_Cells::FlushFieldTextureUpdates 가 칸 상태 텍스처의 더러워진 사각형만
            // 부분 업로드할 때 FUpdateTextureRegion2D 를 직접 만든다. Texture2D.h 에는 전방 선언만
            // 있어서 실제 생성에는 이 모듈이 필요하다. (Painting/Zone/CWWorkZone_Cells.cpp)
            "RHI",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // 클립보드 복사(FPlatformApplicationMisc) 용
            "ApplicationCore"
        });

        DynamicallyLoadedModuleNames.AddRange(new string[]
        {
            "OnlineSubsystemSteam",

            // Steam은 같은 계정으로 인스턴스 2개를 띄워 혼자 검색/참가를 테스트할 수 없다
            // (엔진 소스 확인 - OnlineAsyncTaskManagerSteam.cpp의 OnLobbyDataUpdate가 로비
            // 소유자와 요청자의 SteamID가 같으면 콜백 자체를 버림). 로컬에서 세션/UI 로직만
            // 빠르게 검증하고 싶을 땐 DefaultEngine.ini의 DefaultPlatformService를 Null로
            // 바꾸면 멀티 PIE로 바로 된다 - 코드 수정 없이 ini 한 줄로 전환.
            "OnlineSubsystemNull"
        });
    }
}
