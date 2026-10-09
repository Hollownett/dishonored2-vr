"""Summarize pinned native runs; this does not confer release acceptance."""
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def summarize(archive):
    folder = ROOT / "artifacts" / "runs" / archive
    result = json.loads((folder / "dxgi-probe-result.json").read_text())
    timing = list(csv.DictReader((folder / "xr-frame-timing.csv").open()))
    keys = (
        "first_chance_fault_records", "callback_failed", "callback_error",
        "native_eye_pairs", "stereo_captured", "left_captured",
        "native_pause_button_reads", "native_nonzero_vr_controller_reads",
        "xr_menu_quad_layers", "xr_menu_transfer_pixels_equal",
        "gpu_dispatch_poisoned", "gpu_dispatch_rebindings",
        "native_interaction_routed", "native_interaction_rejected",
        "native_weapon_attachment_trace_requested",
        "native_weapon_attachment_trace_disabled", "native_weapon_attachment_calls",
        "native_weapon_attachment_samples", "native_weapon_attachment_errors",
        "native_weapon_attachment_writes", "native_arm_pose_calls",
        "native_arm_pose_fallbacks", "native_arm_reach_clamps",
        "mode_restored", "values_restored", "controller_import_restored",
        "camera_comfort_restored", "xr_render_thread_shutdown",
    )
    data = {key: result[key] for key in keys if key in result}
    data["archive"] = str(folder.relative_to(ROOT))
    data["submitted_tracked_authored_frames"] = sum(
        row["authored_camera"] == "1" and row["submitted"] == "1"
        and row["tracked_pair"] == "1" for row in timing
    )
    return data


if __name__ == "__main__":
    journal = summarize("20261009T012121.497530Z-pid44016-arm-v83")
    journal["native_journal_visually_verified"] = True
    journal["early_menu_attempt_failed_capture_gate"] = (
        "artifacts/runs/20261009T011623.495821Z-pid44016-arm-v83"
    )
    window = summarize("20261009T012258.861918Z-pid44016-arm-v83")
    window["native_window_open_visually_verified"] = True
    mantle = summarize("20261009T012341.683952Z-pid44016-arm-v84")
    exit_run = summarize("20261009T012452.737936Z-pid44016-arm-v84")
    evidence = {
        "simulated_xr_actual_native_game": True,
        "headset_acceptance": False,
        "full_gameplay_verified": False,
        "world_metric_scale_verified": False,
        "journal": journal, "window_use": window,
        "window_mantle": mantle, "window_exit": exit_run,
        "saves_modified_by_test_tool": False,
        "limits": [
            "Native story progress uses ordinary game input and legitimate autosaves.",
            "Weapon observer has no equipped samples; alignment and outcomes remain unverified.",
            "These scripted arm poses have no accepted calibration and show reach clamps.",
            "Death-menu reload, keyholes, all powers and physical headset comfort remain open.",
        ],
    }
    (ROOT / "artifacts" / "development-validation-v84.json").write_text(
        json.dumps(evidence, indent=2) + "\n"
    )
    candidate = json.loads((ROOT / "artifacts" / "demo-candidate.json").read_text())
    dll = ROOT / "build" / "d2vr_render_probe_arm_v84.dll"
    candidate.update(
        dll_name=dll.name, sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
        state="native_journal_window_and_passive_attachment_lifecycle_verified_full_gameplay_work_active",
        native_simulation_archive=mantle["archive"],
    )
    assert candidate["launch_blocked"] and not candidate["full_demo_complete"]
    (ROOT / "artifacts" / "demo-candidate.json").write_text(json.dumps(candidate, indent=2) + "\n")
