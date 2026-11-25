#!/bin/bash
# Run one simulated session safely: tools/linux/sim_serial.sh <log file> <d2vr-linux arguments...>
#
# - waits until no other process names a game executable and the previous Proton instance of either game has exited
#   (d2vr-linux sim refuses to start while any process command line names the game), plus a 15 s cool-down;
# - reruns once when the session never reached gameplay (Death of the Outsider sometimes hangs while loading when the CPU is busy:
#   avoid compiling while a sim runs);
# - writes the runner's output to <log file> (the first attempt to <log file>.attempt1, waits to <log file>.wait) and prints its tail.
log="$1"; shift
cd "$(dirname "$0")/../.." || exit 2
pat="Dishonored""_DO\.exe|Dishonored""2\.exe"   # split so this script's own command line never matches
settle() {
  for _ in $(seq 1 30); do
    others=$(pgrep -f -i -a "$pat" | grep -v -E "sim_serial\.sh|pgrep")
    wine=$(pgrep -a -f "compatdata/(614570|403640)/pfx" | grep -i -E "wineserver|winedevice|services\.exe|explorer\.exe")
    [ -z "$others$wine" ] && break
    echo "waiting for: $others $wine" >> "$log.wait"; sleep 5
  done
  sleep 15
}
for attempt in 1 2; do
  settle
  timeout 1500 linux/d2vr-linux "$@" > "$log" 2>&1; echo "exit=$?" >> "$log"
  grep -q "gameplay = false" "$log" || break
  [ "$attempt" = 1 ] && { cp "$log" "$log.attempt1"; echo "[sim_serial] gameplay never reached: retrying once" >> "$log.wait"; }
done
tail -45 "$log"
