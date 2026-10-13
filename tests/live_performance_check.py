import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from frame_timing_check import frame
import analyze_live_performance as live


class LivePerformanceCheck(unittest.TestCase):
    def accounting_frames(self):
        return [frame(1, 1, start_cpu_ns=10_000_000, end_cpu_ns=13_000_000,
                      present_start_ns=8_000_000, pre_desktop_ns=58_000_000, present_end_ns=59_000_000,
                      post_xr_cpu_ns=45_000_000, controls_cpu_ns=44_000_000),
                frame(2, 2, start_cpu_ns=106_000_000, end_cpu_ns=109_000_000,
                      present_start_ns=104_000_000, pre_desktop_ns=109_000_000, present_end_ns=110_000_000,
                      engine_cpu_gap_ns=45_000_000, pre_xr_cpu_ns=2_000_000,
                      post_xr_cpu_ns=0, controls_cpu_ns=0),
                frame(3, 3, start_cpu_ns=119_000_000, end_cpu_ns=122_000_000,
                      present_start_ns=117_000_000, pre_desktop_ns=122_000_000, present_end_ns=123_000_000,
                      engine_cpu_gap_ns=7_000_000, pre_xr_cpu_ns=2_000_000, post_xr_cpu_ns=0)]

    def test_interval_attributes_outgoing_not_incoming_controls(self):
        report = next(iter(live.interval_accounting(self.accounting_frames()).values()))
        slow, fast = report['slow'], report['fast']
        self.assertEqual(slow['intervals'], 1)
        self.assertEqual(slow['cpu_interval_ms']['median'], 96)
        self.assertEqual(slow['exclusive_costs_ms']['outgoing_post_xr']['median'], 45)
        self.assertEqual(slow['outgoing_present_subspans_ms']['controls_cpu']['median'], 44)
        self.assertEqual(sum(value['median'] for value in slow['exclusive_costs_ms'].values()), 96)
        self.assertEqual(fast['cpu_interval_ms']['median'], 13)
        self.assertEqual(fast['outgoing_present_subspans_ms']['controls_cpu']['median'], 0)
        self.assertEqual(slow['measured_boundary_intervals'], 1)
        self.assertEqual(report['invalid_clock_intervals'], 0)

    def test_legacy_residual_is_unattributed_and_mixed_clock_is_rejected(self):
        rows = self.accounting_frames()
        for row in rows:
            for key in ('present_start_ns', 'pre_desktop_ns', 'present_end_ns'):
                row.pop(key)
        report = next(iter(live.interval_accounting(rows).values()))
        self.assertEqual(report['slow']['measured_boundary_intervals'], 0)
        self.assertEqual(report['slow']['exclusive_costs_ms']['outgoing_unmeasured_post_xr']['median'], 45)
        rows = self.accounting_frames();rows[1]['engine_cpu_gap_ns'] += 1
        report = next(iter(live.interval_accounting(rows).values()))
        self.assertEqual(report['invalid_clock_intervals'], 1)
        self.assertEqual(report['slow']['intervals'], 0)

    def test_interval_accounting_breaks_at_capture_gap_and_epoch(self):
        for change in (dict(capture_readback=1), dict(present=3), dict(epoch=2), dict(settings=1)):
            rows = self.accounting_frames()[:2];rows[1].update(change)
            self.assertFalse(live.interval_accounting(rows))

    def test_sustained_stalls_survive_fast_median(self):
        rows, clock = [], 0
        for number in range(1, 681):
            clock += 96_000_000 if 301 <= number <= 380 else 13_000_000
            rows.append(frame(number, number, start_cpu_ns=clock, end_cpu_ns=clock+3_000_000))
        report = next(iter(live.clean_cadence(rows).values()))
        self.assertEqual(report['clean_cpu_interval_ms']['median'], 13)
        self.assertAlmostEqual(report['longest_slow_seconds'], 7.68)
        self.assertEqual(report['sustained_slow_windows'][0]['frames'], 80)

    def test_stall_breaks_on_capture_missing_present_and_epoch(self):
        rows = [frame(1, 10), frame(2, 20), frame(3, 30, capture_readback=1),
                frame(4, 40), frame(6, 50), frame(7, 60, epoch=2)]
        report = next(iter(live.clean_cadence(rows).values()))
        self.assertEqual(report['clean_cpu_interval_ms']['count'], 1)
        self.assertEqual(report['slow_intervals'], 1)

    def test_capture_neighbour_and_menu_spans_are_excluded(self):
        rows = [frame(1, 1, engine_cpu_gap_ns=1_000_000),
                frame(2, 2, capture_readback=1, engine_cpu_gap_ns=999_000_000),
                frame(3, 3, engine_cpu_gap_ns=888_000_000),
                frame(4, 4, settings=1, engine_cpu_gap_ns=777_000_000),
                frame(5, 5, temporal_aa=4, engine_cpu_gap_ns=4_000_000),
                frame(6, 6, temporal_aa=4, engine_cpu_gap_ns=5_000_000)]
        report = live.timing_report(rows, 30)
        self.assertEqual(report['capture_readback_samples'], 1)
        self.assertEqual(len(report['gameplay']), 2)
        first, second = report['gameplay'].values()
        self.assertEqual(first['clean_samples'], 1)
        self.assertEqual(first['cpu_spans_ms']['engine_cpu_gap']['maximum'], 1)
        self.assertEqual(second['cpu_spans_ms']['engine_cpu_gap']['maximum'], 5)
        self.assertEqual(second['adjacent_visible_intervals'], 1)
        self.assertFalse(report['hardware_refresh_measured'])

    def test_rolling_window(self):
        report = live.timing_report([frame(n, n*100) for n in range(1, 5)], 1)
        self.assertEqual(report['first_present'], 3)
        self.assertEqual(report['last_present'], 4)
        self.assertEqual(report['rows_in_window'], 2)

    def fake_run(self, load_side_effect, identity_change=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dll = root / 'build.dll'
            dll.write_bytes(b'fixture')
            identity = root / 'identity.json'
            identity_value = dict(pid=10, sha256=hashlib.sha256(b'fixture').hexdigest(), base=4096)
            identity.write_text(json.dumps(identity_value))
            status = root / 'xr-live-status.json'
            status.write_text(json.dumps(dict(pid=10, gameplay=True, full_body=dict(material_failures=4))))
            clock = [0.]

            def sleep(seconds):
                clock[0] += seconds
                status.write_text(json.dumps(dict(pid=10, tick_ms=clock[0], gameplay=True, full_body=dict(material_failures=4))))
                if identity_change:
                    identity.write_text(json.dumps(identity_value | dict(pid=11)))

            args = argparse.Namespace(pid=10, output=root / 'output', seconds=2,
                                      interval=1, window=30)
            def verify(process, pid):
                current = json.loads(identity.read_text())
                if current != identity_value:
                    raise ValueError('Changed identity')
                return current
            candidate = SimpleNamespace(identity_path=identity, manifest_path=root/'candidate.json', verify=verify)
            with patch.object(live, 'Process') as process, patch.object(live, 'Candidate', return_value=candidate), \
                 patch.object(live, 'ARTIFACTS', root), \
                 patch.object(live, 'bounded_rows', side_effect=load_side_effect), \
                 patch.object(live.time, 'monotonic', side_effect=lambda: clock[0]), \
                 patch.object(live.time, 'sleep', side_effect=sleep):
                process.return_value.alive.return_value = True
                self.assertEqual(live.run(args), 0)
                process.return_value.close.assert_called_once()
            history = [json.loads(line) for line in (args.output / 'history.jsonl').read_text().splitlines()]
            latest = json.loads((args.output / 'latest.json').read_text())
            self.assertFalse((root / 'capture-eye-pair.request').exists())
            self.assertEqual(json.loads(status.read_text())['pid'], 10)
            return latest, history

    def test_partial_write_error_is_reported_and_retried(self):
        latest, history = self.fake_run([ValueError('Incomplete timing record'),
                                         ([frame(1, 1), frame(2, 2)], {})])
        self.assertIn('diagnostic_error', history[0])
        self.assertFalse(history[0]['gameplay'])
        self.assertNotIn('diagnostic_error', latest)
        self.assertEqual(latest['samples'], 2)
        self.assertEqual(latest['stop_reason'], 'duration_complete')
        self.assertTrue(latest['timing']['gameplay'])

    def test_pid_change_stops_before_reading_another_session(self):
        latest, history = self.fake_run([([frame(1, 1), frame(2, 2)], {})], True)
        self.assertEqual(latest['samples'], 1)
        self.assertEqual(latest['stop_reason'], 'loaded_identity_changed')


if __name__ == '__main__':
    from record_live_session import low_priority
    low_priority()
    unittest.main()
