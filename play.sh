#!/bin/zsh
# Start the game, then the tracker in this terminal (the camera permission belongs to the terminal you run this from).
# Ctrl+C stops the tracker. The game keeps working with mouse and keys without it.
#
#   ./play.sh                          the main menu: ride alone, host co-op or a 1v1 duel, or join
#   ./play.sh --coop-host              host a co-op ride: waits on the title for a partner
#   ./play.sh --duel-host              host a 1v1 quick-draw duel against your partner
#   ./play.sh --coop-join 192.168.1.20 join a partner's ride or duel
#   ./play.sh --app ...                the packaged app instead of the editor binary
# Anything after these goes to the tracker (e.g. --host for a tracker on another machine).
UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
HERE="${0:A:h}"
# The copy in this repo needs building once (open the .uproject, say yes to rebuild). Until then, Akhil's working copy.
PROJECT="${FG_PROJECT:-$HERE/unreal/FingerGunGame/FingerGunGame.uproject}"
[[ -d "${PROJECT:h}/Binaries" ]] || PROJECT="$HOME/Downloads/FingerGunGame-main 5.8 - 2/FingerGunGame.uproject"
APP="${PROJECT:h}/Packaged/Mac/FingerGunGame.app"
MAP=/Game/Levels/IronHorse
USE_APP=0
while [[ "$1" == --app || "$1" == --coop-host || "$1" == --duel-host || "$1" == --coop-join ]]; do
  case "$1" in
    --app) USE_APP=1; shift ;;
    --coop-host) MAP="/Game/Levels/IronHorse?listen"; shift ;;
    --duel-host) MAP="/Game/Levels/IronHorse?listen?versus"; shift ;;
    --coop-join) MAP="$2"; shift 2 ;;
  esac
done
# --app runs the packaged standalone app instead of the editor binary: lighter, so the tracker keeps its frame rate.
if (( USE_APP )); then
  [[ -d "$APP" ]] || { echo "No packaged app at $APP. Build it with unreal/FingerGunGame/Scripts/package_mac.sh"; exit 1; }
  "$APP/Contents/MacOS/FingerGunGame" "$MAP" -windowed -ResX=1600 -ResY=900 > /dev/null 2>&1 &
  cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window --record "$@"
fi
# The game's log goes to Saved/play.log next to the project, so a problem in a session can be looked up afterwards.
"$UE" "$PROJECT" "$MAP" -game -windowed -ResX=1600 -ResY=900 -abslog="${PROJECT:h}/Saved/play.log" > /dev/null 2>&1 &
# Every session is recorded (landmarks only, no video, gitignored) so tracking problems can be replayed and tuned afterwards.
cd "$HERE/tracker" && exec .venv/bin/python run.py --no-window --record "$@"
