"""Task 13: the corpse Tier B scenarios compile, fit the session, the stress scenario respects the regrab cooldown, and the run checker judges fixtures."""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "linux"))
sys.path.insert(0, str(ROOT / "tools" / "linux"))
from d2vr_linux import sim  # noqa: E402
import bonelab_run_check as brc  # noqa: E402

NAMES = sorted(p.stem for p in (ROOT / "tools/linux/scenarios").glob("corpse_*.py"))
LEAD = 60000
CLEAN = {"physical_grab_lost_ownership": 0, "carry": {"holds": 0}, "prop_grasp_point": True, "prop_seat_gap_m": 0.0,
         "corpse": {"holds": [1, 0], "binds": [1, 1], "releases": {"open_fist": [1, 1], "lag": [0, 0], "lifecycle": [1, 0]},
                    "lag_m_max": [0.1, 0.1], "peak_bone_speed_all17": [3, 4], "runaway": 0, "event_posts_b9": 1, "wake_attempts": 1,
                    "fault_latch": False, "bones": {"limb": 2, "collar": 1}}}


def verdicts(status, scenario):
    return {v for v, _ in brc.check([status], scenario)}


def main():
    assert set(NAMES) == {"corpse_collar", "corpse_drag", "corpse_lift", "corpse_probe", "corpse_prompt", "corpse_reload", "corpse_stress", "corpse_two_hands"}, NAMES
    for name in NAMES:
        segments = sim.scenario_segments(name, LEAD)
        sim.compile_script(segments, 120)
        end = max(s["end_ms"] for s in segments)
        assert end <= 120000, (name, end)
        assert end - LEAD <= 120000 - LEAD, (name, end)
    # the old scenarios still build the same way
    assert sim.scenario_segments("oil_tank_drop", 45000)[-1]["end_ms"] == 45000 + 14000
    stress = sim.scenario_segments("corpse_stress", LEAD)
    grabs = [s for s in stress if s["input"].get("right_trigger", 0) > 0]
    assert len(grabs) >= 50, len(grabs)
    opens = [s for s in stress if s["start_ms"] >= LEAD and "right_grip" not in s["input"]]
    for g, nxt in zip(grabs, grabs[1:]):
        gap = nxt["start_ms"] - g["end_ms"]
        assert gap >= 400 + 200, (g, nxt)  # hold 0.2 s + open 0.4 s between a fist closing and the next one
    for seg in stress:  # each open-fist segment lasts >= 0.4 s
        if seg["start_ms"] > LEAD + 1000 and "right_grip" not in seg["input"]:
            assert seg["end_ms"] - seg["start_ms"] >= 400, seg
    assert opens

    for name in [n for n in NAMES if n != "corpse_stress"] + ["oil_tank_drop", "oil_tank_throw"]:
        assert "FAIL" not in verdicts(CLEAN, name), (name, brc.check([CLEAN], name))
    bad = copy.deepcopy(CLEAN)
    bad["corpse"]["fault_latch"] = True
    assert "FAIL" in verdicts(bad, "corpse_drag")
    for mutate in (lambda s: s["corpse"].update(runaway=1), lambda s: s["corpse"].update(peak_bone_speed_all17=[3, 12]),
                   lambda s: s["corpse"].update(event_posts_b9=3), lambda s: s.update(physical_grab_lost_ownership=1)):
        b = copy.deepcopy(CLEAN)
        mutate(b)
        assert "FAIL" in verdicts(b, "corpse_lift"), b
    b = copy.deepcopy(CLEAN); b["corpse"]["bones"]["collar"] = 0
    assert "FAIL" in verdicts(b, "corpse_collar")
    b = copy.deepcopy(CLEAN); b["corpse"]["binds"] = [1, 0]
    assert "FAIL" in verdicts(b, "corpse_two_hands")
    b = copy.deepcopy(CLEAN); b["corpse"]["releases"]["lifecycle"] = [0, 0]
    assert "FAIL" in verdicts(b, "corpse_reload")
    b = copy.deepcopy(CLEAN); b["corpse"]["binds"] = [10, 10]
    assert "FAIL" in verdicts(b, "corpse_stress")
    sessions = [copy.deepcopy(CLEAN) for _ in range(4)]
    for s in sessions:
        s["corpse"]["binds"] = [25, 25]
    assert all(v != "FAIL" for v, _ in brc.check(sessions, "corpse_stress"))
    assert "FAIL" in {v for v, _ in brc.check([{}], "corpse_drag")}
    with tempfile.TemporaryDirectory() as tmp:  # the command line
        for label, status, code in (("clean", CLEAN, 0), ("latched", bad_latch(), 1)):
            d = Path(tmp) / label / "artifacts"
            d.mkdir(parents=True)
            (d / "xr-live-status.json").write_text(json.dumps(status))
            r = subprocess.run([sys.executable, str(ROOT / "tools/linux/bonelab_run_check.py"), str(d.parent), "--scenario", "corpse_drag"], capture_output=True, text=True)
            assert r.returncode == code, (label, r.stdout, r.stderr)
            assert ("FAIL" in r.stdout) == (code == 1), r.stdout
    print("bonelab_scenarios_check ok")


def bad_latch():
    b = copy.deepcopy(CLEAN)
    b["corpse"]["fault_latch"] = True
    return b


if __name__ == "__main__":
    main()
