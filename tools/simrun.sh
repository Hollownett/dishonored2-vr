#!/bin/bash
# One sim selftest run: closes the sim game (polite, then forced after 30 s), resets the test save (tools/sim_saves.sh reset),
# launches and runs tools/vr_selftest.py, prints PASS/FAIL and the status counters. Requires "bash tools/sim_saves.sh in" first.
#   bash tools/simrun.sh <dll version> <log name> <vr_selftest args...>   (log: artifacts/build-logs/v<version>-<name>.log)
# usage: simrun.sh <dll version> <name> <selftest args...>   closes the sim game first (graceful), then launches and runs
cd "$(dirname "$0")/.."
V=$1; N=$2; shift 2
powershell -NoProfile -Command '$p=Get-Process Dishonored2 -ErrorAction SilentlyContinue; if($p){$p.CloseMainWindow()|Out-Null; $p.WaitForExit(30000)|Out-Null; if(-not $p.HasExited){Stop-Process -Id $p.Id -Force; $p.WaitForExit(15000)|Out-Null; "sim game force-closed"}}'
sleep 3
bash tools/sim_saves.sh reset || exit 1
timeout 900 python tools/vr_selftest.py --dll d2vr_render_probe_arm_v$V.dll "$@" --launch > artifacts/build-logs/v$V-$N.log 2>&1
echo "$N exit $?"
grep -v "injection\|hud \|^info" artifacts/build-logs/v$V-$N.log | grep -E "PASS|FAIL|near |samples|errors|Traceback|Error" | cut -c1-300
R=$(ls -td artifacts/runs/*v$V | head -1); echo "run $R"
python - "$R" <<'PY'
import json,sys,os
p=os.path.join(sys.argv[1],'xr-live-status.json')
if os.path.exists(p):
    sg=json.load(open(p)).get('sword_grip',{})
    print({k:sg.get(k) for k in ('use_diag','look_uses','look_hits','carries','door_dbg','doors','stealth')})
PY
