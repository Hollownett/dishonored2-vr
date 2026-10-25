"""G1 loot measurement, the drawer case alone (hands plan Task 1): loot_reach.py's third case (the commode loot) in its own session,
because the three cases together overflow the 8192-row native-loot-reach.csv before the third one (the table case alone writes ~5400
rows: about four owners per frame). Same spots, poses and checker:
`linux/d2vr-linux sim --scenario loot_reach_drawer --knob loot-reach-trace --knob sim-invulnerable --lead 45 --seconds 120`, then
`tools/linux/bonelab_run_check.py RUN --scenario loot_reach --coin -13.80 59.80 27.28 --case drawer`.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_loot_reach", Path(__file__).with_name("loot_reach.py"))
_loot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_loot)


def build(lead_ms):
    segments = _loot.L.lead_in(lead_ms)
    _, coin, stand, crouch, lift = next(c for c in _loot.CASES if c[0] == "drawer")
    segs, _ = _loot.case_segments(lead_ms, coin, stand, crouch, lift)
    return segments + segs
