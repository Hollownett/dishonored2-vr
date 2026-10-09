"""Record bounded v97 checkpoint-load fit reuse; leave release candidate unchanged."""
import csv
import hashlib
import json
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
artifacts = root / 'artifacts'
run_name = '20261009T051456.874181Z-pid44016-arm-v97'
run = artifacts / 'runs' / run_name
game_sha = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
dll_sha = 'ac04427ec3bad903ce8f80d9273d1bffad1430f3797bb87dc598ba2d971b6da6'


def load(path):
    return json.loads(path.read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


config = load(run / 'probe-launch-config.json')
result = load(run / 'dxgi-probe-result.json')
provenance = load(run / 'build-source-provenance.json')
loaded = load(run / 'loaded-render-probe-arm-v97.json')
pointer_path = root / 'build/d2vr_render_probe_arm_v97.dll.source.json'
pointer = load(pointer_path)
live = load(run / 'xr-live-status.json')
with (run / 'avatar-fit-events.csv').open(newline='') as file:
    fit_events = list(csv.DictReader(file))
with (run / 'xr-lifecycle.csv').open(newline='') as file:
    lifecycle = list(csv.reader(file))

assert config['status'] == 'completed' and config['diagnostic_exit'] == 0
assert config['pid'] == result['pid'] == loaded['pid'] == live['pid'] == 44016
assert config['executable_sha256'] == game_sha
assert config['dll_name'] == provenance['dll_name'] == 'd2vr_render_probe_arm_v97.dll'
assert config['dll_sha256'] == loaded['sha256'] == provenance['dll_sha256'] == dll_sha
assert pointer['dll_sha256'] == provenance['dll_sha256']
assert pointer['source_manifest_sha256'] == provenance['source_manifest_sha256']
assert pointer['source_zip_sha256'] == provenance['source_zip_sha256']
assert config['tracked_arms_requested'] is True and config['arm_palette_copy_requested'] is False
assert provenance['state'] == 'complete' and provenance['source_unchanged_during_build'] is True
assert provenance['compiled_from_frozen_snapshot'] is False
assert sha(run / 'build-source-manifest.json') == provenance['source_manifest_sha256']
assert sha(run / 'build-source.zip') == provenance['source_zip_sha256']
with zipfile.ZipFile(run / 'build-source.zip') as archive:
    assert archive.testzip() is None

accepted = next(row for row in fit_events if row['reason'] == 'fit_accepted')
pause = next(row for row in fit_events if row['tick'] == '81252828')
suspend = next(row for row in fit_events if row['reason'] == 'loading_suspend')
new_owner = next(row for row in fit_events if row['reason'] == 'owner_replaced'
                 and row['left_player'] == '2385959497984')
new_template = next(row for row in fit_events if row['reason'] == 'template_verified'
                   and row['left_player'] == '2385959497984')
reused = next(row for row in fit_events if row['reason'] == 'fit_reused')
gate = next(row for row in fit_events if row['reason'] == 'pose_gate_updated'
            and row['tick'] == reused['tick'] and row['left_player'] == reused['left_player'])
assert accepted['tick'] == '81244609' and accepted['accepted'] == '1'
assert accepted['left_player'] == accepted['right_player'] == '2385959389568'
assert pause['logic_mask'] == '4' and pause['pose_gate'] == '1' and int(pause['tick']) > int(accepted['tick'])
assert suspend['tick'] == '81274000' and suspend['pose_gate'] == '0'
assert new_owner['tick'] == '81275937' and new_owner['logic_mask'] == '2'
assert new_owner['left_player'] == new_owner['right_player'] == '2385959497984'
assert new_template['tick'] == reused['tick'] == gate['tick'] == '81275953'
assert new_template['template_verified'] == '1'
assert reused['reference_epoch'] == accepted['reference_epoch'] == '1'
assert reused['pose_epoch'] == accepted['pose_epoch'] == '1'
assert reused['accepted_epoch'] == accepted['accepted_epoch'] == '1'
assert reused['accepted'] == reused['active'] == '1' and reused['reuses'] == '1'
assert gate['pose_gate'] == '1' and gate['active'] == '1'
assert not any(row['logic_mask'] in ('8192', '8200') for row in fit_events)

assert result['avatar_fit_accepts'] == result['avatar_fit_reuses'] == 1
assert result['avatar_fit_invalidations'] == 0 and result['avatar_fit_suspensions'] == 1
assert result['avatar_fit_active_at_stop'] is True and result['avatar_pose_gate_at_stop'] is True
assert result['avatar_calibration_applied'] is True
assert result['native_arm_pose_calls'] == [3116, 3116]
assert result['native_arm_copy_calls'] == [0, 0] and result['native_arm_reach_clamps'] == [0, 0]
assert result['native_eye_pairs'] == result['xr_gameplay_layers'] == 3558
assert result['first_chance_fault_records'] == 0 and result['callback_failed'] is False
assert result['playable_verified'] is False and result['roomscale_translation_applied'] is False
assert live['gameplay'] is True and live['headset_scene_visually_verified'] is False
restore_checks = {key: value for key, value in result.items()
                  if key.endswith(('_restored', '_hook_disabled'))
                  or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown')}
assert all(value is True for value in restore_checks.values()), restore_checks
restore_keys = sorted(restore_checks)
assert len(restore_keys) == 23

candidate = load(artifacts / 'demo-candidate.json')
assert candidate['dll_name'] == 'd2vr_render_probe_arm_v96.dll'
assert candidate['launch_blocked'] is True and candidate['full_demo_complete'] is False
assert candidate['headset_acceptance'] is False and candidate['default_tracked_arms'] is False
v96 = load(artifacts / 'development-validation-v96.json')
assert v96['death_reload_continuity']['failed_restart_interval']['calibration_accepts'] == 0

evidence = {
    'schema': 'd2vr-development-validation-v97-v1',
    'actual_native_game': True,
    'simulated_vr': True,
    'candidate_dll_tested': {'name': config['dll_name'], 'sha256': dll_sha,
        'archive': f'artifacts/runs/{run_name}',
        'source_provenance': str(pointer_path.relative_to(root)),
        'source_unchanged_during_build': True, 'compiled_from_frozen_snapshot': False,
        'source_manifest_sha256': provenance['source_manifest_sha256'],
        'source_zip_sha256': provenance['source_zip_sha256']},
    'launcher': {'status': config['status'], 'diagnostic_exit': 0, 'pid': config['pid'],
        'game_sha256': game_sha, 'tracked_arms_requested': True,
        'arm_palette_copy_requested': False},
    'calibrated_checkpoint_fit_reuse': {
        'accepted_fit': {'tick': int(accepted['tick']), 'player': int(accepted['left_player']),
            'reference_epoch': int(accepted['reference_epoch']), 'pose_epoch': int(accepted['pose_epoch'])},
        'pause_menu_state': {'tick': int(pause['tick']), 'logic_mask': int(pause['logic_mask'])},
        'loading_suspend_tick': int(suspend['tick']),
        'reloaded_player': int(new_owner['left_player']),
        'new_player_template_verified': True,
        'fit_reuse_tick': int(reused['tick']),
        'reused_reference_epoch': int(reused['reference_epoch']),
        'reused_pose_epoch': int(reused['pose_epoch']),
        'accepted_epoch_preserved': int(reused['accepted_epoch']),
        'pose_gate_active_after_reuse': gate['pose_gate'] == '1' and gate['active'] == '1',
        'fit_invalidations': result['avatar_fit_invalidations'],
        'fit_suspensions': result['avatar_fit_suspensions'],
        'operator_reported_save': 'Preserved Sword autosave dated 03:33:51 selected via Pause > Load.',
        'archive_limit': 'The CSV records Pause-menu logic, loading, owner replacement and fit reuse; it does not encode the displayed save label or prove a forced-death restart.',
    },
    'runtime_diagnostics': {'native_eye_pairs': result['native_eye_pairs'],
        'xr_gameplay_layers': result['xr_gameplay_layers'],
        'tracked_arm_pose_calls': result['native_arm_pose_calls'],
        'palette_copy_calls': result['native_arm_copy_calls'],
        'reach_clamps': result['native_arm_reach_clamps'],
        'fault_records': 0, 'callback_failed': False,
        'restoration_and_hook_disable_checks_true': len(restore_keys),
        'gameplay_alive_at_stop': live['gameplay'],
        'playable_verified': False, 'roomscale_translation_applied': False,
        'headset_visual_acceptance': live['headset_scene_visually_verified']},
    'prior_v96_death_reload_evidence': {
        'record': 'artifacts/development-validation-v96.json',
        'scope': v96['death_reload_continuity'],
        'limit': 'The v96 death interval begins after death and had no accepted calibrated fit; v97 is a separate Pause/load checkpoint trial and contains no forced death.',
    },
    'candidate_advanced': False,
    'candidate_release_gates': {'dll_name': candidate['dll_name'],
        'launch_blocked': True, 'full_demo_complete': False,
        'headset_acceptance': False, 'default_tracked_arms': False},
    'limits': ['This establishes fit reuse across the recorded Pause/load owner replacement in one reference epoch, not death/restart continuity.',
        'Native-arm output counters do not establish Quest visual quality, comfort, natural hands, or complete gameplay.',
        'Roomscale head translation remains unapplied; fit reuse is not whole-game acceptance.'],
}

validation_path = artifacts / 'development-validation-v97.json'
assert not validation_path.exists(), 'Refuse overwrite existing v97 validation artifact'
validation_path.write_text(json.dumps(evidence, indent=2) + '\n')
report = f"""# v97 calibrated checkpoint fit reuse

The completed v97 archive `{run_name}` used DLL SHA-256 `{dll_sha}` with archived source provenance and matching loaded-DLL identity. Its diagnostics exited 0 with zero fault records, no callback failure, {result['native_eye_pairs']} native eye pairs/gameplay layers, tracked-arm pose calls {result['native_arm_pose_calls']}, zero reach clamps, and all {len(restore_keys)} restoration/hook checks true.

The fit event log records acceptance at tick 81244609 for player 2385959389568, then Pause-menu logic at tick 81252828, loading suspension, and a new player 2385959497984. That player's template verifies and the prior fit is reused at tick 81275953 with reference epoch 1, pose epoch 1, accepted epoch 1, and pose gate active. No death-state mask appears in this archive, so this does not establish pre-death fit continuity or death/restart behavior. The selected 03:33:51 Sword autosave is operator-reported context; the CSV does not label the displayed save.

The game remained in gameplay at stop, but `playable_verified` and headset visual acceptance remain false. The separate v96 death-run limit is preserved in the machine record. The release candidate remains v96 and launch-blocked.

Machine-readable record: `artifacts/development-validation-v97.json`.
"""
report_path = artifacts / 'v97-calibration-reuse-audit-20261009.md'
assert not report_path.exists(), 'Refuse overwrite existing v97 audit report'
report_path.write_text(report)
print('v97 checkpoint fit reuse recorded; release candidate remains unchanged.')
