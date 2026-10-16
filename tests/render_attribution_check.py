import importlib.util
import csv
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("attribution", Path(__file__).resolve().parents[1]/"tools/analyze_render_attribution.py")
module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


def gpu(frame=1, category=3, phase=1, ms=1, eye=0, job=10, epoch=7, **extra):
    return dict(epoch=epoch, frame=frame, category=category, eye=eye, id=job, phase=phase,
                ms=ms, dropped=0, replays=0, failures=0, **extra)


def frame(present=1, width=2112, height=2304, **extra):
    return dict(present=present, epoch=7, width=width, height=height, state=5, gameplay=1,
                submitted=1, should_render=1, settings=0, capture_readback=0, authored_camera=0, **extra)


class AttributionChecks(unittest.TestCase):
    @staticmethod
    def readiness_rows(stage=7, hresult=1, version=1, context=2**63+123, attached=True):
        values = [version, 11, 200, 100, context, stage, hresult, 4, 12345, 2, 1, 1,
                  17 if attached else 1, 7, 2, 3, 220, 255]
        rows = [gpu(frame=100, category=15, phase=phase, eye=-1, job=value, ms=0)
                for phase, value in enumerate(values)]
        rows += [gpu(frame=100, category=16, phase=phase, eye=-1, job=hresult if phase==stage else 0, ms=0)
                 for phase in range(1, 9)]
        if attached:
            rows += [gpu(frame=100, category=17, phase=phase, eye=-1, job=value, ms=0)
                     for phase, value in enumerate((2**63+321, 98, 7, 1, 12345))]
        rows += [gpu(frame=100, category=18, phase=0, eye=-1, job=int(attached), ms=0)]
        if attached:
            rows += [gpu(frame=100, category=18, phase=phase, eye=-1, job=value, ms=0)
                     for phase, value in enumerate((12345, 2**63+321, 98, 7, 1, 1), 1)]
        return rows

    def test_pending_query_metadata_preserves_identity_without_gpu_durations(self):
        rows = self.readiness_rows()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"readiness.csv"
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=module.GPU);writer.writeheader();writer.writerows(rows)
            parsed, rejected = module.read_csv(path, module.GPU)
        self.assertFalse(any(rejected.values()))
        result = module.analyze(parsed, [])
        readiness = result["profiler_contract"]["query_readiness"]
        self.assertEqual(len(readiness["snapshots"]), 1)
        snapshot = readiness["snapshots"][0]
        self.assertTrue(snapshot["recognized"])
        self.assertEqual(snapshot["stage_name"], "hud_start")
        self.assertEqual(snapshot["hresult_hex"], "0x00000001")
        self.assertEqual(snapshot["context_identity"], 2**63+123)
        self.assertEqual(snapshot["blocking_hud"]["list_identity"], 2**63+321)
        self.assertEqual(snapshot["attached_hud_at_snapshot"]["first_list_identity"], 2**63+321)
        self.assertEqual(result["gpu_frames"], [])
        self.assertEqual(result["fsr_jobs"], [])

    def test_query_metadata_keeps_terminal_hresult_and_unvisited_stage_distinct(self):
        rows = self.readiness_rows(stage=1, hresult=0x80004005, attached=False)
        for row in rows:
            if row["category"] == 15 and row["phase"] == 17:
                row["id"] = 1
            if row["category"] == 16 and row["phase"] > 1:
                row["id"] = 0xffffffff
        snapshot = module.query_readiness(rows)["snapshots"][0]
        self.assertTrue(snapshot["recognized"])
        self.assertEqual(snapshot["hresult_hex"], "0x80004005")
        self.assertEqual(snapshot["visited_stage_results"], {"clock": 0x80004005})

    def test_partial_or_unknown_query_metadata_is_not_accepted(self):
        rows = self.readiness_rows()
        for category, phase in ((15, 17), (17, 4), (18, 6)):
            truncated = [row for row in rows if (row["category"], row["phase"]) != (category, phase)]
            self.assertFalse(module.query_readiness(truncated)["snapshots"][0]["recognized"])
        self.assertFalse(module.query_readiness(self.readiness_rows(version=2))["snapshots"][0]["recognized"])
        duplicate = rows+[dict(rows[1])]
        self.assertFalse(module.query_readiness(duplicate)["snapshots"][0]["recognized"])
        self.assertEqual(len(module.query_readiness(rows[1:])["orphan_rows"]), len(rows)-1)

    def test_busy_operation_alone_marks_capture_incomplete(self):
        result = module.analyze([gpu(category=9, phase=0, job=19, ms=3)], [])
        self.assertEqual(result["coverage"]["counters"]["failures"]["maximum"], 0)
        self.assertTrue(result["coverage"]["reason_specific_loss_reported"])
        self.assertTrue(result["coverage"]["incomplete"])

    def test_interrupted_query_groups_and_invalid_stage_widths_are_uninterpreted(self):
        rows = self.readiness_rows()
        interrupted = rows[:4]+[gpu(category=4, phase=0, eye=-1, job=0, ms=0)]+rows[4:]
        contract = module.analyze(interrupted, [])["profiler_contract"]
        self.assertTrue(contract["uninterpreted_contract_rows"])
        self.assertFalse(contract["query_readiness"]["snapshots"][0]["recognized"])
        self.assertTrue(contract["query_readiness"]["orphan_rows"])
        for category, phase in ((15, 17), (16, 1)):
            invalid = self.readiness_rows()
            for row in invalid:
                if (row["category"], row["phase"]) == (category, phase):
                    row["id"] = 256 if category == 15 else 2**32
            contract = module.analyze(invalid, [])["profiler_contract"]
            self.assertTrue(contract["uninterpreted_contract_rows"])
            self.assertFalse(contract["query_readiness"]["snapshots"][0]["recognized"])

    def test_hud_admission_snapshots_are_not_summed_or_gpu_durations(self):
        rows = [gpu(category=11, phase=1, job=32, ms=1)]
        rows += [gpu(category=14, phase=1, eye=-1, job=eligible, ms=admitted) for eligible, admitted in ((20, 20), (32, 32), (40, 32), (40, 32))]
        result = module.analyze(rows, [])
        contract = result["profiler_contract"]
        self.assertFalse(contract["uninterpreted_contract_rows"])
        self.assertEqual(contract["hud_sampling_policy_observations"][0]["per_sampled_frame_category_quota"], 32)
        self.assertEqual(len(contract["hud_recording_admission"]), 1)
        observation = contract["hud_recording_admission"][0]
        self.assertEqual((observation["eligible"], observation["admitted"]), (40, 32))
        self.assertFalse(observation["gpu_timings_complete"])
        self.assertEqual(result["gpu_frames"], [])
        self.assertTrue(result["coverage"]["hud_gpu_partial"])

    def test_operation_loss_reasons_are_recognized_even_when_zero(self):
        rows = [gpu(category=9, phase=0, job=reason, ms=0) for reason in range(19, 23)]
        result = module.analyze(rows, [])
        contract = result["profiler_contract"]
        self.assertFalse(contract["uninterpreted_contract_rows"])
        self.assertEqual([reason["name"] for reason in contract["loss_reason_counters"]],
                         ["operation_admission_busy", "operation_owner_conflict", "operation_context_mismatch", "operation_release_rejected"])
        self.assertFalse(contract["diagnostic_loss_reported"])

    def test_fsr_cpu_completeness_is_independent_of_missing_gpu_marks(self):
        rows = [gpu(phase=phase, ms=duration) for phase, duration in ((5, .2), (8, .4), (9, .6), (10, 1.5))]
        result = module.analyze(rows, [])
        self.assertEqual(result["coverage"]["fsr_jobs"], {"total": 1, "complete_gpu": 0, "complete_cpu": 1})

    def test_partial_fsr_cpu_categories_report_counts_and_a_lower_bound(self):
        cpu = [dict(epoch=7, frame=1, thread=123, category=category, count=count, ns=duration, maximum=duration)
               for category, count, duration in ((8, 1, 6000000), (6, 2, 4000000), (7, 2, 6000000))]
        result = module.analyze([], cpu)
        coverage = result["fsr_cpu_thread_coverage"][0]
        self.assertTrue(coverage["counts_differ"])
        self.assertFalse(coverage["complete_union_work_known"])
        self.assertEqual(coverage["observed_work_lower_bound_ms"], 10)
        self.assertEqual(result["cpu_recording_frames"][0]["all_threads_work_ms"], 10)

    def test_fsr_cpu_phases_match_dispatch_identity_and_do_not_double_count(self):
        rows = [gpu(phase=phase, ms=duration) for phase, duration in enumerate((1, 2, 7, 8, .2, 3), 1)]
        rows += [gpu(phase=8, ms=.4), gpu(phase=9, ms=.6), gpu(phase=10, ms=1.5), gpu(frame=2, phase=8, ms=99)]
        cpu = [dict(epoch=7, frame=1, thread=123, category=category, count=1, ns=duration, maximum=duration)
               for category, duration in ((3, 100000), (4, 200000), (6, 400000), (7, 600000), (8, 1500000))]
        result = module.analyze(rows, cpu)
        selected = next(job for job in result["fsr_jobs"] if job["frame"] == 1)
        self.assertEqual([selected[name] for name in ("cpu_record_ms", "cpu_handoff_ms", "cpu_dispatch_ms")], [.4, .6, 1.5])
        self.assertEqual(result["coverage"]["fsr_jobs"], {"total": 2, "complete_gpu": 1, "complete_cpu": 1})
        self.assertAlmostEqual(result["cpu_recording_frames"][0]["all_threads_work_ms"], 1.6)
        self.assertIsNone(next(job for job in result["fsr_jobs"] if job["frame"] == 2)["cpu_handoff_ms"])

    def test_hud_publication_is_reported_separately_from_nested_bounds(self):
        rows = [gpu(category=12, phase=0, ms=4), gpu(category=2, phase=0, ms=1), gpu(category=5, phase=0, ms=10)]
        result = module.analyze(rows, [])
        self.assertEqual(result["gpu_frames"][0]["hud_publication_elapsed_ms"], 4)
        self.assertEqual(result["gpu_frames"][0]["hud_bounds_observed_sum_ms"], 1)
        self.assertEqual(result["gpu_distributions_ms"]["hud_publication_elapsed_ms"]["p50"], 4)

    def test_duplicate_and_negative_fsr_cpu_phases_are_not_complete(self):
        rows = [gpu(phase=phase, ms=duration) for phase, duration in enumerate((1, 2, 7, 8, .2, 3), 1)]
        rows += [gpu(phase=8, ms=.4), gpu(phase=8, ms=.5), gpu(phase=9, ms=-1), gpu(phase=10, ms=1.5)]
        result = module.analyze(rows, [])
        self.assertIsNone(result["fsr_jobs"][0]["cpu_record_ms"])
        self.assertIsNone(result["fsr_jobs"][0]["cpu_handoff_ms"])
        self.assertEqual(result["coverage"]["fsr_jobs"]["complete_cpu"], 0)
        self.assertTrue(result["coverage"]["incomplete"])

    def test_partial_policy_and_quota_counters_are_not_gpu_durations_or_failures(self):
        rows = [gpu(category=9, phase=0, job=16, ms=count) for count in (4, 10, 10)]
        rows += [gpu(category=9, phase=0, job=17, ms=2), gpu(category=9, phase=0, job=15, ms=0),
                 gpu(category=11, phase=1, job=1, ms=1), gpu(category=11, phase=2, job=1, ms=1)]
        result = module.analyze(rows, []);contract = result["profiler_contract"]
        capture = next(counter for counter in contract["loss_reason_counters"] if counter["id"] == 16)
        self.assertEqual((capture["maximum_count"], capture["latest_count"], capture["snapshots"]), (10, 10, 3))
        self.assertEqual(contract["intentional_quota_skips"], {"capture": 10, "bounds": 2})
        self.assertFalse(contract["diagnostic_loss_reported"])
        self.assertTrue(result["coverage"]["incomplete"])
        self.assertTrue(result["coverage"]["hud_gpu_partial"])
        self.assertFalse(result["coverage"]["hud_gpu_work_complete"])
        self.assertEqual(result["coverage"]["counters"]["failures"]["maximum"], 0)
        self.assertEqual(result["coverage"]["counters"]["dropped"]["maximum"], 0)
        self.assertFalse(contract["has_central_gpu_rows"])
        self.assertEqual(result["gpu_frames"], [])
        self.assertEqual(result["fsr_jobs"], [])
        self.assertTrue(all(summary["count"] == 0 for summary in result["gpu_distributions_ms"].values()))

    def test_reason_metadata_preserves_hresult_and_large_context_identity(self):
        context_identity = 0xFEDCBA9876543210
        rows = [gpu(category=9, phase=0, job=0, ms=1), gpu(category=9, phase=0, job=1, ms=2),
                gpu(category=9, phase=0, job=4, ms=3), gpu(category=10, phase=0, job=0x8007000E, ms=0),
                gpu(category=10, phase=1, job=0x887A0005, ms=0), gpu(category=10, phase=2, job=1234, ms=0),
                gpu(category=10, phase=3, job=5678, ms=0), gpu(category=10, phase=4, job=context_identity, ms=0),
                gpu(category=10, phase=5, job=context_identity, ms=0)]
        result = module.analyze(rows, []);contract = result["profiler_contract"]
        self.assertEqual(contract["latest_metadata"]["4"]["value"], context_identity)
        self.assertEqual(contract["latest_metadata"]["4"]["value_hex"], "0xFEDCBA9876543210")
        self.assertEqual(contract["latest_metadata"]["0"]["value"], 0x8007000E)
        self.assertEqual(contract["latest_metadata"]["1"]["value_hex"], "0x887A0005")
        self.assertTrue(contract["failed_hresult_observed"])
        self.assertTrue(contract["diagnostic_loss_reported"])
        self.assertIn("not paired", contract["metadata_limit"])
        self.assertIn("generation", contract["metadata_limit"])

    def test_unknown_reasons_metadata_and_policy_remain_uninterpreted(self):
        rows = [gpu(category=9, phase=0, job=999, ms=12), gpu(category=10, phase=47, job=0xFEDCBA9876543210, ms=0),
                gpu(category=11, phase=42, job=77, ms=.125)]
        result = module.analyze(rows, []);contract = result["profiler_contract"]
        self.assertIsNone(contract["loss_reason_counters"][0]["name"])
        self.assertFalse(contract["loss_reason_counters"][0]["recognized"])
        self.assertEqual(contract["unknown_reason_rows"], [rows[0]])
        self.assertIsNone(contract["metadata_observations"][0]["name"])
        self.assertEqual(contract["metadata_observations"][0]["raw_row"], rows[1])
        policy = contract["hud_sampling_policy_observations"][0]
        self.assertFalse(policy["recognized"])
        self.assertIsNone(policy["per_sampled_frame_category_quota"])
        self.assertEqual(policy["raw_row"], rows[2])
        self.assertTrue(contract["uninterpreted_contract_rows"])
        self.assertTrue(result["coverage"]["incomplete"])

    def test_csv_reason_counts_and_metadata_ids_are_exact_integers(self):
        exact_count = 18446744073709551615
        context_identity = 0xFEDCBA9876543210
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/"profiler.csv"
            rows = [gpu(category=9, phase=0, job=16, ms=exact_count),
                    gpu(category=10, phase=4, job=context_identity, ms=0), gpu(category=10, phase=1, job=0x887A0005, ms=0)]
            with source.open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=module.GPU);writer.writeheader();writer.writerows(rows)
            parsed, rejected = module.read_csv(source, module.GPU)
            self.assertEqual(rejected["malformed"], 0)
            self.assertIsInstance(parsed[0]["ms"], int)
            self.assertEqual(parsed[0]["ms"], exact_count)
            self.assertEqual(parsed[1]["id"], context_identity)
            result = module.analyze(parsed, []);contract = result["profiler_contract"]
            self.assertEqual(contract["intentional_quota_skips"]["capture"], exact_count)
            self.assertEqual(contract["latest_metadata"]["4"]["value"], context_identity)
            self.assertEqual(contract["latest_metadata"]["1"]["value"], 0x887A0005)

    def test_delayed_ffx_requires_complete_original_identity(self):
        rows = [gpu(phase=phase_key, ms=sample_values) for phase_key, sample_values in enumerate((1, 2, 7, 8), 1)]
        rows += [gpu(frame=2, phase=6, ms=99), gpu(phase=5, ms=.25), gpu(phase=6, ms=3)]
        result = module.analyze(rows, [])
        matched = next(dispatch_job for dispatch_job in result["fsr_jobs"] if dispatch_job["frame"] == 1)
        self.assertEqual([matched[column_key] for column_key in ("input_ms", "bridge_ms", "output_copy_ms", "wrapper_ms")], [1, 5, 1, 7])
        self.assertEqual(matched["bridge_residual_ms"], 2)
        self.assertEqual(matched["cpu_flush_ms"], .25)
        self.assertIsNone(next(dispatch_job for dispatch_job in result["fsr_jobs"] if dispatch_job["frame"] == 2)["bridge_residual_ms"])

    def test_missing_and_negative_phases_are_not_clamped(self):
        missing = module.analyze([gpu(phase=1), gpu(phase=3, ms=5)], [])["fsr_jobs"][0]
        self.assertIsNone(missing["wrapper_ms"])
        backwards = [gpu(phase=phase_key, ms=sample_values) for phase_key, sample_values in enumerate((1, 3, 2, 4), 1)]
        bad = module.analyze(backwards, [])["fsr_jobs"][0]
        self.assertIn("nonmonotonic_gpu_phases", bad["issues"])
        self.assertIsNone(bad["bridge_ms"])
        rows = [gpu(phase=phase_key, ms=sample_values) for phase_key, sample_values in enumerate((1, 2, 3, 4), 1)]+[gpu(phase=6, ms=5)]
        negative = module.analyze(rows, [])["fsr_jobs"][0]
        self.assertIsNone(negative["bridge_residual_ms"])
        self.assertIn("negative_bridge_residual", negative["issues"])

    def test_replay_after_publication_removes_only_invalidated_category_ticket(self):
        rows = [gpu(category=0, phase=0, ms=10), gpu(category=1, phase=0, ms=4, job=123),
                gpu(category=2, phase=0, ms=2, job=123), gpu(category=2, phase=0, ms=1, job=456),
                gpu(frame=20, category=6, phase=1, ms=0, job=123)]
        result = module.analyze(rows, [])
        measured = result["gpu_frames"][0]
        self.assertEqual(measured["native_list_sum_ms"], 10)
        self.assertIsNone(measured["hud_capture_observed_sum_ms"])
        self.assertEqual(measured["hud_bounds_observed_sum_ms"], 3)

    def test_replay_keys_include_epoch_category_and_deferred_namespace(self):
        rows = [gpu(category=1, phase=0, ms=1, job=4097), gpu(category=2, phase=0, ms=2, job=4097),
                gpu(category=1, phase=0, ms=3, job=4097, epoch=8), gpu(category=1, phase=0, ms=4, job=8192),
                gpu(category=6, phase=1, ms=0, job=4097), gpu(category=6, phase=1, ms=0, job=8192)]
        frames = module.analyze(rows, [])["gpu_frames"]
        epoch7 = next(sample_row for sample_row in frames if sample_row["epoch"] == 7)
        epoch8 = next(sample_row for sample_row in frames if sample_row["epoch"] == 8)
        self.assertEqual(epoch7["hud_capture_observed_sum_ms"], 4)
        self.assertEqual(epoch7["hud_bounds_observed_sum_ms"], 2)
        self.assertEqual(epoch8["hud_capture_observed_sum_ms"], 3)

    def test_missing_conflicting_and_duplicate_ffx_terminals(self):
        rows = [gpu(phase=phase_key, ms=sample_values) for phase_key, sample_values in enumerate((1, 2, 7, 8), 1)]
        result = module.analyze(rows, [])
        self.assertIn("missing_ffx_terminal", result["fsr_jobs"][0]["issues"])
        self.assertTrue(result["coverage"]["incomplete"])
        conflict = module.analyze(rows+[gpu(phase=6, ms=2), gpu(phase=7, ms=0)], [])["fsr_jobs"][0]
        self.assertIn("conflicting_ffx_terminals", conflict["issues"])
        self.assertIsNone(conflict["ffx_gpu_ms"])
        duplicate = module.analyze(rows+[gpu(phase=7, ms=0), gpu(phase=7, ms=0)], [])["fsr_jobs"][0]
        self.assertIn("duplicate_phase_7", duplicate["issues"])

    def test_additive_cpu_chunks_maximum_is_not_added(self):
        rows = [dict(epoch=7, frame=1, thread=thread_key, category=0, count=category_key, ns=ns, maximum=maximum_value)
                for thread_key, category_key, ns, maximum_value in ((100, 2, 3000000, 2000000), (100, 1, 4000000, 4000000), (200, 1, 5000000, 5000000))]
        result = module.analyze([], rows)
        chunk = result["cpu_recording_threads"][0]
        self.assertEqual((chunk["count"], chunk["ns"], chunk["maximum"]), (3, 7000000, 4000000))
        self.assertEqual(result["cpu_recording_frames"][0]["all_threads_work_ms"], 12)
        self.assertEqual(result["cpu_recording_frames"][0]["max_thread_work_ms"], 7)

    def test_sampled_hook_self_omits_nested_categories_per_thread(self):
        rows = [dict(epoch=7, frame=1, thread=thread_key, category=category_key, count=1, ns=ns, maximum=ns)
            for thread_key, category_key, ns in ((100, 0, 2000000), (100, 1, 3000000), (100, 2, 4000000),
                            (100, 3, 1000000), (100, 4, 500000), (100, 5, 10000000),
                            (200, 0, 2000000), (200, 3, 1000000))]
        result = module.analyze([], rows);measured = result["cpu_recording_frames"][0]
        self.assertEqual(measured["all_threads_work_ms"], 14.5)
        self.assertEqual(measured["max_thread_work_ms"], 11.5)
        self.assertTrue(measured["sampled_hook_self_present"])
        self.assertEqual(measured["sampled_hook_self_threads"], 1)
        self.assertTrue(result["coverage"]["cpu_work_is_partial"])
        self.assertEqual(result["cpu_raw_category_distributions_ms"]["5"]["p50"], 10)

    def test_actual_geometry_and_gameplay_filter(self):
        rows = [gpu(category=0, phase=0, ms=2), gpu(frame=2, category=0, phase=0, ms=100)]
        result = module.analyze(rows, [], [frame(), frame(2, width=2534, height=2765)])
        self.assertFalse(result["output_100_percent_verified"])
        self.assertTrue(result["selected_geometry_verified"])
        evidence = [dict(pid=42, width=2112, height=2304, resolution_multiplier_requested=1, resolution_multiplier_effective=1)]
        self.assertTrue(module.analyze(rows, [], [frame()], resolution_evidence=evidence)["output_100_percent_verified"])
        evidence[0]["resolution_multiplier_effective"] = 1.2
        self.assertFalse(module.analyze(rows, [], [frame()], resolution_evidence=evidence)["output_100_percent_verified"])
        evidence[0]["resolution_multiplier_effective"] = 1;evidence[0]["width"] = 2534
        self.assertFalse(module.analyze(rows, [], [frame()], resolution_evidence=evidence)["output_100_percent_verified"])
        self.assertEqual(len(result["gpu_frames"]), 1)
        wrong = module.analyze(rows, [], [frame(width=2534, height=2765)])
        self.assertFalse(wrong["output_100_percent_verified"])
        self.assertEqual(wrong["gpu_frames"], [])
        self.assertFalse(module.analyze(rows, [])["output_100_percent_verified"])
        self.assertFalse(module.analyze(rows, [], [frame(width=2534, height=2765)], 2534, 2765)["output_100_percent_verified"])
        self.assertFalse(result["headset_fps_claimed"])

    def test_budget_and_incomplete_counter(self):
        rows = [gpu(category=7, phase=120, job=2048, frame=45, ms=2048)]
        rows.append({**gpu(category=4, ms=0), "dropped": 1, "failures": 2})
        coverage = module.analyze(rows, [])["coverage"]
        self.assertTrue(coverage["incomplete"])
        self.assertEqual(coverage["budgets"][0], dict(sampled_record_frames=45, used=2048, stride=120, capacity=2048))

    def test_profiler_epoch_is_not_the_xr_pose_epoch(self):
        rows = [gpu(category=0, phase=0, ms=2, epoch=65543)]
        result = module.analyze(rows, [], [frame()])
        self.assertEqual(result["gpu_frames"][0]["epoch"], 65543)
        self.assertEqual(result["configuration_attribution"][0]["gpu_ms"]["native_list_sum_ms"]["p50"], 2)

    def test_ffx_timing_drop_is_unmatched_not_zero_gpu_time(self):
        rows = [gpu(phase=phase_key, ms=sample_values) for phase_key, sample_values in enumerate((1, 2, 7, 8), 1)]
        rows.append(gpu(phase=7, ms=0))
        result = module.analyze(rows, [])
        self.assertTrue(result["coverage"]["incomplete"])
        self.assertIsNone(result["fsr_jobs"][0]["ffx_gpu_ms"])
        self.assertIsNone(result["fsr_jobs"][0]["bridge_residual_ms"])
        self.assertIn("ffx_timing_dropped_or_unmatched", result["fsr_jobs"][0]["issues"])
        counters = module.analyze([gpu(category=8, phase=4, ms=3)], [])["coverage"]
        self.assertEqual(counters["retirement_counters"]["queued_rows_lost"], 3)
        self.assertTrue(counters["incomplete"])
        self.assertFalse(module.analyze([gpu(category=8, phase=2, ms=0)], [])["coverage"]["incomplete"])

    def test_tail_and_partial_rows_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"gpu.csv"
            path.write_text(",".join(module.GPU)+"\n7,1,3,0,10,1,1,0,0,0\n7,2,3\n7,3,3,0,10,1,1,0,0,0")
            rows, rejected = module.read_csv(path, module.GPU)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rejected["incomplete_tail"], 1)
            self.assertEqual(rejected["malformed"], 1)

    def test_run_reset_rejected_and_cadence_does_not_cross_settings(self):
        with self.assertRaises(ValueError):
            module.analyze([], [], [frame(10), frame(1)])
        frames = [frame(sample_index, start_cpu_ns=sample_index*20000000, temporal_aa=6 if sample_index<3 else 5) for sample_index in range(1, 5)]
        result = module.analyze([], [], frames)
        self.assertEqual(len(result["application_cadence"]), 2)
        self.assertEqual(len(result["configuration_attribution"]), 2)
        self.assertTrue(all(sample_row["application_hz"] == 50 for sample_row in result["application_cadence"]))

    def test_cli_writes_finite_json_with_actual_frame_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = (("gpu", module.GPU, [gpu(category=0, phase=0, ms=3)]),
                    ("cpu", module.CPU, [dict(epoch=7, frame=1, thread=42, category=0, count=2, ns=1000, maximum=750)]),
                    ("frames", module.FRAME, [frame()]))
            for name, fields, rows in data:
                with (root/f"{name}.csv").open("w", newline="") as handle:
                    writer = csv.DictWriter(handle, fieldnames=fields);writer.writeheader();writer.writerows(rows)
            (root/"status.json").write_text(json.dumps(dict(width=2112, height=2304, pid=42,
                resolution_multiplier_requested=1, resolution_multiplier_effective=1)))
            module.main(["--gpu", str(root/"gpu.csv"), "--cpu", str(root/"cpu.csv"),
                         "--frames", str(root/"frames.csv"), "--status", str(root/"status.json"), "--output", str(root/"report.json")])
            result = json.loads((root/"report.json").read_text())
            self.assertTrue(result["output_100_percent_verified"])
            self.assertEqual(result["gpu_frames"][0]["native_list_sum_ms"], 3)
            self.assertEqual(result["cpu_recording_threads"][0]["maximum"], 750)


if __name__ == "__main__":
    unittest.main()
