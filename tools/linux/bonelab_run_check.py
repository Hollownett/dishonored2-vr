#!/usr/bin/env python3
"""Check a sim run of a Part 1 corpse/prop scenario against its assertions.

usage: bonelab_run_check.py RUN_DIR [RUN_DIR ...] --scenario NAME
RUN_DIR is a ~/d2vr-test-builds/sim-runs/<stamp> folder (reads RUN_DIR/artifacts/xr-live-status.json). Several run dirs are
one scenario's sessions (S5 runs four of 50 cycles): counters are summed, binds >= 40 is required per session.
Prints PASS/FAIL/INFO per assertion; exit 1 on any FAIL. Side-pair fields are [left, right].
"""
import argparse
import json
import sys
from pathlib import Path

PROPS = ("oil_tank_drop", "oil_tank_throw")
TANKS = ("tank_wall", "tank_drop", "tank_drop_cycles")  # Part 5A held-clamp scenarios S12-S14
CARRY_FAULTS = ("failed_reads", "rejected_publishes", "bad_quat_reads")


def _pair(v):
    return list(v) if isinstance(v, (list, tuple)) and len(v) == 2 else [0, 0]


def _sum_pairs(statuses, getter):
    out = [0, 0]
    for s in statuses:
        p = _pair(getter(s))
        out = [out[0] + p[0], out[1] + p[1]]
    return out


def check(statuses, scenario):
    """statuses: one parsed xr-live-status.json per session. -> list of (verdict, text); verdict PASS/FAIL/INFO."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    def info(text):
        out.append(("INFO", text))

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


def check_tanks(statuses, scenario, corpse):
    """S12 tank_wall, S13 tank_drop, S14 tank_drop_cycles (summed over sessions) against the held_clamp status block."""
    out = []

    def add(ok, text):
        out.append(("PASS" if ok else "FAIL", text))

    clamps = [s.get("held_clamp") or {} for s in statuses]
    if any(not c for c in clamps):
        return [("FAIL", "status has no held_clamp block")]
    total = lambda key: sum(c.get(key, 0) or 0 for c in clamps)
    sweeps, hits, clamped = total("sweeps"), total("hits"), total("clamped_frames")
    unresolved = total("release_overlap_unresolved")
    out.append(("INFO", f"sweeps {sweeps}, hits {hits}, clamped_frames {clamped}, releases {total('releases')}, "
                f"cast_us {[c.get('cast_us') for c in clamps]}, cast_us_max {max(c.get('cast_us_max', 0) or 0 for c in clamps)}, "
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
        add(all(c.get("layer_at_release") is not None for c in clamps), f"layer_at_release recorded ({[c.get('layer_at_release') for c in clamps]})")
        watch = statuses[-1].get("release_watch") or {}
        count = watch.get("count", 0) or 0
        add(count >= 1, f"release_watch.count >= 1 (got {count})")
        if count >= 1:
            z0, last = watch.get("z_at_release"), watch.get("z_last")
            ok = isinstance(z0, (int, float)) and isinstance(last, (int, float)) and last >= z0 - 0.5
            add(ok, f"tank rest z_last {last} not more than 0.5 m below z_at_release {z0} (read 5 s after the last release)")
    if scenario == "tank_drop_cycles":
        releases = total("releases")
        add(releases >= 100, f"releases >= 100 over {len(statuses)} sessions (got {releases})")
        add(not any(c.get("fault_latch") for c in corpse), "corpse.fault_latch == false")
        faults = sum((s.get("carry") or {}).get(k, 0) or 0 for s in statuses for k in CARRY_FAULTS)
        add(faults == 0, f"carry fault counters {CARRY_FAULTS} == 0 (got {faults})")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run_dirs", nargs="+")
    ap.add_argument("--scenario", required=True)
    args = ap.parse_args(argv)
    statuses = []
    for d in args.run_dirs:
        f = Path(d) / "artifacts" / "xr-live-status.json"
        try:
            statuses.append(json.loads(f.read_text(encoding="utf-8")))
        except (OSError, ValueError) as e:
            print(f"FAIL  cannot read {f}: {e}")
            return 1
    rows = check(statuses, args.scenario)
    for verdict, text in rows:
        print(f"{verdict:4}  {text}")
    return 1 if any(v == "FAIL" for v, _ in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
