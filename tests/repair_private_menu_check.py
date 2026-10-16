import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / 'tools'))
SPEC = importlib.util.spec_from_file_location('integrated_repair', ROOT / 'tools/repair_private_check.py')
REPAIR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPAIR)


class MenuSmokeTests(unittest.TestCase):
    def test_legacy_scenarios_keep_template_and_duration_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            candidate = Path(folder) / 'd2vr_render_probe_arm_v999.dll'
            candidate.touch()
            for scenario in ('smoke', 'reload'):
                with self.subTest(scenario=scenario), patch.object(REPAIR.native, 'main', return_value=7) as prepare:
                    with contextlib.redirect_stdout(io.StringIO()):
                        result = REPAIR.main(['--dll', str(candidate), '--save-template', folder,
                                              '--scenario', scenario, '--inject-loading'])
                    self.assertEqual(result, 7)
                    prepare.assert_called_once()

    def test_argument_rejections_precede_preparation(self):
        for arguments in (['--scenario', 'menu-smoke', '--save-template', 'retail'],
                          ['--scenario', 'menu-smoke', '--inject-loading'],
                          ['--scenario', 'menu-smoke', '--seconds', '31'],
                          ['--scenario', 'menu-smoke', '--seconds', '29'],
                          ['--scenario', 'smoke'], ['--scenario', 'reload']):
            with self.subTest(arguments=arguments), patch.object(REPAIR.native, 'main') as prepare:
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    REPAIR.main(['--dll', 'unused_v999.dll', *arguments])
                prepare.assert_not_called()

    def run_owned(self, restoration_ok=True, extra_launch=False, provenance=True, extra_game=False,
                  guard_failure=False, never_restore=False, late_start=False):
        with tempfile.TemporaryDirectory() as folder, contextlib.ExitStack() as stack:
            root = Path(folder)
            run, stage, game = root / 'run', root / 'stage', root / 'game'
            for directory in (run, game, stage / 'fs-save/base'):
                directory.mkdir(parents=True)
            (stage / 'fs-save/base/dishonored2Config.cfg').touch()
            candidate = root / 'd2vr_render_probe_arm_v999.dll'
            candidate.touch()
            preparation = dict(run_dir=str(run), stage=str(stage), game_source=str(game),
                               shared_save_roots=[str(root / 'protected')], files={'Dishonored2.exe': 'digest'},
                               expected_native_values={'guard': True}, nonce='owned',
                               command=['owned-launcher', '--native-fresh', str(game), 'owned-child.pid'])
            if extra_launch:
                preparation['command'].append('--load-slot-one')
            preparation_path = run / 'native-results.json'
            preparation_path.write_text(json.dumps(preparation))
            clock = [0.0]
            processes = []
            commands = []
            probe = run / 'probe-root'

            def workspace(*unused):
                (probe / 'artifacts').mkdir(parents=True)
                return probe, candidate

            class Process:
                def __init__(self, injector):
                    self.injector, self.alive, self.returncode, self.pid = injector, True, None, 123
                    self.ready = clock[0] + 1

                def poll(self):
                    if self.injector and self.alive and not never_restore and clock[0] >= self.ready:
                        restoration = {key: True for key in (
                            'hook_disabled', 'backend_hook_disabled', 'mode_restored', 'values_restored',
                            'body_material_restored', 'camera_comfort_restored', 'controller_import_restored',
                            'native_joystick_focus_restored', 'temporal_aa_restored', 'cached_output_restored')}
                        restoration.update(first_chance_fault_records=0, recoverable_faults=0, xr_recoveries=0)
                        restoration['mode_restored'] = restoration_ok
                        result = probe / 'artifacts/dxgi-probe-result.json'
                        result.write_text(json.dumps(restoration))
                        os.utime(result, (REPAIR.time.time() + 1, REPAIR.time.time() + 1))
                        (probe / 'artifacts/loaded-render-probe-arm-v999.json').write_text(
                            json.dumps({'pid': 42, 'sha256': 'digest'}))
                        self.alive, self.returncode = False, 0
                    return None if self.alive else self.returncode

                def kill(self):
                    self.alive, self.returncode = False, -1

                def wait(self, timeout):
                    if self.alive:
                        raise subprocess.TimeoutExpired('mock-owned', timeout)
                    return self.returncode

            def popen(command, **unused):
                commands.append(command)
                process = Process(len(processes) == 1)
                processes.append(process)
                if not process.injector:
                    (stage / 'owned-child.pid').write_text('42')
                return process

            reader = Mock()
            reader.games.side_effect = lambda: ({42, 99} if extra_game else {42}) if processes and processes[0].alive else set()
            reader.module.return_value = (100, stage / 'Dishonored2.exe', 0x41cb000)
            reader.values.return_value = {'guard': True}

            def guards(values, expected, valid, mismatch, now):
                if guard_failure:
                    raise RuntimeError('Native isolation guard changed')
                return (valid + 1 if not late_start or clock[0] >= 6 else 0), None

            replacements = [
                patch.object(REPAIR.native, 'main', side_effect=lambda unused: print('Native preparation/startup report: ' + str(preparation_path)) or 0),
                patch.object(REPAIR.native, 'NativeReader', return_value=reader),
                patch.object(REPAIR.native, 'sha', return_value='digest'),
                patch.object(REPAIR.native, 'PIN', 'digest'),
                patch.object(REPAIR.native, 'saved_snapshot', return_value={'protected': 'digest'}),
                patch.object(REPAIR.native, 'advance_guards', side_effect=guards),
                patch.object(REPAIR.native, 'live_guard', return_value={}),
                patch.object(REPAIR, 'matching_provenance', return_value={'pointer': candidate} if provenance else None),
                patch.object(REPAIR, 'probe_workspace', side_effect=workspace),
                patch.object(REPAIR, 'read_memory', return_value=(1, Mock())),
                patch.object(REPAIR, 'game_logic_mask', return_value=64),
                patch.object(REPAIR, 'stage_template', side_effect=AssertionError('No template reads allowed')),
                patch.object(REPAIR, 'private_menu_key', side_effect=AssertionError('No desktop input allowed')),
                patch.object(REPAIR.subprocess, 'Popen', side_effect=popen),
                patch.object(REPAIR.time, 'monotonic', side_effect=lambda: clock[0]),
                patch.object(REPAIR.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)),
            ]
            for replacement in replacements:
                stack.enter_context(replacement)
            with contextlib.redirect_stdout(io.StringIO()):
                result = REPAIR.main(['--dll', str(candidate), '--game', str(game), '--scenario', 'menu-smoke', '--launch'])
            report = json.loads((run / 'repair-results.json').read_text())
            self.assertTrue(all(not process.alive for process in processes))
            self.assertLessEqual(clock[0], 30)
            self.assertEqual(report['commands'], [])
            self.assertFalse(report['desktop_input_used'])
            self.assertTrue(report['shared_saves_unchanged'])
            self.assertTrue(report['owned_pid_stopped'])
            return result, report, commands

    def test_menu_only_candidate_restores_and_stops_without_gameplay(self):
        result, report, commands = self.run_owned()
        self.assertEqual(result, 0)
        self.assertTrue(report['passed'])
        self.assertTrue(report['candidate_menu_startup_verified'])
        self.assertFalse(report['native_gameplay_render_verified'])
        self.assertNotIn('smoke_gameplay_verified', report)
        self.assertNotIn('--load-slot-one', commands[0])
        self.assertNotIn('--simulated-input', commands[1])
        self.assertNotIn('--simulated-gameplay-script', commands[1])
        self.assertIn('--simulated-vr', commands[1])
        self.assertEqual(commands[1][commands[1].index('--pid') + 1], '42')
        self.assertEqual(commands[1][-2:], ['--seconds', '10'])

    def test_restoration_failure_is_not_success(self):
        result, report, unused = self.run_owned(restoration_ok=False)
        self.assertEqual(result, 1)
        self.assertFalse(report['passed'])

    def test_loading_launcher_rejected_before_launch(self):
        result, report, commands = self.run_owned(extra_launch=True)
        self.assertEqual(result, 1)
        self.assertEqual(commands, [])

    def test_missing_provenance_rejected_before_launch(self):
        result, report, commands = self.run_owned(provenance=False)
        self.assertEqual(result, 1)
        self.assertEqual(commands, [])

    def test_unowned_game_rejected_and_only_owned_job_stopped(self):
        result, report, commands = self.run_owned(extra_game=True)
        self.assertEqual(result, 1)
        self.assertEqual(len(commands), 1)

    def test_native_guard_failure_never_injects(self):
        result, report, commands = self.run_owned(guard_failure=True)
        self.assertEqual(result, 1)
        self.assertEqual(len(commands), 1)

    def test_injector_timeout_stops_only_owned_processes_within_budget(self):
        result, report, commands = self.run_owned(never_restore=True)
        self.assertEqual(result, 1)
        self.assertEqual(len(commands), 2)
        self.assertFalse(report.get('restoration_verified', False))

    def test_late_native_start_refuses_to_extend_session(self):
        result, report, commands = self.run_owned(late_start=True)
        self.assertEqual(result, 1)
        self.assertEqual(len(commands), 1)


if __name__ == '__main__':
    unittest.main()
