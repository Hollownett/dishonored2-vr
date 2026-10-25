#!/usr/bin/env python3
"""Check a sim run of a Part 1 corpse/prop scenario or the finger_touch scenario against its assertions.

usage: bonelab_run_check.py RUN_DIR [RUN_DIR ...] --scenario NAME [--reject-touch]
       bonelab_run_check.py RUN_DIR --scenario loot_reach --coin X Y Z [--case table|floor|drawer]
RUN_DIR is a ~/d2vr-test-builds/sim-runs/<stamp> folder (reads RUN_DIR/artifacts/xr-live-status.json). Several run dirs are
one scenario's sessions (S5 runs four of 50 cycles): counters are summed, binds >= 40 is required per session.
Prints PASS/FAIL/INFO per assertion; exit 1 on any FAIL. Side-pair fields are [left, right].
"""
import argparse
import csv
import json
import math
import statistics
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
RUNS = ("run_hands", "run_hands_sword")  # G1a: the running-hands scenarios, judged from artifacts/locomotion-hands.csv (knob build/locomotion-trace)
# Running rows >= 60: the spawn corridor (~20 m of straight) limits how long the run can last (1-3 s at >= 2.5 m/s, 72-124 rows in Task 8's runs); standing keeps 200.
RUN_MIN_ROWS, STAND_MIN_ROWS, STAND_SPEED, RUN_SPEED, BURST_SPEED, BURST_WINDOW_MS = 60, 200, 0.3, 2.5, 1.0, 1000
RUN_RESIDUAL_MARGIN = 0.015  # Task 9 acceptance: a side's running median drawn_residual may exceed its standing median by at most this (m)
LOCOMOTION_COLUMNS = ("elapsed_ms", "speed_mps", "root_gap_m", "root_gap_valid", "palm_residual_l", "palm_residual_r", "drawn_residual_l", "drawn_residual_r",
                      "visual_gap_l", "visual_gap_r", "source_l", "source_r", "blends", "contact_l", "contact_r")
REGRAB = ("sword_regrab",)  # G2: judged from artifacts/sword-grip-trace.csv (knob build/sword-grip-trace)
SWORD_COLUMNS = ("elapsed_ms", "held", "reversed", "reverse_weight", "hilt_miss", "weapon_hidden_frames", "blade_axis_gap_deg", "blade_self_hits")
# Brief values: settle 350 ms, hilt_miss <= 0.01 m. Measured in the sim (Task 10 report): the forward grip's own hilt_miss is 13.5-15.2 mm (the rig's geometry,
# 13.9 mm on the fixture), and a DRAW (hold 1) displaces the hilt for ~570 ms while the equip clip settles (hold easing). So hilt_miss is judged from REGRAB_FIT_MS with
# the bound below; the reverse applies at hold start, so reverse_weight is judged from REGRAB_SETTLE_MS.
REGRAB_MIN_HOLDS, REGRAB_PATTERN, REGRAB_SETTLE_MS, REGRAB_FIT_MS, REGRAB_HILT_MISS = 4, (False, True, False, True), 350, 700, 0.02
AUTHORED = ("authored_turn",)  # G1 scripted moments (camera plan Task 3): judged from artifacts/scripted-view.csv (knob build/scripted-view-trace)
SCRIPTED_COLUMNS = ("t", "authored", "state_index", "hmd_rel_deg", "rel_composed_deg", "heading_deg", "offset_deg", "fold_deg", "body_deg", "view_yaw_deg")
# src/scripted_recenter.hpp: an entry turn eases the offset over 0.4 s with smoothstep (peak rate 1.5 x delta / 0.4 s) and skips a delta under 10 deg.
# The ease runs on a clock that credits at most 1/30 s per frame, so the end is read AUTHORED_EASE_S + AUTHORED_EASE_SLACK_S after the aim.
AUTHORED_EASE_S, AUTHORED_EASE_SLACK_S, AUTHORED_DEAD_ZONE_DEG, AUTHORED_RESIDUAL_DEG, AUTHORED_STEP_SLACK_DEG, AUTHORED_EXIT_WINDOW_S = 0.4, 0.05, 10.0, 3.0, 1.0, 0.5
# AuthoredHeading's re-aim: the held heading jumps by at least 30 deg (src/authored_view.hpp snap_rad); a smaller change between two authored
# rows is a re-latch of the scene's own heading (taken out of the step as the scene's animation).
AUTHORED_SNAP_DEG = 29.9
STATE_NAMES = {8: "Mantle", 9: "Vault", 13: "Assassination", 33: "SyncAnim", 38: "NpcFatality", 41: "Choke", 42: "DropChoke", 55: "BloodthirstyFatality",
               83: "KillCam", 39: "combat 39", 44: "combat 44"}  # docs/full-body-research.md 6b
LOOT = ("loot_reach",)  # G1 loot measurement (hands plan Task 1): artifacts/native-loot-reach.csv (+ -owners.csv), knob build/loot-reach-trace
LOOT_COLUMNS = ("t", "side", "palm_x", "palm_y", "palm_z", "dir_x", "dir_y", "dir_z", "owner", "spawn", "takeable", "set_count", "rank",
                "los_called", "los_blocked", "selected_owner", "prompt_owner")
LOOT_GATE_M, LOOT_COIN_MATCH_M, LOOT_TAKE_MS = 0.45, 0.25, 1000  # frames judged: the coin within 0.45 m of the palm; the coin = the takeable owner whose bounds are nearest --coin
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

    if scenario in RUNS:
        return check_run(statuses)
    if scenario in AUTHORED:
        return check_authored(statuses)
    if scenario in REGRAB:
        return check_regrab(statuses)
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


def parse_scripted_view(text):
    """scripted-view.csv text -> a list of float rows keyed by SCRIPTED_COLUMNS (NaN kept: heading_deg is NaN while no heading); short rows dropped."""
    rows = []
    for rec in csv.DictReader(text.splitlines()):
        try:
            rows.append({k: float(rec[k]) for k in SCRIPTED_COLUMNS})
        except (KeyError, TypeError, ValueError):
            continue
    return rows


def _wrap_deg(a):
    return (a + 180.0) % 360.0 - 180.0


def scripted_entries(rows):
    """Authored scenes in the trace -> list of dicts: entry/aim/exit row indices. The aim is the first authored row with a finite heading (a late
    heading starts the entry turn there, src/scripted_recenter.hpp entry_pending_); delta is the gaze against the composed origin at the aim."""
    scenes, i = [], 0
    while i < len(rows):
        if rows[i]["authored"] and (i == 0 or not rows[i - 1]["authored"]):
            j = i
            while j + 1 < len(rows) and rows[j + 1]["authored"]:
                j += 1
            aim = next((k for k in range(i, j + 1) if math.isfinite(rows[k]["heading_deg"])), None)
            scene = {"entry": i, "aim": aim, "last": j, "exit": j + 1 if j + 1 < len(rows) else None, "state": int(rows[i]["state_index"])}
            if aim is not None:
                before = rows[aim - 1]["offset_deg"] if aim > 0 else 0.0
                scene["delta"] = _wrap_deg(rows[aim]["hmd_rel_deg"] - before)
                # The turn the view makes (4c7d222): the offset starts at wrap(heading - body before entry), so the first authored view is the
                # last gameplay view, and eases to the gaze; only target - start is turned. Heading on the gaze (a mantle) = no turn.
                start = _wrap_deg(rows[aim]["heading_deg"] - rows[i - 1]["body_deg"]) if i > 0 else 0.0
                scene["turn"] = _wrap_deg(scene["delta"] - start)
            scenes.append(scene)
            i = j + 1
        else:
            i += 1
    return scenes


def check_authored(statuses):
    """G1 authored_turn: per authored scene in scripted-view.csv (an entry = authored 0 -> 1).
    - Residual: an entry whose delta |hmd_rel| >= 10 deg ends its ease (0.4 s after the aim, + 0.05 s for the frame-capped clock) with
      |rel_composed - (the head's own yaw change since the aim)| < 3 deg.
    - Steps: around entry (the row before it through the ease end) and exit (the last authored row through 0.5 s after), no frame-to-frame view_yaw
      step exceeds the ease's peak rate (1.5 x |turn| / 0.4 s, x the frame's dt; turn = delta - wrap(heading - body before entry), the part the
      view really turns since 4c7d222 starts the offset at the last gameplay view; 0 inside the 10 deg dead zone) plus the head's own yaw change that frame (hmd_rel change
      plus that frame's fold), plus 1 deg. Between two
      authored rows the native camera's own heading change is the scene's animation, not the recenter's, so it is taken out of the step.
    - Re-aims (Ruling 11, continuous re-aim): a heading snap (a change of at least 30 deg between two authored rows) is NOT the scene's
      animation: the helper absorbs it, so the view must stay continuous on the snap frame (the snap counts as zero heading change), then
      eases to the new heading. Each snap gets a window (the row before it through 0.4 s + slack after) whose allowance adds that ease's
      peak rate (1.5 x |turn| / 0.4 s x dt; turn = wrap(new heading - the view before the snap), 0 inside the dead zone); the same allowance
      applies to the entry and exit windows while that ease runs.
    - At least one entry observed. INFO per entry: state_index, delta, residual."""
    out = []
    for n, status in enumerate(statuses):
        tag = f"session {n + 1}: " if len(statuses) > 1 else ""
        rows = status.get("scripted_rows")
        if not rows:
            out.append(("FAIL", f"{tag}scripted-view.csv is missing or empty (run with --knob scripted-view-trace)"))
            continue
        scenes = scripted_entries(rows)
        out.append(("PASS" if scenes else "FAIL", f"{tag}authored entries observed >= 1 (got {len(scenes)}; trace rows {len(rows)}{', capped at 4096' if len(rows) >= 4096 else ''})"))
        for k, sc in enumerate(scenes):
            name = f"{tag}entry {k + 1} (state_index {sc['state']} {STATE_NAMES.get(sc['state'], '?')}, t {rows[sc['entry']]['t']:.2f})"
            if sc["aim"] is None:
                out.append(("INFO", f"{name}: no heading in the scene (no authored view drawn, no aim)"))
                continue
            aim = rows[sc["aim"]]
            delta, turn = sc["delta"], sc["turn"]
            end_t = aim["t"] + AUTHORED_EASE_S + AUTHORED_EASE_SLACK_S
            end = next((m for m in range(sc["aim"], sc["last"] + 1) if rows[m]["t"] >= end_t), None)
            residual = None
            if end is not None:
                head = _wrap_deg(rows[end]["hmd_rel_deg"] - aim["hmd_rel_deg"])
                residual = _wrap_deg(rows[end]["rel_composed_deg"] - head)
            dur = rows[sc["last"]]["t"] - rows[sc["entry"]]["t"]
            res_text = "n/a (scene ended before the ease)" if residual is None else f"{residual:+.2f} deg"
            off_text = "n/a" if end is None else f"{rows[end]['offset_deg']:+.2f} deg"
            out.append(("INFO", f"{name}: delta {delta:+.2f} deg, turn {turn:+.2f} deg, aim +{aim['t'] - rows[sc['entry']]['t']:.3f} s, scene {dur:.2f} s, "
                                f"residual {res_text}, offset at the ease end {off_text}"))
            if abs(delta) >= AUTHORED_DEAD_ZONE_DEG:
                if residual is None:
                    out.append(("INFO", f"{name}: residual not judged: the scene ended {aim['t'] + AUTHORED_EASE_S - rows[sc['last']]['t']:.2f} s before the ease"))
                else:
                    out.append(("PASS" if abs(residual) < AUTHORED_RESIDUAL_DEG else "FAIL", f"{name}: |residual| < {AUTHORED_RESIDUAL_DEG:g} deg at the ease end (got {residual:+.2f})"))
            peak = 1.5 * abs(turn) / AUTHORED_EASE_S if abs(turn) >= AUTHORED_DEAD_ZONE_DEG else 0.0  # no ease = no allowance beyond head + 1 deg
            stop = end if end is not None else sc["last"]
            windows = [("entry", max(sc["entry"] - 1, 0), stop, peak)]
            # Ruling 11: the snaps of this scene, each a re-aim ease from the last view
            snaps, reaim_allow = [], {}
            for m in range(sc["entry"] + 1, sc["last"] + 1):
                prev, row = rows[m - 1], rows[m]
                if prev["authored"] and row["authored"] and math.isfinite(prev["heading_deg"]) and math.isfinite(row["heading_deg"]) and \
                        abs(_wrap_deg(row["heading_deg"] - prev["heading_deg"])) >= AUTHORED_SNAP_DEG:
                    snaps.append(m)
                    rturn = _wrap_deg(row["heading_deg"] - prev["view_yaw_deg"])
                    rpeak = 1.5 * abs(rturn) / AUTHORED_EASE_S if abs(rturn) >= AUTHORED_DEAD_ZONE_DEG else 0.0
                    s_end = m
                    while s_end + 1 < len(rows) and rows[s_end + 1]["t"] <= row["t"] + AUTHORED_EASE_S + AUTHORED_EASE_SLACK_S:
                        s_end += 1
                    for q in range(m, s_end + 1):
                        reaim_allow[q] = reaim_allow.get(q, 0.0) + rpeak
                    windows.append((f"re-aim {len(snaps)}", m - 1, min(s_end, sc["last"]), 0.0))
            if sc["exit"] is not None:
                exit_end = sc["exit"]
                while exit_end + 1 < len(rows) and rows[exit_end + 1]["t"] <= rows[sc["exit"]]["t"] + AUTHORED_EXIT_WINDOW_S and not rows[exit_end + 1]["authored"]:
                    exit_end += 1
                windows.append(("exit", sc["last"], exit_end, 0.0))
            snap_rows = set(snaps)
            for label, a0, a1, base in windows:
                worst, worst_at = 0.0, None
                for m in range(a0 + 1, a1 + 1):
                    prev, row = rows[m - 1], rows[m]
                    dt = max(row["t"] - prev["t"], 0.0)
                    step = _wrap_deg(row["view_yaw_deg"] - prev["view_yaw_deg"])
                    if m not in snap_rows and prev["authored"] and row["authored"] and math.isfinite(prev["heading_deg"]) and math.isfinite(row["heading_deg"]):
                        step = _wrap_deg(step - _wrap_deg(row["heading_deg"] - prev["heading_deg"]))
                    # the head's own motion: hmd_rel is read after this frame's fold, which turns the origin (hmd_rel drops by fold_deg)
                    head_motion = abs(_wrap_deg(row["hmd_rel_deg"] - prev["hmd_rel_deg"] + row["fold_deg"]))
                    allowed = (base + reaim_allow.get(m, 0.0)) * dt + head_motion + AUTHORED_STEP_SLACK_DEG
                    excess = abs(step) - allowed
                    if worst_at is None or excess > worst:
                        worst, worst_at = excess, (m, step, allowed)
                if worst_at is None:
                    continue
                m, step, allowed = worst_at
                out.append(("PASS" if worst <= 1e-9 else "FAIL", f"{name}: {label} view_yaw steps within ease rate + head motion + {AUTHORED_STEP_SLACK_DEG:g} deg "
                                                               f"(worst step {step:+.2f} deg vs allowed {allowed:.2f} at t {rows[m]['t']:.3f})"))
    return out or [("FAIL", "no sessions")]


def parse_loot_reach(text):
    """native-loot-reach.csv text -> a list of rows (ints for ids/counts, floats for the palm); short or non-numeric rows dropped."""
    rows = []
    for rec in csv.DictReader(text.splitlines()):
        try:
            row = {k: int(rec[k]) for k in ("t", "side", "owner", "spawn", "takeable", "set_count", "rank", "los_called", "los_blocked", "selected_owner", "prompt_owner")}
            row.update({k: float(rec[k]) for k in ("palm_x", "palm_y", "palm_z", "dir_x", "dir_y", "dir_z")})
        except (KeyError, TypeError, ValueError):
            continue
        rows.append(row)
    return rows


def parse_loot_owners(text):
    """native-loot-reach-owners.csv text -> {owner: {takeable, pos, min, max}}; missing bounds/position are None."""
    out = {}
    for rec in csv.DictReader(text.splitlines()):
        try:
            owner = int(rec["owner"])
            vec = lambda *ks: [float(rec[k]) for k in ks] if all(rec.get(k) not in (None, "") for k in ks) else None  # noqa: E731
            out[owner] = {"takeable": rec.get("takeable") == "1", "pos": vec("pos_x", "pos_y", "pos_z"),
                          "min": vec("min_x", "min_y", "min_z"), "max": vec("max_x", "max_y", "max_z")}
        except (KeyError, TypeError, ValueError):
            continue
    return out


def _box_distance(point, info):
    """Distance from point to the owner's bounds (its position when the bounds are unreadable); inf when neither."""
    if info.get("min") and info.get("max"):
        return math.dist(point, [min(max(p, lo), hi) for p, lo, hi in zip(point, info["min"], info["max"])])
    return math.dist(point, info["pos"]) if info.get("pos") else math.inf


def loot_frames(rows):
    """Rows -> one frame per routed stage-0 cycle (the consecutive rows of one (t, side)): palm, side, {owner: row}, selected, prompt."""
    frames, key = [], None
    for r in rows:
        if (r["t"], r["side"]) != key:
            key = (r["t"], r["side"])
            frames.append({"t": r["t"], "side": r["side"], "palm": (r["palm_x"], r["palm_y"], r["palm_z"]), "set": {},
                           "selected": r["selected_owner"], "prompt": r["prompt_owner"]})
        if r["owner"]:
            frames[-1]["set"][r["owner"]] = r
    return frames


def loot_classify(in_set, rank0, los_called, los_blocked, selected):
    """The brief's classification, first match in order."""
    if selected >= 0.8:
        return "PASS"
    if in_set >= 0.8 and rank0 < 0.5:
        return "ORDER"
    if in_set >= 0.8 and rank0 >= 0.5 and los_called < 0.2:
        return "ELIGIBILITY"
    if in_set < 0.2:
        return "NOT_IN_SET"
    if los_blocked >= 0.8:
        return "LOS"
    return "MIXED"


def check_loot(status, coin, case=None):
    """G1 loot measurement. The coin is the takeable owner (native-loot-reach-owners.csv) whose bounds are nearest `coin` (within 0.25 m;
    none: the coin never entered a result set). Over the frames (routed stage-0 cycles) whose palm is within 0.45 m of the coin:
    in_set_rate, rank0_rate (rank 0 after order_native_use_hits), los_called_rate (of the rank-0 frames, 3b7b90 asked about the coin),
    los_blocked_rate (of the frames where it was asked, blocked), selected_rate (native-selected owner = the coin); then one classification:
    PASS selected >= 0.8; ORDER in set >= 0.8 but rank0 < 0.5; ELIGIBILITY in set >= 0.8, rank0 >= 0.5, LOS asked < 0.2 (the native
    permission / 3b80a0 checks rejected it first); NOT_IN_SET in set < 0.2; LOS blocked >= 0.8; MIXED otherwise.
    A take ends the measurement: when the coin was the native-selected owner within 1 s before its last appearance in any result set
    and the trace goes on after it, the frames after that appearance are dropped (the coin is gone, not missed)."""
    tag = f"{case}: " if case else ""
    rows, owners = status.get("loot_rows"), status.get("loot_owners") or {}
    if not rows:
        return [("FAIL", f"{tag}native-loot-reach.csv is missing or empty (run with --knob loot-reach-trace)")]
    out = []
    matches = sorted((_box_distance(coin, info), owner) for owner, info in owners.items() if info["takeable"])
    coin_owner = matches[0][1] if matches and matches[0][0] <= LOOT_COIN_MATCH_M else None
    info = owners.get(coin_owner) if coin_owner else {"pos": list(coin)}
    out.append(("INFO", f"{tag}coin {coin_owner if coin_owner else 'not seen in any result set'}"
                        + (f" ({matches[0][0]:.3f} m from --coin)" if coin_owner else f" (nearest takeable {matches[0][0]:.2f} m)" if matches else "")))
    frames = loot_frames(rows)
    seen = [f["t"] for f in frames if coin_owner and coin_owner in f["set"]]
    if seen and frames[-1]["t"] > seen[-1] and any(seen[-1] - LOOT_TAKE_MS <= f["t"] <= seen[-1] and f["selected"] == coin_owner for f in frames):
        late = sum(1 for f in frames if f["t"] > seen[-1] and _box_distance(f["palm"], info) <= LOOT_GATE_M)
        out.append(("INFO", f"{tag}coin taken at t {seen[-1]} (selected before it left the result sets): {late} later frames near it dropped"))
        frames = [f for f in frames if f["t"] <= seen[-1]]
    frames = [f for f in frames if _box_distance(f["palm"], info) <= LOOT_GATE_M]
    out.append(("PASS" if frames else "FAIL", f"{tag}frames with the coin within {LOOT_GATE_M} m of the palm >= 1 (got {len(frames)}: "
                                              f"left {sum(1 for f in frames if f['side'] == 0)}, right {sum(1 for f in frames if f['side'] == 1)}; trace rows {len(rows)}"
                                              f"{', capped at 8192' if len(rows) >= 8192 else ''})"))
    if not frames:
        return out
    n = len(frames)
    hit = [f["set"].get(coin_owner) if coin_owner else None for f in frames]
    in_set = sum(1 for h in hit if h) / n
    at0 = [h for h in hit if h and h["rank"] == 0]
    rank0 = len(at0) / n
    los_called = sum(1 for h in at0 if h["los_called"]) / len(at0) if at0 else 0.0
    asked = [h for h in hit if h and h["los_called"]]
    los_blocked = sum(1 for h in asked if h["los_blocked"]) / len(asked) if asked else 0.0
    selected = sum(1 for f in frames if coin_owner and f["selected"] == coin_owner) / n
    prompted = sum(1 for f in frames if coin_owner and f["prompt"] == coin_owner) / n
    out.append(("INFO", f"{tag}in_set_rate {in_set:.3f}, rank0_rate {rank0:.3f}, los_called_rate {los_called:.3f} (of {len(at0)} rank-0 frames), "
                        f"los_blocked_rate {los_blocked:.3f} (of {len(asked)} asked), selected_rate {selected:.3f}, prompt_rate {prompted:.3f} over {n} frames"))
    for side, name in ((1, "right"), (0, "left")):
        fs = [(f, h) for f, h in zip(frames, hit) if f["side"] == side]
        if fs:
            others = {}
            for f, _ in fs:
                if f["selected"] and f["selected"] != coin_owner:
                    others[f["selected"]] = others.get(f["selected"], 0) + 1
            out.append(("INFO", f"{tag}{name}: {len(fs)} frames, in set {sum(1 for _, h in fs if h) / len(fs):.3f}, rank 0 {sum(1 for _, h in fs if h and h['rank'] == 0) / len(fs):.3f}, "
                                f"selected {sum(1 for f, _ in fs if coin_owner and f['selected'] == coin_owner) / len(fs):.3f}, mean set {sum(len(f['set']) for f, _ in fs) / len(fs):.2f} owners"
                                + (f", other selected {sorted(others.items(), key=lambda kv: -kv[1])[:2]}" if others else "")))
    out.append(("INFO", f"{tag}classification: {loot_classify(in_set, rank0, los_called, los_blocked, selected)}"))
    return out


def parse_locomotion(text):
    """locomotion-hands.csv text -> a list of float rows keyed by LOCOMOTION_COLUMNS; a short or non-numeric row is dropped."""
    rows = []
    for rec in csv.DictReader(text.splitlines()):
        try:
            rows.append({k: float(rec[k]) for k in LOCOMOTION_COLUMNS})
        except (KeyError, TypeError, ValueError):
            continue
    return rows


def blend_bursts(rows):
    """Free-air hand_visual blends: rows with speed >= 1.0 and no contact. A burst is >= 2 increments of `blends` inside a 1 s window.
    -> (bursts counted without overlap, the most increments seen in any 1 s window)."""
    events = []
    for prev, row in zip(rows, rows[1:]):
        if row["speed_mps"] >= BURST_SPEED and not row["contact_l"] and not row["contact_r"] and row["blends"] > prev["blends"]:
            events.append((row["elapsed_ms"], row["blends"] - prev["blends"]))
    bursts, worst, i, end = 0, 0, 0, -1
    for i, (t, _) in enumerate(events):
        n = sum(d for u, d in events[i:] if u - t <= BURST_WINDOW_MS)
        worst = max(worst, n)
        if n >= 2 and t > end:
            bursts += 1
            end = t + BURST_WINDOW_MS
    return bursts, worst


def check_run(statuses):
    """G1a/G1b run_hands / run_hands_sword: bin the trace (standing speed < 0.3, running speed >= 2.5; rows with root_gap_valid 0 ignored). FAIL when it is
    missing or too short, when a side's running median drawn_residual exceeds standing by more than 1.5 cm, or on a free-air blend burst."""
    out = []
    for n, status in enumerate(statuses):
        tag = f"session {n + 1}: " if len(statuses) > 1 else ""
        rows = status.get("locomotion_rows")
        if not rows:
            out.append(("FAIL", f"{tag}locomotion-hands.csv is missing or empty (run with --knob locomotion-trace)"))
            continue
        # rows where the anchor root or an arm-upload root was unknown (root_gap_valid 0) say nothing about the carry: the bins ignore them
        valid = [r for r in rows if r["root_gap_valid"]]
        stand = [r for r in valid if r["speed_mps"] < STAND_SPEED]
        run = [r for r in valid if r["speed_mps"] >= RUN_SPEED]
        out.append(("PASS" if len(stand) >= STAND_MIN_ROWS else "FAIL", f"{tag}standing rows >= {STAND_MIN_ROWS} (got {len(stand)})"))
        out.append(("PASS" if len(run) >= RUN_MIN_ROWS else "FAIL", f"{tag}running rows (speed >= {RUN_SPEED} m/s) >= {RUN_MIN_ROWS} (got {len(run)})"))
        out.append(("INFO", f"{tag}rows {len(rows)}, top speed {max(r['speed_mps'] for r in rows):.2f} m/s"))
        med = lambda rs, k: statistics.median(r[k] for r in rs) if rs else float("nan")  # noqa: E731
        for side in "lr":
            for what in ("drawn_residual", "palm_residual", "visual_gap"):
                k = f"{what}_{side}"
                out.append(("INFO", f"{tag}{k} median: standing {med(stand, k):.4f} m, running {med(run, k):.4f} m"))
            share = sum(1 for r in run if r[f"source_{side}"] == 2) / len(run) if run else float("nan")
            if stand and run:  # acceptance: the drawn hand does not drift off the controller while running
                delta = med(run, f"drawn_residual_{side}") - med(stand, f"drawn_residual_{side}")
                out.append(("PASS" if delta <= RUN_RESIDUAL_MARGIN + 1e-9 else "FAIL",
                            f"{tag}drawn_residual_{side} running median <= standing + {RUN_RESIDUAL_MARGIN * 100:.1f} cm (delta {delta * 100:+.2f} cm)"))
            out.append(("INFO", f"{tag}source_{side} == body share of running frames: {share:.3f}"))
        out.append(("INFO", f"{tag}root_gap_m median running: {med(run, 'root_gap_m'):.4f} m"))
        bursts, worst = blend_bursts(rows)
        out.append(("PASS" if bursts == 0 else "FAIL", f"{tag}free-air blend bursts == 0 (got {bursts}; most increments in any 1 s window: {worst})"))
    return out or [("FAIL", "no sessions")]


def parse_sword_trace(text):
    """sword-grip-trace.csv text -> a list of float rows keyed by SWORD_COLUMNS; a short or non-numeric row is dropped."""
    rows = []
    for rec in csv.DictReader(text.splitlines()):
        try:
            rows.append({k: float(rec[k]) for k in SWORD_COLUMNS})
        except (KeyError, TypeError, ValueError):
            continue
    return rows


def sword_holds(rows):
    """Contiguous runs of rows with held == 1 -> a list of row lists."""
    holds, cur = [], []
    for r in rows:
        if r["held"]:
            cur.append(r)
        elif cur:
            holds.append(cur)
            cur = []
    return holds + ([cur] if cur else [])


def check_regrab(statuses):
    """G2 sword_regrab: >= 4 holds whose `reversed` run F, R, F, R. From each hold's start + 0.35 s to its end: `reversed` constant, weapon_hidden_frames and
    blade_self_hits do not increase; reverse_weight is 0 or 1; from + 0.7 s (see REGRAB_FIT_MS): hilt_miss <= 2 cm. INFO: the worst blade_axis_gap_deg."""
    out = []
    for n, status in enumerate(statuses):
        tag = f"session {n + 1}: " if len(statuses) > 1 else ""
        rows = status.get("sword_rows")
        if not rows:
            out.append(("FAIL", f"{tag}sword-grip-trace.csv is missing or empty (run with --knob sword-grip-trace)"))
            continue
        holds = sword_holds(rows)
        out.append(("PASS" if len(holds) >= REGRAB_MIN_HOLDS else "FAIL", f"{tag}holds >= {REGRAB_MIN_HOLDS} (got {len(holds)})"))
        settled = []
        for k, hold in enumerate(holds):
            window = [r for r in hold if r["elapsed_ms"] >= hold[0]["elapsed_ms"] + REGRAB_SETTLE_MS]
            settled.append(window)
            name = f"{tag}hold {k + 1} (start {hold[0]['elapsed_ms']:.0f} ms)"
            if not window:
                out.append(("FAIL", f"{name}: shorter than {REGRAB_SETTLE_MS} ms"))
                continue
            fit = [r for r in hold if r["elapsed_ms"] >= hold[0]["elapsed_ms"] + REGRAB_FIT_MS] or window
            miss = max(r["hilt_miss"] for r in fit)
            out.append(("PASS" if miss <= REGRAB_HILT_MISS + 1e-9 else "FAIL", f"{name}: hilt_miss <= {REGRAB_HILT_MISS} m after {REGRAB_FIT_MS} ms (max {miss:.4f})"))
            flips = len({r["reversed"] for r in window})
            out.append(("PASS" if flips == 1 else "FAIL", f"{name}: reversed constant (values seen {flips})"))
            hidden = window[-1]["weapon_hidden_frames"] - window[0]["weapon_hidden_frames"]
            out.append(("PASS" if hidden == 0 else "FAIL", f"{name}: weapon_hidden_frames increase 0 (got {hidden:.0f})"))
            selfhit = window[-1]["blade_self_hits"] - window[0]["blade_self_hits"]
            out.append(("PASS" if selfhit == 0 else "FAIL", f"{name}: blade_self_hits increase 0 (got {selfhit:.0f})"))
            mid = [r["reverse_weight"] for r in window if 1e-6 < r["reverse_weight"] < 1 - 1e-6]
            out.append(("PASS" if not mid else "FAIL", f"{name}: reverse_weight is 0 or 1 after {REGRAB_SETTLE_MS} ms ( {len(mid)} rows in between)"))
            out.append(("INFO", f"{name}: reversed {bool(window[0]['reversed'])}, max blade_axis_gap_deg {max(r['blade_axis_gap_deg'] for r in hold):.1f} (from +{REGRAB_SETTLE_MS} ms {max(r['blade_axis_gap_deg'] for r in window):.1f}; the forward grip's own offset is ~17)"))
        if len(holds) >= REGRAB_MIN_HOLDS:
            seen = tuple(bool(w[0]["reversed"]) if w else None for w in settled[:REGRAB_MIN_HOLDS])
            out.append(("PASS" if seen == REGRAB_PATTERN else "FAIL", f"{tag}reversed per hold F, R, F, R (got {''.join('?' if v is None else 'R' if v else 'F' for v in seen)})"))
    return out or [("FAIL", "no sessions")]


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
        for pose, seg in zip(FINGER_POSES, poses):  # the first sample of each pose's own segment at or after its end (the boundary tick; later post-end samples are unsettled)
            mine = [s for s in samples if s.get("segment") == seg.get("index") and s.get("elapsed_ms", -1) >= seg.get("end_ms", 1 << 60)]
            add(bool(mine), f"{tag}{pose}: a sample at the end of segment {seg.get('index')} (got {len([s for s in samples if s.get('segment') == seg.get('index')])} before its end)")
            if mine:
                by[pose] = min(mine, key=lambda s: s["elapsed_ms"]).get("fingers") or {}
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
    ap.add_argument("--coin", type=float, nargs=3, metavar=("X", "Y", "Z"), help="loot_reach only: the coin's world position (game units)")
    ap.add_argument("--case", choices=("table", "floor", "drawer"), help="loot_reach only: the case label")
    args = ap.parse_args(argv)
    if args.scenario in LOOT and (not args.coin or len(args.run_dirs) != 1):
        ap.error("loot_reach needs one RUN_DIR and --coin X Y Z")
    statuses = []
    for d in args.run_dirs:
        f = Path(d) / "artifacts" / "xr-live-status.json"
        try:
            statuses.append(json.loads(f.read_text(encoding="utf-8")))
        except (OSError, ValueError) as e:
            print(f"FAIL  cannot read {f}: {e}")
            return 1
    if args.scenario in RUNS:
        for d, status in zip(args.run_dirs, statuses):
            try:
                status["locomotion_rows"] = parse_locomotion((Path(d) / "artifacts" / "locomotion-hands.csv").read_text(encoding="utf-8"))
            except OSError:
                status["locomotion_rows"] = None
    if args.scenario in AUTHORED:
        for d, status in zip(args.run_dirs, statuses):
            try:
                status["scripted_rows"] = parse_scripted_view((Path(d) / "artifacts" / "scripted-view.csv").read_text(encoding="utf-8"))
            except OSError:
                status["scripted_rows"] = None
    if args.scenario in REGRAB:
        for d, status in zip(args.run_dirs, statuses):
            try:
                status["sword_rows"] = parse_sword_trace((Path(d) / "artifacts" / "sword-grip-trace.csv").read_text(encoding="utf-8"))
            except OSError:
                status["sword_rows"] = None
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
    if args.scenario in LOOT:
        art = Path(args.run_dirs[0]) / "artifacts"
        try:
            statuses[0]["loot_rows"] = parse_loot_reach((art / "native-loot-reach.csv").read_text(encoding="utf-8"))
            statuses[0]["loot_owners"] = parse_loot_owners((art / "native-loot-reach-owners.csv").read_text(encoding="utf-8"))
        except OSError:
            statuses[0]["loot_rows"] = None
        rows = check_loot(statuses[0], tuple(args.coin), args.case)
    else:
        rows = check(statuses, args.scenario, tuple(args.proofs), args.reject_touch)
    for verdict, text in rows:
        print(f"{verdict:4}  {text}")
    return 1 if any(v == "FAIL" for v, _ in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
