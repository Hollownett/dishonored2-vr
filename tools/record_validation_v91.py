"""Record actual native v91 outcomes; preserve the incomplete release gate."""
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
artifacts = root / 'artifacts'
runs = artifacts / 'runs'
names = {
    'death_restart': '20261009T023041.973186Z-pid44016-arm-v91',
    'required_fit_reload': '20261009T023241.155043Z-pid44016-arm-v91',
    'lean_release': '20261009T023329.572444Z-pid44016-arm-v91',
}
results = {}
for name, directory in names.items():
    value = json.loads((runs / directory / 'dxgi-probe-result.json').read_text())
    assert value['first_chance_fault_records'] == 0
    assert not value['callback_failed'] and not value['gpu_dispatch_poisoned']
    assert value['native_eye_pairs'] > 180 and value['xr_gameplay_layers'] > 180
    for key, state in value.items():
        if key.endswith(('_restored', '_hook_disabled')) or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'):
            assert state, (name, key)
    results[name] = {'archive': str((runs / directory).relative_to(root)), 'result': value}

def rows(name, filename):
    with (runs / names[name] / filename).open() as source:
        return list(csv.DictReader(source))

death = rows('death_restart', 'avatar-fit-events.csv')
masks = [int(row['logic_mask']) for row in death]
assert masks.index(8192) < masks.index(0) < masks.index(32768) < masks.index(2)
attachment = rows('death_restart', 'native-weapon-attachment.csv')
assert len(attachment) > 1000
assert all(row['receipt_matched'] == 'true' and row['receipt_reason'] == '0' and
           row['native_frame'] == row['receipt_frame'] and
           int(row['receipt_producer_caller_rva']) == 0x5508f8 and
           row['receipt_producer_stage'] == '3' for row in attachment)

fit = rows('required_fit_reload', 'avatar-fit-events.csv')
accepted_index = next(i for i, row in enumerate(fit) if row['reason'] == 'fit_accepted')
assert all(row['pose_gate'] == '0' for row in fit[:accepted_index])
assert any(row['runtime_status'] == '3' for row in fit[:accepted_index])
ordered = []
start = 0
for reason in ('fit_accepted', 'loading_suspend', 'owner_replaced', 'template_verified', 'fit_reused', 'pose_gate_updated'):
    index = next(i for i in range(start, len(fit)) if fit[i]['reason'] == reason)
    ordered.append(fit[index]); start = index + 1
accepted, suspended, replaced, verified, reused, resumed = ordered
assert accepted['left_player'] != resumed['left_player']
assert accepted['reference_epoch'] == resumed['reference_epoch'] == '1'
assert suspended['pose_gate'] == suspended['active'] == '0'
assert replaced['pose_gate'] == verified['pose_gate'] == reused['pose_gate'] == '0'
assert resumed['pose_gate'] == resumed['active'] == '1'
for side in ('left', 'right'):
    for field in ('resource_source', 'resource_primary', 'surface_identity', 'surface_count'):
        assert accepted[f'{side}_{field}'] == verified[f'{side}_{field}']
calibrated = results['required_fit_reload']['result']
assert calibrated['avatar_fit_accepts'] == calibrated['avatar_fit_reuses'] == calibrated['avatar_fit_suspensions'] == 1
assert calibrated['avatar_fit_invalidations'] == calibrated['avatar_fit_event_dropped'] == 0

trial_path = artifacts / 'native-lean-release-trial-v91-20261009.json'
trial = json.loads(trial_path.read_text())
samples = trial['samples']
assert len(samples) == 600 and not trial['rejected'] and len(trial['owners']) == 1
assert all(row['feet_consistent'] and row['lean_consistent'] and row['camera_read_consistent'] for row in samples)
assert len({tuple(row['feet']) for row in samples}) == 1
active = [row for row in samples if row['lean']['active']]
assert len(active) > 100
assert len({row['heading'] for row in active}) == len({row['pitch'] for row in active}) == 1
assert not any(row['lean']['active'] for row in samples[-100:])
assert samples[0]['aim_origin'] == samples[-1]['aim_origin']
assert max(row['aim_origin'][0] for row in samples) - min(row['aim_origin'][0] for row in samples) > 1

evidence = {
    'simulated_vr': True, 'actual_native_game': True, 'results': results,
    'required_fit_reload_events': ordered,
    'lean_trial': str(trial_path.relative_to(root)),
    'death_menu_restart_verified': True, 'required_calibration_verified': True,
    'hold_lean_release_verified': True, 'weapon_source_frame_matches_verified': True,
    'headset_acceptance': False, 'full_gameplay_verified': False,
    'limits': ['Death restart began with VR active on Game Over; a calibrated pre-death transition is separate.',
               'Lean polling is read-consistent, not an atomic engine frame snapshot.',
               'Physical head translation, natural fists, weapon retargeting and all-gameplay acceptance remain open.'],
}
(artifacts / 'development-validation-v91.json').write_text(json.dumps(evidence, indent=2) + '\n')
candidate_path = artifacts / 'demo-candidate.json'
candidate = json.loads(candidate_path.read_text())
dll = root / 'build/d2vr_render_probe_arm_v91.dll'
assert candidate['launch_blocked'] and not candidate['full_demo_complete']
candidate.update(dll_name=dll.name, sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                 state='native_death_restart_required_fit_and_lean_release_verified_gameplay_work_active',
                 native_simulation_archive=results['required_fit_reload']['archive'],
                 launch_blocked_reason='Native death-menu restart, required calibration/reload and hold-lean release passed. Physical head movement, natural hands, tracked weapons/powers and complete gameplay still need implementation and validation.')
candidate_path.write_text(json.dumps(candidate, indent=2) + '\n')
checkpoint_path = artifacts / 'development-checkpoint-2026-10-08.json'
checkpoint = json.loads(checkpoint_path.read_text())
checkpoint['active_candidate'] = candidate; checkpoint['goal_complete'] = False
checkpoint_path.write_text(json.dumps(checkpoint, indent=2) + '\n')
print('v91 native restart, required fit/reload, lean release and source-frame matches verified; release remains incomplete.')
