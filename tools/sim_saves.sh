#!/bin/bash
# Swap the known sim test save in before a sim batch and the user's saves back after it (timestamps kept: Continue loads the
# newest slot by file time).  bash tools/sim_saves.sh in   |   bash tools/sim_saves.sh out
cd "$(dirname "$0")/.."
SRC="$USERPROFILE/Saved Games/Arkane Studios/Dishonored2/base/savegame"
TEST=artifacts/save-backups/20261013T1325-before-drag-test
USER=artifacts/save-backups/current-user-saves
if tasklist | grep -qi dishonored2; then echo "close the game first"; exit 1; fi
case "$1" in
  in)  { [ ! -d "$USER" ] || mv "$USER" "$USER-$(date +%Y%m%dT%H%M%S)"; } && mkdir -p "$USER" && cp -rp "$SRC"/. "$USER"/ && rm -rf "$SRC"/SLOT* "$SRC"/profile.bin && cp -rp "$TEST"/. "$SRC"/ && echo "test save in (user saves kept in $USER)";;
  reset) [ -d "$USER" ] || { echo "run 'in' first: the user's saves are not kept yet"; exit 1; }; rm -rf "$SRC"/SLOT* "$SRC"/profile.bin && cp -rp "$TEST"/. "$SRC"/ && echo "test save reset";;  # between sim runs: autosaves drift the start
  out) [ -d "$USER" ] || { echo "no user saves kept"; exit 1; }; rm -rf "$SRC"/SLOT* "$SRC"/profile.bin && cp -rp "$USER"/. "$SRC"/ && diff -rq "$SRC" "$USER" && echo "user saves back";;
  *) echo "usage: sim_saves.sh in|reset|out"; exit 1;;
esac
