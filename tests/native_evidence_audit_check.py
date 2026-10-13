"""Checks the evidence tool's trust boundaries and independently known pose math."""
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("audit_native_evidence", Path(__file__).resolve().parents[1] / "tools/audit_native_evidence.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class NativeEvidenceCheck(unittest.TestCase):
    def test_hidden_wheel_and_native_copy_are_not_tracking_error(self):
        row = {"gate": 0, "publication_consistent": True, "replaced": True,
               "palette_valid": True, "consumer_landmarks": {"valid": True}}
        self.assertTrue(audit.tracked_palette_metric(row))
        for gate in (1, 3, 5):
            row["gate"] = gate
            self.assertFalse(audit.tracked_palette_metric(row))
        row["gate"] = 0
        row["publication_consistent"] = False
        self.assertFalse(audit.tracked_palette_metric(row))

    def test_rigid_composition_and_quaternion_sign(self):
        parent = {"position": [10, 0, 0], "orientation": [0, 0, math.sqrt(.5), math.sqrt(.5)]}
        local = {"position": [2, 0, 0], "orientation": [0, 0, 0, 1]}
        expected = {"position": [10, 2, 0], "orientation": [0, 0, -math.sqrt(.5), -math.sqrt(.5)]}
        position, rotation = audit.pose_difference(audit.compose(parent, local), expected)
        self.assertLess(position, 1e-12)
        self.assertLess(rotation, 1e-5)
        self.assertAlmostEqual(audit.pose_difference(parent, expected)[0], 2)

    def test_bad_pose_and_nonfinite_json_refused(self):
        with self.assertRaises(ValueError):
            audit.quaternion([0, 0, 0, 0])
        with self.assertRaises(ValueError):
            audit.vector([1, float("nan"), 0], 3)
        with self.assertRaises(ValueError):
            json.loads('{"tick": NaN}', parse_constant=audit.reject_constant)

    def test_only_incomplete_final_record_is_ignored(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "trace.jsonl"
            path.write_bytes(b'{"tick": 1}\n{"tick":')
            counts = audit.Counter()
            self.assertEqual(list(audit.read_rows(path, counts)), [{"tick": 1}])
            self.assertEqual(counts["incomplete_tail_records"], 1)
            path.write_bytes(b'{"tick":\n{"tick": 2}\n')
            with self.assertRaises(ValueError):
                list(audit.read_rows(path, audit.Counter()))

    def test_wrong_loaded_identity_cannot_publish_metrics(self):
        with tempfile.TemporaryDirectory() as folder:
            run = Path(folder)
            (run / "recording").mkdir()
            (run / "recording/manifest.json").write_text(json.dumps({"state": "complete", "pid": 12, "loaded_identity": {"pid": 13, "sha256": "a"*64}}))
            (run / "candidate-at-launch.json").write_text(json.dumps({"dll_name": "d2vr_render_probe_arm_v226.dll", "sha256": "a"*64}))
            with self.assertRaisesRegex(ValueError, "PID mismatch"):
                audit.audit(run)
            value = json.loads((run / "recording/manifest.json").read_text())
            value["loaded_identity"]["pid"] = 12
            value["loaded_identity"]["sha256"] = "b"*64
            (run / "recording/manifest.json").write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, "SHA mismatch"):
                audit.audit(run)


if __name__ == "__main__":
    unittest.main()
