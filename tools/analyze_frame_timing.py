"""Summarize the bridge's CPU spans and OpenXR prediction times, not GPU/scanout timing."""
import argparse
import csv
import json
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


def analyze(rows):
    groups = {}
    previous = None
    for row in rows:
        if row['display_period_ns'] <= 0 or row['end_cpu_ns'] < row['start_cpu_ns']:
            raise ValueError('Invalid runtime period or CPU span')
        if previous and (row['present'] <= previous['present'] or
                         row['start_cpu_ns'] <= previous['start_cpu_ns']):
            raise ValueError('Frame sequence or CPU clock went backwards')
        name = mode(row)
        group = groups.setdefault(name, dict(samples=0, spans={}, cpu_intervals=[], display_steps=[],
                                             periods=[], pose_lag=[], pose_age=[], mismatched_eye_times=0, readbacks=0))
        group['samples'] += 1
        group['readbacks'] += row['capture_readback']
        group['periods'].append(row['display_period_ns'])
        for field in ('engine_cpu_gap_ns', 'previous_desktop_present_ns', 'pre_xr_cpu_ns',
                      'wait_cpu_ns', 'begin_cpu_ns', 'locate_input_cpu_ns', 'transfer_cpu_ns', 'submit_cpu_ns'):
            value = row[field]
            if value < 0 and field != 'engine_cpu_gap_ns':
                raise ValueError('Negative CPU span: ' + field)
            if value >= 0:
                group['spans'].setdefault(field, []).append(value)
        group['spans'].setdefault('xr_bridge_cpu_ns', []).append(row['end_cpu_ns'] - row['start_cpu_ns'])
        # Compare adjacent frames only within one visible presentation/epoch/size/period.
        keys = ('state', 'epoch', 'should_render', 'width', 'height', 'display_period_ns', 'smaa_enabled')
        comparable = (previous and row['present'] == previous['present'] + 1 and
                      row['state'] in (4, 5) and row['should_render'] and name != 'empty' and
                      not row['capture_readback'] and not previous['capture_readback'] and
                      mode(previous) == name and all(row[k] == previous[k] for k in keys))
        if comparable:
            delta = row['display_time_ns'] - previous['display_time_ns']
            if delta <= 0:
                raise ValueError('Runtime display prediction did not advance')
            group['display_steps'].append(delta / row['display_period_ns'])
            group['cpu_intervals'].append(row['start_cpu_ns'] - previous['start_cpu_ns'])
        if name == 'tracked_gameplay':
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
    with (folder/'xr-frame-timing.csv').open(newline='', encoding='utf-8') as stream:
        report = analyze([{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)])
    result_file=folder/'dxgi-probe-result.json'
    report['simulated_vr']=result_file.is_file() and json.loads(result_file.read_text()).get('simulated_vr') is True
    if report['simulated_vr']:
        report['limitation']='SIMULATED VR: unthrottled development dispatcher; cannot estimate real XR runtime or headset performance. '+report['limitation']
    (folder/'xr-frame-timing.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
