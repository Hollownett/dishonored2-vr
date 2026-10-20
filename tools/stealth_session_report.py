#!/usr/bin/env python3
"""Summarise why sword stealth kills from behind were missed in one headset session.

Usage: stealth_session_report.py <xr-live-status.json> [native-combat.csv]
"""
import csv
import json
import statistics
import sys

# Tie order for the dominant reject bucket: stealth_reject index, name, cause letter.
REJECT_ORDER = (
    ("not_behind", 0, "Task 10 cause B"),
    ("slow", 1, "Task 10 cause S"),
    ("not_unaware", 2, "Task 10 cause U"),
    ("not_damageable", 3, "Task 10 cause D"),
    ("other", 4, "Task 10 (hold slot taken by another victim)"),
)
CSV_COLUMNS = ("tick", "what", "entity", "value", "extra")


def _counts(section, key, size):
    value = section.get(key)
    if not isinstance(value, list) or len(value) < size:
        return None
    try:
        return [int(v) for v in value[:size]]
    except (TypeError, ValueError):
        return None


def _result(contacts, buckets, stage, cause, fix, notes):
    return {"contacts": contacts, "buckets": buckets, "stage": stage,
            "likely_cause": cause, "fix_task": fix, "notes": notes}


def verdict(status, combat_rows):
    empty = {"stealth_held": 0, "not_behind": 0, "slow": 0, "not_unaware": 0,
             "not_damageable": 0, "other": 0, "unaccounted": 0}
    sc = status.get("sword_contact")
    sc = sc if isinstance(sc, dict) else {}
    stealth = _counts(sc, "stealth", 5)
    reject = _counts(sc, "stealth_reject", 5)
    if stealth is None or reject is None or "enqueued" not in sc:
        return _result(0, empty, "none", "unknown_old_build", "", ["status lacks sword_contact.stealth/stealth_reject (build older than v601)"])
    td = status.get("takedown")
    td = td if isinstance(td, dict) else {}
    swings = int(td.get("swings", 0) or 0)
    ga_frames = int(td.get("ga_offered_frames", 0) or 0)
    contacts = int(sc["enqueued"])
    held, native, killed, noticed, dropped = stealth
    buckets = dict(empty)
    buckets["stealth_held"] = held
    for name, index, _ in REJECT_ORDER:
        buckets[name] = reject[index]
    accounted = sum(buckets.values())
    enqueued = contacts
    contacts = max(enqueued, accounted)
    buckets["unaccounted"] = contacts - accounted
    notes = []
    if accounted != enqueued:
        notes.append("buckets sum to %d, enqueued=%d (pre-enqueue rejects or consumer rejects)" % (accounted, enqueued))
    hh = sc.get("hit_height")
    if isinstance(hh, list) and len(hh) >= 3 and hh[2] > 0 and hh[0] == 0 and hh[1] == 0:
        notes.append("head-height contacts only")
    speeds = [float(r["value"]) for r in combat_rows
              if r.get("what") == "contact_hit" and str(r.get("value", "")).replace(".", "", 1).replace("-", "", 1).isdigit()]
    if speeds:
        notes.append("contact_hit speed median %.2f m/s over %d rows" % (statistics.median(speeds), len(speeds)))
    stage = "killed" if killed else "dropped" if (native or noticed or dropped) else "held" if held else "none"
    if contacts == 0:
        if swings > 0:
            return _result(0, buckets, stage, "no_contact_near_pass", "Task 8", notes)
        return _result(0, buckets, stage, "no_swings", "", notes)
    best = None
    for name, index, fix in REJECT_ORDER:
        if reject[index] > 0 and (best is None or reject[index] > best[0]):
            best = (reject[index], name, fix)
    if best and best[0] > held:
        return _result(contacts, buckets, stage, best[1], best[2], notes)
    if killed:
        return _result(contacts, buckets, stage, "none", "", notes)
    if native:
        notes.append("native Assassination took the kill: not a miss")
        return _result(contacts, buckets, stage, "native_took_over", "", notes)
    if noticed:
        if ga_frames <= 2:
            return _result(contacts, buckets, stage, "hold_expired_no_native_offer", "Task 10 cause W", notes)
        return _result(contacts, buckets, stage, "engine_rule_rejects", "Task 10 cause K", notes)
    if best:
        notes.append("held strikes outnumber the largest reject bucket (%s=%d) and none resolved" % (best[1], best[0]))
        return _result(contacts, buckets, stage, "unresolved", "collect again", notes)
    return _result(contacts, buckets, stage, "unresolved", "", notes)


def _read_csv(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        return [dict(zip(CSV_COLUMNS, row)) for row in csv.reader(handle) if row]


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    with open(argv[0], encoding="utf-8") as handle:
        status = json.load(handle)
    rows = _read_csv(argv[1]) if len(argv) > 1 else []
    result = verdict(status, rows)
    swing_rows = sum(1 for r in rows if r.get("what") in ("slash", "thrust", "overhead"))
    print(json.dumps(result, indent=2))
    print("contacts=%d stage=%s likely_cause=%s fix=%s swing_rows=%d" % (
        result["contacts"], result["stage"], result["likely_cause"], result["fix_task"] or "-", swing_rows))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
