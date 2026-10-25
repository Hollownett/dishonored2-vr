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
    run_hands_checks()
    sword_regrab_checks()
    authored_turn_checks()
    cutscene_checks()
    loot_reach_checks()
    print("bonelab_scenarios_check ok")


def finger_sample(curl, mapping="touch"):
    return {"profile": "oculus/touch_controller", "mapping": [mapping, mapping], "touch_available": [True, True], "touch_silent": [False, False],
            "raw": {"trigger_touch": [False, False], "thumb_touch": [False, False]}, "curl": [[0.0] * 5, list(curl)], "floor_active": [False, False],
            "action_create_failed": False, "touch_bound": True}


REST, POINT, THUMBS, FIST, OPEN = ([0, 0.3, 0, 0, 0], [1, 0.02, 1, 1, 1], [0.0, 0.3, 1, 1, 1], [1, 1, 1, 1, 1], [0, 0.02, 0, 0, 0])


POSES_AT = [{"index": 27 + i, "end_ms": 47500 + 2500 * i} for i in range(5)]  # as the run checker derives them from the compiled script


def finger_samples(curls, mapping="touch"):
    return [{"segment": 27 + i, "elapsed_ms": 47500 + 2500 * i + 6, "fingers": finger_sample(c, mapping)} for i, c in enumerate(curls)]


def finger_status(samples=(REST, POINT, THUMBS, FIST, OPEN), **over):
    status = {"fingers": finger_sample(OPEN), "finger_segments": finger_samples(samples), "finger_poses": POSES_AT}
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
    legacy = finger_status()
    legacy["finger_segments"] = finger_samples((REST, POINT, THUMBS, FIST, OPEN), "legacy")
    assert any(v == "FAIL" and "mapping touch" in t for v, t in verdict(legacy))
    stuck_thumb = finger_status(samples=(REST, POINT, [1, 0.3, 1, 1, 1], FIST, OPEN))
    assert any(v == "FAIL" and "thumb" in t for v, t in verdict(stuck_thumb))
    legacy_curls = finger_status(samples=([0, 0, 0, 0, 0], [1, 1, 1, 1, 1], [1, 1, 1, 1, 1], FIST, OPEN))  # a controller-value mapping: no rest bend, point curls the index
    f = [t for v, t in verdict(legacy_curls) if v == "FAIL"]
    assert any("rest: index" in t for t in f) and any("point: index" in t for t in f), f
    for bad in (finger_status(samples=(REST, POINT, THUMBS, [1, 1, 1, 0.5, 1], OPEN)), finger_status(samples=(REST, POINT, THUMBS, FIST, [0, 0, 0.5, 0, 0]))):
        assert "FAIL" in {v for v, _ in verdict(bad)}
    short = finger_status()
    short["finger_segments"] = short["finger_segments"][:4]  # the run ended before the open segment's end tick
    assert any(v == "FAIL" and t.startswith("open: a sample") for v, t in verdict(short)), verdict(short)
    lead_only = finger_status()
    lead_only["finger_segments"] = [{"segment": i, "elapsed_ms": 1000 * i, "fingers": finger_sample(OPEN)} for i in range(20)]  # only lead-in samples
    assert "FAIL" in {v for v, _ in verdict(lead_only)}
    late = finger_status()  # an unsettled sample AFTER the settled boundary sample of segment 28 (end 50000): the first one at/after the end is judged
    late["finger_segments"].insert(2, {"segment": 28, "elapsed_ms": 50300, "fingers": finger_sample([1, 0.5, 1, 1, 1])})
    assert not any(v == "FAIL" for v, _ in verdict(late)), verdict(late)
    assert "FAIL" in {v for v, _ in verdict(finger_status(samples=(REST, [1, 0.5, 1, 1, 1], THUMBS, FIST, OPEN)))}, "the boundary sample itself is judged"
    blip = finger_status()  # a genuine in-segment blip (focus loss): bad curls stamped before the segment's end are ignored
    blip["finger_segments"].insert(2, {"segment": 28, "elapsed_ms": 50000 - 700, "fingers": finger_sample([0, 0.9, 0, 0, 0])})
    blip["finger_segments"].insert(1, {"segment": 27, "elapsed_ms": 47500 - 900, "fingers": finger_sample([1, 1, 1, 1, 1])})
    assert not any(v == "FAIL" for v, _ in verdict(blip)), verdict(blip)
    early = finger_status()  # a sample stamped before the segment's end never counts
    early["finger_segments"][3]["elapsed_ms"] = 52500 + 2500 + 100 - 2600
    assert any(v == "FAIL" and t.startswith("fist: a sample") for v, t in verdict(early))
    no_script = finger_status()
    no_script["finger_poses"] = []
    assert any(v == "FAIL" and "touch pose segments" in t for v, t in verdict(no_script))
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
            (d / "simulated-gameplay-script.json").write_text(json.dumps({"segments": segments}))
            first = len(segments) - 5  # the pose segments are the last five; one sample per segment, at its end tick
            (d / "finger-touch-segments.jsonl").write_text("".join(json.dumps({"segment": first + i, "elapsed_ms": segments[first + i]["end_ms"] + 5, "fingers": finger_sample(c)}) + "\n" for i, c in enumerate(samples)))
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


def locomotion_rows(stand=300, run=300, running_residual=0.01, blends_at=(), contact=0, run_speed=3.0, valid=1):
    """Fixture trace: `stand` rows at speed 0, then `run` rows at run_speed, 11 ms apart; blends steps at the elapsed_ms values in blends_at.
    Standing residual is 0.01 m. `valid` is the root_gap_valid column of the running rows."""
    rows, blends = [], 0
    for i in range(stand + run):
        t = i * 11
        blends += sum(1 for b in blends_at if t - 11 < b <= t)
        moving = i >= stand
        rows.append({"elapsed_ms": t, "speed_mps": run_speed if moving else 0.0, "root_gap_m": 0.03 if moving else 0.0, "root_gap_valid": valid if moving else 1,
                     "palm_residual_l": running_residual if moving else 0.01, "palm_residual_r": running_residual if moving else 0.01,
                     "drawn_residual_l": running_residual if moving else 0.01, "drawn_residual_r": running_residual if moving else 0.01,
                     "visual_gap_l": 0.02, "visual_gap_r": 0.02, "source_l": 2 if moving else 1, "source_r": 1, "blends": blends,
                     "contact_l": contact, "contact_r": contact})
    return rows


def run_hands_checks():
    for name in ("run_hands", "run_hands_sword"):
        segments = sim.scenario_segments(name, LEAD)
        sim.compile_script(segments, 120)
        assert max(s["end_ms"] for s in segments) <= 120000, name
        inputs = [s["input"] for s in segments if s["start_ms"] >= LEAD]
        assert any(i.get("move_y") == 1.0 and i.get("left_click") for i in inputs), name  # the sprint
        assert any(i.get("move_y") == 0.5 for i in inputs), name  # the walk
    sword = [s for s in sim.scenario_segments("run_hands_sword", LEAD) if s["start_ms"] >= LEAD]
    assert sword[0]["right_hand_offset"] == [-0.03, 0.25, 0.50] and sword[0]["hand_pitch_degrees"] == 60
    assert all(s["input"].get("right_grip") == 1.0 for s in sword[1:]), "the grip is held through the whole timeline"
    assert not any("right_grip" in s["input"] for s in sim.scenario_segments("run_hands", LEAD))
    for name in ("run_hands", "run_hands_sword"):  # a stand/run trace with equal residuals: PASS rows and INFO numbers
        rows = brc.check([{"locomotion_rows": locomotion_rows()}], name)
        assert {v for v, _ in rows} == {"PASS", "INFO"}, rows
        text = "\n".join(t for v, t in rows if v == "INFO")
        assert "drawn_residual_l median: standing 0.0100 m, running 0.0100 m" in text and "root_gap_m median running: 0.0300 m" in text, text
        assert "source_l == body share of running frames: 1.000" in text and "top speed 3.00" in text and "free-air blend bursts == 0 (got 0" in "\n".join(t for _, t in rows), text
    assert "FAIL" in verdicts({"locomotion_rows": None}, "run_hands") and "FAIL" in verdicts({}, "run_hands_sword")  # a missing trace
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(run=50)}, "run_hands")  # fewer than 60 running rows (the spawn corridor limits the run)
    assert {v for v, _ in brc.check([{"locomotion_rows": locomotion_rows(run=60)}], "run_hands")} == {"PASS", "INFO"}  # exactly 60 is enough
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(stand=150)}, "run_hands")  # fewer than 200 standing rows
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(run_speed=2.0)}, "run_hands")  # never reached 2.5 m/s
    # acceptance: a running median drawn_residual more than 1.5 cm over standing (0.01 m) FAILs, +1 cm passes
    assert "FAIL" not in verdicts({"locomotion_rows": locomotion_rows(running_residual=0.02)}, "run_hands")
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(running_residual=0.03)}, "run_hands")
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(blends_at=(3500, 3900))}, "run_hands")  # a free-air blend burst
    assert "FAIL" in verdicts({"locomotion_rows": locomotion_rows(valid=0)}, "run_hands")  # rows with root_gap_valid 0 are ignored: no running rows left
    bad = locomotion_rows(running_residual=0.5, valid=0) + locomotion_rows()  # invalid running rows with a huge residual do not count
    assert "FAIL" not in verdicts({"locomotion_rows": bad}, "run_hands")
    assert brc.blend_bursts(locomotion_rows(blends_at=(3500, 3900))) == (1, 2)  # two increments inside 1 s, free air
    assert brc.blend_bursts(locomotion_rows(blends_at=(3500, 3900), contact=1)) == (0, 0)  # contact is not free air
    assert brc.blend_bursts(locomotion_rows(blends_at=(3500, 4700))) == (0, 1)  # more than 1 s apart
    assert brc.blend_bursts(locomotion_rows(blends_at=(1000, 1100))) == (0, 0)  # standing (speed < 1.0)
    parsed = brc.parse_locomotion(",".join(brc.LOCOMOTION_COLUMNS) + "\n" + ",".join("1" for _ in brc.LOCOMOTION_COLUMNS) + "\nbad,row\n")
    assert len(parsed) == 1 and parsed[0]["blends"] == 1.0
    with tempfile.TemporaryDirectory() as tmp:  # the command line reads artifacts/locomotion-hands.csv
        for label, rows, code in (("ok", locomotion_rows(), 0), ("short", locomotion_rows(run=10), 1), ("none", None, 1)):
            d = Path(tmp) / label / "artifacts"
            d.mkdir(parents=True)
            (d / "xr-live-status.json").write_text("{}")
            if rows:
                (d / "locomotion-hands.csv").write_text(",".join(brc.LOCOMOTION_COLUMNS) + "\n" + "\n".join(",".join(str(r[k]) for k in brc.LOCOMOTION_COLUMNS) for r in rows) + "\n")
            r = subprocess.run([sys.executable, str(ROOT / "tools/linux/bonelab_run_check.py"), str(d.parent), "--scenario", "run_hands"], capture_output=True, text=True)
            assert r.returncode == code, (label, r.stdout, r.stderr)


def sword_rows(reversed_seq=(False, True, False, True), flip_at=None, hidden_rise_at=None, self_hit_at=None, miss=0.005, blend=True, hold_ms=3000, gap_ms=1000):
    """Fixture trace: one hold per entry of reversed_seq, 11 ms rows, `gap_ms` of nothing held between them. reverse_weight ramps 0 -> 1 over the first 350 ms
    of a reversed hold. flip_at / hidden_rise_at / self_hit_at: a (hold index, ms into the hold) fault; blend=False leaves the weight at 0.5."""
    rows, t, hidden, hits = [], 0, 0, 0
    for k, rev in enumerate(reversed_seq):
        for _ in range(gap_ms // 11):
            rows.append({"elapsed_ms": t, "held": 0, "reversed": 0, "reverse_weight": 0.0, "hilt_miss": 0.5, "weapon_hidden_frames": hidden, "blade_axis_gap_deg": 90.0, "blade_self_hits": hits})
            t += 11
        for i in range(hold_ms // 11):
            ms = i * 11
            flipped = flip_at is not None and flip_at[0] == k and ms >= flip_at[1]
            if hidden_rise_at is not None and hidden_rise_at[0] == k and ms // 11 == hidden_rise_at[1] // 11:
                hidden += 1
            if self_hit_at is not None and self_hit_at[0] == k and ms // 11 == self_hit_at[1] // 11:
                hits += 1
            weight = (min(1.0, ms / 350.0) if blend else 0.5) if rev else 0.0
            rows.append({"elapsed_ms": t, "held": 1, "reversed": int(rev != flipped), "reverse_weight": weight, "hilt_miss": miss if ms >= 700 else 0.3,
                         "weapon_hidden_frames": hidden, "blade_axis_gap_deg": 4.0 if ms >= 350 else 60.0, "blade_self_hits": hits})
            t += 11
    return rows


def sword_regrab_checks():
    segments = sim.scenario_segments("sword_regrab", LEAD)
    sim.compile_script(segments, 120)
    assert max(s["end_ms"] for s in segments) <= 120000
    run = [s for s in segments if s["start_ms"] >= LEAD]
    assert all(s["end_ms"] - s["start_ms"] > 0 for s in run)
    grips = [(s["start_ms"], s["end_ms"], s["input"].get("right_grip", 0), s.get("hand_pitch_degrees", 0), s["right_hand_offset"]) for s in run]
    edges = [g for prev, g in zip([(0, 0, 0, 0, None)] + grips, grips) if g[2] and not prev[2]]
    assert [e[3] for e in edges][:4] == [60, -60, 60, -60] and all(e[4] == [-0.03, 0.25, 0.50] for e in edges[:4]), edges  # F, R, F, R at the zone
    assert len(edges) == 4, "exactly four grips: the sweeps hold the grip through"
    assert all(e[1] - e[0] >= 2000 for e in edges)
    rows = brc.check([{"sword_rows": sword_rows()}], "sword_regrab")
    assert {v for v, _ in rows} == {"PASS", "INFO"}, rows
    assert "max blade_axis_gap_deg 60.0 (from +350 ms 4.0;" in "\n".join(t for v, t in rows if v == "INFO")
    assert "FAIL" in verdicts({"sword_rows": None}, "sword_regrab") and "FAIL" in verdicts({}, "sword_regrab")  # a missing trace
    assert "FAIL" in verdicts({"sword_rows": sword_rows((False, True, False))}, "sword_regrab")  # fewer than 4 holds
    assert "FAIL" in verdicts({"sword_rows": sword_rows((False, False, True, True))}, "sword_regrab")  # not F, R, F, R
    assert "FAIL" in verdicts({"sword_rows": sword_rows(flip_at=(1, 1500))}, "sword_regrab")  # `reversed` flips mid-hold
    assert "FAIL" in verdicts({"sword_rows": sword_rows(hidden_rise_at=(2, 1000))}, "sword_regrab")  # weapon_hidden_frames rises after +0.35 s
    assert "FAIL" not in verdicts({"sword_rows": sword_rows(hidden_rise_at=(2, 100))}, "sword_regrab")  # ... but before it is fine (the blend window)
    assert "FAIL" in verdicts({"sword_rows": sword_rows(self_hit_at=(0, 2000))}, "sword_regrab")  # a blade self hit
    assert "FAIL" in verdicts({"sword_rows": sword_rows(miss=0.03)}, "sword_regrab")  # the hilt leaves the fist
    assert "FAIL" in verdicts({"sword_rows": sword_rows(blend=False)}, "sword_regrab")  # the blend never finishes
    assert "FAIL" in verdicts({"sword_rows": sword_rows(hold_ms=300)}, "sword_regrab")  # holds shorter than the settle time
    parsed = brc.parse_sword_trace(",".join(brc.SWORD_COLUMNS) + "\n" + ",".join("1" for _ in brc.SWORD_COLUMNS) + "\nbad,row\n")
    assert len(parsed) == 1 and parsed[0]["held"] == 1.0
    with tempfile.TemporaryDirectory() as tmp:  # the command line reads artifacts/sword-grip-trace.csv
        for label, rows, code in (("ok", sword_rows(), 0), ("flip", sword_rows(flip_at=(1, 1500)), 1), ("none", None, 1)):
            d = Path(tmp) / label / "artifacts"
            d.mkdir(parents=True)
            (d / "xr-live-status.json").write_text("{}")
            if rows:
                (d / "sword-grip-trace.csv").write_text(",".join(brc.SWORD_COLUMNS) + "\n" + "\n".join(",".join(str(r[k]) for k in brc.SWORD_COLUMNS) for r in rows) + "\n")
            r = subprocess.run([sys.executable, str(ROOT / "tools/linux/bonelab_run_check.py"), str(d.parent), "--scenario", "sword_regrab"], capture_output=True, text=True)
            assert r.returncode == code, (label, r.stdout, r.stderr)


def bad_latch():
    b = copy.deepcopy(CLEAN)
    b["corpse"]["fault_latch"] = True
    return b



def scripted_trace(heading_offset=0.0, residual=0.0, exit_jump=0.0, hmd=40.0, body=-130.0, end_s=3.0, fps=90, old_start=False, entry_jump=0.0,
                   snap_at=None, snap_deg=31.0, old_snap=False):
    """A scripted-view.csv fixture: gameplay, then a scene from t 1.0 to end_s with the head held at `hmd` deg. heading = body + heading_offset.
    As 4c7d222: the offset starts at heading_offset (the last gameplay view) and eases (0.4 s smoothstep) to the gaze, stopping `residual`
    short; inside the 10 deg dead zone it holds at the start. old_start: the pre-4c7d222 helper (start 0, a one-frame jump of heading_offset).
    entry_jump: an extra view jump on the entry frame only.
    snap_at: the native heading snaps by snap_deg at that time (AuthoredHeading's 30 deg re-aim). Ruling 11 (continuous re-aim): the offset
    takes the jump on the snap frame (the view is continuous) and eases it out over 0.4 s, so the gaze lands on the new heading. old_snap:
    the pre-Ruling-11 helper, whose view steps by snap_deg on the snap frame (the gaze is already on the action: dead zone, no ease)."""
    rows, dt = [], 1.0 / fps
    start = 0.0 if old_start else heading_offset
    target = hmd - residual
    turns = abs(target - start) >= 10.0
    smooth = lambda u: u * u * (3 - 2 * u)  # noqa: E731
    ease = lambda t: start + ((target - start) * smooth(min(max((t - 1.0) / 0.4, 0.0), 1.0)) if turns else 0.0)  # noqa: E731
    snapped = lambda t: snap_at is not None and t >= snap_at  # noqa: E731
    extra = lambda t: 0.0 if old_snap or not snapped(t) else snap_deg * (1 - smooth(min(max((t - snap_at) / 0.4, 0.0), 1.0)))  # noqa: E731
    last = max(i * dt for i in range(int(4.0 * fps)) if 1.0 <= i * dt < end_s)
    fold = ease(last) + extra(last) - heading_offset - (snap_deg if snapped(last) else 0.0)  # the exit folds offset + wrap(body - heading)
    for i in range(int(4.0 * fps)):
        t = i * dt
        auth = 1.0 <= t < end_s
        off = ease(t) + extra(t) if auth else 0.0
        heading = body + heading_offset + (snap_deg if snapped(t) else 0.0) if auth else float("nan")
        head = hmd - fold if t >= end_s else hmd
        rel = hmd - off if auth else head
        view = heading + rel + (entry_jump if 1.0 <= t < 1.0 + dt else 0.0) if auth else body + head + (exit_jump if t >= end_s else 0.0)
        rows.append({"t": 100.0 + t, "authored": 1.0 if auth else 0.0, "state_index": 8.0 if auth else -1.0, "hmd_rel_deg": head,
                     "rel_composed_deg": rel, "heading_deg": heading, "offset_deg": off, "fold_deg": fold if end_s <= t < end_s + dt else 0.0, "body_deg": body, "view_yaw_deg": view})
    return rows


def authored_turn_checks():
    """Camera plan Task 3: authored_turn compiles as script v8 (console teleports), fits --lead 45 --seconds 120 with its actions inside the
    4096-row scripted-view trace, and the checker judges fixtures (clean, residual, entry jump, exit jump, no trace, no scene)."""
    segments = sim.scenario_segments("authored_turn", 45000)
    _, transport = sim.compile_script(segments, 120)
    assert transport.splitlines()[0].startswith("D2VR_GAMEPLAY_SCRIPT 8 120000 "), transport.splitlines()[0]
    lines = [s["console"] for s in segments if "console" in s]
    assert lines[0] == "ark_god_player 1" and sum(1 for l in lines if l.startswith("setviewpos ")) == 2, lines
    assert all(s["start_ms"] >= 45000 for s in segments if "console" in s)
    end = max(s["end_ms"] for s in segments)
    assert end <= 45000 + 30000, end  # the trace holds ~57 s of gameplay from the load: keep the actions early
    heads = {round(s["head_pose"][4], 4) for s in segments if "head_pose" in s}
    assert len(heads) == 3, heads  # +40 (mantle), -35 (takedown), 0 (idle)
    grips = [s for s in segments if s["input"].get("right_grip")]
    assert grips and not any(s["input"].get("right_trigger") for s in grips), "the takedown is the grip alone"
    judge = lambda rows: brc.check([{"scripted_rows": rows}], "authored_turn")  # noqa: E731
    ok = judge(scripted_trace())
    assert not any(v == "FAIL" for v, _ in ok), ok
    assert any(v == "INFO" and "state_index 8 Mantle" in t and "delta +40.00" in t for v, t in ok), ok
    assert any(v == "FAIL" and "residual" in t for v, t in judge(scripted_trace(residual=5.0)))
    assert not any(v == "FAIL" for v, _ in judge(scripted_trace(residual=2.0)))
    assert any(v == "FAIL" and "entry view_yaw" in t for v, t in judge(scripted_trace(heading_offset=40.0, old_start=True)))  # pre-4c7d222: a 40 deg entry jump
    gaze = judge(scripted_trace(heading_offset=40.0))  # the mantle in the sim: native heading on the gaze, so no turn is needed
    assert not any(v == "FAIL" for v, _ in gaze) and any("turn +0.00" in t for _, t in gaze), gaze
    for fps in (30, 72, 90):  # a real one-frame jump over 3.1 deg fails whatever the frame rate, eased or not
        assert any(v == "FAIL" and "entry view_yaw" in t for v, t in judge(scripted_trace(heading_offset=40.0, entry_jump=3.2, fps=fps))), fps
        assert any(v == "FAIL" and "entry view_yaw" in t for v, t in judge(scripted_trace(heading_offset=10.0, entry_jump=3.2 + 1.5 * 30 / 0.4 / fps, fps=fps))), fps
        assert not any(v == "FAIL" for v, _ in judge(scripted_trace(heading_offset=10.0, fps=fps))), fps
    assert any(v == "FAIL" and "residual" in t for v, t in judge(scripted_trace(residual=3.0)))  # a residual of exactly 3 deg fails
    assert any(v == "FAIL" and "exit view_yaw" in t for v, t in judge(scripted_trace(exit_jump=10.0)))
    # Ruling 11: a 30 deg heading snap inside the scene. Continuous re-aim (the view eases from the last view) passes at any frame rate;
    # the old one-frame step of the snap fails; a snap right at the end of the scene (its ease cut by the exit) still passes.
    for fps in (30, 72, 90):
        cont = judge(scripted_trace(snap_at=2.0, fps=fps))
        assert not any(v == "FAIL" for v, _ in cont) and any(v == "PASS" and "re-aim 1" in t for v, t in cont), (fps, cont)
        assert any(v == "FAIL" and "re-aim 1" in t for v, t in judge(scripted_trace(snap_at=2.0, old_snap=True, fps=fps))), fps
        assert not any(v == "FAIL" for v, _ in judge(scripted_trace(snap_at=2.0, snap_deg=-45.0, fps=fps))), fps
    assert not any(v == "FAIL" for v, _ in judge(scripted_trace(snap_at=2.9)))
    short = judge(scripted_trace(end_s=1.3))  # a scene shorter than the ease: residual not judged, still observed
    assert not any(v == "FAIL" for v, _ in short) and any("not judged" in t for _, t in short), short
    assert any(v == "FAIL" for v, _ in judge([]))
    assert any(v == "FAIL" and "observed" in t for v, t in judge([r for r in scripted_trace() if not r["authored"]]))
    assert brc.parse_scripted_view("t,authored,state_index,hmd_rel_deg,rel_composed_deg,heading_deg,offset_deg,fold_deg,body_deg,view_yaw_deg\n"
                                   "1,1,8,40,40,nan,0,0,-130,-90\n1,2\n")[0]["state_index"] == 8.0



def cutscene_checks():
    """Camera plan Task 4: the cutscene scenario compiles, fits --lead 45 --seconds 120, and presses nothing a cinematic could take as a
    skip: menu-only A pulses during the first 24 s of the lead, then neutral input only (no button, stick, trigger or grip); one
    console line at the lead's end loads the cutscene slot."""
    segments = sim.scenario_segments("cutscene", 45000)
    _, transport = sim.compile_script(segments, 120)
    assert transport.splitlines()[0].startswith("D2VR_GAMEPLAY_SCRIPT 8 120000 "), transport.splitlines()[0]
    lines = [s["console"] for s in segments if "console" in s]
    assert len(lines) == 1 and lines[0].startswith("loadgame ") and lines[0].split()[1].isdigit(), lines  # the cutscene slot, at the lead's end
    assert [s["start_ms"] for s in segments if "console" in s] == [45000]
    end = max(s["end_ms"] for s in segments)
    assert end <= 120000 - 6000, end  # the rig adds 6 s; the session cap is 120 s
    assert end >= 45000 + 60000, end  # a minute of watching after the lead
    pulses = [s for s in segments if s["input"].get("a")]
    assert pulses and all(s.get("when") == "menu" and s["end_ms"] <= 24000 for s in pulses), pulses
    for seg in segments:
        if seg["start_ms"] >= 24000 or not seg["input"].get("a"):
            assert seg["input"] == {"move_x": 0.0} and "when" not in seg, seg
    starts = sorted((s["start_ms"], s["end_ms"]) for s in segments)
    assert starts[0][0] == 0 and all(a[1] == b[0] for a, b in zip(starts, starts[1:])), "contiguous, no gaps"


COIN, COIN_OWNER, SHELF = (1.0, 2.0, 0.85), 9001, 9002
LOOT_OWNERS = "owner,spawn,takeable,pos_x,pos_y,pos_z,min_x,min_y,min_z,max_x,max_y,max_z\n" \
              "9001,11,1,1.0,2.0,0.85,0.98,1.98,0.85,1.02,2.02,0.86\n9002,12,0,1.2,2.0,0.8,0.9,1.7,0.0,1.5,2.3,0.85\n9003,13,1,4,4,1,,,,,,\n"


def loot_trace(frames):
    """frames: list of dicts (side, palm, coin=None or (rank, los_called, los_blocked), selected, prompt) -> native-loot-reach.csv text."""
    lines = [",".join(brc.LOOT_COLUMNS)]
    for i, f in enumerate(frames):
        members = [(SHELF, 0 if not f.get("coin") or f["coin"][0] else 1, 1, 0, 0)]
        if f.get("coin"):
            members.append((COIN_OWNER, *f["coin"]))
        for owner, rank, called, blocked in [(m[0], m[1], m[2], m[3]) for m in members]:
            lines.append(",".join(str(v) for v in (1000 + 11 * i, f.get("side", 1), *f.get("palm", (1.0, 2.0, 0.95)), 1, 0, 0, owner, 11, int(owner == COIN_OWNER),
                                                   len(members), rank, called, blocked, f.get("selected", 0), f.get("prompt", 0))))
    return "\n".join(lines) + "\n"


def loot_judge(frames, coin=COIN):
    status = {"loot_rows": brc.parse_loot_reach(loot_trace(frames)), "loot_owners": brc.parse_loot_owners(LOOT_OWNERS)}
    out = brc.check_loot(status, coin, "table")
    cls = [t.split("classification: ")[1] for v, t in out if "classification: " in t]
    return out, cls[0] if cls else None


def loot_reach_checks():
    """Hands plan Task 1: loot_reach compiles as script v8 (three teleports), fits --lead 45 --seconds 120, sweeps both palms
    before each case's squeezes, and the checker classifies fixtures (PASS, ORDER, ELIGIBILITY, NOT_IN_SET, LOS, MIXED, the 0.45 m gate,
    a take, a missing trace)."""
    segments = sim.scenario_segments("loot_reach", 45000)
    _, transport = sim.compile_script(segments, 120)
    assert transport.splitlines()[0].startswith("D2VR_GAMEPLAY_SCRIPT 8 120000 "), transport.splitlines()[0]
    lines = [s["console"] for s in segments if "console" in s]
    assert [l.split()[0] for l in lines] == ["setviewpos"] * 3, lines  # the player is kept alive by --knob sim-invulnerable, not god/buddha
    assert all(s["start_ms"] >= 45000 for s in segments if "console" in s)
    end = max(s["end_ms"] for s in segments)
    assert end <= 120000 - 6000, end  # the rig adds 6 s; the session cap is 120 s
    after = [s for s in segments if s["start_ms"] >= 45000 and "console" not in s]
    assert all(s.get("head_pitch_degrees") == -35.0 and "right_hand_pose" in s and "left_hand_pose" in s for s in after)
    for side in ("right", "left"):
        squeezes = [s for s in after if s["input"].get(side + "_grip")]
        assert len(squeezes) == 3 and all(s["end_ms"] - s["start_ms"] == 600 for s in squeezes), (side, squeezes)
    first_squeeze = min(s["start_ms"] for s in after if s["input"].get("right_grip"))
    left_moves = [s for s in after if s["start_ms"] < first_squeeze and s["left_hand_pose"][:3] != [-0.2, 1.3, -0.4]]
    assert left_moves, "the left sweep comes before the first squeeze"
    # the table case's lowest palm-down keyframe sits 5 cm above the coin (tracking y = height above the feet - 0.05)
    lr = sim._load_tool("d2vr_scenario_loot_reach", ROOT / "tools/linux/scenarios/loot_reach.py")
    name, coin, stand, crouch, lift = lr.CASES[0]
    yaw = lr.yaw_to(stand, coin)
    assert name == "table" and crouch == lift == 0 and \
        lr.to_tracking((coin[0], coin[1], coin[2] + 0.05), stand, yaw) == [0.0, round(coin[2] + 0.05 - stand[2] - 0.05, 4), round(-0.6 + lr.PALM_AHEAD_M, 4)]
    assert [c[0] for c in lr.CASES] == ["table", "floor", "drawer"] and all(c[3] <= 0.7 for c in lr.CASES)  # the head offset clamps at -0.7
    drawer = sim.scenario_segments("loot_reach_drawer", 45000)
    sim.compile_script(drawer, 120)
    assert [s["console"] for s in drawer if "console" in s] == [l for l in lines if l.startswith("setviewpos -13.230")], "the drawer case alone"
    near = {"palm": (1.0, 2.0, 0.95)}
    ok, cls = loot_judge([dict(near, coin=(0, 1, 0), selected=COIN_OWNER, prompt=COIN_OWNER)] * 10)
    assert cls == "PASS" and any(v == "PASS" and "got 10" in t for v, t in ok), ok
    assert loot_judge([dict(near, coin=(1, 0, 0), selected=SHELF)] * 10)[1] == "ORDER"
    assert loot_judge([dict(near, coin=(0, 0, 0), selected=SHELF)] * 10)[1] == "ELIGIBILITY"
    assert loot_judge([dict(near, selected=SHELF)] * 10)[1] == "NOT_IN_SET"
    assert loot_judge([dict(near, coin=(0, 1, 1), selected=SHELF)] * 10)[1] == "LOS"
    assert loot_judge([dict(near, coin=(0, 1, 0), selected=SHELF)] * 5 + [dict(near, coin=(0, 1, 0), selected=COIN_OWNER)] * 5)[1] == "MIXED"
    # boundaries: selected 0.8 passes, 0.7 does not; rank0 exactly 0.5 is not ORDER
    assert loot_judge([dict(near, coin=(0, 1, 0), selected=COIN_OWNER)] * 8 + [dict(near, coin=(0, 1, 0), selected=SHELF)] * 2)[1] == "PASS"
    assert loot_judge([dict(near, coin=(0, 1, 0), selected=COIN_OWNER)] * 7 + [dict(near, coin=(0, 1, 0), selected=SHELF)] * 3)[1] == "MIXED"
    assert loot_judge([dict(near, coin=(0, 0, 0))] * 5 + [dict(near, coin=(2, 0, 0))] * 5)[1] == "ELIGIBILITY"
    # the 0.45 m gate: far frames do not count
    far = [dict(palm=(1.0, 2.0, 1.5), selected=SHELF)] * 30
    out, cls = loot_judge(far + [dict(near, coin=(0, 1, 0), selected=COIN_OWNER)] * 10)
    assert cls == "PASS" and any("got 10" in t for _, t in out), out
    out, cls = loot_judge(far)
    assert cls is None and any(v == "FAIL" and "got 0" in t for v, t in out), out
    # a take: selected, then gone from every set while the trace goes on -> the later frames are dropped, not NOT_IN_SET
    out, cls = loot_judge([dict(near, coin=(0, 1, 0), selected=COIN_OWNER)] * 10 + [dict(near, selected=SHELF)] * 20)
    assert cls == "PASS" and any("coin taken" in t for _, t in out), out
    # gone without being selected first is a miss, not a take
    out, cls = loot_judge([dict(near, coin=(0, 1, 0), selected=SHELF)] * 10 + [dict(near, selected=SHELF)] * 20)
    assert cls == "MIXED" and not any("coin taken" in t for _, t in out), out
    # an unseen coin: nearest takeable too far -> NOT_IN_SET over the frames near --coin
    out, cls = loot_judge([dict(near, selected=SHELF)] * 10, coin=(1.0, 2.0, 0.85))
    assert cls == "NOT_IN_SET", out
    out, cls = loot_judge([dict(palm=(7.0, 7.0, 1.0), selected=SHELF)] * 10, coin=(7.0, 7.0, 0.9))
    assert cls == "NOT_IN_SET" and any("not seen" in t for _, t in out), out
    assert any(v == "FAIL" for v, _ in brc.check_loot({"loot_rows": [], "loot_owners": {}}, COIN))
    assert brc.parse_loot_owners(LOOT_OWNERS)[9003]["min"] is None and brc.parse_loot_owners(LOOT_OWNERS)[9003]["pos"] == [4.0, 4.0, 1.0]
    assert len(brc.parse_loot_reach(loot_trace([near]) + "1,2\n")) == 1  # a short row is dropped


if __name__ == "__main__":
    main()
