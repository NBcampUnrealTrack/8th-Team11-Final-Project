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
