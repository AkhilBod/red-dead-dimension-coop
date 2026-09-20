#!/bin/zsh
# Builds a standalone Mac app (no editor needed to play). About 10 minutes on an M3 Air once shaders are cached.
# Output: Packaged/Mac/FingerGunGame.app
# Close the game and any Unreal window first: a second UnrealBuildTool is refused ("ConflictingInstance").
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
HERE="${0:A:h}"
"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$HERE/../FingerGunGame.uproject" -noP4 -platform=Mac -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$HERE/../Packaged" -map=/Game/Levels/IronHorse -IgnoreCookErrors -iterate -utf8output || exit 1
# The archive step leaves the content out of the bundle. The staged one is complete, so use that.
rm -rf "$HERE/../Packaged/Mac/FingerGunGame.app"
ditto "$HERE/../Saved/StagedBuilds/Mac/FingerGunGame.app" "$HERE/../Packaged/Mac/FingerGunGame.app"
echo "Packaged/Mac/FingerGunGame.app is ready. Its log: ~/Library/Containers/com.YourCompany.FingerGunGame/Data/Library/Logs/FingerGunGame/"
