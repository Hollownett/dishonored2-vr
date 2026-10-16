"""CPU-only ownership, stale-session and Win64 cleanup regression checks."""
import collections
import ctypes
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import record_live_session as live
import sample_threads as profiler


class LiveToolsCheck(unittest.TestCase):
    def test_candidate_selection_requires_manifest_hash_and_loaded_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'build').mkdir()
            artifacts = root / 'artifacts'; artifacts.mkdir()
            dll = root / 'build/d2vr_render_probe_arm_v221.dll'; dll.write_bytes(b'candidate')
            digest = hashlib.sha256(b'candidate').hexdigest()
            manifest = artifacts / 'repair-v221-test-candidate.json'
            manifest.write_text(json.dumps(dict(dll_name=dll.name, sha256=digest, game_sha256='a'*64)))
            identity = artifacts / 'loaded-render-probe-arm-v221.json'
            identity.write_text(json.dumps(dict(pid=42, base=0x12340000, sha256=digest)))
            with patch.object(live, 'ROOT', root), patch.object(live, 'ARTIFACTS', artifacts):
                candidate = live.Candidate(manifest)
                process = Mock(); process.last_build_check = -1e9
                self.assertEqual(candidate.verify(process, 42)['base'], 0x12340000)
                process.verify_build.assert_called_once()
                with self.assertRaises(ValueError): candidate.verify(process, 43)
                identity.write_text(json.dumps(dict(pid=42, base=0x12340000, sha256='b'*64)))
                with self.assertRaises(ValueError): candidate.verify(process, 42)
                dll.write_bytes(b'wrong build')
                with self.assertRaises(ValueError): live.Candidate(manifest)

    def test_watch_stops_ended_same_pid_and_unchanged_status(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'xr-live-status.json'
            status = dict(pid=42, tick_ms=123, game_layers_submitted=9)
            path.write_text(json.dumps(status))
            watch = live.SessionWatch(42, path.stat().st_mtime_ns-1, 5)
            self.assertEqual(watch.check(status, path, 100), (None, True))
            self.assertEqual(watch.check(status, path, 101), (None, False))
            self.assertEqual(watch.check(status, path, 106)[0], 'status_no_progress')
            self.assertEqual(watch.check(status | dict(pid=43), path, 107)[0], 'live_pid_changed')
            (path.parent / 'dxgi-probe-result.json').write_text(json.dumps(dict(pid=42, stop_requested=True)))
            self.assertEqual(watch.check(status, path, 108)[0], 'session_stopped')

    def test_watch_does_not_confuse_old_stopped_result_with_new_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = root / 'dxgi-probe-result.json'
            result.write_text(json.dumps(dict(pid=42, stop_requested=True)))
            identity_mtime = result.stat().st_mtime_ns+1
            path = root / 'xr-live-status.json'; path.write_text('{}')
            watch = live.SessionWatch(42, identity_mtime, 5)
            self.assertEqual(watch.check(dict(pid=42, tick_ms=1), path, 1), (None, True))

    def test_recorder_owner_stop_finalizes_without_new_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); status = root / 'xr-live-status.json'
            status.write_text(json.dumps(dict(pid=42, tick_ms=1)))
            identity = root / 'identity.json'; identity.write_text(json.dumps(dict(pid=42, sha256='a'*64)))
            manifest = root / 'candidate.json'; manifest.write_text('{}')
            candidate = Mock(identity_path=identity, manifest_path=manifest, hash='a'*64)
            candidate.verify.return_value = dict(pid=42, sha256='a'*64)
            with patch.object(live, 'Process') as process, patch.object(live, 'Candidate', return_value=candidate), patch.object(live, 'ARTIFACTS', root):
                process.return_value.image = 'Dishonored2.exe'
                recorder = live.Recorder(42, root/'output', 10, 0)
                (recorder.output/'stop.request').touch()
                self.assertEqual(recorder.run(), 0)
                saved = json.loads((recorder.output/'manifest.json').read_text())
                self.assertEqual(saved['stop_reason'], 'stopped_by_owner')
                self.assertEqual(saved['samples'], 0)
                self.assertTrue((recorder.output/'final/artifacts/xr-live-status.json').exists())
                process.return_value.close.assert_called_once()

    def test_independent_timing_and_boundary_rotations_preserve_every_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            status = root / 'xr-live-status.json'; status.write_text(json.dumps(dict(pid=42, tick_ms=1)))
            identity = root / 'identity.json'; identity.write_text(json.dumps(dict(pid=42, sha256='a'*64)))
            manifest = root / 'candidate.json'; manifest.write_text('{}')
            candidate = Mock(identity_path=identity, manifest_path=manifest, hash='a'*64)
            candidate.verify.return_value = dict(pid=42, sha256='a'*64)
            timing = root / 'xr-frame-timing-previous.csv'
            boundary = root / 'present-boundaries-previous.csv'
            timing.write_bytes(b'present,time\n1,100\n')
            boundary.write_bytes(b'present,boundary\n1,200\n')
            with patch.object(live, 'Process') as process, patch.object(live, 'Candidate', return_value=candidate), patch.object(live, 'ARTIFACTS', root):
                process.return_value.image = 'Dishonored2.exe'
                recorder = live.Recorder(42, root/'output', 3600, 0)
                try:
                    recorder.preserve_rotation()
                    first_timing = recorder.output/'timing-rotations/0001.csv'
                    first_boundary = recorder.output/'boundary-rotations/0001.csv'
                    self.assertEqual(first_timing.read_bytes(), timing.read_bytes())
                    self.assertEqual(first_boundary.read_bytes(), boundary.read_bytes())
                    recorder.preserve_rotation()
                    self.assertEqual(len(recorder.manifest['snapshots']), 2)
                    timing.write_bytes(b'present,time\n2,300\n3,400\n')
                    recorder.preserve_rotation()
                    self.assertEqual(recorder.rotations, {timing.name: 2, boundary.name: 1})
                    self.assertEqual(first_timing.read_bytes(), b'present,time\n1,100\n')
                    self.assertEqual(first_boundary.read_bytes(), b'present,boundary\n1,200\n')
                    boundary.write_bytes(b'present,boundary\n4,500\n5,600\n')
                    recorder.preserve_rotation()
                    self.assertEqual(recorder.rotations, {timing.name: 2, boundary.name: 2})
                    self.assertEqual((recorder.output/'boundary-rotations/0002.csv').read_bytes(), boundary.read_bytes())
                    self.assertEqual((recorder.output/'timing-rotations/0002.csv').read_bytes(), timing.read_bytes())
                    recorder.preserve_rotation()
                    self.assertEqual(len(recorder.manifest['snapshots']), 4)
                    occupied = recorder.output/'boundary-rotations/0003.csv'; occupied.write_bytes(b'preserve me')
                    boundary.write_bytes(b'present,boundary\n6,700\n')
                    recorder.preserve_rotation()
                    self.assertEqual(occupied.read_bytes(), b'preserve me')
                    self.assertEqual((recorder.output/'boundary-rotations/0004.csv').read_bytes(), boundary.read_bytes())
                    self.assertEqual(recorder.rotations, {timing.name: 2, boundary.name: 4})
                    timing.write_bytes(b'present,time\n7,800\n')
                    with patch.object(live, 'MAX_ARCHIVE_BYTES', recorder.archive_bytes):
                        recorder.preserve_rotation()
                    self.assertFalse((recorder.output/'timing-rotations/0003.csv').exists())
                    self.assertEqual(recorder.rotations[timing.name], 2)
                    recorder.preserve_rotation()
                    self.assertEqual((recorder.output/'timing-rotations/0003.csv').read_bytes(), timing.read_bytes())
                    recorder.save_manifest()
                    saved = json.loads((recorder.output/'manifest.json').read_text())
                    self.assertEqual(saved['rotation_counts'], recorder.rotations)
                    self.assertEqual(saved['capture_attempts'], 0)
                    candidate.verify.assert_called_once()
                finally:
                    recorder.process.close()

    def test_profiler_win64_prototypes_and_resume_on_failure(self):
        self.assertEqual(profiler.k32.CreateToolhelp32Snapshot.restype, ctypes.wintypes.HANDLE)
        self.assertEqual(profiler.k32.SuspendThread.argtypes, [ctypes.wintypes.HANDLE])
        context = profiler.CONTEXT()
        for error in (RuntimeError('context failure'), KeyboardInterrupt()):
            api = Mock()
            api.SuspendThread.return_value = 0
            api.GetThreadContext.side_effect = error
            api.ResumeThread.return_value = 1
            with patch.object(profiler, 'k32', api):
                with self.assertRaises(type(error)):
                    profiler.sample_thread(0x123456789, 0x234567890, context, collections.Counter())
                api.ResumeThread.assert_called_once_with(0x234567890)

    def test_profiler_snapshot_closes_handle_on_interrupt(self):
        api = Mock(); api.CreateToolhelp32Snapshot.return_value = 0x123456789
        api.Thread32First.side_effect = KeyboardInterrupt()
        with patch.object(profiler, 'k32', api):
            with self.assertRaises(KeyboardInterrupt): profiler.threads(42)
            api.CloseHandle.assert_called_once_with(0x123456789)

    def test_profiler_process_handle_closes_on_module_failure(self):
        api = Mock(); api.OpenProcess.return_value = 0x123456789
        with patch.object(profiler, 'k32', api), patch.object(profiler, 'modules', side_effect=RuntimeError('module read failed')), patch.object(sys, 'argv', ['sample_threads.py', '42', '1', '10']):
            with self.assertRaises(RuntimeError): profiler.main()
            api.CloseHandle.assert_called_once_with(0x123456789)


if __name__ == '__main__':
    live.low_priority()
    unittest.main()
