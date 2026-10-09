"""Record bounded v96 archive evidence without opening the release gate."""
import csv
import collections
import hashlib
import json
from pathlib import Path
from analyze_physical_grab import analyze

root = Path(__file__).resolve().parents[1]
artifacts = root / 'artifacts'
clean_name = '20261009T042250.457466Z-pid44016-arm-v96'
dry_name = '20261009T042001.271781Z-pid44016-arm-v96'
lean_name = '20261009T035737.514572Z-pid44016-arm-v96'
grab_name = '20261009T045348.957914Z-pid44016-arm-v96'
ranged_name = '20261009T045035.998020Z-pid44016-arm-v96'
clean_dir = artifacts / 'runs' / clean_name
dry_dir = artifacts / 'runs' / dry_name
lean_dir = artifacts / 'runs' / lean_name
grab_dir = artifacts / 'runs' / grab_name
ranged_dir = artifacts / 'runs' / ranged_name
carry_path = artifacts / 'research/native-arm-assets/native-empty-carryqualified-v96-20261009/capture.json'
dll_path = root / 'build/d2vr_render_probe_arm_v96.dll'
pointer_path = root / 'build/d2vr_render_probe_arm_v96.dll.source.json'
expected_dll_sha = '40e92ab56aa9332b79372122ac54bb437d5f49f68bf7fdf4641624e3924a26e7'
game_sha = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'


def read_json(path):
    return json.loads(path.read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


clean = read_json(clean_dir / 'dxgi-probe-result.json')
launch = read_json(clean_dir / 'probe-launch-config.json')
loaded = read_json(clean_dir / 'loaded-render-probe-arm-v96.json')
pointer = read_json(pointer_path)
dry = read_json(dry_dir / 'dxgi-probe-result.json')
carry = read_json(carry_path)
lean = read_json(artifacts / 'native-y-first-lean-v96-20261009.json')
grab = read_json(grab_dir / 'dxgi-probe-result.json')
grab_launch = read_json(grab_dir / 'probe-launch-config.json')
grab_loaded = read_json(grab_dir / 'loaded-render-probe-arm-v96.json')
ranged = read_json(ranged_dir / 'dxgi-probe-result.json')

# Require exact records and safety properties before any output file is written.
assert launch['status'] == 'completed' and launch['diagnostic_exit'] == 0
assert launch['pid'] == clean['pid'] == loaded['pid'] == 44016
assert launch['executable_sha256'] == game_sha
assert launch['dll_name'] == pointer['dll_name'] == 'd2vr_render_probe_arm_v96.dll'
assert launch['dll_sha256'] == loaded['sha256'] == pointer['dll_sha256'] == expected_dll_sha
assert digest(dll_path) == expected_dll_sha
assert pointer['source_unchanged_during_build'] is True
assert pointer['compiled_from_frozen_snapshot'] is False
assert digest(clean_dir / 'build-source-manifest.json') == pointer['source_manifest_sha256']
assert digest(clean_dir / 'build-source.zip') == pointer['source_zip_sha256']
assert launch['tracked_arms_requested'] is False
assert launch['arm_palette_copy_requested'] is False
assert clean['tracked_arms_requested'] is False and clean['arm_palette_copy_requested'] is False
assert clean['first_chance_fault_records'] == 0 and clean['callback_failed'] is False
assert clean['native_arm_pose_calls'] == [0, 0] and clean['native_arm_copy_calls'] == [0, 0]
assert clean['native_eye_pairs'] == clean['xr_gameplay_layers'] == 1921
assert clean['playable_verified'] is False and clean['native_weapon_attachment_writes'] is False
for key, value in clean.items():
    if key.endswith(('_restored', '_hook_disabled')) or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'):
        assert value is True, ('clean', key, value)

assert dry['tracked_arms_requested'] is True and dry['arm_palette_copy_requested'] is False
assert dry['avatar_fit_accepts'] == 1
assert dry['avatar_fit_invalidations'] == dry['avatar_fit_event_dropped'] == 0
assert dry['native_arm_reach_clamps'] == [0, 0]
assert dry['native_arm_pose_calls'] == [329, 329] and dry['native_arm_copy_calls'] == [0, 0]
assert dry['native_weapon_dry_prepared'] == dry['native_weapon_dry_completed'] == 328
assert dry['native_weapon_dry_outputs_changed'] is False
assert dry['native_weapon_attachment_writes'] is False
assert dry['first_chance_fault_records'] == 0 and dry['callback_failed'] is False
with (dry_dir / 'native-weapon-dry-lease.csv').open(newline='') as file:
    dry_rows = list(csv.DictReader(file))
assert len(dry_rows) == 1312
assert collections.Counter(row['site'] for row in dry_rows) == {'0': 328, '1': 328, '4': 328, '5': 328}
assert all(row['accepted'] == '1' and row['outputs_changed'] == 'false' for row in dry_rows)
assert all(row['reason'] == '0' for row in dry_rows)

assert carry['pid'] == 44016 and carry['executable_sha256'] == game_sha
assert carry['game_memory_writes'] is False and carry['engine_calls'] is False
assert carry['authored_fist_captured'] is False and carry['occupied_source_capture_allowed'] is False
carry_samples = carry['samples']
assert len(carry_samples) == 299
assert all(sample['gameplay_window_focused'] for sample in carry_samples)
assert all(sample['state']['native_carry']['typed_owner_verified'] for sample in carry_samples)
assert all(sample['state']['native_carry']['active'] == [False, False] for sample in carry_samples)
assert all(sample['state']['native_carry']['carrying'] is False for sample in carry_samples)

assert lean['pid'] == 44016 and lean['executable_sha256'] == game_sha
assert lean['game_memory_writes'] is False and lean['engine_calls'] is False
assert lean['frame_synchronised'] is False and not lean['rejected'] and len(lean['owners']) == 1
lean_samples = lean['samples']
stable = [row for row in lean_samples if row['tick_ms'] == row['end_tick_ms']
          and row['feet_consistent'] and row['lean_consistent'] and row['camera_read_consistent']]
assert len(lean_samples) == 500 and len(stable) == 464
stable_active = [row for row in stable if row['lean']['active']]
assert len(stable_active) == 169
assert len({tuple(row['feet']) for row in stable_active}) == 1
assert not any(row['lean']['active'] for row in lean_samples[-50:])
with (lean_dir / 'simulated-gameplay-commands.csv').open(newline='') as file:
    commands = list(csv.DictReader(file))
first_nonzero = next(row for row in commands if any(float(row[key]) != 0 for key in (
    'move_x', 'move_y', 'turn_x', 'turn_y', 'left_trigger', 'right_trigger',
    'left_grip', 'right_grip', 'a', 'b', 'x', 'y')))
assert first_nonzero['y'] == '1' and first_nonzero['elapsed_ms'] == '1407'
assert first_nonzero['focused'] == '1' and first_nonzero['left_click'] == '0' and first_nonzero['right_click'] == '0'

assert grab_launch['status'] == 'completed' and grab_launch['diagnostic_exit'] == 0
assert grab_launch['pid'] == grab['pid'] == grab_loaded['pid'] == 44016
assert grab_launch['executable_sha256'] == game_sha
assert grab_launch['dll_sha256'] == grab_loaded['sha256'] == pointer['dll_sha256'] == expected_dll_sha
assert digest(grab_dir / 'build-source-manifest.json') == pointer['source_manifest_sha256']
assert digest(grab_dir / 'build-source.zip') == pointer['source_zip_sha256']
assert grab_launch['tracked_arms_requested'] is True and grab_launch['arm_palette_copy_requested'] is False
assert grab['tracked_arms_requested'] is True and grab['arm_palette_copy_requested'] is False
assert grab['avatar_fit_accepts'] == 1
assert grab['avatar_fit_invalidations'] == grab['avatar_fit_event_dropped'] == 0
assert grab['fixture_grip_path'] == 'ordinary' and grab['fixture_ranged_frames'] == 0
assert grab['fixture_ordinary_frames'] > 0
assert grab['fixture_initial_ordinary_contact_gap_native_units'] == 0.276198
assert grab['native_arm_reach_clamps'] == [0, 0]
assert grab['physical_grab_picked'] == grab['physical_grab_dropped'] == 2
assert grab['physical_grab_moved'] == 135 and grab['physical_grab_lost_ownership'] == 0
assert grab['first_chance_fault_records'] == 0 and grab['callback_failed'] is False
with (grab_dir / 'avatar-fit-events.csv').open(newline='') as file:
    grab_fit = list(csv.DictReader(file))
accepted_fit = next(row for row in grab_fit if row['reason'] == 'fit_accepted')
with (grab_dir / 'native-physical-grab.csv').open(newline='') as file:
    motor_rows = list(csv.DictReader(file))
assert motor_rows and min(int(row['tick_ms']) for row in motor_rows) > int(accepted_fit['tick'])
grab_analysis = analyze(grab_dir)
assert grab_analysis['passed'] and grab_analysis['motor_samples'] == 135
assert abs(grab_analysis['vertical_travel_native_units'] - 0.2309875) < 1e-9
assert abs(grab_analysis['maximum_target_error_native_units'] - 0.01588384) < 1e-8
assert abs(grab_analysis['releases'][0]['speed'] - 0.253353894) < 1e-9
assert grab_analysis['native_world_metric_scale_verified'] is False
for key, value in grab.items():
    if key.endswith(('_restored', '_hook_disabled')) or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'):
        assert value is True, ('calibrated_grab', key, value)
assert ranged['fixture_grip_path'] == 'ranged-development'
assert ranged['fixture_initial_ordinary_contact_gap_native_units'] == 0.302655
assert ranged['fixture_ordinary_frames'] == 0 and ranged['fixture_ranged_frames'] == 554

prior_validation_path = artifacts / 'development-validation-v96.json'
prior_validation = read_json(prior_validation_path)
assert prior_validation['death_reload_continuity']['launch_blocked'] is True

clean_record = {
    'archive': f'artifacts/runs/{clean_name}',
    'launch_config_sha256': digest(clean_dir / 'probe-launch-config.json'),
    'result_sha256': digest(clean_dir / 'dxgi-probe-result.json'),
    'pid': clean['pid'], 'game_sha256': game_sha, 'dll_sha256': expected_dll_sha,
    'launcher_status': launch['status'], 'diagnostic_exit': launch['diagnostic_exit'],
    'tracked_arms_requested': False, 'arm_palette_copy_requested': False,
    'fault_records': 0, 'callback_failed': False, 'pose_calls': [0, 0], 'copy_calls': [0, 0],
    'native_eye_pairs': 1921, 'xr_gameplay_layers': 1921,
    'restoration_and_hooks_disabled': True, 'playable_verified': False,
    'native_weapon_attachment_writes': False,
}
dry_record = {
    'archive': f'artifacts/runs/{dry_name}',
    'result_sha256': digest(dry_dir / 'dxgi-probe-result.json'),
    'lease_csv_sha256': digest(dry_dir / 'native-weapon-dry-lease.csv'),
    'tracked_arms_requested': True, 'fit_accepts': 1, 'fit_invalidations': 0,
    'reach_clamps': [0, 0], 'pose_calls': [329, 329], 'palette_copy_calls': [0, 0],
    'dry_lease_prepared_completed': [328, 328], 'lease_rows': len(dry_rows),
    'lease_sites': {'0': 328, '1': 328, '4': 328, '5': 328},
    'lease_outputs_changed': False, 'native_weapon_attachment_writes': False,
}
grab_record = {
    'archive': f'artifacts/runs/{grab_name}',
    'launch_config_sha256': digest(grab_dir / 'probe-launch-config.json'),
    'result_sha256': digest(grab_dir / 'dxgi-probe-result.json'),
    'fit_events_sha256': digest(grab_dir / 'avatar-fit-events.csv'),
    'physical_grab_csv_sha256': digest(grab_dir / 'native-physical-grab.csv'),
    'launcher_status': grab_launch['status'], 'diagnostic_exit': grab_launch['diagnostic_exit'],
    'tracked_arms_requested': True, 'arm_palette_copy_requested': False,
    'fit_accepts': 1, 'fit_invalidations': 0,
    'accepted_fit_tick': int(accepted_fit['tick']),
    'first_physics_motor_tick': min(int(row['tick_ms']) for row in motor_rows),
    'initial_ordinary_contact_gap_native_units': 0.276198,
    'grip_path': 'ordinary', 'ordinary_frames': grab['fixture_ordinary_frames'], 'ranged_frames': 0,
    'reach_clamps': [0, 0], 'pickups': 2, 'motor_samples': 135, 'drops': 2, 'ownership_losses': 0,
    'release_speeds': [round(item['speed'], 9) for item in grab_analysis['releases']],
    'analysis': grab_analysis,
    'fault_records': 0, 'callback_failed': False,
    'restoration_and_hooks_disabled': True,
    'limits': 'Simulated controllers and one nearby dynamic prop; world metric scale is unverified. This is not natural-fist, weapon, large-object, headset comfort, or whole-game acceptance.',
}
evidence = {
    'schema': 'd2vr-development-validation-v96-v1',
    'actual_native_game': True,
    'simulated_vr': True,
    'candidate_dll': {'name': dll_path.name, 'sha256': expected_dll_sha,
                      'source_provenance': 'build/d2vr_render_probe_arm_v96.dll.source.json',
                      'source_unchanged_during_build': pointer['source_unchanged_during_build'],
                      'compiled_from_frozen_snapshot': pointer['compiled_from_frozen_snapshot']},
    'clean_run': clean_record,
    'calibrated_sword_dry_lease_trial': dry_record,
    'calibrated_ordinary_contact_grab_trial': grab_record,
    'excluded_nonordinary_grab_trial': {
        'archive': f'artifacts/runs/{ranged_name}',
        'grip_path': ranged['fixture_grip_path'],
        'initial_gap_native_units': ranged['fixture_initial_ordinary_contact_gap_native_units'],
        'ordinary_frames': ranged['fixture_ordinary_frames'],
        'ranged_frames': ranged['fixture_ranged_frames'],
        'counted_as_ordinary_contact_proof': False,
    },
    'typed_empty_carry_companion': {
        'capture': str(carry_path.relative_to(root)), 'samples': len(carry_samples),
        'typed_owner_verified_samples': len(carry_samples), 'both_typed_states_inactive_samples': len(carry_samples),
        'game_memory_writes': False, 'engine_calls': False,
        'limit': 'Typed HoldObject49/CarryCorpse45 owner/state reads in an empty-hand capture; not a carry action, occupied-hand proof, or fist capture.',
    },
    'y_first_lean_trial': {
        'capture': 'artifacts/native-y-first-lean-v96-20261009.json',
        'archive': f'artifacts/runs/{lean_name}', 'raw_samples': 500,
        'stable_window_samples': 464, 'stable_active_lean_samples': 169,
        'active_lean_unique_feet_positions': 1, 'active_in_final_50_samples': 0,
        'first_nonzero_ordinary_input': {'elapsed_ms': 1407, 'input': 'Y', 'focused': True},
        'resume_click_recorded': False,
        'limits': ['Command log does not record a mouse Resume click.',
                   'Stable-window filtering excludes 36 samples where tick changed during sampling.',
                   'Reads are not frame-synchronised and do not establish headset comfort or visual acceptance.'],
    },
    'prior_v95_baseline': {
        'record': 'artifacts/development-validation-v95.json',
        'claim': 'The v95 calibrated ordinary-contact grab is an earlier baseline; a separate matching ordinary-contact result is now directly recorded in v96.',
    },
    'death_reload_continuity': prior_validation['death_reload_continuity'],
    'headset_acceptance': False,
    'full_gameplay_verified': False,
    'launch_blocked': True,
    'default_tracked_arms': False,
    'unfinished': ['Natural fists and hand appearance', 'Tracked weapons and powers',
                   'Physical head translation', 'Complete gameplay and headset comfort/visual acceptance'],
    'release_limit': 'The 328 accepted dry leases changed no observed outputs and performed no native weapon attachment writes; they are diagnostic evidence only.',
}

prior_validation_path.write_text(json.dumps(evidence, indent=2) + '\n')
candidate_path = artifacts / 'demo-candidate.json'
candidate = read_json(candidate_path)
assert candidate['launch_blocked'] and not candidate['full_demo_complete']
assert candidate['headset_acceptance'] is False and candidate['default_tracked_arms'] is False
candidate.update(
    dll_name=dll_path.name,
    sha256=expected_dll_sha,
    state='v96_bounded_native_diagnostics_gameplay_work_active',
    full_demo_complete=False,
    headset_acceptance=False,
    launch_blocked=True,
    default_tracked_arms=False,
    native_simulation_archive=grab_record['archive'],
    launch_blocked_reason='v96 now includes a calibrated ordinary-contact grab of one nearby native dynamic prop using simulated controllers, plus clean restoration and bounded diagnostic evidence. Natural fists, tracked weapons/powers, physical head translation, complete gameplay and headset acceptance remain unfinished; native world metric scale is unverified.',
)
candidate_path.write_text(json.dumps(candidate, indent=2) + '\n')
checkpoint_path = artifacts / 'development-checkpoint-2026-10-08.json'
checkpoint = read_json(checkpoint_path)
checkpoint['active_candidate'] = candidate
checkpoint['goal_complete'] = False
checkpoint_path.write_text(json.dumps(checkpoint, indent=2) + '\n')
print('v96 bounded evidence, including calibrated ordinary-contact grab, recorded; release remains blocked.')
