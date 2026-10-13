"""Audit a frozen recorder session. Never opens a process or changes the game.

Reports measured palette facts separately from missing engine/render/weapon
evidence. A consistent publication is not a verified native consumer frame.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

MAX_FILE = 128 * 1024 * 1024
MAX_LINE = 128 * 1024
MAX_ROWS = 20000


def reject_constant(value):
    raise ValueError(f"Non-finite JSON constant: {value}")


def load_json(path):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError(f"Oversized metadata: {path}")
    value = json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError(f"Expected object: {path}")
    return value


def read_rows(path, counts):
    if path.stat().st_size > MAX_FILE:
        raise ValueError(f"Evidence exceeds read budget: {path}")
    with path.open("rb") as stream:
        for number in range(1, MAX_ROWS + 2):
            raw = stream.readline(MAX_LINE + 1)
            if not raw:
                return
            if number > MAX_ROWS or len(raw) > MAX_LINE:
                raise ValueError(f"Record budget exceeded at {path}:{number}")
            try:
                row = json.loads(raw, parse_constant=reject_constant)
            except (json.JSONDecodeError, UnicodeDecodeError):
                if not raw.endswith(b"\n") and not stream.read(1):
                    counts["incomplete_tail_records"] += 1
                    return
                raise ValueError(f"Malformed complete record at {path}:{number}")
            if not isinstance(row, dict):
                raise ValueError(f"Expected record object at {path}:{number}")
            yield row


def vector(value, size):
    if not isinstance(value, list) or len(value) != size:
        raise ValueError("Invalid pose dimensions")
    if any(type(x) not in (int, float) or not math.isfinite(x) for x in value):
        raise ValueError("Invalid pose number")
    return value


def quaternion(value):
    value = vector(value, 4)
    norm = math.sqrt(sum(x*x for x in value))
    if abs(norm - 1) > .001:
        raise ValueError("Pose quaternion is not unit length")
    return [x/norm for x in value]


def multiply(a, b):
    x, y, z, w = a
    u, v, t, s = b
    return [w*u+x*s+y*t-z*v, w*v-x*t+y*s+z*u,
            w*t+x*v-y*u+z*s, w*s-x*u-y*v-z*t]


def compose(parent, local):
    p = vector(parent["position"], 3)
    q = quaternion(parent["orientation"])
    offset = vector(local["position"], 3)
    rotated = multiply(multiply(q, offset+[0]), [-q[0], -q[1], -q[2], q[3]])[:3]
    return {"position": [p[i]+rotated[i] for i in range(3)],
            "orientation": multiply(q, quaternion(local["orientation"]))}


def pose_difference(a, b):
    p, r = vector(a["position"], 3), vector(b["position"], 3)
    q, s = quaternion(a["orientation"]), quaternion(b["orientation"])
    dot = min(1, abs(sum(q[i]*s[i] for i in range(4))))
    return math.dist(p, r), math.degrees(2*math.acos(dot))


def summary(values):
    if not values:
        return {"samples": 0, "median": None, "p95": None, "max": None}
    ordered = sorted(values)
    return {"samples": len(values), "median": statistics.median(values),
            "p95": ordered[math.ceil(.95*len(ordered))-1], "max": ordered[-1]}


def tracked_palette_metric(row):
    # Schema 1 gate 5 deliberately hides wheel geometry 100 m below the world;
    # gate 1 can be an unmodified native palette. Neither is tracked alignment.
    return (row.get("gate") == 0 and row.get("publication_consistent") is True
            and row.get("replaced") is True and row.get("palette_valid") is True
            and row.get("consumer_landmarks", {}).get("valid") is True)


def audit(run):
    run = run.resolve()
    manifest = load_json(run / "recording/manifest.json")
    candidate = load_json(run / "candidate-at-launch.json")
    identity = manifest["loaded_identity"]
    if manifest.get("state") != "complete":
        raise ValueError("Freeze and finalize the recorder before auditing")
    pid = manifest["pid"]
    if type(pid) is not int or pid <= 0 or identity.get("pid") != pid:
        raise ValueError("Recorder/loaded identity PID mismatch")
    if not re.fullmatch(r"d2vr_render_probe_arm_v[0-9]+\.dll", candidate.get("dll_name", "")):
        raise ValueError("Invalid numbered candidate identity")
    if not re.fullmatch(r"[0-9a-f]{64}", candidate.get("sha256", "")) or candidate["sha256"] != identity.get("sha256"):
        raise ValueError("Candidate/loaded identity SHA mismatch")
    trace = run / "recording/final/artifacts/synchronized-diagnostics.jsonl"
    counts = Counter()
    kinds = {i: Counter() for i in range(3)}
    metrics = {i: {k: [] for k in ("attachment_chain_position_metres", "attachment_chain_rotation_degrees", "consumer_palm_to_grip_metres")} for i in (1, 2)}
    palm_states = {1: {}, 2: {}}
    ticks, presents, ticks_by_kind = [], [], {i: [] for i in range(3)}
    for row in read_rows(trace, counts):
        if row.get("pid") != pid or row.get("schema_version") != 1 or row.get("source") != "owned_native_palette":
            raise ValueError("Trace PID/schema/source mismatch")
        kind, tick = row.get("source_kind"), row.get("tick")
        if type(kind) is not int or kind not in kinds or type(tick) is not int or tick < 0:
            raise ValueError("Invalid trace consumer/tick")
        counts["complete_records"] += 1
        ticks.append(tick)
        ticks_by_kind[kind].append(tick)
        present = row.get("consumer_present")
        if type(present) is int:
            presents.append(present)
        group = kinds[kind]
        group["records"] += 1
        group["gate_"+str(row.get("gate"))] += 1
        for flag in ("publication_consistent", "consumer_frame_verified", "replaced", "palette_valid", "culling_observed", "lens_visibility_observed"):
            group[flag] += row.get(flag) is True
        landmarks = row.get("consumer_landmarks", {})
        if kind and tracked_palette_metric(row):
            hand = row["hands"][kind-1]
            try:
                expected = compose(hand["world_grip"], hand["grip_to_attachment"])
                position, rotation = pose_difference(expected, hand["target_attachment"])
                palm_position = vector(landmarks["palm"]["position"], 3)
                grip_position = vector(hand["world_grip"]["position"], 3)
            except (ValueError, KeyError, TypeError, IndexError):
                group["invalid_metric_records"] += 1
                continue
            metrics[kind]["attachment_chain_position_metres"].append(position)
            metrics[kind]["attachment_chain_rotation_degrees"].append(rotation)
            metrics[kind]["consumer_palm_to_grip_metres"].append(math.dist(palm_position, grip_position))
            fit = "fit_present" if row.get("fit", {}).get("revision", 0) > 0 else "fit_absent"
            ownership = "full_ownership" if hand.get("owned", 0) >= .999 else "fractional_ownership"
            palm_states[kind].setdefault(fit+":"+ownership, []).append(math.dist(palm_position, grip_position))
            group["qualified_palette_metric_records"] += 1
    if not ticks:
        raise ValueError("No complete trace records")
    status_ticks = []
    for row in read_rows(run / "recording/status.jsonl", Counter()):
        status = row.get("status", {})
        if status.get("pid") != pid:
            raise ValueError("Status sample belongs to a different process")
        tick = status.get("tick_ms")
        if type(tick) is int:
            status_ticks.append(tick)
    consumers = {}
    for kind, group in kinds.items():
        series = ticks_by_kind[kind]
        consumers[str(kind)] = dict(group) | {"tick_order_regressions": sum(b < a for a, b in zip(series, series[1:]))}
        if kind:
            consumers[str(kind)]["metrics"] = {k: summary(v) for k, v in metrics[kind].items()}
            consumers[str(kind)]["palm_distance_by_fit_and_ownership"] = {k: summary(v) for k, v in palm_states[kind].items()}
    with trace.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {
        "schema_version": 1, "candidate": candidate["dll_name"], "pid": pid,
        "loaded_sha256": identity["sha256"], "evidence_path": str(trace),
        "evidence_sha256": digest, "counts": dict(counts), "consumers": consumers,
        "coverage": {"trace_first_tick": min(ticks), "trace_last_tick": max(ticks),
                     "trace_span_seconds": (max(ticks)-min(ticks))/1000,
                     "status_first_tick": min(status_ticks) if status_ticks else None,
                     "status_last_tick": max(status_ticks) if status_ticks else None,
                     "unobserved_after_trace_seconds": max(0, max(status_ticks)-max(ticks))/1000 if status_ticks else None,
                     "first_consumer_present": min(presents) if presents else None,
                     "last_consumer_present": max(presents) if presents else None,
                     "stereo_captures": len(manifest.get("captures", [])),
                     "recorder_duration_seconds": manifest.get("duration_seconds")},
        "conclusions": {
            "headset_acceptance_established": False,
            "native_world_depth_established": False,
            "weapon_functional_axis_alignment_established": False,
            "full_engine_pose_chain_established": False,
            "metric_scope": "Gate-0 native tracked palette consumer estimates; wheel hiding/native copies excluded. Not final GPU vertices or headset pixels",
            "missing_evidence": [
                "Verified native consumer frame and shared weapon/arm/body ownership",
                "Weapon resource, final weapon model frame, handle/barrel/blade axes and shot ray",
                "Model-to-draw association, render bucket, DSV/state/clears and deferred execution order",
                "Rendered final wrist/palm/controller alignment and world occlusion",
                "Headset compositor timing; current cadence is not headset FPS"]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    # Compute first; a failed audit never overwrites or publishes a success report.
    report = audit(args.run)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"report": str(args.output), "records": report["counts"]["complete_records"],
                      "acceptance_established": False}))


if __name__ == "__main__":
    main()
