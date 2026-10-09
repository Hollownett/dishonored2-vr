import argparse
import contextlib
import io
import itertools
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_weapon_attachment_profile import validate_native_weapon_attachment
from inject_probe import main,probe_config,preserve_probe_outputs,validate_native_left_aim
from native_left_aim_profile_check import profile


def attachment_profile(**changes):
    fields=dict(native_weapon_attachment_trace=True,native_left_aim_experiment=False,arm_upload_observer=True)
    fields.update(changes)
    return profile(**fields)


class NativeAttachmentProfileCheck(unittest.TestCase):
    def test_default_off_and_feature_combinations(self):
        validate_native_weapon_attachment(profile(seconds=0,paired=False))
        for simulated,arms,grab in itertools.product((False,True),repeat=3):
            with self.subTest(simulated=simulated,arms=arms,grab=grab):
                validate_native_weapon_attachment(attachment_profile(simulated_vr=simulated,tracked_arms=arms,physical_grab=grab))
        validate_native_weapon_attachment(attachment_profile(simulated_vr=True,simulated_input=True,
            simulated_gameplay_script=Path('story.json'),tracked_arms=True,controller_aim=True))

    def test_timed_ownership_and_writer_exclusions(self):
        for change in (dict(paired=False),dict(vr_preview=False),dict(native_eye_capture=False),dict(head_tracking=False),
                       dict(controllers=False),dict(dll_name=None),dict(until_stopped=True),dict(seconds=9),dict(seconds=121),
                       dict(native_left_aim_experiment=True),dict(saved_settings=True),dict(simulated_cleanup_exception=True)):
            with self.subTest(change=change),self.assertRaises(ValueError):validate_native_weapon_attachment(attachment_profile(**change))
        with self.assertRaises(ValueError):validate_native_left_aim(attachment_profile(native_left_aim_experiment=True))

    def test_append_only_index31(self):
        on=probe_config(attachment_profile(),4096,6,1,0,0).split()
        off=probe_config(attachment_profile(native_weapon_attachment_trace=False),4096,6,1,0,0).split()
        self.assertEqual(len(on),32);self.assertEqual(on[:31],off[:31]);self.assertEqual((on[31],off[31]),('1','0'))

    def test_cli_default_off(self):
        original=argparse.ArgumentParser.parse_args;captured=[]
        class Captured(Exception):pass
        def parse(parser,*args,**kwargs):captured.append(original(parser,*args,**kwargs));raise Captured
        with patch.object(sys,'argv',['inject_probe.py','--pid','1']),patch.object(argparse.ArgumentParser,'parse_args',parse),self.assertRaises(Captured):main()
        self.assertFalse(captured[0].native_weapon_attachment_trace)

    def test_invalid_cli_precedes_process_access(self):
        argv=['inject_probe.py','--pid','1','--paired','--vr-preview','--head-tracking','--controllers',
              '--native-eye-capture','--dll-name','d2vr_render_probe_test.dll','--native-weapon-attachment-trace','--seconds','9']
        with patch.object(sys,'argv',argv),patch('inject_probe.PE') as pe,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):main()
        pe.assert_not_called()

    def test_csv_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'artifacts').mkdir();(root/'artifacts/native-weapon-attachment.csv').write_text('passive only')
            archive=preserve_probe_outputs(root,'attachment')
            self.assertEqual((archive/'native-weapon-attachment.csv').read_text(),'passive only')

if __name__=='__main__':unittest.main()
