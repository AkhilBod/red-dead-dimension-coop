#!/bin/zsh
# Start the game, then the tracker in this terminal (the camera permission belongs to the terminal you run this from).
# Ctrl+C stops the tracker. The game keeps working with mouse and keys without it.
UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
HERE="${0:A:h}"
# The copy in this repo needs building once (open the .uproject, say yes to rebuild). Until then, Akhil's working copy.
PROJECT="${FG_PROJECT:-$HERE/unreal/FingerGunGame/FingerGunGame.uproject}"
[[ -d "${PROJECT:h}/Binaries" ]] || PROJECT="$HOME/Downloads/FingerGunGame-main 5.8 - 2/FingerGunGame.uproject"
# ./play.sh --app  runs the packaged standalone app instead of the editor binary: lighter, so the tracker keeps its frame rate.
APP="${PROJECT:h}/Packaged/Mac/FingerGunGame.app"
if [[ "$1" == "--app" ]]; then
  shift
  [[ -d "$APP" ]] || { echo "No packaged app at $APP. Build it with unreal/FingerGunGame/Scripts/package_mac.sh"; exit 1; }
  "$APP/Contents/MacOS/FingerGunGame" -windowed -ResX=1600 -ResY=900 > /dev/null 2>&1 &
  cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window --record "$@"
fi
# The game's log goes to Saved/play.log next to the project, so a problem in a session can be looked up afterwards.
"$UE" "$PROJECT" /Game/Levels/IronHorse -game -windowed -ResX=1600 -ResY=900 -abslog="${PROJECT:h}/Saved/play.log" > /dev/null 2>&1 &
# Every session is recorded (landmarks only, no video, gitignored) so tracking problems can be replayed and tuned afterwards.
cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window --record "$@"
