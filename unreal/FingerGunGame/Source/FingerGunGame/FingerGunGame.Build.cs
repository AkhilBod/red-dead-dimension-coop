using UnrealBuildTool;

public class FingerGunGame : ModuleRules
{
    public FingerGunGame(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "EnhancedInput",
                "UMG",
                "Sockets",
                "Networking",
                "ImageWrapper",
                "Json",
                "RenderCore",
                "RHI"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Slate",
                "SlateCore"
            }
        );
    }
}