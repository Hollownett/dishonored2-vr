#!/bin/sh
# Offline push lab: compile the harness, simulate hand-to-prop pushing with the CURRENT src/hand_contact.hpp math, plot it.
#   tools/push_lab.sh [tag]      -> artifacts/push-lab/<tag>/{index.html, summary.csv, summary.txt, scenarios.json, <scenario>.png, <scenario>.csv, <scenario>-proxies.csv}
# Seconds, no game, no asset pack. The lab compiles whatever src/ holds right now, committed or not.
tag=${1:-current}
cd "$(dirname "$0")/.." || exit 1
out=artifacts/push-lab/$tag
mkdir -p "$out" artifacts/push-lab/bin || exit 1
rm -f "$out"/*.png "$out"/*.csv "$out"/index.html # a rerun of a tag must not keep another run's scenarios
# which sources this run saw
{ git rev-parse --short HEAD; sha1sum src/hand_contact.hpp src/finger_ik.hpp src/native_rigid_body.hpp src/native_hand_contact.hpp; } > "$out/source.txt" 2>/dev/null
cmd.exe //c "$(cygpath -w "$PWD/tools/push_lab.cmd")" > "$out/compile.log" 2>&1 || { cat "$out/compile.log"; echo "push_lab: compile failed"; exit 1; }
artifacts/push-lab/bin/push_lab.exe "$out" > "$out/summary.txt" || { cat "$out/summary.txt"; echo "push_lab: harness failed"; exit 1; }
cat "$out/summary.txt"
python tools/push_lab_plots.py "$out" || exit 1
echo "push_lab: $out/index.html"
