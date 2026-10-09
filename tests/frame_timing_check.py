import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_frame_timing import analyze


def frame(number, periods, **changes):
    period = 10_000_000
    row = dict(present=number, start_cpu_ns=periods*period, end_cpu_ns=periods*period+3_000_000,
               engine_cpu_gap_ns=6_000_000, previous_desktop_present_ns=1_000_000, pre_xr_cpu_ns=0,
               wait_cpu_ns=1_000_000, begin_cpu_ns=0, locate_input_cpu_ns=100_000,
               transfer_cpu_ns=900_000, submit_cpu_ns=1_000_000,
               display_time_ns=periods*period+100_000_000, display_period_ns=period,
               rendered_display_left_ns=periods*period+90_000_000,
               rendered_display_right_ns=periods*period+90_000_000,
               rendered_locate_left_cpu_ns=periods*period-8_000_000,
               rendered_locate_right_cpu_ns=periods*period-8_000_000,
               state=5, epoch=1, should_render=1, submitted=1, tracked_pair=1, gameplay=1,
               authored_camera=0, settings=0, width=3102, height=3208, smaa_enabled=1, capture_readback=0)
    return dict(row, **changes)


class TimingCheck(unittest.TestCase):
    def test_pacing_and_pose_age_keep_clock_domains_separate(self):
        report = analyze([frame(1, 1), frame(2, 2), frame(3, 4)])
        gameplay = report['modes']['tracked_gameplay']
        self.assertAlmostEqual(gameplay['observed_bridge_calls_per_second'], 200/3)
        self.assertEqual(gameplay['estimated_skipped_prediction_intervals'], 1)
        self.assertEqual(gameplay['rendered_pose_prediction_lag_ms']['median'], 10)
        self.assertEqual(gameplay['rendered_pose_cpu_age_at_submit_ms']['median'], 11)
        self.assertFalse(report['hardware_refresh_measured'])

    def test_transitions_resize_epoch_hidden_and_refresh_changes_are_excluded(self):
        rows = [frame(1, 1), frame(2, 20, epoch=2), frame(3, 40, width=1000),
                frame(4, 60, gameplay=0), frame(5, 80, submitted=0, state=3),
                frame(6, 100), frame(7, 120, display_period_ns=20_000_000)]
        for value in analyze(rows)['modes'].values():
            self.assertEqual(value['adjacent_visible_intervals'], 0)
            self.assertEqual(value['estimated_skipped_prediction_intervals'], 0)

    def test_first_gap_and_eye_timestamp_mismatch(self):
        report = analyze([frame(1, 1, engine_cpu_gap_ns=-1, rendered_display_right_ns=123)])
        gameplay = report['modes']['tracked_gameplay']
        self.assertNotIn('engine_cpu_gap', gameplay['cpu_spans_ms'])
        self.assertEqual(gameplay['mismatched_eye_pose_times'], 1)
        self.assertIsNone(gameplay['rendered_pose_prediction_lag_ms'])

    def test_aa_changes_and_readback_stalls_do_not_count_as_pacing(self):
        rows = [frame(1, 1), frame(2, 10, capture_readback=1), frame(3, 20), frame(4, 30, smaa_enabled=0)]
        gameplay = analyze(rows)['modes']['tracked_gameplay']
        self.assertEqual(gameplay['adjacent_visible_intervals'], 0)
        self.assertEqual(gameplay['diagnostic_readback_samples'], 1)

    def test_invalid_trace_fails(self):
        for rows in ([], [frame(1, 1, display_period_ns=0)], [frame(1, 2), frame(2, 1)],
                     [frame(1, 1, rendered_locate_left_cpu_ns=0)],
                     [frame(1, 1, wait_cpu_ns=-1)]):
            with self.assertRaises(ValueError):
                analyze(rows)


if __name__ == '__main__':
    unittest.main()
