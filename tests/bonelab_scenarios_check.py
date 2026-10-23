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
    sim.compile_script(sim.scenario_segments("guard_probe", 45000), 120)  # Task 14
    hands = (ROOT / "src/native_weapon_hands.hpp").read_text(encoding="utf-8")
    fire = hands[hands.index("static bool guard_probe_fire("):hands.index("static void service_guard_probe(")]
    assert "ragdoll(" not in fire and "0xb9" not in fire and "0x149" not in fire, "the probe never posts START_RAGDOLL or a velocity"
    assert "if(guard_probe_fire(base,read,best,hand,r)) return true;" in hands[hands.index("static bool yank("):hands.index("static bool yank(") + 1600]
    assert "arm_guard_probe(root/L\"build\"/L\"guard-probe\"" in (ROOT / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
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
    held_clamp_checks()
    hand_body_checks()
    print("bonelab_scenarios_check ok")


def tank_status(**over):
    clamp = {"sweeps": 100, "hits": 20, "clamped_frames": 10, "releases": 25, "release_overlap_unresolved": 0, "layer_at_release": 3,
             "cast_us": 19, "cast_us_max": 80, "release_depen_frames": 0}
    clamp.update(over.pop("clamp", {}))
    status = copy.deepcopy(CLEAN)
    status.update({"held_clamp": clamp, "layer": {"restores": 1, "pending": 0},
                   "release_watch": {"count": 2, "z_at_release": 27.3, "z_min": 27.2, "z_last": 27.22, "floor_z": None}})
    status["carry"] = {"holds": 1, "failed_reads": 0, "rejected_publishes": 0, "bad_quat_reads": 0}
    status.update(over)
    return status


def held_clamp_checks():
    """Task B5: S12-S14 compile within the session and the run checker judges the held_clamp fixtures."""
    for name in ("tank_wall", "tank_drop", "tank_drop_cycles"):
        segments = sim.scenario_segments(name, 28000)
        sim.compile_script(segments, 120)
        assert max(s["end_ms"] for s in segments) <= 120000, name
        assert len(segments) <= 128, (name, len(segments))
    cycles = sim.scenario_segments("tank_drop_cycles", 28000)
    assert len([s for s in cycles if s["input"].get("right_trigger", 0) > 0]) == 25
    drop = sim.scenario_segments("tank_drop", 28000)
    last_grip = max(s["end_ms"] for s in drop if s["input"].get("right_grip"))  # the release is the first non-grip segment after it
    assert max(s["end_ms"] for s in drop) - last_grip >= 5000, "5 s wait measured from the release"
    assert any(s["input"].get("move_y") for s in sim.scenario_segments("tank_wall", 28000))
    assert "FAIL" not in verdicts(tank_status(), "tank_wall")
    assert "FAIL" not in verdicts(tank_status(), "tank_drop")
    for key in ("sweeps", "hits", "clamped_frames"):
        assert "FAIL" in verdicts(tank_status(clamp={key: 0}), "tank_wall"), key
    assert "FAIL" in verdicts(tank_status(clamp={"release_overlap_unresolved": 1}), "tank_drop")
    assert "FAIL" in verdicts(tank_status(clamp={"release_overlap_unresolved": 1}), "tank_drop_cycles")
    assert "FAIL" in verdicts(tank_status(clamp={"layer_at_release": None}), "tank_drop")
    assert "FAIL" in verdicts(tank_status(clamp={"layer_at_release": -1}), "tank_drop")
    assert "FAIL" in verdicts(tank_status(release_watch={"count": 2, "z_at_release": 27.3, "z_min": 27.2, "z_last": 27.1}), "tank_drop")  # 0.2 m below
    assert "FAIL" in verdicts(tank_status(release_watch={"count": 1, "z_at_release": 27.3, "z_min": 27.2, "z_last": 27.3}), "tank_drop")
    assert "FAIL" in verdicts(tank_status(clamp={"releases": 1}), "tank_drop")
    for key in ("release_overlap_unresolved", "releases"):  # absent counters never default to 0
        for name in ("tank_drop", "tank_drop_cycles"):
            absent = tank_status(); del absent["held_clamp"][key]
            assert "FAIL" in verdicts(absent, name), (key, name)
    absent = tank_status(); del absent["carry"]
    assert "FAIL" in verdicts(absent, "tank_drop_cycles")
    absent = tank_status(); del absent["carry"]["bad_quat_reads"]
    assert "FAIL" in verdicts(absent, "tank_drop_cycles")
    for key in ("sweeps", "hits", "clamped_frames"):
        absent = tank_status(); del absent["held_clamp"][key]
        assert "FAIL" in verdicts(absent, "tank_wall"), key
    assert "FAIL" in verdicts(tank_status(release_watch={"count": 2, "z_at_release": 27.3, "z_last": 27.3}), "tank_drop")  # z_min absent
    assert "FAIL" in verdicts(tank_status(release_watch={"count": 0}), "tank_drop")
    four = [tank_status() for _ in range(4)]
    assert all(v != "FAIL" for v, _ in brc.check(four, "tank_drop_cycles")), brc.check(four, "tank_drop_cycles")
    four[2]["held_clamp"]["releases"] = 0
    assert "FAIL" in {v for v, _ in brc.check(four, "tank_drop_cycles")}  # 75 < 100
    for mutate in (lambda s: s["corpse"].update(fault_latch=True), lambda s: s["carry"].update(failed_reads=1)):
        b = tank_status(); mutate(b)
        assert "FAIL" in verdicts(b, "tank_drop_cycles")
    assert "FAIL" in {v for v, _ in brc.check([{**CLEAN}], "tank_wall")}  # no held_clamp block


def hand_body_status(**over):
    side = {"creates": 1, "destroys": 0, "faults": 0, "drive": {"drive_writes": 5000, "snaps": 0, "snap_blocked": 0, "yields": 0, "contact_frames": 40}}
    hb = {"enabled": True, "installed": True, "available": True, "hand_body_fault": False, "why": "", "faults": 0, "cost_us": 20, "cost_max_us": 120,
          "tick_cost_max_us": 400, "steady_tick_cost_max_us": 150, "player_drift_um": 0, "contact_frames": 80, "snaps": 0, "gap_m": [.01, .01],
          "abandons": 0, "sides": [copy.deepcopy(side), copy.deepcopy(side)]}
    hb.update(over)
    return {"hand_body": hb}


def hand_body_checks():
    """Task B14: the soak compiles within a 120 s session at a 45 s lead, and the checker judges the hand_body fixtures (proofs a, b, d)."""
    segments = sim.scenario_segments("hand_body_soak", 45000)
    sim.compile_script(segments, 120)
    assert max(s["end_ms"] for s in segments) <= 120000 and len(segments) <= 128
    capsule = [s for s in segments if s.get("right_hand_offset") == [-0.18, -0.55, 0.4]]
    assert capsule and max(s["end_ms"] - s["start_ms"] for s in capsule) >= 10000, "a pressed-into-capsule hold of >= 10 s"
    assert all(not s["input"].get("move_y") and not s["input"].get("move_x") for s in capsule), "no locomotion while pressed"
    five = [hand_body_status() for _ in range(5)]
    assert all(v != "FAIL" for v, _ in brc.check(five, "hand_body_soak")), brc.check(five, "hand_body_soak")
    for over in ({"faults": 1}, {"cost_max_us": 201}, {"player_drift_um": 1001}, {"available": False}, {"hand_body_fault": True},
                 {"installed": False}):
        bad = [hand_body_status() for _ in range(5)]
        bad[3]["hand_body"].update(over)
        assert "FAIL" in {v for v, _ in brc.check(bad, "hand_body_soak")}, over
    vacuous = [hand_body_status() for _ in range(5)]
    vacuous[1]["hand_body"]["sides"][0]["creates"] = 0
    assert "FAIL" in {v for v, _ in brc.check(vacuous, "hand_body_soak")}, "a body never created"
    undriven = [hand_body_status() for _ in range(5)]
    undriven[2]["hand_body"]["sides"][1]["drive"]["drive_writes"] = 0
    assert "FAIL" in {v for v, _ in brc.check(undriven, "hand_body_soak")}, "a body never driven"
    for key in ("faults", "cost_max_us", "player_drift_um"):   # absent counters never default to 0
        absent = [hand_body_status() for _ in range(5)]
        del absent[0]["hand_body"][key]
        assert "FAIL" in {v for v, _ in brc.check(absent, "hand_body_soak")}, key
    assert "FAIL" in {v for v, _ in brc.check([{"corpse": {}}], "hand_body_soak")}, "no hand_body block"
    assert "FAIL" in {v for v, _ in brc.check([], "hand_body_soak")}


def bad_latch():
    b = copy.deepcopy(CLEAN)
    b["corpse"]["fault_latch"] = True
    return b


if __name__ == "__main__":
    main()
