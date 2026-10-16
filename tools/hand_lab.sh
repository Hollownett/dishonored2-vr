#!/bin/sh
# Offline hand-pose lab: compile the harness, pose the right hand with the CURRENT finger solver, render close-ups.
#   tools/hand_lab.sh [tag] [corvo|emily] [--snapshot FILE[:index]]      -> artifacts/hand-lab/<tag>/{<case>-views.png, index.html, frames.json, solver.txt}
#   --snapshot renders exactly one record of artifacts/finger-snapshots.jsonl (the live solver inputs: palm, shapes, squeeze, reach) instead of the built-in cases
#   (add --refit [--along F] [--profile r0,..,r7] to seat the record's prop again with the current code: a neck grip at 0.85, a body grip at 0.3)
#   --palette FILE[:index] draws the hand the game submitted (artifacts/arm-palettes.jsonl); --write-geometry FILE writes the lab's finger geometry for
#   tools/finger_geometry_diff.py (against artifacts/finger-geometry.jsonl); --lift-mm N stands the shaft off the palm (experiment)
#   a --refit / built-in shaft is seated with the game's grip fit (finger_ik.hpp GripFitter); --no-fit seats it with the constant knuckle shift (v532 and before),
#   --shift MM / --slide MM fix the knuckle shift / the slide along the hole axis; the report's last columns are each phalanx's mean gap to the shapes
# Seconds, no game. Needs the local player asset pack (artifacts/fullbody-assets), never committed.
tag=${1:-current}
[ $# -ge 1 ] && shift
character=corvo
case "$1" in ""|--*) ;; *) character=$1; shift ;; esac   # what is left goes to the harness: --snapshot FILE[:index]
cd "$(dirname "$0")/.." || exit 1
out=artifacts/hand-lab/$tag
mkdir -p "$out" artifacts/hand-lab/bin || exit 1
# which solver sources this run saw (the lab compiles whatever src/ holds right now, committed or not)
{ git rev-parse --short HEAD; sha1sum src/finger_ik.hpp src/body_ik.hpp src/hand_bind.hpp src/full_body_driver.hpp; } > "$out/source.txt" 2>/dev/null
cmd.exe //c "$(cygpath -w "$PWD/tools/hand_pose_lab.cmd")" > "$out/compile.log" 2>&1 || { cat "$out/compile.log"; echo "hand_lab: compile failed"; exit 1; }
artifacts/hand-lab/bin/hand_pose_lab.exe "$out/frames.json" "$character" "$@" > "$out/solver.txt" || { cat "$out/solver.txt"; echo "hand_lab: harness failed"; exit 1; }
python tools/render_body_ik.py "$out/frames.json" "$out" --hand-views || exit 1
echo "hand_lab: $out/index.html"
