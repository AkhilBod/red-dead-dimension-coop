#!/bin/zsh
# Two copies of the game on this Mac, a host and a guest joined to it: co-op without a second computer.
#
#   Scripts/coop_test.sh              both play themselves (-FGAuto), screenshots and frame times in Saved/
#   Scripts/coop_test.sh --manual     you play the host with the mouse; the guest plays itself
#   Scripts/coop_test.sh --god        nobody takes damage (a long soak)
#   Scripts/coop_test.sh --versus     a 1v1 quick-draw duel instead of riding together
#
# More switches for either side through the environment, e.g. straight to the duel with the guest on a slow line:
#   HOST_EXTRA="-FGStage=3" GUEST_EXTRA='-ExecCmds="NetEmulation.PktLag 60"' Scripts/coop_test.sh
# GUEST_DELAY=60 joins the guest a minute into the host's ride (with HOST_EXTRA=-FGAlone so the host does not wait).
#
# The guest runs mouse-only (-FGNoTracker) so the webcam tracker, if running, drives the host.
# Logs: Saved/host.log, Saved/guest.log. "IronHorse: chunk N <name>" lines must match between them.

cd "$(dirname "$0")/.."
UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
PROJECT="$PWD/FingerGunGame.uproject"

HOST=(-FGPerf -FGShots=${SHOTS:-20} -nosound)
GUEST=(-FGPerf -FGShots=${SHOTS:-20} -nosound -FGNoTracker -FGAuto)
[[ "$*" != *--manual* ]] && HOST+=(-FGAuto)
[[ "$*" == *--manual* ]] && HOST=(-FGPerf)
[[ "$*" == *--god* ]] && HOST+=(-FGGod)
MAP="/Game/Levels/IronHorse?listen"
[[ "$*" == *--versus* ]] && MAP="/Game/Levels/IronHorse?listen?versus"

eval "\"\$UE\" \"\$PROJECT\" \"\$MAP\" -game -windowed -ResX=960 -ResY=540 -WinX=0 -WinY=40 \${HOST[@]} $HOST_EXTRA -abslog=\"\$PWD/Saved/host.log\" &"
HOST_PID=$!
# Let the host load and start listening first.
for i in {1..120}; do grep -q "hosting co-op" Saved/host.log 2>/dev/null && break; sleep 1; done
sleep $(( 3 + ${GUEST_DELAY:-0} ))
eval "\"\$UE\" \"\$PROJECT\" 127.0.0.1 -game -windowed -ResX=960 -ResY=540 -WinX=970 -WinY=40 \${GUEST[@]} $GUEST_EXTRA -abslog=\"\$PWD/Saved/guest.log\" &"
GUEST_PID=$!
trap "kill $HOST_PID $GUEST_PID 2>/dev/null" INT TERM
wait
