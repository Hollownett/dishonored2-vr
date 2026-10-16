#!/bin/bash
# Headset session: start the game from Steam, inject the candidate at the title screen, try to continue the last save.
#   bash tools/launch_headset.sh artifacts/repair-vNNN-test-candidate.json
# Check the desktop first (the user may be playing another game). Record with: python tools/session_recorder.py --fps 10 --minutes 60
# (start it ~75 s after the game process appears). The scripted Enter presses often land before the title is ready: the
# player then presses a button at "PRESS ANY KEY" and picks Campaign > Continue in the headset.
cd "$(dirname "$0")/.."
python tools/game_keys.py --prepare-config
rm -f artifacts/xr-live-status.json artifacts/vr-command.txt
powershell -NoProfile -Command "Start-Process 'steam://rungameid/403640'"
for i in $(seq 1 90); do tasklist | grep -qi dishonored2 && break; sleep 2; done
sleep 45
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_demo.ps1 -CandidateManifest "$1" -Launch -SkipPreflightWait &
DEMO=$!
sleep 25
python tools/game_keys.py 0x0D 0x0D 0x0D
wait $DEMO
