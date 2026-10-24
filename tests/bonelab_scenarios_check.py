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
    hold_coupling_checks()
    finger_touch_checks()
    print("bonelab_scenarios_check ok")


def finger_sample(curl, mapping="touch"):
    return {"profile": "oculus/touch_controller", "mapping": [mapping, mapping], "touch_available": [True, True], "touch_silent": [False, False],
            "raw": {"trigger_touch": [False, False], "thumb_touch": [False, False]}, "curl": [[0.0] * 5, list(curl)], "floor_active": [False, False],
            "action_create_failed": False, "touch_bound": True}


REST, POINT, THUMBS, FIST, OPEN = ([0, 0.3, 0, 0, 0], [1, 0.02, 1, 1, 1], [0.0, 0.3, 1, 1, 1], [1, 1, 1, 1, 1], [0, 0.02, 0, 0, 0])


def finger_status(samples=(REST, POINT, THUMBS, FIST, OPEN), **over):
    status = {"fingers": finger_sample(OPEN), "finger_segments": [finger_sample(c) for c in samples]}
    status["fingers"].update(over)
    return status


def finger_touch_checks():
    """Plan C Task 4: S10 compiles (v7 touch columns, 5 poses of >= 2 s, the last five segments), older scripts still compile, and the checker judges fixtures."""
    segments = sim.scenario_segments("finger_touch", 45000)
    text, transport = sim.compile_script(segments, 120)
    assert transport.splitlines()[0].startswith("D2VR_GAMEPLAY_SCRIPT 7 120000 "), transport.splitlines()[0]
    poses = segments[-5:]
    assert all(s["end_ms"] - s["start_ms"] >= 2000 for s in poses) and poses[0]["start_ms"] == 45000 and poses[-1]["end_ms"] <= 120000
    assert all("touch" in s for s in poses) and not any("touch" in s for s in segments[:-5])
    assert poses[0]["touch"] == {"right_trigger": True} and "right_trigger" not in poses[1]["touch"] and poses[1]["input"]["right_grip"] == 1.0
    assert not any(k.startswith("right_thumb") or k in ("right_first", "right_second") for k in poses[2]["touch"]) and poses[2]["touch"]
    assert poses[3]["input"]["right_trigger"] == 1.0 and poses[3]["input"]["right_grip"] == 1.0 and not poses[4]["touch"]
    assert sim.compile_script(sim.scenario_segments("corpse_drag", 45000), 120)[1].splitlines()[0].startswith("D2VR_GAMEPLAY_SCRIPT 6 "), "older scenarios keep their version"
    verdict = lambda status, **kw: [(v, t) for v, t in brc.check([status], "finger_touch", **kw)]  # noqa: E731
    ok = verdict(finger_status())
    assert not any(v == "FAIL" for v, _ in ok), ok
    assert sum(1 for v, t in ok if v == "INFO") == 5 and any("fist: all fingers" in t for v, t in ok)
    legacy = finger_status(samples=(REST, POINT, THUMBS, FIST, OPEN))
    legacy["finger_segments"] = [finger_sample(c, "legacy") for c in (REST, POINT, THUMBS, FIST, OPEN)]
    assert any(v == "FAIL" and "mapping touch" in t for v, t in verdict(legacy))
    stuck_thumb = finger_status(samples=(REST, POINT, [1, 0.3, 1, 1, 1], FIST, OPEN))
    assert any(v == "FAIL" and "thumb" in t for v, t in verdict(stuck_thumb))
    legacy_curls = finger_status(samples=([0, 0, 0, 0, 0], [1, 1, 1, 1, 1], [1, 1, 1, 1, 1], FIST, OPEN))  # a controller-value mapping: no rest bend, point curls the index
    f = [t for v, t in verdict(legacy_curls) if v == "FAIL"]
    assert any("rest: index" in t for t in f) and any("point: index" in t for t in f), f
    for bad in (finger_status(samples=(REST, POINT, THUMBS, [1, 1, 1, 0.5, 1], OPEN)), finger_status(samples=(REST, POINT, THUMBS, FIST, [0, 0, 0.5, 0, 0]))):
        assert "FAIL" in {v for v, _ in verdict(bad)}
    assert "FAIL" in {v for v, _ in verdict(finger_status(samples=(REST, POINT)))}, "too few samples"
    unbound = finger_status(touch_bound=False)
    assert any(v == "FAIL" and "touch_bound == true" in t for v, t in verdict(unbound))
    rejected = finger_status(touch_bound=False, mapping=["legacy", "legacy"])
    assert not any(v == "FAIL" for v, _ in verdict(rejected, reject_touch=True)), verdict(rejected, reject_touch=True)
    assert "FAIL" in {v for v, _ in verdict(finger_status(), reject_touch=True)}, "the reject run must report touch_bound false"
    assert "FAIL" in {v for v, _ in brc.check([{}], "finger_touch")}
    with tempfile.TemporaryDirectory() as tmp:  # the command line reads the jsonl beside the status
        for label, samples, code in (("good", (REST, POINT, THUMBS, FIST, OPEN), 0), ("legacy", ([0] * 5, [1] * 5, [1] * 5, FIST, OPEN), 1)):
            d = Path(tmp) / label / "artifacts"
            d.mkdir(parents=True)
            (d / "xr-live-status.json").write_text(json.dumps({"fingers": finger_sample(OPEN)}))
            (d / "finger-touch-segments.jsonl").write_text("".join(json.dumps({"segment": i, "elapsed_ms": 1000 * i, "fingers": finger_sample(c)}) + "\n" for i, c in enumerate(samples)))
            r = subprocess.run([sys.executable, str(ROOT / "tools/linux/bonelab_run_check.py"), str(d.parent), "--scenario", "finger_touch"], capture_output=True, text=True)
            assert r.returncode == code, (label, r.stdout, r.stderr)


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
          "tick_cost_max_us": 400, "steady_tick_cost_max_us": 150, "player_drift_um": 0, "pressed_ticks": 2600, "contact_frames": 80, "snaps": 0, "gap_m": [.01, .01],
          "abandons": 0, "g1": "pass", "g1_violation": "", "root_moved_ticks": 300, "root_travel_um": 900000,
          "timing": {"cpu_available": True, "cycles_per_us": 3000, "ticks": 7000, "over_wall": 1, "over_cpu": 0, "wall_max_us": 269, "cpu_max_us": 60,
                     "hist_edges_us": [10, 20, 50, 100, 150, 200, 300, 500, 1000], "hist": [6000, 900, 80, 15, 4, 0, 1, 0, 0, 0],
                     "slow": {"wall_us": 269, "cpu_us": 40, "read_us": 5, "servo_us": 3, "write_us": 255, "other_us": 6}},
          "sides": [copy.deepcopy(side), copy.deepcopy(side)]}
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
    # fix round 1: the wall max (269 us above) and the old max-only cost fields are reported, not judged; proof (d) is CPU max AND wall p99.9
    for over in ({"cost_max_us": 269}, {"steady_tick_cost_max_us": 278}):
        ok = [hand_body_status() for _ in range(5)]
        ok[3]["hand_body"].update(over)
        assert all(v != "FAIL" for v, _ in brc.check(ok, "hand_body_soak")), over
    timing = lambda **t: {"timing": {**hand_body_status()["hand_body"]["timing"], **t}}
    for over in ({"faults": 1}, {"pressed_ticks": 999}, {"pressed_ticks": None}, {"root_moved_ticks": 0}, {"root_moved_ticks": None}, {"player_drift_um": 1001},
                 {"available": False}, {"hand_body_fault": True}, {"installed": False}, {"g1": "failed: x"}, {"g1_violation": "G1 violated"}, {"timing": None},
                 timing(cpu_available=False), timing(cpu_max_us=201), timing(hist=[1, 2, 3]),
                 timing(hist_edges_us=[10, 20])):
        bad = [hand_body_status() for _ in range(5)]
        bad[3]["hand_body"].update(over)
        assert "FAIL" in {v for v, _ in brc.check(bad, "hand_body_soak")}, over
    slow = [hand_body_status(**timing(hist=[6000, 900, 80, 15, 4, 0, 8, 0, 0, 0])) for _ in range(5)]   # 40 of 35035 ticks over budget: p99.9 > 200
    assert "FAIL" in {v for v, _ in brc.check(slow, "hand_body_soak")}
    # p99.9 is judged on the summed histogram: 35 over-budget ticks in 35000 is exactly 0.1 % -> still <= 200; 36 is not
    edge = [hand_body_status() for _ in range(5)]
    for h in edge:
        h["hand_body"]["timing"]["hist"] = [6993, 0, 0, 0, 0, 0, 7, 0, 0, 0]
    assert all(v != "FAIL" for v, _ in brc.check(edge, "hand_body_soak")), brc.check(edge, "hand_body_soak")
    edge[0]["hand_body"]["timing"]["hist"] = [6992, 0, 0, 0, 0, 0, 8, 0, 0, 0]
    assert "FAIL" in {v for v, _ in brc.check(edge, "hand_body_soak")}
    assert brc.wall_percentile_upper_us([0, 0, 0], [10, 20], .999) is None
    assert brc.wall_percentile_upper_us([0, 0, 1], [10, 20], .999) == float("inf")
    assert brc.wall_percentile_upper_us([999, 1, 0], [10, 20], .999) == 10 and brc.wall_percentile_upper_us([998, 2, 0], [10, 20], .999) == 20
    # --proofs a: older sessions (no timing block, no positive control) are judged for proof (a) only
    old = [hand_body_status() for _ in range(3)]
    for h in old:
        for key in ("timing", "root_moved_ticks", "pressed_ticks"):
            del h["hand_body"][key]
    assert all(v != "FAIL" for v, _ in brc.check(old, "hand_body_soak", ("a",))), brc.check(old, "hand_body_soak", ("a",))
    assert "FAIL" in {v for v, _ in brc.check(old, "hand_body_soak")}
    old[1]["hand_body"]["faults"] = 1
    assert "FAIL" in {v for v, _ in brc.check(old, "hand_body_soak", ("a",))}
    stopped = [hand_body_status() for _ in range(5)]   # a snapshot after the session-end stop: installed false, bodies destroyed
    for sd in stopped[4]["hand_body"]["sides"]:
        sd.update(state=0, destroys=1, abandons=0, lost=0)
    stopped[4]["hand_body"]["installed"] = False
    assert all(v != "FAIL" for v, _ in brc.check(stopped, "hand_body_soak")), brc.check(stopped, "hand_body_soak")
    for bad_stop in ({"destroys": 0}, {"abandons": 1}, {"lost": 1}, {"state": 1}):
        b = [hand_body_status() for _ in range(5)]
        b[0]["hand_body"]["installed"] = False
        for sd in b[0]["hand_body"]["sides"]:
            sd.update(state=0, destroys=1, abandons=0, lost=0)
        b[0]["hand_body"]["sides"][1].update(bad_stop)
        assert "FAIL" in {v for v, _ in brc.check(b, "hand_body_soak")}, bad_stop
    vacuous = [hand_body_status() for _ in range(5)]
    vacuous[1]["hand_body"]["sides"][0]["creates"] = 0
    assert "FAIL" in {v for v, _ in brc.check(vacuous, "hand_body_soak")}, "a body never created"
    undriven = [hand_body_status() for _ in range(5)]
    undriven[2]["hand_body"]["sides"][1]["drive"]["drive_writes"] = 0
    assert "FAIL" in {v for v, _ in brc.check(undriven, "hand_body_soak")}, "a body never driven"
    for key in ("faults", "player_drift_um", "timing", "root_moved_ticks"):   # absent counters never default to 0
        absent = [hand_body_status() for _ in range(5)]
        del absent[0]["hand_body"][key]
        assert "FAIL" in {v for v, _ in brc.check(absent, "hand_body_soak")}, key
    assert "FAIL" in {v for v, _ in brc.check([{"corpse": {}}], "hand_body_soak")}, "no hand_body block"
    assert "FAIL" in {v for v, _ in brc.check([], "hand_body_soak")}


def coupling_block(**side_over):
    side = {"frames": 0, "uses_body": 0, "motor": 0, "corpse": 0, "native": 0, "releases": 0, "lag_m": 0, "word": brc.HAND_FILTER_DEFAULT,
            "swaps": 0, "restores": 0, "unresolved": 0, "body_resets": 0, "overlap_frames": 0}
    side.update(side_over)
    return {"sides": [dict(side), dict(side)], "default_word": brc.HAND_FILTER_DEFAULT}


def tank_hand_body_status(on=True, **over):
    status = tank_status()
    hb = hand_body_status()["hand_body"] if on else {"enabled": False, "installed": False}
    if on:
        for sd in hb["sides"]:
            sd.update(filter=brc.HAND_FILTER_DEFAULT, filter_stranded=0)
        hb["filter_readback_mismatches"] = 0
    status["hand_body"] = hb
    status["hold_coupling"] = coupling_block(frames=900, native=900, uses_body=900 if on else 0)
    for key, value in over.items():
        if isinstance(value, dict) and isinstance(status.get(key), dict):
            status[key].update(value)
        else:
            status[key] = value
    return status


def hold_coupling_checks():
    """Task B16: S15 tank_hand_body compiles (idle before the grab, 5 s after the last release) and the checker judges the coupling fixtures."""
    segments = sim.scenario_segments("tank_hand_body", 45000)
    sim.compile_script(segments, 120)
    assert max(s["end_ms"] for s in segments) <= 120000 and len(segments) <= 128
    first_fist = min(s["start_ms"] for s in segments if s["input"].get("right_trigger"))
    assert first_fist - 45000 >= 12000, "the hand bodies exist before the grab (G1 8 s, then the creates)"
    last_grip = max(s["end_ms"] for s in segments if s["input"].get("right_grip"))
    assert max(s["end_ms"] for s in segments) - last_grip >= 5000, "5 s after the last release"
    assert any(s["input"].get("move_y") for s in segments), "the tank is walked into the wall"
    for on in (True, False):
        assert "FAIL" not in verdicts(tank_hand_body_status(on), "tank_hand_body"), (on, brc.check([tank_hand_body_status(on)], "tank_hand_body"))
    side = lambda **o: coupling_block(frames=900, native=900, uses_body=900, **o)
    for over in ({"held_clamp": {"release_overlap_unresolved": 1}}, {"held_clamp": {"releases": 1}},
                 {"release_watch": {"count": 2, "z_at_release": 27.3, "z_min": 27.2, "z_last": 27.1}},
                 {"hand_body": {"faults": 1}}, {"hand_body": {"contact_frames": 0}}, {"hand_body": {"available": False}},
                 {"hold_coupling": coupling_block(frames=900, native=900, uses_body=0)},          # the tank never followed the body
                 {"hold_coupling": side(word=0x85E4)},                                            # a stripped word left behind
                 {"hold_coupling": side(swaps=2, restores=1)},                                    # a swap that never came back
                 {"hold_coupling": coupling_block(frames=0, uses_body=5)},                        # idle, yet the body was used
                 {"layer": {"pending": 1}}, {"hold_coupling": None}):
        assert "FAIL" in verdicts(tank_hand_body_status(True, **over), "tank_hand_body"), over
    stripped = tank_hand_body_status(True)
    stripped["hand_body"]["sides"][1]["filter"] = 0x85E4
    assert "FAIL" in verdicts(stripped, "tank_hand_body"), "the body's own word left stripped"
    for mutate in (lambda s: s["hand_body"]["sides"][0].update(filter_stranded=1), lambda s: s["hand_body"].update(filter_readback_mismatches=2),
                   lambda s: s["hand_body"]["sides"][0].pop("filter_stranded"), lambda s: s["hand_body"].pop("filter_readback_mismatches")):
        b = tank_hand_body_status(True); mutate(b)
        assert "FAIL" in verdicts(b, "tank_hand_body")
    assert "FAIL" in verdicts(tank_hand_body_status(False, hold_coupling=coupling_block(frames=900, native=900, uses_body=3)), "tank_hand_body")
    absent = tank_hand_body_status(True)
    del absent["hold_coupling"]["sides"][0]["swaps"]
    assert "FAIL" in verdicts(absent, "tank_hand_body")
    # the soak (no hold): idle-correct coupling passes; a used body with no hold, or a word left stripped, fails
    soak = [{**hand_body_status(), "hold_coupling": coupling_block(), "layer": {"pending": 0}} for _ in range(5)]
    for s in soak:
        s["hand_body"]["filter_readback_mismatches"] = 0
        for sd in s["hand_body"]["sides"]:
            sd.update(filter=brc.HAND_FILTER_DEFAULT, filter_stranded=0)
    assert all(v != "FAIL" for v, _ in brc.check(soak, "hand_body_soak")), brc.check(soak, "hand_body_soak")
    for bad in (coupling_block(uses_body=1), coupling_block(word=0x85E4), coupling_block(swaps=1)):
        b = [dict(s) for s in soak]
        b[2] = {**soak[2], "hold_coupling": bad}
        assert "FAIL" in {v for v, _ in brc.check(b, "hand_body_soak")}, bad
    mixed = [dict(s) for s in soak]
    mixed[0] = {k: v for k, v in soak[0].items() if k != "hold_coupling"}
    assert "FAIL" in {v for v, _ in brc.check(mixed, "hand_body_soak")}


def bad_latch():
    b = copy.deepcopy(CLEAN)
    b["corpse"]["fault_latch"] = True
    return b


if __name__ == "__main__":
    main()
