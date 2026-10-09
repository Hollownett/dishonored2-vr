"""Validate and record the native same-session checkpoint evidence."""
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
archive = root / "artifacts/runs/20261009T015057.025773Z-pid44016-arm-v89"
result = json.loads((archive / "dxgi-probe-result.json").read_text())
events = list(csv.DictReader((archive / "avatar-fit-events.csv").open()))
sequence = ["fit_accepted", "loading_suspend", "owner_replaced", "template_verified", "fit_reused", "pose_gate_updated"]
ordered = []
start = 0
for reason in sequence:
    index = next(i for i in range(start, len(events)) if events[i]["reason"] == reason)
    ordered.append(events[index])
    start = index + 1
accepted, suspended, replaced, verified, reused, resumed = ordered
assert accepted["reference_epoch"] == resumed["reference_epoch"] == "1"
assert suspended["pose_gate"] == suspended["active"] == "0"
assert replaced["pose_gate"] == verified["pose_gate"] == reused["pose_gate"] == "0"
assert resumed["pose_gate"] == resumed["active"] == "1"
assert accepted["left_player"] != resumed["left_player"]
for side in ("left", "right"):
    for field in ("resource_source", "resource_primary", "surface_identity", "surface_count"):
        assert accepted[f"{side}_{field}"] == verified[f"{side}_{field}"]
assert result["avatar_fit_accepts"] == result["avatar_fit_reuses"] == result["avatar_fit_suspensions"] == 1
assert result["avatar_fit_event_dropped"] == result["avatar_fit_invalidations"] == 0
assert not result["callback_failed"] and not result["gpu_dispatch_poisoned"]
assert result["first_chance_fault_records"] == 0 and result["native_eye_pairs"] > 180
for key in ("hook_disabled", "arm_upload_hook_disabled", "mode_restored", "values_restored",
            "camera_comfort_restored", "controller_import_restored", "xr_render_thread_shutdown"):
    assert result[key], key
evidence = {
    "archive": str(archive.relative_to(root)),
    "diagnostic_exit": 0,
    "actual_native_same_xr_session": True,
    "simulated_vr": True,
    "loaded_save": "Latest native autosave 2026-10-09 03:33:51 Warsaw, legitimate Sword story progress",
    "chronological_events": ordered,
    "result": result,
    "headset_acceptance": False,
    "death_menu_reload_verified": False,
    "full_gameplay_verified": False,
    "limits": ["Resource verification has a 500ms bound.",
               "Event order does not prove callback upload atomicity.",
               "Changed-template/reference invalidation remains isolated-test evidence."],
}
(root / "artifacts/development-validation-v89.json").write_text(json.dumps(evidence, indent=2) + "\n")
candidate_path = root / "artifacts/demo-candidate.json"
candidate = json.loads(candidate_path.read_text())
dll = root / "build/d2vr_render_probe_arm_v89.dll"
candidate.update(dll_name=dll.name, sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                 state="native_calibrated_checkpoint_return_verified_full_gameplay_work_active",
                 native_simulation_archive=evidence["archive"])
assert candidate["launch_blocked"] and not candidate["full_demo_complete"]
candidate_path.write_text(json.dumps(candidate, indent=2) + "\n")
checkpoint_path = root / "artifacts/development-checkpoint-2026-10-08.json"
checkpoint = json.loads(checkpoint_path.read_text())
checkpoint["active_candidate"] = candidate
checkpoint["goal_complete"] = False
checkpoint_path.write_text(json.dumps(checkpoint, indent=2) + "\n")
print("Verified ordered native fit suspension, owner replacement, exact resident-template reuse and stereo return; release remains incomplete.")
