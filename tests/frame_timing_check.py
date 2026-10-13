import sys
import unittest
import csv
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_frame_timing import analyze, load_rows, merge_present_boundaries


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
    def test_present_boundaries_match_exact_frame_and_clock(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            row = frame(1, 1)
            boundary = dict(present=1, present_start_ns=9_000_000, xr_end_ns=13_000_000,
                            pre_desktop_ns=58_000_000, present_end_ns=59_000_000,
                            post_xr_cpu_ns=45_000_000, desktop_present_cpu_ns=1_000_000,
                            present_total_cpu_ns=50_000_000, logic_discovery_cpu_ns=25_000_000,
                            lean_cpu_ns=8_000_000, grab_control_cpu_ns=1_000_000,
                            takedown_crouch_cpu_ns=3_000_000, controls_cpu_ns=40_000_000,
                            post_xr_housekeeping_cpu_ns=4_000_000, gpu_hook_maintenance_cpu_ns=1_000_000,
                            epoch=1, gameplay=1, capture_readback=0)
            path = folder/'present-boundaries.csv'
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=boundary)
                writer.writeheader();writer.writerow(boundary)
            merge_present_boundaries(folder, [row])
            self.assertEqual(row['post_xr_cpu_ns'], 45_000_000)
            unmatched = frame(1, 1, epoch=2)
            merge_present_boundaries(folder, [unmatched])
            self.assertNotIn('post_xr_cpu_ns', unmatched)
            group = next(iter(analyze([row])['modes'].values()))
            self.assertEqual(group['cpu_spans_ms']['post_xr_cpu']['maximum'], 45)
            self.assertEqual(group['cpu_spans_ms']['post_xr_housekeeping_cpu']['maximum'], 4)
            self.assertEqual(group['cpu_spans_ms']['gpu_hook_maintenance_cpu']['maximum'], 1)
            # Older boundary logs retain their existing spans without invented new samples.
            boundary.pop('post_xr_housekeeping_cpu_ns');boundary.pop('gpu_hook_maintenance_cpu_ns')
            with path.open('w', newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=boundary);writer.writeheader();writer.writerow(boundary)
            legacy=frame(1,1);merge_present_boundaries(folder,[legacy])
            self.assertNotIn('post_xr_housekeeping_cpu_ns',legacy)
            # A capture after the XR timing snapshot belongs to this interval too.
            boundary['capture_readback']=1
            with path.open('w', newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=boundary);writer.writeheader();writer.writerow(boundary)
            late_capture=frame(1,1);merge_present_boundaries(folder,[late_capture])
            self.assertEqual(late_capture['capture_readback'],1)
            self.assertEqual(late_capture['present_end_ns'],59_000_000)

    def modern(self, number, **changes):
        return frame(number, number, temporal_aa=3, render_fraction_permille=770,
                     sync_interval=1, desktop_sync_interval=0,
                     fsr_render_left_width=2388, fsr_render_left_height=2470,
                     fsr_render_right_width=2388, fsr_render_right_height=2470,
                     fsr_gpu_left_samples=number, fsr_gpu_right_samples=number,
                     fsr_gpu_left_ns=1_000_000, fsr_gpu_right_ns=2_000_000,
                     fsr_cpu_left_ns=10_000, fsr_cpu_right_ns=20_000,
                     fsr_upscaled=number*2, fsr_failures=0) | changes

    def native(self, number, **changes):
        return self.modern(number, native_gpu_frames=number, native_gpu_lists=number*4,
                           native_gpu_sequence=number, native_gpu_list_sum_ns=4_000_000,
                           native_gpu_elapsed_ns=8_000_000, native_gpu_last_lists=4,
                           native_gpu_dropped_lists=0, native_gpu_epoch=1,
                           taa_viewport_left_width=2388, taa_viewport_left_height=2470,
                           taa_viewport_right_width=2388, taa_viewport_right_height=2470) | changes

    def test_native_gpu_sequences_epochs_and_coverage(self):
        rows = [self.native(1), self.native(2),
                self.native(3, native_gpu_frames=2, native_gpu_lists=8, native_gpu_sequence=2,
                            native_gpu_list_sum_ns=99_000_000, native_gpu_elapsed_ns=100_000_000),
                self.native(4, native_gpu_frames=5, native_gpu_lists=20, native_gpu_sequence=5,
                            native_gpu_dropped_lists=2),
                self.native(5, native_gpu_epoch=2, native_gpu_frames=1, native_gpu_lists=4),
                self.native(6, native_gpu_epoch=2, native_gpu_frames=2, native_gpu_lists=8)]
        gpu = next(iter(analyze(rows)['modes'].values()))['native_gpu']
        self.assertEqual(gpu['completed_frame_increments'], 5)
        self.assertEqual(gpu['completed_list_increments'], 20)
        self.assertEqual(gpu['observed_list_sum_ms']['count'], 3)
        self.assertEqual(gpu['observed_list_sum_ms']['median'], 4)
        self.assertEqual(gpu['observed_elapsed_span_ms']['median'], 8)
        self.assertEqual(gpu['partial_coverage_samples'], 1)
        self.assertEqual(gpu['observed_last_dropped_lists']['maximum'], 2)
        self.assertEqual(gpu['epochs'], [1, 2])

    def test_native_gpu_config_viewport_readback_and_counter_resets(self):
        rows = [self.native(1), self.native(2, temporal_aa=4), self.native(3),
                self.native(4, taa_viewport_left_width=2000), self.native(5),
                self.native(6, capture_readback=1), self.native(7),
                self.native(8, native_gpu_frames=0, native_gpu_lists=0, native_gpu_sequence=0),
                self.native(9, native_gpu_frames=1, native_gpu_lists=4, native_gpu_sequence=9)]
        groups = list(analyze(rows)['modes'].values())
        self.assertEqual(len(groups), 3)
        gpu = groups[0]['native_gpu']
        self.assertEqual(gpu['observed_list_sum_ms']['count'], 1)
        self.assertEqual(gpu['counter_resets'], 1)
        self.assertIsNone(groups[1]['native_gpu']['observed_list_sum_ms'])
        self.assertIsNone(groups[2]['native_gpu']['observed_list_sum_ms'])
        self.assertIn('TAA-stage viewport', analyze(rows)['limitation'])

    def test_invalid_native_gpu_telemetry_fails(self):
        for changes in ({'native_gpu_list_sum_ns': 9_000_000}, {'native_gpu_epoch': -1}):
            with self.assertRaises(ValueError):
                analyze([self.native(1, **changes)])

    def test_aa_fraction_smaa_and_actual_dimensions_split_comparisons(self):
        rows = [self.modern(1), self.modern(2),
                self.modern(3, temporal_aa=4), self.modern(4, temporal_aa=4),
                self.modern(5, render_fraction_permille=670),
                self.modern(6, smaa_enabled=0), self.modern(7, fsr_render_right_width=2100)]
        groups = list(analyze(rows)['modes'].values())
        self.assertEqual(len(groups), 5)
        self.assertEqual([group['adjacent_visible_intervals'] for group in groups], [1, 1, 0, 0, 0])
        self.assertEqual(groups[0]['requested_desktop_sync_intervals'], {1: 2})
        self.assertEqual(groups[0]['effective_desktop_sync_intervals'], {0: 2})

    def test_gpu_last_value_is_not_duplicated_and_resets_and_readbacks_are_excluded(self):
        rows = [self.modern(1, fsr_gpu_left_samples=10),
                self.modern(2, fsr_gpu_left_samples=10, fsr_gpu_left_ns=9_000_000),
                self.modern(3, fsr_gpu_left_samples=13, fsr_gpu_left_ns=3_000_000),
                self.modern(4, fsr_gpu_left_samples=0),
                self.modern(5, fsr_gpu_left_samples=1, capture_readback=1),
                self.modern(6, fsr_gpu_left_samples=2),
                self.modern(7, fsr_gpu_left_samples=3, fsr_gpu_left_ns=5_000_000)]
        group = next(iter(analyze(rows)['modes'].values()))
        gpu = group['fsr_gpu']['left']
        self.assertEqual(gpu['completed_query_increments'], 6)
        self.assertEqual(gpu['counter_resets'], 1)
        self.assertEqual(gpu['observed_last_query_ms']['count'], 2)
        self.assertEqual(gpu['observed_last_query_ms']['median'], 4)
        self.assertEqual(group['fsr_counter_increments'], {'fsr_upscaled': 12, 'fsr_failures': 0})

    def test_desktop_sync_transition_is_not_a_pacing_interval(self):
        rows = [self.modern(1), self.modern(2, desktop_sync_interval=1)]
        group = next(iter(analyze(rows)['modes'].values()))
        self.assertEqual(group['adjacent_visible_intervals'], 0)
        self.assertEqual(group['effective_desktop_sync_intervals'], {0: 1, 1: 1})

    def test_native_fsr_cpu_telemetry_is_reported_without_upscale_copies(self):
        rows = [self.modern(1, fsr_upscaled=0), self.modern(2, fsr_upscaled=0)]
        group = next(iter(analyze(rows)['modes'].values()))
        self.assertAlmostEqual(group['cpu_spans_ms']['fsr_cpu_left']['median'], .01)
        self.assertAlmostEqual(group['cpu_spans_ms']['fsr_cpu_right']['median'], .02)

    def test_rotated_chunks_preserve_boundary_and_ignore_only_incomplete_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            for name, rows in (('xr-frame-timing-previous.csv', [self.modern(1), self.modern(2)]),
                               ('xr-frame-timing.csv', [self.modern(3)])):
                with (folder/name).open('w', newline='') as stream:
                    writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
                    writer.writeheader()
                    writer.writerows(rows)
            with (folder/'xr-frame-timing.csv').open('a') as stream:
                stream.write('4,40000000,')
            rows, info = load_rows(folder)
            self.assertEqual([row['present'] for row in rows], [1, 2, 3])
            self.assertEqual(info['incomplete_tail_records'], 1)
            self.assertEqual(next(iter(analyze(rows)['modes'].values()))['adjacent_visible_intervals'], 2)
            with (folder/'xr-frame-timing-previous.csv').open('a') as stream:
                stream.write('4,40000000,')
            with self.assertRaises(ValueError):
                load_rows(folder)

    def test_malformed_complete_tail_is_not_silently_discarded(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            for text in ('present,start_cpu_ns\n1,2,3\n', 'present,start_cpu_ns\n1,no-number\n'):
                (folder/'xr-frame-timing.csv').write_text(text)
                with self.assertRaises(ValueError):
                    load_rows(folder)

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
