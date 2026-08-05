// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Crosswalk : ModuleRules
{
    public Crosswalk(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Lets code include headers by subfolder-qualified path (e.g. "Character/CWCharacter.h")
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
            "OnlineBase",
            "OnlineSubsystem",
            "OnlineSubsystemUtils"
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