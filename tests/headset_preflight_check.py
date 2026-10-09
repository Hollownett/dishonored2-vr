import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from inject_probe import wait_for_headset


class HeadsetPreflightCheck(unittest.TestCase):
    def invoke(self, results, clock_values=None):
        with patch.object(Path, 'is_file', return_value=True), patch.object(Path, 'write_text'), \
                patch('inject_probe.subprocess.run', side_effect=results) as run, \
                patch('inject_probe.time.sleep') as sleep, \
                patch('inject_probe.time.monotonic', side_effect=clock_values or [0]*10):
            output = wait_for_headset(Path('workspace'))
            for call in run.call_args_list:
                self.assertEqual(len(call.args[0]), 2)
                self.assertNotIn('--track', call.args[0])
            return output, run.call_count, sleep.call_args_list

    def test_ready_without_starting_tracking(self):
        output, calls, sleeps = self.invoke([SimpleNamespace(returncode=0, stdout='PASS', stderr='')])
        self.assertEqual((output, calls, sleeps), ('PASS', 1, []))

    def test_unavailable_retries_before_launch(self):
        output, calls, sleeps = self.invoke([SimpleNamespace(returncode=3, stdout='HEADSET_UNAVAILABLE', stderr=''),
                                            SimpleNamespace(returncode=0, stdout='PASS', stderr='')])
        self.assertEqual((output, calls), ('PASS', 2))
        self.assertEqual(sleeps[0].args, (5,))

    def test_hard_runtime_error_aborts(self):
        with self.assertRaisesRegex(RuntimeError, 'BROKEN'):
            self.invoke([SimpleNamespace(returncode=1, stdout='', stderr='BROKEN')])

    def test_unavailable_deadline_aborts(self):
        with self.assertRaises(TimeoutError):
            self.invoke([SimpleNamespace(returncode=3, stdout='HEADSET_UNAVAILABLE', stderr='')], [0, 301])


if __name__ == '__main__':
    unittest.main()
