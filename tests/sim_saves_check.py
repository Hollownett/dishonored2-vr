"""Validator checks for the Tier B simulated-VR test saves."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

_PATH = Path(__file__).resolve().parents[1] / 'tools' / 'linux' / 'check_test_saves.py'
_SPEC = importlib.util.spec_from_file_location('check_test_saves', _PATH)
check_test_saves = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(check_test_saves)


class SimSavesCheck(unittest.TestCase):
    def test_empty_dir_reports_missing_slot_and_profile(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(len(check_test_saves.check_save_dir(Path(d))), 2)

    def test_not_a_directory_reports_one_problem(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(len(check_test_saves.check_save_dir(Path(d) / 'nope')), 1)

    def test_slot_and_profile_ok(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'SLOT0').mkdir()
            (Path(d) / 'profile.bin').write_bytes(b'x')
            self.assertEqual(check_test_saves.check_save_dir(Path(d)), [])

    def test_slot_without_profile(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'SLOT0').mkdir()
            self.assertEqual(len(check_test_saves.check_save_dir(Path(d))), 1)


if __name__ == '__main__':
    unittest.main()
