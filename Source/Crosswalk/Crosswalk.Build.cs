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
        });

        DynamicallyLoadedModuleNames.AddRange(new string[]
        {
            "OnlineSubsystemEOS",
            "OnlineSubsystemSteam"
        });
    }
}
