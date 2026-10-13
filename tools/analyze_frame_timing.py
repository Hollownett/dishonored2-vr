"""Summarize matched CPU/FSR GPU samples and OpenXR predictions, not headset scanout."""
import argparse
import csv
import json
import io
import math
from pathlib import Path


def distribution(values, scale=1e-6):
    if not values:
        return None
    values = sorted(v * scale for v in values)

    def percentile(p):
        index = (len(values) - 1) * p
        lower = int(index)
        return values[lower] + (values[math.ceil(index)] - values[lower]) * (index - lower)

    return dict(count=len(values), median=percentile(.5), p95=percentile(.95),
                maximum=values[-1], minimum=values[0])


def mode(row):
    if not row['submitted']:
        return 'empty'
    if row['settings']:
        return 'settings'
    if row['authored_camera']:
        return 'authored_camera'
    if not row['gameplay']:
        return 'native_menu'
    return 'tracked_gameplay' if row['tracked_pair'] else 'untracked_gameplay'


CONFIG_FIELDS = ('temporal_aa', 'render_fraction_permille', 'smaa_enabled', 'width', 'height',
                 'fsr_render_left_width', 'fsr_render_left_height',
                 'fsr_render_right_width', 'fsr_render_right_height',
                 'taa_viewport_left_width', 'taa_viewport_left_height',
                 'taa_viewport_right_width', 'taa_viewport_right_height')

NATIVE_GPU_FIELDS = ('native_gpu_frames', 'native_gpu_lists', 'native_gpu_sequence',
                     'native_gpu_list_sum_ns', 'native_gpu_elapsed_ns', 'native_gpu_last_lists',
                     'native_gpu_dropped_lists', 'native_gpu_epoch')

PRESENT_SPANS = ('post_xr_cpu_ns', 'desktop_present_cpu_ns', 'present_total_cpu_ns',
                 'logic_discovery_cpu_ns', 'lean_cpu_ns', 'grab_control_cpu_ns',
                 'takedown_crouch_cpu_ns', 'controls_cpu_ns',
                 'post_xr_housekeeping_cpu_ns', 'gpu_hook_maintenance_cpu_ns')


def merge_present_boundaries(folder, rows, tail_bytes=None):
    """Match the same completed Present/XR frame; never shift a late row onto its neighbour."""
    boundaries = {}
    for name in ('present-boundaries-previous.csv', 'present-boundaries.csv'):
        path = folder / name
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            header = stream.readline(16384)
            start = stream.tell()
            if tail_bytes:
                stream.seek(0, 2)
                if stream.tell()-start > tail_bytes:
                    stream.seek(-tail_bytes, 2)
                    stream.readline(16384)
                else:
                    stream.seek(start)
            content = stream.read()
        records = list(csv.DictReader(io.StringIO((header+content).decode('utf-8'))))
        for index, record in enumerate(records):
            if None in record:
                raise ValueError('Extra Present boundary columns')
            if any(value in (None, '') for value in record.values()):
                if name == 'present-boundaries.csv' and index == len(records)-1:
                    continue
                raise ValueError('Incomplete Present boundary record')
            value = {key: int(item) for key, item in record.items()}
            if value['xr_end_ns'] <= 0:
                continue
            if not (value['present_start_ns'] <= value['xr_end_ns'] <= value['pre_desktop_ns'] <= value['present_end_ns']):
                raise ValueError('Invalid Present boundary order')
            expected = (value['pre_desktop_ns']-value['xr_end_ns'],
                        value['present_end_ns']-value['pre_desktop_ns'],
                        value['present_end_ns']-value['present_start_ns'])
            if expected != tuple(value[key] for key in PRESENT_SPANS[:3]):
                raise ValueError('Inconsistent Present boundary duration')
            boundaries[value['present']] = value
    for row in rows:
        value = boundaries.get(row['present'])
        if value and (value['xr_end_ns'], value['epoch'], value['gameplay']) == (row['end_cpu_ns'], row['epoch'], row['gameplay']):
            row.update({key: value[key] for key in PRESENT_SPANS if key in value})
            row.update({key: value[key] for key in ('present_start_ns', 'pre_desktop_ns', 'present_end_ns')})
            row['capture_readback'] = int(bool(row['capture_readback'] or value['capture_readback']))
    return rows


def configuration(row):
    return {key: row.get(key) for key in CONFIG_FIELDS}


def group_name(row):
    name = mode(row)
    # Preserve the legacy output keys for traces without the newer AA telemetry.
    if 'temporal_aa' not in row:
        return name
    config = configuration(row)
    return name + ':' + ','.join(f'{key}={value}' for key, value in config.items())


def load_rows(folder):
    """Read the older retained chunk first; tolerate only a truncated final record."""
    paths = [folder/name for name in ('xr-frame-timing-previous.csv', 'xr-frame-timing.csv')
             if (folder/name).is_file()]
    rows, skipped = [], 0
    for path in paths:
        with path.open(newline='', encoding='utf-8') as stream:
            reader = csv.DictReader(stream)
            records = list(reader)
            for index, record in enumerate(records):
                if None in record:
                    raise ValueError(f'Extra timing columns in {path.name}')
                truncated = any(value in (None, '') for value in record.values())
                if truncated:
                    if path == paths[-1] and index == len(records)-1:
                        skipped += 1
                        continue
                    raise ValueError(f'Incomplete timing record in {path.name}')
                rows.append({key: int(value) for key, value in record.items()})
    merge_present_boundaries(folder, rows)
    return rows, dict(timing_chunks=[path.name for path in paths], incomplete_tail_records=skipped)


def analyze(rows):
    groups = {}
    previous = None
    for row in rows:
        if row['display_period_ns'] <= 0 or row['end_cpu_ns'] < row['start_cpu_ns']:
            raise ValueError('Invalid runtime period or CPU span')
        if previous and (row['present'] <= previous['present'] or
                         row['start_cpu_ns'] <= previous['start_cpu_ns']):
            raise ValueError('Frame sequence or CPU clock went backwards')
        name = group_name(row)
        group = groups.setdefault(name, dict(samples=0, spans={}, cpu_intervals=[], display_steps=[],
                                             periods=[], pose_lag=[], pose_age=[], mismatched_eye_times=0, readbacks=0,
                                             configuration=configuration(row), mode=mode(row), desktop_sync={},
                                             requested_sync={}, gpu={side: dict(values=[], completed=0, resets=0)
                                                                     for side in ('left', 'right')}, fsr_counters={},
                                             native_gpu=dict(available=False, sums=[], elapsed=[], coverage=[],
                                                             dropped_coverage=[], completed=0, lists=0, resets=0, epochs=set())))
        group['samples'] += 1
        group['readbacks'] += row['capture_readback']
        group['periods'].append(row['display_period_ns'])
        for field, target in (('sync_interval', 'requested_sync'), ('desktop_sync_interval', 'desktop_sync')):
            if field in row:
                counts = group[target]
                counts[row[field]] = counts.get(row[field], 0) + 1
        for field in ('engine_cpu_gap_ns', 'previous_desktop_present_ns', 'pre_xr_cpu_ns',
                      'wait_cpu_ns', 'begin_cpu_ns', 'locate_input_cpu_ns', 'transfer_cpu_ns', 'submit_cpu_ns'):
            value = row[field]
            if value < 0 and field != 'engine_cpu_gap_ns':
                raise ValueError('Negative CPU span: ' + field)
            if value >= 0:
                group['spans'].setdefault(field, []).append(value)
        group['spans'].setdefault('xr_bridge_cpu_ns', []).append(row['end_cpu_ns'] - row['start_cpu_ns'])
        for field in PRESENT_SPANS:
            if field in row:
                if row[field] < 0:
                    raise ValueError('Negative Present CPU span: '+field)
                group['spans'].setdefault(field, []).append(row[field])
        # Last-query telemetry may repeat for many Presents. Count it only when
        # a new result completes, within a stable configuration/visible mode.
        same_group = previous is not None and group_name(previous) == name and row['epoch'] == previous['epoch']
        native = group['native_gpu']
        if 'native_gpu_sequence' in row:
            if any(field not in row or row[field] < 0 for field in NATIVE_GPU_FIELDS):
                raise ValueError('Missing or negative native GPU telemetry')
            if row['native_gpu_list_sum_ns'] > row['native_gpu_elapsed_ns']:
                raise ValueError('Native GPU list sum exceeds elapsed span')
            native['available'] = True
            native['epochs'].add(row['native_gpu_epoch'])
            same_native_epoch = (same_group and 'native_gpu_sequence' in previous and
                                 row['native_gpu_epoch'] == previous['native_gpu_epoch'])
            if same_native_epoch:
                completed = row['native_gpu_frames'] - previous['native_gpu_frames']
                lists = row['native_gpu_lists'] - previous['native_gpu_lists']
                sequence = row['native_gpu_sequence'] - previous['native_gpu_sequence']
                if min(completed, lists, sequence) < 0:
                    native['resets'] += 1
                else:
                    native['completed'] += completed
                    native['lists'] += lists
                    if sequence > 0 and completed > 0 and not row['capture_readback'] and not previous['capture_readback']:
                        native['sums'].append(row['native_gpu_list_sum_ns'])
                        native['elapsed'].append(row['native_gpu_elapsed_ns'])
                        native['coverage'].append(row['native_gpu_last_lists'])
                        native['dropped_coverage'].append(row['native_gpu_dropped_lists'])
        for side, gpu in group['gpu'].items():
            count_field, value_field = f'fsr_gpu_{side}_samples', f'fsr_gpu_{side}_ns'
            if count_field not in row or value_field not in row:
                continue
            if row[count_field] < 0 or row[value_field] < 0:
                raise ValueError('Negative FSR GPU telemetry')
            if same_group and count_field in previous:
                delta = row[count_field] - previous[count_field]
                if delta < 0:
                    gpu['resets'] += 1
                elif delta > 0:
                    gpu['completed'] += delta
                    if not row['capture_readback'] and not previous['capture_readback']:
                        gpu['values'].append(row[value_field])
        for field in ('fsr_upscaled', 'fsr_failures'):
            if same_group and field in row and field in previous:
                group['fsr_counters'][field] = group['fsr_counters'].get(field, 0) + max(0, row[field]-previous[field])
        if same_group and not row['capture_readback'] and not previous['capture_readback']:
            for side in ('left', 'right'):
                field = f'fsr_cpu_{side}_ns'
                samples = f'fsr_gpu_{side}_samples'
                advanced = (row.get(samples, 0) > previous.get(samples, 0) or
                            row.get('fsr_upscaled', 0) > previous.get('fsr_upscaled', 0))
                if advanced and field in row and row[field] >= 0:
                    group['spans'].setdefault(field, []).append(row[field])
        # Compare adjacent frames only within one visible presentation/epoch/size/period.
        keys = ('state', 'epoch', 'should_render', 'width', 'height', 'display_period_ns', 'smaa_enabled')
        comparable = (previous and row['present'] == previous['present'] + 1 and
                      row['state'] in (4, 5) and row['should_render'] and mode(row) != 'empty' and
                      not row['capture_readback'] and not previous['capture_readback'] and
                      same_group and all(row[k] == previous[k] for k in keys) and
                      row.get('desktop_sync_interval') == previous.get('desktop_sync_interval'))
        if comparable:
            delta = row['display_time_ns'] - previous['display_time_ns']
            if delta <= 0:
                raise ValueError('Runtime display prediction did not advance')
            group['display_steps'].append(delta / row['display_period_ns'])
            group['cpu_intervals'].append(row['start_cpu_ns'] - previous['start_cpu_ns'])
        if mode(row) == 'tracked_gameplay':
            left, right = row['rendered_display_left_ns'], row['rendered_display_right_ns']
            locate_left, locate_right = row['rendered_locate_left_cpu_ns'], row['rendered_locate_right_cpu_ns']
            if not left or not right or locate_left <= 0 or locate_right <= 0:
                raise ValueError('Tracked pair is missing its rendered pose timestamps')
            if left != right or locate_left != locate_right:
                group['mismatched_eye_times'] += 1
            else:
                group['pose_lag'].append(row['display_time_ns'] - left)
                age = row['end_cpu_ns'] - locate_left
                if age < 0:
                    raise ValueError('Rendered pose locate timestamp is in the CPU future')
                group['pose_age'].append(age)
        previous = row
    if not groups:
        raise ValueError('No completed XR wait/begin/end frames in trace')
    result = {}
    for name, group in groups.items():
        intervals = group['cpu_intervals']
        result[name] = dict(samples=group['samples'], adjacent_visible_intervals=len(intervals),
                            mode=group['mode'], configuration=group['configuration'],
                            requested_desktop_sync_intervals=group['requested_sync'],
                            effective_desktop_sync_intervals=group['desktop_sync'],
                            fsr_counter_increments=group['fsr_counters'],
                            native_gpu=(dict(completed_frame_increments=group['native_gpu']['completed'],
                                             completed_list_increments=group['native_gpu']['lists'],
                                             observed_list_sum_ms=distribution(group['native_gpu']['sums']),
                                             observed_elapsed_span_ms=distribution(group['native_gpu']['elapsed']),
                                             observed_last_lists=distribution(group['native_gpu']['coverage'], 1),
                                             observed_last_dropped_lists=distribution(group['native_gpu']['dropped_coverage'], 1),
                                             partial_coverage_samples=sum(value > 0 for value in group['native_gpu']['dropped_coverage']),
                                             counter_resets=group['native_gpu']['resets'],
                                             epochs=sorted(group['native_gpu']['epochs']))
                                        if group['native_gpu']['available'] else None),
                            fsr_gpu={side: dict(completed_query_increments=gpu['completed'],
                                                observed_last_query_ms=distribution(gpu['values']),
                                                counter_resets=gpu['resets']) for side, gpu in group['gpu'].items()},
                            diagnostic_readback_samples=group['readbacks'],
                            observed_bridge_calls_per_second=(len(intervals)*1e9/sum(intervals) if intervals else None),
                            cpu_interval_ms=distribution(intervals),
                            cpu_spans_ms={k.removesuffix('_ns'): distribution(v) for k, v in group['spans'].items()},
                            runtime_display_period_ms=distribution(group['periods']),
                            predicted_display_step_periods=distribution(group['display_steps'], 1),
                            estimated_skipped_prediction_intervals=sum(max(0, math.floor(step+.5)-1)
                                                                      for step in group['display_steps']),
                            rendered_pose_prediction_lag_ms=distribution(group['pose_lag']),
                            rendered_pose_cpu_age_at_submit_ms=distribution(group['pose_age']),
                            mismatched_eye_pose_times=group['mismatched_eye_times'])
    return dict(modes=result, headset_acceptance=False, hardware_refresh_measured=False,
                limitation='CPU spans include driver waits and diagnostic readbacks, not GPU execution. '
                           'FSR GPU durations cover only the timed per-eye wrapper pass, not native scene rendering '
                           'or a full GPU frame. Only changing completed query counters contribute last-query '
                           'durations; multiple completions between rows expose only the newest duration. '
                           'First rows, mode transitions and counter resets are excluded from GPU comparisons. '
                           'Native GPU list sums cover only instrumented original ExecuteCommandList intervals; '
                           'elapsed spans also include work and queue idle between lists, not GPU busy time. '
                           'Native durations require a changing sample sequence within one configuration and GPU epoch. '
                           'TAA-stage viewport dimensions are not proof of every native scene viewport. '
                           'OpenXR predicted periods and skipped prediction intervals do not measure actual '
                           'headset scanout, compositor drops, reprojection or motion-to-photon latency. '
                           'Pose age ends at xrEndFrame return; compare matched scenes with the same capture settings.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path, help='Workspace run directory containing xr-frame-timing.csv')
    args = parser.parse_args()
    folder = args.folder.resolve()
    if not folder.is_relative_to(Path(__file__).resolve().parents[1]):
        raise ValueError('Use a workspace run directory')
    rows, chunk_info = load_rows(folder)
    report = analyze(rows)
    report.update(chunk_info)
    result_file=folder/'dxgi-probe-result.json'
    report['simulated_vr']=result_file.is_file() and json.loads(result_file.read_text()).get('simulated_vr') is True
    if report['simulated_vr']:
        report['limitation']='SIMULATED VR: unthrottled development dispatcher; cannot estimate real XR runtime or headset performance. '+report['limitation']
    (folder/'xr-frame-timing.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
