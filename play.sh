#!/bin/zsh
# Start the game, then the tracker in this terminal (the camera permission belongs to the terminal you run this from).
# Ctrl+C stops the tracker. The game keeps working with mouse and keys without it.
UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
HERE="${0:A:h}"
# The copy in this repo needs building once (open the .uproject, say yes to rebuild). Until then, Akhil's working copy.
PROJECT="${FG_PROJECT:-$HERE/unreal/FingerGunGame/FingerGunGame.uproject}"
[[ -d "${PROJECT:h}/Binaries" ]] || PROJECT="$HOME/Downloads/FingerGunGame-main 5.8 - 2/FingerGunGame.uproject"
"$UE" "$PROJECT" /Game/Levels/IronHorse -game -windowed -ResX=1600 -ResY=900 > /dev/null 2>&1 &
# Every session is recorded (landmarks only, no video, gitignored) so tracking problems can be replayed and tuned afterwards.
cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window --record "$@"
