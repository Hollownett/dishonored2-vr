"""Record bounded native v95 outcomes; do not open the incomplete release gate."""
import csv
import hashlib
import json
from pathlib import Path

from analyze_physical_grab import analyze

root = Path(__file__).resolve().parents[1]
artifacts = root / 'artifacts'
names = {
    'input_return_lean': '20261009T033719.458444Z-pid44016-arm-v95',
    'calibrated_grab': '20261009T033905.865649Z-pid44016-arm-v95',
    'native_carry': '20261009T034249.862354Z-pid44016-arm-v95',
}
results = {}
for name, directory in names.items():
    folder = artifacts / 'runs' / directory
    value = json.loads((folder / 'dxgi-probe-result.json').read_text())
    assert value['first_chance_fault_records'] == 0
    assert not value['callback_failed'] and not value['gpu_dispatch_poisoned']
    assert value['native_eye_pairs'] > 180 and value['xr_gameplay_layers'] > 180
    for key, state in value.items():
        if key.endswith(('_restored', '_hook_disabled')) or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'):
            assert state, (name, key)
    results[name] = {'archive': str(folder.relative_to(root)), 'result': value,
                     'result_sha256': hashlib.sha256((folder / 'dxgi-probe-result.json').read_bytes()).hexdigest()}

def rows(name, filename):
    with (artifacts / 'runs' / names[name] / filename).open() as file:
        return list(csv.DictReader(file))

lean = results['input_return_lean']['result']
assert lean['native_lean_keyboard_inactive'] > 0 and lean['native_nonzero_vr_controller_reads'] > 1000
assert lean['native_lean_output_y_frames'] > 100
assert lean['native_lean_query_rejected'] == lean['native_lean_query_dropped'] == 0
assert lean['native_lean_lifetime_rejected'] == lean['native_lean_lifetime_overflow'] == 0
assert not lean['native_lean_lifetime_failed']
trial_path = artifacts / 'native-input-return-lean-v95-20261009.json'
trial = json.loads(trial_path.read_text())
samples = trial['samples']
assert len(samples) == 500 and not trial['rejected'] and len(trial['owners']) == 1
consistent = [row for row in samples if row['feet_consistent'] and row['lean_consistent'] and row['camera_read_consistent']]
inconsistent = [row for row in samples if row not in consistent]
assert len(consistent) == 497 and len(inconsistent) == 3
assert all(not row['lean']['active'] and not row['lean_after']['active'] for row in inconsistent)
assert any(not row['lean']['gamepad'] for row in samples)
active = [row for row in consistent if row['lean']['active']]
assert max(row['tick_ms'] for row in inconsistent) < min(row['tick_ms'] for row in active)
assert len(active) > 100 and len({tuple(row['feet']) for row in active}) == 1
assert len({row['heading'] for row in active}) == len({row['pitch'] for row in active}) == 1
assert not any(row['lean']['active'] for row in samples[-100:])
assert all(row in consistent for row in samples[-100:])

grab = results['calibrated_grab']['result']
assert grab['avatar_fit_accepts'] == 1 and grab['avatar_fit_invalidations'] == grab['avatar_fit_event_dropped'] == 0
assert grab['native_arm_reach_clamps'] == [0, 0]
assert grab['fixture_grip_path'] == 'ordinary' and grab['fixture_ranged_frames'] == 0
assert 0 <= grab['fixture_initial_ordinary_contact_gap_native_units'] <= .3
assert grab['physical_grab_picked'] == grab['physical_grab_dropped'] == 2
assert grab['physical_grab_lost_ownership'] == 0
fit = rows('calibrated_grab', 'avatar-fit-events.csv')
accepted_index = next(i for i, row in enumerate(fit) if row['reason'] == 'fit_accepted')
assert all(row['pose_gate'] == '0' for row in fit[:accepted_index])
motor = rows('calibrated_grab', 'native-physical-grab.csv')
assert min(int(row['tick_ms']) for row in motor) > int(fit[accepted_index]['tick'])
grab_analysis = analyze(artifacts / 'runs' / names['calibrated_grab'])
assert grab_analysis['passed'] and grab_analysis['motor_samples'] == grab['physical_grab_moved']

evidence = {
    'actual_native_game': True, 'simulated_vr': True, 'results': results,
    'mouse_resume_input_bootstrap_verified': True, 'hold_lean_release_verified': True,
    'calibration_before_ordinary_contact_grab_verified': True,
    'physical_grab_analysis': grab_analysis,
    'lean_trial': str(trial_path.relative_to(root)),
    'lean_raw_samples': len(samples), 'lean_read_consistent_samples': len(consistent),
    'lean_excluded_camera_race_samples': len(inconsistent),
    'passive_lifetime_proof': 'artifacts/research/native-arm-assets/native-lean-v95-lifetime-proof.json',
    'native_carry_visual_outcome': 'Native X removed the bottle from the table and displayed Drop X / Throw RT; source capture label alone is not proof.',
    'native_carry_source_capture': 'artifacts/research/native-arm-assets/native-bottle-use-v95-20261009/capture.json',
    'headset_acceptance': False, 'full_gameplay_verified': False,
    'limits': [
        'Single nearby dynamic prop, simulated controllers; natural finger appearance and Quest comfort still need acceptance.',
        'Native carry leaves both inventory hand slots null with Empty graph tags; those tags alone do not certify an unoccupied hand.',
        'Lifecycle log capped at 256 rows after 50 complete token retirements; it does not prove full-run or world-teardown fencing.',
        'Y alone cannot bootstrap the native gamepad device flag after mouse Resume; ordinary input then fresh Y is currently required.',
        'Weapon dry oracle is diagnostic only; tracked weapons/powers, physical head translation and complete gameplay remain unfinished.',
    ],
}
(artifacts / 'development-validation-v95.json').write_text(json.dumps(evidence, indent=2) + '\n')
candidate_path = artifacts / 'demo-candidate.json'
candidate = json.loads(candidate_path.read_text())
assert candidate['launch_blocked'] and not candidate['full_demo_complete']
dll = root / 'build/d2vr_render_probe_arm_v95.dll'
candidate.update(dll_name=dll.name, sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                 state='native_input_return_lean_and_calibrated_ordinary_grab_verified_gameplay_work_active',
                 native_simulation_archive=results['calibrated_grab']['archive'],
                 launch_blocked_reason='Native input return, lean release and calibrated ordinary-contact grabbing passed. Natural hands, native carry occupancy, tracked weapons/powers, physical head movement and complete gameplay remain unfinished.')
candidate_path.write_text(json.dumps(candidate, indent=2) + '\n')
checkpoint_path = artifacts / 'development-checkpoint-2026-10-08.json'
checkpoint = json.loads(checkpoint_path.read_text())
checkpoint['active_candidate'] = candidate
checkpoint['goal_complete'] = False
checkpoint_path.write_text(json.dumps(checkpoint, indent=2) + '\n')
print('v95 input return, lean release and calibrated ordinary contact verified; complete release remains blocked.')
