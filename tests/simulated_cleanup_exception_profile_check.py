import argparse
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from inject_probe import main, preserve_probe_outputs, probe_config, validate_simulated_cleanup_exception
from native_left_aim_profile_check import profile


def cleanup_profile(**changes):
    fields=dict(native_left_aim_experiment=False, simulated_cleanup_exception=True, simulated_vr=True)
    fields.update(changes)
    return profile(**fields)


class CleanupExceptionProfileCheck(unittest.TestCase):
    def test_valid_and_default_off(self):
        for seconds in (10,30,120):
            validate_simulated_cleanup_exception(cleanup_profile(seconds=seconds))
        validate_simulated_cleanup_exception(cleanup_profile(simulated_cleanup_exception=False, simulated_vr=False,
                                            controllers=False, seconds=0, until_stopped=True, saved_settings=True))

    def test_each_required_guard(self):
        for change in (dict(simulated_vr=False), dict(vr_preview=False), dict(paired=False),
                       dict(native_eye_capture=False), dict(head_tracking=False), dict(controllers=False),
                       dict(dll_name=None), dict(until_stopped=True), dict(seconds=0), dict(seconds=9),
                       dict(seconds=121), dict(native_left_aim_experiment=True), dict(saved_settings=True)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_simulated_cleanup_exception(cleanup_profile(**change))

    def test_append_only_index_29(self):
        off=probe_config(cleanup_profile(simulated_cleanup_exception=False),4096,6,1,0,1).split()
        on=probe_config(cleanup_profile(),4096,6,1,0,1).split()
        self.assertEqual(len(on),32)
        self.assertEqual(off[:29],on[:29])
        self.assertEqual(off[29],'0')
        self.assertEqual(on[29],'1')

    def test_cli_default_off(self):
        original=argparse.ArgumentParser.parse_args
        captured=[]
        class Captured(Exception): pass
        def parse(parser,*args,**kwargs):
            captured.append(original(parser,*args,**kwargs));raise Captured
        with patch.object(sys,'argv',['inject_probe.py','--pid','1']), \
                patch.object(argparse.ArgumentParser,'parse_args',parse), self.assertRaises(Captured):
            main()
        self.assertFalse(captured[0].simulated_cleanup_exception)

    def test_invalid_cli_rejected_before_process_or_dll_access(self):
        argv=['inject_probe.py','--pid','1','--paired','--vr-preview','--head-tracking','--controllers',
              '--dll-name','d2vr_render_probe_test.dll','--simulated-vr','--simulated-cleanup-exception']
        with patch.object(sys,'argv',argv),patch('inject_probe.PE') as pe, \
                contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as result:
            main()
        self.assertEqual(result.exception.code,2)
        pe.assert_not_called()

    def test_cleanup_report_archived(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'artifacts').mkdir()
            source=root/'artifacts'/'simulated-cleanup-exception.json'
            source.write_text('{"expected_diagnostic_exit":1,"resources_reusable":true}\n')
            archive=preserve_probe_outputs(root,'cleanup')
            self.assertEqual((archive/source.name).read_text(),source.read_text())


if __name__=='__main__': unittest.main()
