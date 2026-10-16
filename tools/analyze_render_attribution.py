"""Offline attribution only. GPU elapsed spans are not GPU busy time or headset FPS."""
import argparse
import csv
import json
import math
from decimal import Decimal, InvalidOperation
from collections import defaultdict
from pathlib import Path

GPU = "epoch frame category eye id phase ms dropped replays failures".split()
CPU = "epoch frame thread category count ns maximum".split()
FRAME = "present epoch width height state gameplay submitted should_render settings capture_readback authored_camera".split()
CONFIG = "width height temporal_aa render_fraction_permille smaa_enabled desktop_sync_interval fsr_render_left_width fsr_render_left_height fsr_render_right_width fsr_render_right_height taa_viewport_left_width taa_viewport_left_height taa_viewport_right_width taa_viewport_right_height".split()
LOSS_REASONS = dict(enumerate(("query_create_failed", "query_read_failed", "prepare_owner", "begin_owner", "end_owner",
    "abort_owner", "span_owner", "mark_owner", "execute_owner", "poll_owner", "clear_owner", "active_frame_conflict",
    "frame_capacity", "span_capacity", "mark_capacity", "hud_capacity", "hud_capture_quota", "hud_bounds_quota", "span_end_owner",
    "operation_admission_busy", "operation_owner_conflict", "operation_context_mismatch", "operation_release_rejected")))
LOSS_METADATA = {0: "last_query_create_hresult", 1: "last_query_read_hresult", 2: "owner_thread",
    3: "last_rejected_thread", 4: "owner_context_identity", 5: "last_rejected_context"}


def read_csv(path, required):
    lines = Path(path).read_text(encoding="utf-8-sig").splitlines(keepends=True)
    rejected = {"incomplete_tail": 0, "malformed": 0, "duplicate_headers": 0}
    if lines and not lines[-1].endswith(("\n", "\r")):
        lines.pop(); rejected["incomplete_tail"] = 1
    if not lines:
        raise ValueError(f"No complete CSV header: {path}")
    header = next(csv.reader([lines[0]]))
    if not set(required).issubset(header):
        raise ValueError(f"Missing columns in {path}: {sorted(set(required)-set(header))}")
    rows = []
    for line in lines[1:]:
        try:
            values = next(csv.reader([line]))
            if values == header:
                rejected["duplicate_headers"] += 1; continue
            if len(values) != len(header):
                raise ValueError("partial row")
            raw_fields = dict(zip(header, values))
            row = {}
            for name, value in zip(header, values):
                if name == "ms":
                    if raw_fields.get("category") == "9":
                        exact_count = Decimal(value)
                        if not exact_count.is_finite() or exact_count < 0 or exact_count != exact_count.to_integral_value():
                            raise ValueError("invalid loss reason count")
                        number = int(exact_count)
                    else:
                        number = float(value)
                        if not math.isfinite(number):
                            raise ValueError("nonfinite duration")
                else:
                    number = int(value)
                row[name] = number
            if any(row[column_key] < 0 for column_key in required if column_key not in ("eye", "ms")):
                raise ValueError("negative counter/identity")
            rows.append(row)
        except (ValueError, InvalidOperation, csv.Error, StopIteration):
            rejected["malformed"] += 1
    return rows, rejected


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "p50": None, "p95": None, "p99": None, "maximum": None}
    return {"count": len(values), "p50": values[math.ceil(.50*len(values))-1],
            "p95": values[math.ceil(.95*len(values))-1], "p99": values[math.ceil(.99*len(values))-1],
            "maximum": values[-1]}


def read_status(path):
    source = Path(path).read_text(encoding="utf-8-sig")
    rejected = 0
    try:
        parsed = json.loads(source)
        records = parsed if isinstance(parsed, list) else parsed.get("status_samples", [parsed])
    except json.JSONDecodeError:
        records = []
        for line in source.splitlines():
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                rejected += 1
    return [sample_row.get("status", sample_row) for sample_row in records if isinstance(sample_row, dict)], rejected


def query_readiness(gpu):
    field_names = ("schema_version", "frame_serial", "poll_present", "age_presents", "context_identity",
        "blocking_stage", "hresult_u32", "query_index", "blocking_hud_ticket", "span_count", "mark_count",
        "hud_pairs_checked", "flags", "read_count", "coalesced_observations", "observation_count",
        "snapshot_present", "visited_stage_mask")
    stage_names = ("ready", "clock", "frame_start", "frame_end", "span_start", "span_end", "mark",
                   "hud_start", "hud_end", "invalid_frame_envelope")
    snapshots = [];orphans = [];current = None
    for row in gpu:
        category = row["category"]
        if category not in (15, 16, 17, 18):
            current = None
            continue
        if category == 15 and row["phase"] == 0:
            current = {"epoch": row["epoch"], "frame": row["frame"], "values": {}, "issues": []}
            snapshots.append(current)
        if current is None or (row["epoch"], row["frame"]) != (current["epoch"], current["frame"]):
            orphans.append(dict(row));continue
        key = (category, row["phase"])
        if key in current["values"]:
            current["issues"].append("duplicate_metadata_field")
        if row["eye"] != -1 or row["ms"] != 0:
            current["issues"].append("invalid_metadata_shape")
        current["values"][key] = row["id"]
    for snapshot in snapshots:
        values = snapshot.pop("values")
        required = {(15, phase) for phase in range(18)} | {(16, phase) for phase in range(1, 9)} | {(18, 0)}
        if values.get((15, 12), 0)&16:
            required |= {(17, phase) for phase in range(5)}
        if values.get((18, 0), 0):
            required |= {(18, phase) for phase in range(1, 7)}
        allowed = {(15, phase) for phase in range(18)} | {(16, phase) for phase in range(1, 9)} | {(17, phase) for phase in range(5)} | {(18, phase) for phase in range(7)}
        if required-set(values):
            snapshot["issues"].append("incomplete_metadata_snapshot")
        if set(values)-allowed:
            snapshot["issues"].append("unknown_metadata_field")
        snapshot.update({name: values.get((15, phase)) for phase, name in enumerate(field_names)})
        if snapshot["schema_version"] != 1:
            snapshot["issues"].append("unsupported_metadata_version")
        stage = snapshot["blocking_stage"]
        if stage is None or not 0 <= stage < len(stage_names):
            snapshot["issues"].append("unknown_query_stage")
        snapshot["stage_name"] = stage_names[stage] if stage is not None and 0 <= stage < len(stage_names) else None
        snapshot["hresult_hex"] = f"0x{snapshot['hresult_u32']:08X}" if snapshot["hresult_u32"] is not None else None
        if snapshot["hresult_u32"] is not None and not 0 <= snapshot["hresult_u32"] <= 0xffffffff:
            snapshot["issues"].append("invalid_hresult_width")
        if snapshot["visited_stage_mask"] is not None and not 0 <= snapshot["visited_stage_mask"] <= 255:
            snapshot["issues"].append("invalid_visited_stage_mask")
        if any(not 0 <= values[(16, phase)] <= 0xffffffff for phase in range(1, 9) if (16, phase) in values):
            snapshot["issues"].append("invalid_stage_hresult_width")
        snapshot["visited_stage_results"] = {stage_names[phase]: values.get((16, phase))
            for phase in range(1, 9) if (snapshot["visited_stage_mask"] or 0)&(1<<(phase-1))}
        snapshot["blocking_hud"] = dict(zip(("list_identity", "origin_present", "origin_epoch", "executions", "ticket"),
            (values.get((17, phase)) for phase in range(5)))) if (snapshot["flags"] or 0)&16 else None
        snapshot["attached_hud_at_snapshot"] = {"count": values.get((18, 0)), **dict(zip(
            ("first_ticket", "first_list_identity", "first_origin_present", "first_origin_epoch", "first_executions", "first_flags"),
            (values.get((18, phase)) for phase in range(1, 7))))}
        snapshot["recognized"] = not snapshot["issues"]
    return {"snapshots": snapshots, "orphan_rows": orphans,
        "units": "Categories15-18 are versioned integer readiness metadata, never GPU durations.",
        "limit": "Snapshots cache existing nonblocking query results. Terminal records can precede their snapshot; coalescing is explicit. Attached HUD identity is sampled later than the cached poll. S_FALSE is not evidence of a GPU performance bottleneck."}


def profiler_contract(gpu):
    reason_values = defaultdict(list);unknown_reason_rows = [];invalid_reason_rows = []
    metadata = [];latest_metadata = {};policy_rows = [];admission_rows = [];latest_admission = {}
    for row in gpu:
        if row["category"] == 9:
            count = row["ms"]
            if row["phase"] != 0 or not math.isfinite(count) or count < 0 or int(count) != count:
                invalid_reason_rows.append(dict(row));continue
            reason_values[row["id"]].append(int(count))
            if row["id"] not in LOSS_REASONS:
                unknown_reason_rows.append(dict(row))
        elif row["category"] == 10:
            observation = {"phase": row["phase"], "name": LOSS_METADATA.get(row["phase"]),
                "recognized": row["phase"] in LOSS_METADATA, "value": row["id"],
                "value_hex": f"0x{row['id']:X}", "raw_ms": row["ms"], "raw_row": dict(row)}
            metadata.append(observation);latest_metadata[str(row["phase"])] = observation
        elif row["category"] == 11:
            recognized = row["phase"] in (1, 2) and row["id"] > 0 and row["ms"] == 1
            policy_rows.append({"phase": row["phase"], "raw_id": row["id"], "raw_ms": row["ms"], "raw_row": dict(row),
                "recognized": recognized, "hud_category": row["phase"] if recognized else None,
                "per_sampled_frame_category_quota": row["id"] if recognized else None,
                "partial_category_observations": True if recognized else None})
        elif row["category"] == 14:
            recognized = row["phase"] in (1, 2) and row["eye"] == -1 and 0 <= row["ms"] <= row["id"] and int(row["ms"]) == row["ms"]
            observation = {"epoch": row["epoch"], "recording_frame": row["frame"], "category": row["phase"],
                "recognized": recognized, "eligible": row["id"], "admitted": int(row["ms"]) if recognized else None,
                "raw_row": dict(row), "gpu_timings_complete": False}
            admission_rows.append(observation)
            latest_admission[(row["epoch"], row["frame"], row["phase"])] = observation
    reasons = [{"id": reason_id, "name": LOSS_REASONS.get(reason_id), "recognized": reason_id in LOSS_REASONS,
        "latest_count": counts[-1], "maximum_count": max(counts), "snapshots": len(counts),
        "counter_decreased": any(current < previous for previous, current in zip(counts, counts[1:])),
        "intentional_quota_skip": reason_id in (16, 17) if reason_id in LOSS_REASONS else None} for reason_id, counts in sorted(reason_values.items())]
    quota = {"capture": max(reason_values.get(16, [0])), "bounds": max(reason_values.get(17, [0]))}
    diagnostic_loss = any(reason["maximum_count"] > 0 for reason in reasons if reason["recognized"] and not reason["intentional_quota_skip"])
    failed_hresult = any(row["phase"] in (0, 1) and row["value"]&0x80000000 for row in metadata)
    partial = any(row["recognized"] for row in policy_rows) or any(quota.values())
    readiness = query_readiness(gpu)
    uninterpreted = bool(unknown_reason_rows or invalid_reason_rows or any(not row["recognized"] for row in metadata+policy_rows+admission_rows)
        or readiness["orphan_rows"] or any(not snapshot["recognized"] for snapshot in readiness["snapshots"]))
    return {"loss_reason_counters": reasons, "intentional_quota_skips": quota,
        "unknown_reason_rows": unknown_reason_rows, "invalid_reason_rows": invalid_reason_rows,
        "metadata_observations": metadata, "latest_metadata": latest_metadata,
        "metadata_limit": "Latest global observations are not paired by reason, frame, callsite or prepare/retire generation. Owner and rejected metadata may describe different observations.",
        "hud_sampling_policy_observations": policy_rows, "hud_gpu_partial": partial,
        "hud_recording_admission": list(latest_admission.values()),
        "hud_admission_limit": "Category14 contains repeated eligible/admitted recording-frame snapshots, not GPU durations or proof that all admitted queries were published. Latest snapshots are not summed.",
        "hud_gpu_work_complete": False, "diagnostic_loss_reported": diagnostic_loss or failed_hresult,
        "failed_hresult_observed": failed_hresult,
        "uninterpreted_contract_rows": uninterpreted,
        "counter_decreased": any(reason["counter_decreased"] for reason in reasons),
        "has_central_gpu_rows": any(row["category"] in (0, 1, 2, 5, 12) or
            (row["category"] == 3 and row["phase"] in (1, 2, 3, 4)) for row in gpu),
        "query_readiness": readiness,
        "units": "Category9 ms fields contain cumulative counts; category10/11/14/15-18 rows contain metadata, policy or admission counts, not GPU durations."}


def analyze(gpu, cpu, frames=None, width=2112, height=2304, resolution_evidence=None):
    result = {"headset_fps_claimed": False, "output_100_percent_verified": False,
              "notes": ["Frame GPU elapsed includes queue idle and synchronization; it is not GPU busy time.",
                        "HUD capture GPU spans are subsets of native lists; do not add them to native sums.",
                        "Sampled HUD GPU sums are raw observations, not complete frame totals.",
                        "CPU cross-thread sums describe work, not the critical path.",
                        "HUD publication GPU elapsed is inclusive of bounds and crop/cache copies; do not add nested bounds to it.",
                        "FSR CPU dispatch contains record/handoff and handoff contains Flush. Per-thread work uses the maximum of their observed sums as a lower bound; category counts cannot identify every nested dispatch.",
                        "Sampled HUD hook-self category 5 contains categories 0..2; those nested categories are excluded from work when hook-self is present.",
                        "CPU CSV has no dropped-record counter; its completeness is not established."]}
    selected = None; selected_rows = {}; partitions = {}; frame_partitions = {}
    if frames is not None:
        seen = {}; by_present = {}; previous = None
        for row in frames:
            key = (row["epoch"], row["present"])
            if row["present"] in by_present and row != by_present[row["present"]]:
                raise ValueError("Conflicting Present identities: select one run before joining")
            if key in seen:
                if row != seen[key]:
                    raise ValueError("Conflicting frame identities: select one run before joining")
                continue
            if previous is not None and row["present"] < previous:
                raise ValueError("Present reset: frame inputs contain multiple runs; select one run")
            previous = row["present"]; seen[key] = row;by_present[row["present"]] = row
        for key, row in seen.items():
            valid = (row["width"] == width and row["height"] == height and row["state"] == 5
                     and all(row[column_key] == 1 for column_key in ("gameplay", "submitted", "should_render"))
                     and all(row[column_key] == 0 for column_key in ("settings", "capture_readback", "authored_camera")))
            if not valid:
                continue
            config = {column_key: row.get(column_key) for column_key in CONFIG}
            partition = json.dumps(config, sort_keys=True)
            partitions.setdefault(partition, {"configuration": config, "frames": 0})["frames"] += 1
            frame_partitions[key] = partition; selected_rows[key] = row
        selected = {key[1] for key in selected_rows}
        result["selected_geometry_verified"] = bool(selected)
        result["frame_selection"] = {"expected_actual_width": width, "expected_actual_height": height,
                                     "input_frames": len(seen), "eligible_frames": len(selected),
                                     "configuration_partitions": list(partitions.values())}
    else:
        result["notes"].append("No frame CSV: output geometry, gameplay eligibility and settings are unverified.")
    status = [sample_row.get("status", sample_row) for sample_row in resolution_evidence or []]
    matching_status = [sample_row for sample_row in status if sample_row.get("width") == width and sample_row.get("height") == height]
    pids = {sample_row["pid"] for sample_row in status if sample_row.get("pid") is not None}
    multipliers_one = all(sample_row.get("resolution_multiplier_requested") == 1.0 and
                          sample_row.get("resolution_multiplier_effective") == 1.0 for sample_row in matching_status)
    result["output_100_percent_verified"] = bool(selected and matching_status and multipliers_one and len(pids) <= 1)
    result["resolution_evidence"] = {"input_samples": len(status), "matching_geometry_samples": len(matching_status),
        "matching_samples_all_requested_effective_one": bool(matching_status and multipliers_one), "pids": sorted(pids),
        "basis": "Explicit status samples plus eligible frame geometry; caller must supply files from the same run."}
    if not result["output_100_percent_verified"]:
        result["notes"].append("Output dimensions alone do not prove a 100% multiplier; matching explicit status evidence is required.")
    # Diagnostic epochs include activation bits and differ from the XR pose epoch.
    # Present alone is a valid cross-CSV join only after rejecting multiple runs.
    accepted = lambda row: selected is None or row["frame"] in selected
    invalidated = {(row["epoch"], row["phase"], row["id"]) for row in gpu if row["category"] == 6}
    counters = {}
    for name in ("dropped", "failures", "replays"):
        values = [row[name] for row in gpu]
        counters[name] = {"maximum": max(values, default=0),
                          "increases": sum(current_value > previous_value for previous_value, current_value in zip([0]+values, values))}
    result["coverage"] = {"counters": counters, "incomplete": any(counters[column_key]["maximum"] for column_key in ("dropped", "failures")),
                          "invalidated_hud_tickets": [{"epoch": epoch_key, "category": category_key, "ticket": ticket} for epoch_key, category_key, ticket in sorted(invalidated)],
                          "budgets": [{"sampled_record_frames": sample_row["frame"], "used": sample_row["id"],
                                       "stride": sample_row["phase"], "capacity": sample_row["ms"]} for sample_row in gpu if sample_row["category"] == 7]}
    contract = profiler_contract(gpu);result["profiler_contract"] = contract
    result["coverage"]["hud_gpu_partial"] = contract["hud_gpu_partial"]
    result["coverage"]["hud_gpu_work_complete"] = False
    result["coverage"]["reason_specific_loss_reported"] = contract["diagnostic_loss_reported"]
    result["coverage"]["intentional_quota_skips"] = contract["intentional_quota_skips"]
    if any(contract[name] for name in ("hud_gpu_partial", "diagnostic_loss_reported", "uninterpreted_contract_rows", "counter_decreased")):
        result["coverage"]["incomplete"] = True
    if contract["hud_gpu_partial"]:
        result["notes"].append("HUD admission is partial: deferred per-category quotas are declared by category11 and recording admission by category14. Intentional quota skips are separate from legacy dropped/failures and are not complete HUD costs.")
    retirement = [sample_row for sample_row in gpu if sample_row["category"] == 8]
    result["coverage"]["retired_unfinished"] = [{"epoch": sample_row["epoch"], "frame": sample_row["frame"], "id": sample_row["id"],
        "kind": sample_row["phase"], "count": sample_row["ms"]} for sample_row in retirement if sample_row["phase"] in (0, 1)]
    result["coverage"]["retirement_counters"] = {name: max((sample_row["ms"] for sample_row in retirement if sample_row["phase"] == phase), default=0)
        for phase, name in ((2, "unfinished_frames"), (3, "unpublished_hud_pairs"), (4, "queued_rows_lost"))}
    if any(sample_row["ms"] > 0 for sample_row in retirement):
        result["coverage"]["incomplete"] = True
    # Scan the whole file first, but invalidate only that epoch/category/deferred ticket.
    # Immediate HUD ticket IDs have low 12 bits zero; deferred ticket IDs do not.
    filtered = [sample_row for sample_row in gpu if accepted(sample_row) and not (sample_row["category"] in (1, 2) and sample_row["id"]&4095
        and (sample_row["epoch"], sample_row["category"], sample_row["id"]) in invalidated)]
    groups = defaultdict(lambda: defaultdict(list))
    frame_values = defaultdict(lambda: defaultdict(list))
    negative_rows = []
    for row in filtered:
        key = (row["epoch"], row["frame"])
        if row["category"] == 3:
            groups[(row["epoch"], row["frame"], row["eye"], row["id"])][row["phase"]].append(row["ms"])
        elif row["category"] in (0, 1, 2, 5, 12):
            if row["ms"] < 0:
                negative_rows.append({column_key: row[column_key] for column_key in GPU[:6]}); continue
            frame_values[key][row["category"]].append(row["ms"])
    jobs = []
    for (epoch, frame, eye, job), phases in sorted(groups.items()):
        item = {"epoch": epoch, "frame": frame, "eye": eye, "job": job, "issues": [],
                "phase_observations_ms": dict(phases),
                "input_ms": None, "bridge_ms": None, "output_copy_ms": None, "wrapper_ms": None,
                "cpu_flush_ms": None, "cpu_record_ms": None, "cpu_handoff_ms": None, "cpu_dispatch_ms": None,
                "ffx_gpu_ms": None, "bridge_residual_ms": None}
        for phase in range(1, 11):
            if len(phases.get(phase, [])) > 1:
                item["issues"].append(f"duplicate_phase_{phase}")
        unique = {phase_key: values[0] for phase_key, values in phases.items() if len(values) == 1}
        if 7 in phases:
            item["issues"].append("ffx_timing_dropped_or_unmatched")
        if 6 in phases and 7 in phases:
            item["issues"].append("conflicting_ffx_terminals")
        for phase, name in ((5, "cpu_flush_ms"), (6, "ffx_gpu_ms"), (8, "cpu_record_ms"), (9, "cpu_handoff_ms"), (10, "cpu_dispatch_ms")):
            if phase == 6 and 7 in phases:
                continue
            if phase in unique:
                if unique[phase] < 0:
                    item["issues"].append(f"negative_phase_{phase}")
                else:
                    item[name] = unique[phase]
        missing = [phase_key for phase_key in range(1, 5) if phase_key not in unique]
        if missing:
            item["issues"].append("missing_or_duplicate_gpu_phases:"+",".join(map(str, missing)))
        elif 6 not in phases and 7 not in phases:
            item["issues"].append("missing_ffx_terminal")
        if not missing and not (0 <= unique[1] <= unique[2] <= unique[3] <= unique[4]):
            item["issues"].append("nonmonotonic_gpu_phases")
        elif not missing:
            item.update(input_ms=unique[2]-unique[1], bridge_ms=unique[3]-unique[2],
                        output_copy_ms=unique[4]-unique[3], wrapper_ms=unique[4]-unique[1])
            if item["ffx_gpu_ms"] is not None:
                residual = item["bridge_ms"]-item["ffx_gpu_ms"]
                if residual < 0:
                    item["issues"].append("negative_bridge_residual")
                else:
                    item["bridge_residual_ms"] = residual
        item["gpu_complete"] = all(item[name] is not None for name in ("input_ms", "bridge_ms", "output_copy_ms", "ffx_gpu_ms", "bridge_residual_ms"))
        item["cpu_complete"] = all(item[name] is not None for name in ("cpu_flush_ms", "cpu_record_ms", "cpu_handoff_ms", "cpu_dispatch_ms"))
        jobs.append(item)
    result["fsr_jobs"] = jobs
    result["fsr_distributions_ms"] = {name: distribution([dispatch_job[name] for dispatch_job in jobs if dispatch_job[name] is not None])
        for name in ("input_ms", "bridge_ms", "output_copy_ms", "wrapper_ms", "cpu_flush_ms", "cpu_record_ms", "cpu_handoff_ms", "cpu_dispatch_ms", "ffx_gpu_ms", "bridge_residual_ms")}
    result["coverage"]["fsr_jobs"] = {"total": len(jobs),
        "complete_gpu": sum(dispatch_job["gpu_complete"] for dispatch_job in jobs),
        "complete_cpu": sum(dispatch_job["cpu_complete"] for dispatch_job in jobs)}
    result["gpu_frames"] = [{"epoch": epoch_key, "frame": frame_key, "native_list_sum_ms": sum(sample_values[0]) if sample_values[0] else None,
        "native_list_count": len(sample_values[0]), "hud_capture_observed_sum_ms": sum(sample_values[1]) if sample_values[1] else None,
        "hud_capture_samples": len(sample_values[1]), "hud_bounds_observed_sum_ms": sum(sample_values[2]) if sample_values[2] else None,
        "hud_bounds_samples": len(sample_values[2]), "hud_publication_elapsed_ms": sum(sample_values[12]) if sample_values[12] else None,
        "hud_publication_samples": len(sample_values[12]), "frame_elapsed_ms": sample_values[5][0] if len(sample_values[5]) == 1 else None}
        for (epoch_key, frame_key), sample_values in sorted(frame_values.items())]
    result["negative_gpu_rows"] = negative_rows
    result["coverage"]["measurement_issue_count"] = len(negative_rows)+sum(bool(dispatch_job["issues"]) for dispatch_job in jobs)
    if result["coverage"]["measurement_issue_count"]:
        result["coverage"]["incomplete"] = True
    chunks = defaultdict(lambda: {"count": 0, "ns": 0, "maximum": 0})
    for row in cpu:
        if not accepted(row):
            continue
        key = (row["epoch"], row["frame"], row["thread"], row["category"])
        total = chunks[key];total["count"] += row["count"];total["ns"] += row["ns"]
        total["maximum"] = max(total["maximum"], row["maximum"])
    result["cpu_recording_threads"] = [{"epoch": epoch_key, "recording_frame": frame_key, "thread": thread_key, "category": category_key, **sample_values}
                                       for (epoch_key, frame_key, thread_key, category_key), sample_values in sorted(chunks.items())]
    thread_categories = defaultdict(dict)
    for (epoch_key, frame_key, thread, category), value in chunks.items():
        thread_categories[(epoch_key, frame_key, thread)][category] = value["ns"]
    work = defaultdict(lambda: defaultdict(int));sampled_self = defaultdict(set);fsr_cpu_coverage = []
    for (epoch_key, frame_key, thread), categories in thread_categories.items():
        has_self = 5 in categories
        included = (3, 5) if has_self else (0, 1, 2, 3)
        fsr_work = max(categories.get(8, 0), categories.get(6, 0)+categories.get(7, 0), categories.get(4, 0))
        work[(epoch_key, frame_key)][thread] = sum(categories.get(category_key, 0) for category_key in included)+fsr_work
        phase_counts = {str(category): chunks[(epoch_key, frame_key, thread, category)]["count"] for category in (4, 6, 7, 8) if category in categories}
        if phase_counts:
            fsr_cpu_coverage.append({"epoch": epoch_key, "frame": frame_key, "thread": thread, "category_counts": phase_counts,
                "counts_differ": len(set(phase_counts.values())) > 1, "observed_work_lower_bound_ms": fsr_work/1e6,
                "complete_union_work_known": False})
        if has_self:
            sampled_self[(epoch_key, frame_key)].add(thread)
    result["cpu_recording_frames"] = [{"epoch": epoch_key, "recording_frame": frame_key, "all_threads_work_ms": sum(sample_values.values())/1e6,
        "max_thread_work_ms": max(sample_values.values())/1e6, "threads": len(sample_values), "sampled_hook_self_present": bool(sampled_self[(epoch_key, frame_key)]),
        "sampled_hook_self_threads": len(sampled_self[(epoch_key, frame_key)]),
        "work_coverage": "partial HUD work plus a lower bound for observed FSR CPU work; cross-thread sums are not frame duration"}
        for (epoch_key, frame_key), sample_values in sorted(work.items())]
    result["cpu_raw_category_distributions_ms"] = {str(category): distribution([value["ns"]/1e6
        for (_, _, _, category_key), value in chunks.items() if category_key == category]) for category in sorted({column_key[3] for column_key in chunks})}
    result["coverage"]["cpu_sampled_hook_self_present"] = any(sampled_self.values())
    result["coverage"]["cpu_sampled_hook_self_recording_frames"] = sum(bool(sample_values) for sample_values in sampled_self.values())
    result["coverage"]["cpu_work_is_partial"] = True
    result["fsr_cpu_thread_coverage"] = fsr_cpu_coverage
    result["cpu_work_distributions_ms"] = {name: distribution([sample_row[name] for sample_row in result["cpu_recording_frames"]])
        for name in ("all_threads_work_ms", "max_thread_work_ms")}
    result["gpu_distributions_ms"] = {name: distribution([sample_row[name] for sample_row in result["gpu_frames"] if sample_row[name] is not None])
        for name in ("native_list_sum_ms", "hud_capture_observed_sum_ms", "hud_bounds_observed_sum_ms", "hud_publication_elapsed_ms", "frame_elapsed_ms")}
    # Application cadence is derived only from consecutive eligible rows of one configuration.
    intervals = defaultdict(list)
    if frames is not None:
        ordered = sorted(selected_rows.items(), key=lambda kv: kv[0][1])
        for (previous_key, previous_row), (key, row) in zip(ordered, ordered[1:]):
            if (key[0] == previous_key[0] and key[1] == previous_key[1]+1
                    and frame_partitions[key] == frame_partitions[previous_key]
                    and "start_cpu_ns" in row and "start_cpu_ns" in previous_row):
                delta = (row["start_cpu_ns"]-previous_row["start_cpu_ns"])/1e6
                if delta > 0:
                    intervals[frame_partitions[key]].append(delta)
    result["application_cadence"] = [{"configuration": json.loads(config), "interval_ms": distribution(values),
        "application_hz": len(values)*1000/sum(values)} for config, values in intervals.items()]
    result["configuration_attribution"] = []
    for config in partitions:
        keys = {key for key, value in frame_partitions.items() if value == config}
        presents = {key[1] for key in keys}
        selected_jobs = [dispatch_job for dispatch_job in jobs if dispatch_job["frame"] in presents]
        selected_gpu = [sample_row for sample_row in result["gpu_frames"] if sample_row["frame"] in presents]
        selected_cpu = [sample_row for sample_row in result["cpu_recording_frames"] if sample_row["recording_frame"] in presents]
        result["configuration_attribution"].append({"configuration": json.loads(config), "eligible_frames": len(keys),
            "fsr_ms": {name: distribution([dispatch_job[name] for dispatch_job in selected_jobs if dispatch_job[name] is not None]) for name in result["fsr_distributions_ms"]},
            "gpu_ms": {name: distribution([sample_row[name] for sample_row in selected_gpu if sample_row[name] is not None]) for name in result["gpu_distributions_ms"]},
            "cpu_work_ms": {name: distribution([sample_row[name] for sample_row in selected_cpu]) for name in result["cpu_work_distributions_ms"]},
            "application_cpu_ms": {name: distribution([selected_rows[column_key][name]/1e6 for column_key in keys
                if name in selected_rows[column_key] and selected_rows[column_key][name] >= 0]) for name in
                ("engine_cpu_gap_ns", "pre_xr_cpu_ns", "previous_desktop_present_ns", "transfer_cpu_ns", "submit_cpu_ns")}})
    if len(partitions) > 1:
        result["notes"].append("Top-level distributions mix configurations; use configuration_attribution for comparisons.")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gpu", required=True);parser.add_argument("--cpu", required=True)
    parser.add_argument("--frames", action="append");parser.add_argument("--output", required=True)
    parser.add_argument("--status", help="Same-run status JSON, JSON array, or JSONL wrappers containing status")
    parser.add_argument("--expected-width", type=int, default=2112);parser.add_argument("--expected-height", type=int, default=2304)
    args = parser.parse_args(argv)
    gpu, gpu_rejected = read_csv(args.gpu, GPU);cpu, cpu_rejected = read_csv(args.cpu, CPU)
    frames = [] if args.frames else None;frame_rejected = []
    for path in args.frames or []:
        rows, rejects = read_csv(path, FRAME);frames.extend(rows);frame_rejected.append({"path": path, **rejects})
    status, status_rejected = read_status(args.status) if args.status else ([], 0)
    result = analyze(gpu, cpu, frames, args.expected_width, args.expected_height, status)
    result["inputs"] = {"gpu": args.gpu, "cpu": args.cpu, "frames": args.frames, "status": args.status}
    result["resolution_evidence"]["rejected_samples"] = status_rejected
    result["rejected_rows"] = {"gpu": gpu_rejected, "cpu": cpu_rejected, "frames": frame_rejected}
    if any(sample_row["malformed"] or sample_row["incomplete_tail"] for sample_row in [gpu_rejected, cpu_rejected]+frame_rejected):
        result["coverage"]["incomplete"] = True
    if status_rejected:
        result["coverage"]["incomplete"] = True;result["output_100_percent_verified"] = False
    Path(args.output).write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    main()
