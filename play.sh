#!/bin/zsh
# Start the game, then the tracker in this terminal (the camera permission belongs to the terminal you run this from).
# Ctrl+C stops the tracker. The game keeps working with mouse and keys without it.
PROJECT="${FG_PROJECT:-$HOME/Downloads/FingerGunGame-main 5.8 - 2/FingerGunGame.uproject}"
UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
HERE="${0:A:h}"
"$UE" "$PROJECT" /Game/Levels/IronHorse -game -windowed -ResX=1600 -ResY=900 > /dev/null 2>&1 &
cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window "$@"
