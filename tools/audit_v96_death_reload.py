"""Record bounded v96 death/reload continuity evidence from archived files."""
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
artifacts = root / 'artifacts'
failed_name = '20261009T043813.501232Z-pid44016-arm-v96-failed'
ready_name = '20261009T043935.687941Z-pid44016-arm-v96'
failed_dir = artifacts / 'runs' / failed_name
ready_dir = artifacts / 'runs' / ready_name
expected_game = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
expected_dll = '40e92ab56aa9332b79372122ac54bb437d5f49f68bf7fdf4641624e3924a26e7'


def load(path):
    return json.loads(path.read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_csv(path):
    with path.open(newline='') as file:
        return list(csv.DictReader(file))


failed_config = load(failed_dir / 'probe-launch-config.json')
failed = load(failed_dir / 'dxgi-probe-result.json')
fit = read_csv(failed_dir / 'avatar-fit-events.csv')
ready_config = load(ready_dir / 'probe-launch-config.json')
ready = load(ready_dir / 'dxgi-probe-result.json')
position_path = artifacts / 'native-sword-reloaded-position-v96-20261009.json'
position = load(position_path)
position_samples = position['samples']

# Confirm the transitions, the expected arm-capability failure and clean teardown.
assert failed_config['status'] == 'diagnostic_failed' and failed_config['diagnostic_exit'] == 4
assert failed_config['pid'] == failed['pid'] == 44016
assert failed_config['executable_sha256'] == expected_game
assert failed_config['dll_sha256'] == expected_dll
assert 90 <= failed_config['ended_unix'] - failed_config['started_unix'] <= 93
assert failed_config['tracked_arms_requested'] is True
assert fit[0]['logic_mask'] == '8192'
assert any(row['reason'] == 'loading_suspend' for row in fit)
assert any(row['logic_mask'] == '32768' for row in fit)
resumed = next(row for row in fit if row['reason'] == 'owner_replaced')
assert resumed['logic_mask'] == '2' and resumed['left_player'] == '2385959468352'
template = next(row for row in fit if row['reason'] == 'template_verified' and row['left_player'] == resumed['left_player'])
assert template['logic_mask'] == '2' and template['template_verified'] == '1'
assert any(row['logic_mask'] == '8192' and row['tick'] > template['tick'] for row in fit)
assert failed['native_eye_pairs'] == failed['xr_gameplay_layers'] == failed['tracked_gameplay_transfer_frames'] == 1521
assert failed['avatar_fit_accepts'] == 0 and failed['avatar_calibration_applied'] is False
assert failed['native_arm_pose_calls'] == [0, 0] and failed['native_arm_copy_calls'] == [0, 0]
assert failed['first_chance_fault_records'] == 0 and failed['callback_failed'] is False

restored_keys = sorted(key for key, value in failed.items()
                       if (key.endswith(('_restored', '_hook_disabled'))
                           or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'))
                       and value is True)
assert len(restored_keys) == 23, restored_keys

# Confirm a separate clean XR session and a timestamped, read-only position sample.
assert ready_config['status'] == 'completed' and ready_config['diagnostic_exit'] == 0
assert ready_config['pid'] == ready['pid'] == position['pid'] == 44016
assert ready_config['executable_sha256'] == position['executable_sha256'] == expected_game
assert ready_config['dll_sha256'] == expected_dll
assert ready_config['tracked_arms_requested'] is False
assert ready['native_eye_pairs'] == ready['xr_gameplay_layers'] == 311
assert ready['first_chance_fault_records'] == 0 and ready['callback_failed'] is False
assert ready['native_arm_pose_calls'] == [0, 0]
for key, value in ready.items():
    if key.endswith(('_restored', '_hook_disabled')) or key in ('hook_disabled', 'gpu_hooks_disabled', 'xr_render_thread_shutdown'):
        assert value is True, ('ready', key, value)
assert position['game_memory_writes'] is False and position['engine_calls'] is False
assert position['frame_synchronised'] is False and len(position_samples) == 4
feet = [tuple(row['feet']) for row in position_samples]
assert len(set(feet)) == 1 and all(row['feet_consistent'] and row['camera_read_consistent'] for row in position_samples)
assert feet[0] == (-39.19350051879883, -161.55999755859375, 66.40364837646484)
position_unix = __import__('datetime').datetime.fromisoformat(position['time_utc'].replace('Z', '+00:00')).timestamp()
assert ready_config['started_unix'] <= position_unix <= ready_config['ended_unix']

# The exit predicate makes the tracked-arm zero-pose condition an expected
# diagnostic failure, independent of the clean teardown evidence.
source_lines = (root / 'src/dxgi_probe.cpp').read_text().splitlines()
guard_line = next(index + 1 for index, line in enumerate(source_lines)
                  if '(!tracked_arms_requested || (arm_pose_calls[0].load() && arm_pose_calls[1].load()))' in line)
assert guard_line == 3032

proof = {
    'schema': 'd2vr-v96-death-reload-audit-v1',
    'failed_restart_interval': {
        'archive': f'artifacts/runs/{failed_name}',
        'launch_config_sha256': sha(failed_dir / 'probe-launch-config.json'),
        'fit_events_sha256': sha(failed_dir / 'avatar-fit-events.csv'),
        'result_sha256': sha(failed_dir / 'dxgi-probe-result.json'),
        'duration_seconds': round(failed_config['ended_unix'] - failed_config['started_unix'], 3),
        'diagnostic_exit': 4,
        'tracked_arms_requested': True,
        'logic_masks_and_owner_timeline': [
            {'logic_mask': 8192, 'meaning': 'death state already present at first sample'},
            {'event': 'loading_suspend'},
            {'logic_mask': 32768, 'meaning': 'loading'},
            {'logic_mask': 2, 'event': 'owner_replaced', 'player': int(resumed['left_player'])},
            {'logic_mask': 2, 'event': 'template_verified', 'player': int(template['left_player'])},
            {'logic_mask': 8192, 'meaning': 'later death state observed'},
        ],
        'gameplay_eye_pairs_and_layers': 1521,
        'calibration_accepts': 0,
        'calibration_applied': False,
        'pose_calls': [0, 0],
        'copy_calls': [0, 0],
        'fault_records': 0,
        'callback_failed': False,
        'restoration_or_hook_disable_checks_true': len(restored_keys),
        'source_exit_guard': f'src/dxgi_probe.cpp:{guard_line}',
        'interpretation': 'XR rendering survived a death/loading/new-player gameplay interval, then returned to death/loading. This archive starts while the death state is already active; it does not establish pre-death calibrated-fit continuity. Exit 4 is expected because tracked arms were requested but neither arm produced a native pose.',
    },
    'separate_ready_gameplay_pause_session': {
        'archive': f'artifacts/runs/{ready_name}',
        'launch_config_sha256': sha(ready_dir / 'probe-launch-config.json'),
        'result_sha256': sha(ready_dir / 'dxgi-probe-result.json'),
        'diagnostic_exit': 0,
        'native_eye_pairs_and_gameplay_layers': 311,
        'fault_records': 0,
        'callback_failed': False,
        'tracked_arms_requested': False,
        'restoration_verified': True,
        'interpretation': 'A separate short clean XR session with Ready/gameplay/menu activity; no calibration or tracked-arm pose was accepted.',
    },
    'post_reload_position_read': {
        'capture': str(position_path.relative_to(root)),
        'time_utc': position['time_utc'],
        'samples': len(position_samples),
        'feet_native_units': list(feet[0]),
        'reads_consistent': True,
        'game_memory_writes': False,
        'engine_calls': False,
        'frame_synchronised': False,
        'interpretation': 'Four consistent feet-position reads at the recorded former Sword location during the separate clean session; this does not establish Sword object identity or calibrated-fit continuity.',
    },
    'separate_context': {
        'keyhole_presentation_proof': 'artifacts/native-keyhole-presentation-proof-v96-20261009.json',
        'combined_interrupted_run': 'artifacts/runs/20261009T043557.065944Z-pid44016-arm-v96',
        'head_motion_claim': False,
    },
    'headset_acceptance': False,
    'launch_blocked': True,
    'full_gameplay_verified': False,
    'limits': [
        'The restart interval begins at death; there is no pre-death calibrated fit in that interval.',
        'The later death/loading transition is not a successful second checkpoint-load result.',
        'The separate position sample is unsynchronised and reads feet position, not weapon identity.',
        'Neither archive is headset comfort, visual acceptance or complete gameplay proof.',
    ],
}

# Outputs are written only after every archive assertion passes.
proof_path = artifacts / 'v96-death-reload-audit-20261009.json'
proof_path.write_text(json.dumps(proof, indent=2) + '\n')
validation_path = artifacts / 'development-validation-v96.json'
validation = load(validation_path)
assert validation['launch_blocked'] is True and validation['headset_acceptance'] is False
assert validation['full_gameplay_verified'] is False
validation['death_reload_continuity'] = proof
validation_path.write_text(json.dumps(validation, indent=2) + '\n')

report = f"""# v96 death and reload continuity audit

The {failed_name} archive records an XR interval of {proof['failed_restart_interval']['duration_seconds']} seconds. Its first fit event is already in death logic mask `8192`; it then records `loading_suspend`, loading mask `32768`, gameplay mask `2`, replacement player `{resumed['left_player']}`, and a verified template for that new player. It later returns to death masks `8192`/`8200`. The run recorded {failed['native_eye_pairs']} native eye pairs and gameplay layers.

This is bounded death/loading/new-player continuity evidence, not a clean overall pass. The requested tracked-arm path had zero accepts, no applied calibration, and `[0, 0]` pose calls. The source exit predicate at [dxgi_probe.cpp:{guard_line}](/C:/Users/hollo/Documents/ChatGPT/VR%20mods/src/dxgi_probe.cpp:{guard_line}) returns failure when tracked arms are requested without pose calls for both arms, explaining diagnostic exit 4. The archive reports zero fault records, no callback failure, and {len(restored_keys)} restoration/hook-disable checks true. Because sampling began on Game Over, it cannot prove a calibrated fit survived a pre-death transition.

A separate {ready_name} session completed with exit 0, 311 native eye pairs/gameplay layers, zero faults and clean restoration, with tracked arms disabled. The position sampler recorded four consistent reads at `[-39.1935, -161.56, 66.40365]` native units during that session. It is an unsynchronised feet-position read and does not identify a weapon or prove fit reuse.

The keyhole presentation proof is a separate archive. The combined 043557 run is not used as head-motion evidence. Headset acceptance and full gameplay remain unverified; the candidate stays launch-blocked.

Machine-readable evidence: `artifacts/v96-death-reload-audit-20261009.json` and the `death_reload_continuity` field in `artifacts/development-validation-v96.json`.
"""
(artifacts / 'v96-death-reload-audit-20261009.md').write_text(report)
print('v96 death/reload evidence recorded with calibration and headset limits.')
