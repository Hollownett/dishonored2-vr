import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from inject_probe import main, preserve_probe_outputs, probe_config, validate_native_left_aim


def profile(**changes):
    fields = dict(native_left_aim_experiment=True, simulated_cleanup_exception=False, simulated_gameplay_script=None, native_weapon_attachment_trace=False, paired=True, vr_preview=True, head_tracking=True,
                  controllers=True, dll_name='d2vr_render_probe_test.dll', simulated_vr=False,
                  vr_turning='snap', saved_settings=False, native_camera_trace=False,
                  controller_aim=False, physical_grab=False, until_stopped=False, seconds=30,
                  pid=123, pipeline=False, skip_temporal_aa=False, full_render_scale=False,
                  symmetric_frustum=False, headset_size=False, eye_boundary=True, native_eye_capture=True,
                  arm_upload_observer=False, optical_density=False, arm_palette_copy=False, tracked_arms=False,
                  stable_camera=False, resolution_multiplier=1.0, eye_antialiasing=False,
                  snap_degrees=45.0, smooth_degrees_per_second=90.0)
    fields.update(changes)
    return SimpleNamespace(**fields)


class NativeLeftAimProfileCheck(unittest.TestCase):
    def test_real_and_simulated_timed_profiles(self):
        for turning in ('snap', 'smooth'):
            for simulated in (False, True):
                for seconds in (10, 30):
                    validate_native_left_aim(profile(vr_turning=turning, simulated_vr=simulated, seconds=seconds), [])

    def test_each_required_guard(self):
        for change in (dict(paired=False), dict(vr_preview=False), dict(head_tracking=False),
                       dict(controllers=False), dict(dll_name=None), dict(native_eye_capture=False),
                       dict(native_camera_trace=True), dict(native_weapon_attachment_trace=True)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_native_left_aim(profile(**change))

    def test_playable_profiles_allowed(self):
        # Hand aim is part of normal play: continuous, with grab, interaction aim, saved settings, any turning.
        for change in (dict(controller_aim=True), dict(physical_grab=True), dict(until_stopped=True, seconds=None),
                       dict(saved_settings=True), dict(vr_turning='native'), dict(seconds=120)):
            with self.subTest(change=change):
                validate_native_left_aim(profile(**change), [dict(path=Path('d2vr_render_probe_arm_v66.dll'))])

    def test_default_off_does_not_change_other_profiles(self):
        validate_native_left_aim(profile(native_left_aim_experiment=False, seconds=120, until_stopped=True,
                                        vr_turning='native', native_camera_trace=True),
                                 [dict(path=Path('d2vr_render_probe.dll'))])

    def test_cli_default_is_false(self):
        import argparse
        original = argparse.ArgumentParser.parse_args
        captured = []
        class Captured(Exception):
            pass
        def parse(parser, *args, **kwargs):
            captured.append(original(parser, *args, **kwargs))
            raise Captured
        with patch.object(sys, 'argv', ['inject_probe.py', '--pid', '1']), \
                patch.object(argparse.ArgumentParser, 'parse_args', parse), self.assertRaises(Captured):
            main()
        self.assertFalse(captured[0].native_left_aim_experiment)

    def test_invalid_cli_rejected_before_dll_or_process_access(self):
        argv = ['inject_probe.py', '--pid', '1', '--paired', '--vr-preview', '--head-tracking',
                '--controllers', '--dll-name', 'd2vr_render_probe_test.dll', '--vr-turning', 'snap',
                '--native-eye-capture', '--native-left-aim-experiment', '--native-camera-trace', '--seconds', '30']
        with patch.object(sys, 'argv', argv), patch('inject_probe.PE') as pe, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            main()
        self.assertEqual(result.exception.code, 2)
        pe.assert_not_called()

    def test_append_only_config_field(self):
        expected = '123 4096 6 1 0 0 0 1 0 1 1 30 0 0 0 0 0 1.0 0 0 1 45.0 90.0 0 0 0 0 0'
        for enabled in (False, True):
            text = probe_config(profile(native_left_aim_experiment=enabled), 4096, 6, 1, 0, 0)
            self.assertEqual(text, expected+' '+str(int(enabled))+' 0 0 0\n')

    def test_base_profile_config_prefix_remains_unchanged(self):
        args=profile(native_left_aim_experiment=False,eye_boundary=False,native_eye_capture=False)
        expected='123 4096 6 1 0 0 0 1 0 0 0 30 0 0 0 0 0 1.0 0 0 1 45.0 90.0 0 0 0 0 0'
        self.assertEqual(probe_config(args,4096,6,1,0,0),expected+' 0 0 0 0\n')

    def test_archive_preserves_separate_writer_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'artifacts').mkdir()
            source = root/'artifacts'/'native-left-aim.csv'
            source.write_text('applied\n0\n', encoding='ascii')
            archive = preserve_probe_outputs(root, 'test')
            self.assertEqual((archive/source.name).read_text(), source.read_text())


if __name__ == '__main__':
    unittest.main()
