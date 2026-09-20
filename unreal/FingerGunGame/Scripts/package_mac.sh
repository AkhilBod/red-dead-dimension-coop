#!/bin/zsh
# Builds a standalone Mac app (no editor needed to play). Takes 30-40 minutes on an M3 Air the first time: shaders.
# Output: Packaged/Mac/FingerGunGame.app
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
HERE="${0:A:h}"
"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$HERE/../FingerGunGame.uproject" -noP4 -platform=Mac -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$HERE/../Packaged" -map=/Game/Levels/IronHorse -utf8output
