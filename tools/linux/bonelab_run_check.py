#!/usr/bin/env python3
"""Check a sim run of a Part 1 corpse/prop scenario against its assertions.

usage: bonelab_run_check.py RUN_DIR [RUN_DIR ...] --scenario NAME [--reject-touch]
RUN_DIR is a ~/d2vr-test-builds/sim-runs/<stamp> folder (reads RUN_DIR/artifacts/xr-live-status.json). Several run dirs are
one scenario's sessions (S5 runs four of 50 cycles): counters are summed, binds >= 40 is required per session.
Prints PASS/FAIL/INFO per assertion; exit 1 on any FAIL. Side-pair fields are [left, right].
"""
import argparse
import json
import sys
from pathlib import Path

FINGERS = ("finger_touch",)  # Plan C S10: scripted touch poses; the DLL's per-segment `fingers` samples ride in status["finger_segments"]
FINGER_POSES = ("rest", "point", "thumbs_up", "fist", "open")
FINGER_NAMES = ("thumb", "index", "middle", "ring", "pinky")
SIDE = 1  # the scenario drives the right hand
PROPS = ("oil_tank_drop", "oil_tank_throw")
TANKS = ("tank_wall", "tank_drop", "tank_drop_cycles")  # Part 5A held-clamp scenarios S12-S14
CARRY_FAULTS = ("failed_reads", "rejected_publishes", "bad_quat_reads")
HAND_BODY = ("hand_body_soak",)  # Plan B B14: the driven hand bodies (proofs a, b, d), summed over sessions
HAND_BODY_COST_US = 200          # proof (d): the drive's cost per tick, both hands summed
HAND_BODY_PRESSED_TICKS = 1000  # review P3-5: about 5 s per hand inside the capsule per session (the scripted segment holds 14 s; ~2100 seen)
HAND_BODY_DRIFT_UM = 1000        # proof (b): the player root's unexplained movement while a hand is pressed into the capsule
TANK_HAND_BODY = ("tank_hand_body",)  # Plan B B16 S15: the tank carried by the hand body (run with --knob hand-body and --knob no-hand-body)
HAND_FILTER_DEFAULT = 0x000885E4  # R1Recipe::filter: the hand body's word whenever it holds nothing through the body


def _pair(v):
    return list(v) if isinstance(v, (list, tuple)) and len(v) == 2 else [0, 0]


def _sum_pairs(statuses, getter):
    out = [0, 0]
    for s in statuses:
        p = _pair(getter(s))
        out = [out[0] + p[0], out[1] + p[1]]
    return out


def check(statuses, scenario, proofs=("a", "b", "d"), reject_touch=False):
    """statuses: one parsed xr-live-status.json per session. -> list of (verdict, text); verdict PASS/FAIL/INFO."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    def info(text):
        out.append(("INFO", text))

    if scenario in HAND_BODY:
        return check_hand_body(statuses, proofs)
    if scenario in TANK_HAND_BODY:
        return check_tank_hand_body(statuses)
    if scenario in FINGERS:
        return check_fingers(statuses, reject_touch)
    corpse = [s.get("corpse") or {} for s in statuses]
    if not statuses or any(not c for c in corpse):
        return [("FAIL", "status has no corpse block")]
    if scenario in TANKS:
        return check_tanks(statuses, scenario, corpse)
    prop_scenario = scenario in PROPS
    add(not any(c.get("fault_latch") for c in corpse), "corpse.fault_latch == false")
    add(sum(c.get("runaway", 0) for c in corpse) == 0, "corpse.runaway == 0")
    peak = max([max(_pair(c.get("peak_bone_speed_all17"))) for c in corpse])
    add(peak < 12, f"peak_bone_speed_all17 < 12 (max {peak:.3g})")
    posts = sum(c.get("event_posts_b9", 0) for c in corpse)
    attempts = sum(c.get("wake_attempts", 0) for c in corpse)
    add(posts <= 2 * attempts, f"event_posts_b9 {posts} <= 2 x wake_attempts {attempts}")
    lost = sum(s.get("physical_grab_lost_ownership", 0) for s in statuses)
    add(lost == 0, f"physical_grab_lost_ownership == 0 (got {lost})")
    binds = _sum_pairs(corpse, lambda c: c.get("binds"))
    holds = _sum_pairs(corpse, lambda c: c.get("holds"))
    rel = lambda key: _sum_pairs(corpse, lambda c: (c.get("releases") or {}).get(key))
    if scenario in ("corpse_drag", "corpse_lift", "corpse_probe"):
        add(sum(binds) >= 1, f"binds >= 1 (got {binds})")
    if scenario == "corpse_drag":
        add(sum(rel("open_fist")) >= 1, f"releases.open_fist >= 1 (got {rel('open_fist')})")
        info(f"lag_m_max {[max(_pair(c.get('lag_m_max'))) for c in corpse]}")
    if scenario == "corpse_collar":
        collar = sum((c.get("bones") or {}).get("collar", 0) for c in corpse)
        add(collar >= 1, f"bones.collar >= 1 (got {collar})")
    if scenario == "corpse_two_hands":
        add(binds[0] >= 1 and binds[1] >= 1, f"binds both sides >= 1 (got {binds})")
        add(all(h >= 0 for h in holds), f"holds both sides recorded (got {holds})")
    if scenario == "corpse_stress":
        for i, c in enumerate(corpse):
            b = sum(_pair(c.get("binds")))
            add(b >= 40, f"session {i + 1}: binds >= 40 (got {b})")
        info(f"summed binds {sum(binds)}, releases open_fist {rel('open_fist')}, lag {rel('lag')}")
    if scenario == "corpse_reload":
        add(sum(rel("lifecycle")) >= 1, f"releases.lifecycle >= 1 (got {rel('lifecycle')})")
    if scenario == "corpse_prompt":
        carries = [s.get("carry", {}).get("holds") if isinstance(s.get("carry"), dict) else None for s in statuses]
        winner = "grab" if sum(binds) else "carry/prompt" if any(carries) else "neither"
        info(f"binds {binds}, releases {{open_fist {rel('open_fist')}, lifecycle {rel('lifecycle')}}}, carry holds {carries}: {winner} won")
    if prop_scenario:
        gap = max(float(s.get("prop_seat_gap_m", 0) or 0) for s in statuses)
        info(f"prop_grasp_point {statuses[-1].get('prop_grasp_point')}, prop_seat_gap_m {gap:.3g}, corpse holds {holds} (a native HoldObject tank is not a lane)")
    return out


def check_fingers(statuses, reject_touch=False):
    """S10: the right hand's curl at the end of each of the last five segments (rest, point, thumbs-up, fist, open). With the reject switch
    the runtime refuses the touch paths: the check then only needs touch_bound == false and the legacy mapping."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    if not statuses or any(not isinstance(s.get("fingers"), dict) for s in statuses):
        return [("FAIL", "status has no fingers block")]
    for n, status in enumerate(statuses):
        tag = f"session {n + 1}: " if len(statuses) > 1 else ""
        final = status["fingers"]
        bound = bool(final.get("touch_bound"))
        if reject_touch:
            add(not bound, f"{tag}touch_bound == false with the reject switch (got {bound})")
            add((final.get("mapping") or [None, None])[SIDE] == "legacy", f"{tag}mapping legacy under reject (got {final.get('mapping')})")
            continue
        add(bound, f"{tag}touch_bound == true (got {bound})")
        add(final.get("action_create_failed") is False, f"{tag}action_create_failed == false (got {final.get('action_create_failed')})")
        samples, poses = status.get("finger_segments") or [], status.get("finger_poses") or []
        add(len(poses) == len(FINGER_POSES), f"{tag}the script has {len(FINGER_POSES)} touch pose segments (got {len(poses)})")
        by = {}
        for pose, seg in zip(FINGER_POSES, poses):  # the last sample of each pose's own segment, taken at or after its end (a blip sample mid-segment is superseded)
            mine = [s for s in samples if s.get("segment") == seg.get("index") and s.get("elapsed_ms", -1) >= seg.get("end_ms", 1 << 60)]
            add(bool(mine), f"{tag}{pose}: a sample at the end of segment {seg.get('index')} (got {len([s for s in samples if s.get('segment') == seg.get('index')])} before its end)")
            if mine:
                by[pose] = mine[-1].get("fingers") or {}
        if len(by) < len(FINGER_POSES):
            continue
        curl = {}
        for pose, sample in by.items():
            row = (sample.get("curl") or [[], []])[SIDE]
            curl[pose] = [float(v) for v in row] if len(row) == 5 else [float("nan")] * 5
            add((sample.get("mapping") or [None, None])[SIDE] == "touch", f"{tag}{pose}: mapping touch (got {sample.get('mapping')})")
        pr = lambda pose: ", ".join(f"{n}={v:.2f}" for n, v in zip(FINGER_NAMES, curl[pose]))  # noqa: E731
        add(abs(curl["rest"][1] - 0.3) <= 0.1, f"{tag}rest: index ~ 0.3 (got {curl['rest'][1]:.3f})")
        add(curl["point"][1] < 0.1, f"{tag}point: index < 0.1 (got {curl['point'][1]:.3f})")
        add(curl["thumbs_up"][0] < 0.1, f"{tag}thumbs_up: thumb < 0.1 (got {curl['thumbs_up'][0]:.3f})")
        add(all(v > 0.9 for v in curl["fist"]), f"{tag}fist: all fingers > 0.9 ({pr('fist')})")
        add(all(v < 0.1 for v in curl["open"]), f"{tag}open: all fingers < 0.1 ({pr('open')})")
        for pose in FINGER_POSES:
            out.append(("INFO", f"{tag}curl at the end of {pose}: {pr(pose)}"))
    return out


def check_tanks(statuses, scenario, corpse):
    """S12 tank_wall, S13 tank_drop, S14 tank_drop_cycles (summed over sessions) against the held_clamp status block."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    clamps = [s.get("held_clamp") or {} for s in statuses]
    if any(not c for c in clamps):
        return [("FAIL", "status has no held_clamp block")]
    missing = lambda key: [i for i, c in enumerate(clamps) if not isinstance(c.get(key), (int, float)) or isinstance(c.get(key), bool)]
    total = lambda key: sum(c.get(key) or 0 for c in clamps)
    needs = ("sweeps", "hits", "clamped_frames") if scenario == "tank_wall" else ("release_overlap_unresolved", "releases")
    for key in needs:  # an absent counter is a FAIL, never a zero
        add(not missing(key), f"held_clamp.{key} present in every session")
    if any(missing(k) for k in needs):
        return out
    sweeps, hits, clamped = total("sweeps"), total("hits"), total("clamped_frames")
    unresolved = total("release_overlap_unresolved")
    cast_max = max((c.get("cast_us_max") or 0) for c in clamps)
    out.append(("INFO", f"sweeps {sweeps}, hits {hits}, clamped_frames {clamped}, releases {total('releases')}, "
                f"cast_us {[c.get('cast_us') for c in clamps]}, cast_us_max {cast_max}, "
                f"layer_at_release {[c.get('layer_at_release') for c in clamps]}, depen_frames {total('release_depen_frames')}"))
    layers = [s.get("layer") or {} for s in statuses]
    out.append(("INFO", f"layer restores {[l.get('restores') for l in layers]}, pending {[l.get('pending') for l in layers]}"))
    if scenario == "tank_wall":
        add(sweeps > 0, f"held_clamp.sweeps > 0 (got {sweeps})")
        add(hits > 0, f"held_clamp.hits > 0 (got {hits})")
        add(clamped > 0, f"held_clamp.clamped_frames > 0 (got {clamped})")
    else:
        add(unresolved == 0, f"release_overlap_unresolved == 0 (got {unresolved})")
    if scenario == "tank_drop":
        add(total("releases") >= 2, f"held_clamp.releases >= 2 for the two S13 releases (got {total('releases')})")
        layer_ok = all(isinstance(c.get("layer_at_release"), int) and not isinstance(c.get("layer_at_release"), bool)
                       and c["layer_at_release"] >= 0 for c in clamps)  # the writer starts at -1 = never recorded
        add(layer_ok, f"layer_at_release recorded, >= 0 (got {[c.get('layer_at_release') for c in clamps]})")
        watch = statuses[-1].get("release_watch") or {}
        count = watch.get("count")
        add(isinstance(count, int) and count >= 2, f"release_watch.count >= 2 (got {count})")
        # release_watch keeps only the LAST release (pressed into the floor, 3 s window, z0 = height at the layer restore); the
        # 1.5 m drop is not judged here, only by the headset Tier C item. The writer emits no age/sample field, so a watch that
        # ended early (stale z_last) cannot be detected.
        z0, last, zmin = watch.get("z_at_release"), watch.get("z_last"), watch.get("z_min")
        nums = all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in (z0, last, zmin))
        add(nums and last >= z0 - 0.15 and zmin >= z0 - 0.5,
            f"last (floor-pressed) release rests near its release height over the 3 s watch: z_last {last} >= z_at_release {z0} - 0.15, z_min {zmin} >= z_at_release - 0.5")
    if scenario == "tank_drop_cycles":
        releases = total("releases")
        add(releases >= 100, f"releases >= 100 over {len(statuses)} sessions (got {releases})")
        add(not any(c.get("fault_latch") for c in corpse), "corpse.fault_latch == false")
        carries = [s.get("carry") for s in statuses]
        present = all(isinstance(c, dict) and all(isinstance(c.get(k), int) for k in CARRY_FAULTS) for c in carries)
        add(present, f"carry block with {CARRY_FAULTS} present in every session")
        faults = sum(c.get(k, 0) for c in carries if isinstance(c, dict) for k in CARRY_FAULTS if isinstance(c.get(k), int))
        # the status has end values only, so "no new" is checked as absolute zero (a start baseline is not recorded)
        add(faults == 0, f"carry fault counters {CARRY_FAULTS} == 0 (got {faults})")
    return out


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def check_hold_coupling(statuses):
    """B16, every session's "hold_coupling" object (and the hand body's own filter word): idle-correct (a side that held nothing never used the
    body), and no layer left swapped at the end (the keeper's word and the body's word are the R1 default on both sides, every swap came back
    through SafeRelease or with a new body, and the held-object layer ledger has nothing pending). An absent block or counter is a FAIL."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    keys = ("frames", "uses_body", "native", "motor", "corpse", "word", "swaps", "restores", "unresolved", "body_resets")
    for i, s in enumerate(statuses):
        hc = s.get("hold_coupling")
        sides = hc.get("sides") if isinstance(hc, dict) else None
        if not isinstance(sides, list) or len(sides) != 2 or not all(isinstance(sd, dict) and all(_num(sd.get(k)) for k in keys) for sd in sides):
            add(False, f"session {i + 1}: hold_coupling block with sides[2] {keys}")
            continue
        for k, sd in enumerate(sides):
            if sd["frames"] == 0:
                add(sd["uses_body"] == 0, f"session {i + 1} side {k}: no hold, so the body was never used (uses_body {sd['uses_body']})")
            add(sd["uses_body"] <= sd["frames"], f"session {i + 1} side {k}: uses_body {sd['uses_body']} <= hold frames {sd['frames']}")
            add(sd["word"] == HAND_FILTER_DEFAULT, f"session {i + 1} side {k}: the hand's filter word is the default at the end ({sd['word']:#x})")
            add(sd["swaps"] == sd["restores"] + sd["body_resets"],
                f"session {i + 1} side {k}: every swap came back (swaps {sd['swaps']} == restores {sd['restores']} + body_resets {sd['body_resets']})")
        hb = s.get("hand_body") if isinstance(s.get("hand_body"), dict) else {}
        if hb.get("enabled") is True and isinstance(hb.get("sides"), list):
            words = [sd.get("filter") for sd in hb["sides"]]
            add(all(w == HAND_FILTER_DEFAULT for w in words) and len(words) == 2, f"session {i + 1}: the hand bodies' own filter words are the default ({words})")
            stranded = [sd.get("filter_stranded") for sd in hb["sides"]]
            add(all(v == 0 for v in stranded), f"session {i + 1}: no untracked body may still carry a stripped word (filter_stranded {stranded})")
            add(hb.get("filter_readback_mismatches") == 0,
                f"session {i + 1}: every filter write read back as written (filter_readback_mismatches {hb.get('filter_readback_mismatches')})")
        layer = s.get("layer") if isinstance(s.get("layer"), dict) else {}
        add(layer.get("pending") == 0, f"session {i + 1}: the held-object layer ledger has nothing pending (got {layer.get('pending')})")
    return out


def check_tank_hand_body(statuses):
    """S15: the tank carried by the hand body, in both knob states. The last (floor-pressed) release rests near its release height,
    release_overlap_unresolved == 0, the held clamp released at least twice, hand_body.faults == 0 (knob on), no layer left swapped; knob on: the
    native hold used the body and the body touched the world (contact_frames > 0); knob off: the body was never used."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    if not statuses:
        return [("FAIL", "no sessions")]
    for i, s in enumerate(statuses):
        clamp = s.get("held_clamp") if isinstance(s.get("held_clamp"), dict) else {}
        hb = s.get("hand_body") if isinstance(s.get("hand_body"), dict) else None
        if hb is None:
            add(False, f"session {i + 1}: status has no hand_body block")
            continue
        on = hb.get("enabled") is True
        for key in ("release_overlap_unresolved", "releases"):
            add(_num(clamp.get(key)), f"session {i + 1}: held_clamp.{key} present")
        add(clamp.get("release_overlap_unresolved") == 0, f"session {i + 1}: release_overlap_unresolved == 0 (got {clamp.get('release_overlap_unresolved')})")
        add(_num(clamp.get("releases")) and clamp["releases"] >= 2, f"session {i + 1}: held_clamp.releases >= 2 (got {clamp.get('releases')})")
        watch = s.get("release_watch") if isinstance(s.get("release_watch"), dict) else {}
        z0, last, zmin = watch.get("z_at_release"), watch.get("z_last"), watch.get("z_min")
        add(_num(watch.get("count")) and watch["count"] >= 2, f"session {i + 1}: release_watch.count >= 2 (got {watch.get('count')})")
        add(all(_num(v) for v in (z0, last, zmin)) and last >= z0 - 0.15 and zmin >= z0 - 0.5,
            f"session {i + 1}: the floor-pressed tank rests above the floor after 5 s: z_last {last} >= z_at_release {z0} - 0.15, z_min {zmin} >= z_at_release - 0.5")
        sides = (s.get("hold_coupling") or {}).get("sides") if isinstance(s.get("hold_coupling"), dict) else None
        native = sum(sd.get("native", 0) for sd in sides) if isinstance(sides, list) else None
        used = sum(sd.get("uses_body", 0) for sd in sides) if isinstance(sides, list) else None
        if on:
            add(_num(hb.get("faults")) and hb["faults"] == 0, f"session {i + 1}: hand_body.faults == 0 (got {hb.get('faults')})")
            add(hb.get("available") is True and hb.get("hand_body_fault") is False, f"session {i + 1}: hand body available, no HandBodyFault")
            add(_num(hb.get("contact_frames")) and hb["contact_frames"] > 0, f"session {i + 1}: hand_body.contact_frames > 0 (got {hb.get('contact_frames')})")
            add(_num(native) and native > 0 and _num(used) and used > 0, f"session {i + 1}: the tank's native hold was coupled to the body (native frames {native}, uses_body {used})")
        else:
            add(used == 0, f"session {i + 1}: knob off: the body was never used (uses_body {used})")
    return out + check_hold_coupling(statuses)


def wall_percentile_upper_us(hist, edges, q):
    """The upper edge of the histogram bin holding the q-quantile (conservative); inf when it falls in the open last bin; None if empty."""
    total = sum(hist)
    if not total:
        return None
    need, run = q * total, 0
    for i, n in enumerate(hist):
        run += n
        if run >= need:
            return edges[i] if i < len(edges) else float("inf")
    return float("inf")


def check_hand_body(statuses, proofs=("a", "b", "d")):
    """B14 soak: every session's "hand_body" object. Proof (a): faults summed == 0, available, bodies created and driven, a clean or live
    install. Proof (b): max player_drift_um <= 1000, pressed_ticks >= 1000 per session, and the positive control root_moved_ticks > 0 (the
    root is seen to move under locomotion input, so a stale root cannot pass with 0 um). Proof (d), fix-round-1 criteria set before the
    re-run: per-tick drive CPU time max <= 200 us (both hands; CPU time must be available) AND the wall-time p99.9 (from the summed
    histogram, conservative bin edge) <= 200 us; the wall max is reported only. An absent counter is a FAIL, never a zero.
    clean_stop (below) was added after run 20261024T022440 wrote its status after the session-end stop; reviewed (task-B14-review P3-4)."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    if not statuses:
        return [("FAIL", "no sessions")]
    hb = [s.get("hand_body") if isinstance(s.get("hand_body"), dict) else {} for s in statuses]
    if any(not h for h in hb):
        return [("FAIL", "status has no hand_body block")]
    for key in ("faults", "available", "hand_body_fault", "sides") + (("player_drift_um", "pressed_ticks", "root_moved_ticks") if "b" in proofs else ()) + (("timing",) if "d" in proofs else ()):
        add(all(key in h for h in hb), f"hand_body.{key} present in every session")
    nums = ("faults",) + (("player_drift_um", "pressed_ticks", "root_moved_ticks") if "b" in proofs else ())
    if any(not all(_num(h.get(k)) for k in nums) or not isinstance(h.get("sides"), list) or len(h["sides"]) != 2 for h in hb) or \
            ("d" in proofs and any(not isinstance(h.get("timing"), dict) for h in hb)):
        out.append(("FAIL", f"hand_body counters missing or not numbers ({', '.join(nums)}, sides[2]{', timing' if 'd' in proofs else ''})"))
        return out
    for i, h in enumerate(hb):
        # The status file may be written after the session-end stop(): installed is then false, which is fine only after a clean stop
        # (every side absent, each created body destroyed by us, nothing abandoned or lost).
        clean_stop = all(sd.get("state") == 0 and sd.get("destroys", 0) >= sd.get("creates", 0) >= 1 and sd.get("abandons", 1) == 0
                         and sd.get("lost", 1) == 0 for sd in h["sides"])
        add(h.get("enabled") is True and (h.get("installed") is True or clean_stop),
            f"session {i + 1}: hand body enabled and installed (or cleanly stopped before the snapshot: installed {h.get('installed')})")
        add(h.get("available") is True and h.get("hand_body_fault") is False, f"session {i + 1}: available, no HandBodyFault (why {h.get('why')!r})")
        add(h.get("g1") == "pass" and not h.get("g1_violation"), f"session {i + 1}: G1 pass, no violation (g1 {h.get('g1')!r})")
        creates = [sd.get("creates", 0) for sd in h["sides"]]
        writes = [(sd.get("drive") or {}).get("drive_writes", 0) for sd in h["sides"]]
        add(all(c >= 1 for c in creates) and all(w >= 100 for w in writes),
            f"session {i + 1}: both bodies created and driven (creates {creates}, drive_writes {writes})")
        if "b" in proofs:
            add(h["pressed_ticks"] >= HAND_BODY_PRESSED_TICKS, f"session {i + 1}: pressed_ticks {h['pressed_ticks']} >= {HAND_BODY_PRESSED_TICKS} (the drift window was entered)")
            add(h["root_moved_ticks"] > 0, f"session {i + 1}: root_moved_ticks {h['root_moved_ticks']} > 0 (positive control: the root moves under locomotion, travel {h.get('root_travel_um')} um)")
    faults = sum(h["faults"] for h in hb)
    add(faults == 0, f"hand_body.faults summed over {len(hb)} sessions == 0 (got {faults})")
    if "b" in proofs:
        drift = max(h["player_drift_um"] for h in hb)
        add(drift <= HAND_BODY_DRIFT_UM, f"player_drift_um max {drift} <= {HAND_BODY_DRIFT_UM}")
    if "d" in proofs:
        tm = [h["timing"] for h in hb]
        edges = tm[0].get("hist_edges_us")
        ok_shape = all(isinstance(t.get("hist"), list) and t.get("hist_edges_us") == edges and isinstance(edges, list)
                       and len(t["hist"]) == len(edges) + 1 for t in tm)
        add(ok_shape, "timing.hist present with the same edges in every session")
        if ok_shape:
            summed = [sum(t["hist"][k] for t in tm) for k in range(len(edges) + 1)]
            p999 = wall_percentile_upper_us(summed, edges, 0.999)
            add(p999 is not None and p999 <= HAND_BODY_COST_US,
                f"wall p99.9 (conservative bin edge, {sum(summed)} ticks over {len(tm)} sessions) {p999} <= {HAND_BODY_COST_US} us")
        cpu_ok = all(t.get("cpu_available") is True for t in tm)
        cpu_max = max((t.get("cpu_max_us") or 0) for t in tm)
        add(cpu_ok and cpu_max <= HAND_BODY_COST_US,
            f"CPU time per tick max {cpu_max if cpu_ok else 'UNMEASURED'} <= {HAND_BODY_COST_US} us (cpu_available {[t.get('cpu_available') for t in tm]})")
        out.append(("INFO", f"wall max us {[t.get('wall_max_us') for t in tm]}, over_wall {[t.get('over_wall') for t in tm]}, ticks {[t.get('ticks') for t in tm]}, "
                    f"slowest tick {[t.get('slow') for t in tm]}, hist {[t.get('hist') for t in tm]}"))
    if all("hold_coupling" in s for s in statuses):   # B16 builds: the coupling is idle-correct and no layer is left swapped
        out += check_hold_coupling(statuses)
    elif any("hold_coupling" in s for s in statuses):
        out.append(("FAIL", "hold_coupling present in some sessions only"))
    side = lambda h, k: [(sd.get("drive") or {}).get(k) for sd in h["sides"]]
    cast = [((s.get("hand_clamp") or {}).get("cast_us"), (s.get("hand_clamp") or {}).get("cast_us_max")) for s in statuses]
    out.append(("INFO", f"per session: gap_m {[h.get('gap_m') for h in hb]}, contact_frames (a diagnostic, not contact evidence) {[h.get('contact_frames') for h in hb]}, "
                f"snaps {[h.get('snaps') for h in hb]}, cost_us {[h.get('cost_us') for h in hb]}, cost_max_us {[h.get('cost_max_us') for h in hb]}, "
                f"tick_cost_max_us {[h.get('tick_cost_max_us') for h in hb]}, steady_tick_cost_max_us {[h.get('steady_tick_cost_max_us') for h in hb]}, "
                f"hand_clamp cast_us/cast_us_max {cast}, player_drift_um {[h.get('player_drift_um') for h in hb]}, pressed_ticks {[h.get('pressed_ticks') for h in hb]}, "
                f"root_moved_ticks {[h.get('root_moved_ticks') for h in hb]}, abandons {[h.get('abandons') for h in hb]}, "
                f"snap_blocked {[side(h, 'snap_blocked') for h in hb]}, yields {[side(h, 'yields') for h in hb]}"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run_dirs", nargs="+")
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--proofs", default="abd", help="hand_body_soak only: the proofs to judge (a, b, d); e.g. --proofs a over sessions without the timing block")
    ap.add_argument("--reject-touch", action="store_true", help="finger_touch only: the run used the reject switch, so touch_bound must be false")
    args = ap.parse_args(argv)
    statuses = []
    for d in args.run_dirs:
        f = Path(d) / "artifacts" / "xr-live-status.json"
        try:
            statuses.append(json.loads(f.read_text(encoding="utf-8")))
        except (OSError, ValueError) as e:
            print(f"FAIL  cannot read {f}: {e}")
            return 1
    if args.scenario in FINGERS and not args.reject_touch:  # the per-segment samples the DLL logged beside the status
        for d, status in zip(args.run_dirs, statuses):
            f = Path(d) / "artifacts" / "finger-touch-segments.jsonl"
            try:
                status["finger_segments"] = [json.loads(line) for line in f.read_text(encoding="utf-8").splitlines() if line.strip()]
                script = json.loads((Path(d) / "artifacts" / "simulated-gameplay-script.json").read_text(encoding="utf-8-sig"))["segments"]
                status["finger_poses"] = [{"index": i, "end_ms": s["end_ms"]} for i, s in enumerate(script) if "touch" in s]
            except (OSError, ValueError, KeyError) as e:
                print(f"FAIL  cannot read the finger samples or script in {d}: {e}")
                return 1
    rows = check(statuses, args.scenario, tuple(args.proofs), args.reject_touch)
    for verdict, text in rows:
        print(f"{verdict:4}  {text}")
    return 1 if any(v == "FAIL" for v, _ in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
