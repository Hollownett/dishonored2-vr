#!/usr/bin/env python3
"""Read-only rolling VR timing analysis; submission cadence is not headset FPS."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import csv
import io
import json
from pathlib import Path
import time

from analyze_frame_timing import analyze, distribution, group_name, merge_present_boundaries, PRESENT_SPANS
from record_live_session import ARTIFACTS, Candidate, SessionWatch, Process, read_json, low_priority, stop_requested

COUNTERS = (
    'game_layers_submitted', 'dropped_layers', 'state_events',
    'frame_timing_dropped',
    'full_body.material_failures', 'full_body.solver_failures',
    'wrist_hud.optional_failures', 'wrist_hud.wait_timeouts',
    'feedback_render.fsr4_dispatches', 'feedback_render.fsr4_fallbacks',
    'feedback_render.fsr4_create_failures',
)
SPANS = ('engine_cpu_gap_ns', 'previous_desktop_present_ns', 'pre_xr_cpu_ns',
         'wait_cpu_ns', 'begin_cpu_ns', 'locate_input_cpu_ns', 'transfer_cpu_ns', 'submit_cpu_ns')


def clean_cadence(rows, threshold_ms=70):
    """Consecutive intervals only: break at captures, mode/epoch changes or missing Presents."""
    result, previous, run = {}, None, None
    for row in rows:
        name = group_name(row)
        valid = (row['gameplay'] and row['submitted'] and not row['settings'] and
                 not row['authored_camera'] and not row['capture_readback'])
        adjacent = (valid and previous is not None and group_name(previous) == name and
                    row['epoch'] == previous['epoch'] and row['present'] == previous['present']+1)
        if valid:
            group = result.setdefault(name, dict(intervals=[], stalls=[]))
        if adjacent:
            interval = row['start_cpu_ns']-previous['start_cpu_ns']
            group['intervals'].append(interval)
            if interval >= threshold_ms*1e6:
                if run is None:
                    run = dict(first_present=previous['present'], last_present=row['present'],
                               frames=0, duration_seconds=0, maximum_interval_ms=0)
                    group['stalls'].append(run)
                run['last_present'] = row['present']
                run['frames'] += 1
                run['duration_seconds'] += interval/1e9
                run['maximum_interval_ms'] = max(run['maximum_interval_ms'], interval/1e6)
            else:
                run = None
        else:
            run = None
        previous = row if valid else None
    return {name: dict(clean_cpu_interval_ms=distribution(group['intervals']),
                       slow_threshold_ms=threshold_ms,
                       sustained_slow_windows=[run for run in group['stalls'] if run['duration_seconds'] >= 1],
                       longest_slow_seconds=max((run['duration_seconds'] for run in group['stalls']), default=0),
                       slow_intervals=sum(value >= threshold_ms*1e6 for value in group['intervals']))
            for name, group in result.items()}


def utc():
    return datetime.now(timezone.utc).isoformat()


def interval_accounting(rows, threshold_ms=70):
    """An XR-start interval contains the previous bridge/tail and the current engine/pre-XR gap."""
    groups, previous = {}, None
    for row in rows:
        valid = (row['gameplay'] and row['submitted'] and not row['settings'] and
                 not row['authored_camera'] and not row['capture_readback'])
        name = group_name(row)
        adjacent = (valid and previous is not None and name == group_name(previous) and
                    row['epoch'] == previous['epoch'] and row['present'] == previous['present']+1)
        if adjacent:
            group = groups.setdefault(name, dict(fast=[], slow=[], invalid_clock_intervals=0))
            interval = row['start_cpu_ns']-previous['start_cpu_ns']
            costs = dict(outgoing_xr_bridge_ns=previous['end_cpu_ns']-previous['start_cpu_ns'],
                         outgoing_desktop_present_ns=row['previous_desktop_present_ns'],
                         incoming_engine_gap_ns=row['engine_cpu_gap_ns'],
                         incoming_pre_xr_ns=row['pre_xr_cpu_ns'])
            remainder = interval-sum(costs.values())
            clocks = ('present_start_ns', 'pre_desktop_ns', 'present_end_ns')
            measured = all(key in previous and key in row for key in clocks)
            okay = interval > 0 and min(*costs.values(), remainder) >= 0
            if measured:
                okay = okay and (previous['end_cpu_ns'] <= previous['pre_desktop_ns'] <=
                                 previous['present_end_ns'] <= row['present_start_ns'] <= row['start_cpu_ns'])
                okay = okay and costs['outgoing_desktop_present_ns'] == previous['present_end_ns']-previous['pre_desktop_ns']
                okay = okay and costs['incoming_engine_gap_ns'] == row['present_start_ns']-previous['present_end_ns']
                okay = okay and costs['incoming_pre_xr_ns'] == row['start_cpu_ns']-row['present_start_ns']
                okay = okay and remainder == previous['pre_desktop_ns']-previous['end_cpu_ns']
            if okay:
                costs['outgoing_post_xr_ns' if measured else 'outgoing_unmeasured_post_xr_ns'] = remainder
                # Nested timers can cross the XR boundary; retain their scope and never add them to costs.
                nested = {key: previous[key] for key in PRESENT_SPANS[3:] if key in previous}
                category = 'slow' if interval >= threshold_ms*1e6 else 'fast'
                group[category].append(dict(interval_ns=interval, costs=costs, outgoing_present_subspans=nested,
                                            from_present=previous['present'], to_present=row['present'], measured=measured))
            else:
                group['invalid_clock_intervals'] += 1
        previous = row if valid else None

    def summarize(samples):
        fields = set().union(*(sample['costs'].keys() for sample in samples)) if samples else set()
        nested = set().union(*(sample['outgoing_present_subspans'].keys() for sample in samples)) if samples else set()
        return dict(intervals=len(samples), measured_boundary_intervals=sum(sample['measured'] for sample in samples),
                    cpu_interval_ms=distribution([sample['interval_ns'] for sample in samples]),
                    exclusive_costs_ms={key.removesuffix('_ns'): distribution([sample['costs'][key] for sample in samples if key in sample['costs']])
                                        for key in sorted(fields)},
                    outgoing_present_subspans_ms={key.removesuffix('_ns'): distribution([sample['outgoing_present_subspans'][key] for sample in samples if key in sample['outgoing_present_subspans']])
                                                  for key in sorted(nested)})
    return {name: dict(fast=summarize(group['fast']), slow=summarize(group['slow']),
                       invalid_clock_intervals=group['invalid_clock_intervals'], slow_threshold_ms=threshold_ms,
                       subspans_are_nested_and_not_additive=True,
                       residual_is_unattributed_without_matched_boundaries=True)
            for name, group in groups.items()}


def counter_values(status):
    result = {}
    for name in COUNTERS:
        value = status
        for part in name.split('.'):
            value = value.get(part) if isinstance(value, dict) else None
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            result[name] = value
    return result


def timing_report(rows, window):
    if not rows:
        raise ValueError('No completed timing samples available')
    cutoff = rows[-1]['start_cpu_ns'] - int(window * 1e9)
    rows = [row for row in rows if row['start_cpu_ns'] >= cutoff]
    raw = analyze(rows)
    cadence = clean_cadence(rows)
    accounting = interval_accounting(rows)
    clean = {}
    previous = None
    for row in rows:
        name = group_name(row)
        if raw['modes'][name]['mode'] in ('tracked_gameplay', 'untracked_gameplay'):
            group = clean.setdefault(name, dict(samples=0, spans={}))
            # The following frame can contain a capture's engine/desktop gap.
            if not row['capture_readback'] and not (previous and previous['capture_readback']):
                group['samples'] += 1
                for field in SPANS + PRESENT_SPANS:
                    if row.get(field, -1) >= 0:
                        group['spans'].setdefault(field.removesuffix('_ns'), []).append(row[field])
                group['spans'].setdefault('xr_bridge_cpu', []).append(row['end_cpu_ns']-row['start_cpu_ns'])
        previous = row
    gameplay = {}
    for name, values in clean.items():
        source = raw['modes'][name]
        gameplay[name] = dict(mode=source['mode'], configuration=source['configuration'],
                             clean_samples=values['samples'],
                             submission_calls_per_second=source['observed_bridge_calls_per_second'],
                             cpu_interval_ms=source['cpu_interval_ms'],
                             adjacent_visible_intervals=source['adjacent_visible_intervals'],
                             cpu_spans_ms={key: distribution(value) for key, value in values['spans'].items()},
                             native_gpu=source['native_gpu'], fsr_gpu=source['fsr_gpu'])
        gameplay[name].update(cadence.get(name, {}))
        gameplay[name]['interval_accounting'] = accounting.get(name)
    return dict(requested_window_seconds=window, rows_in_window=len(rows),
                observed_window_seconds=(rows[-1]['start_cpu_ns']-rows[0]['start_cpu_ns'])/1e9,
                first_present=rows[0]['present'], last_present=rows[-1]['present'],
                capture_readback_samples=sum(row['capture_readback'] for row in rows),
                gameplay=gameplay, raw_analysis=raw,
                raw_analysis_cpu_spans_include_capture_frames=True,
                gameplay_cpu_spans_exclude_capture_and_following_frame=True,
                hardware_refresh_measured=False, headset_acceptance=False)


def source_info(path):
    try:
        stat = path.stat()
        return dict(mtime_ns=stat.st_mtime_ns, bytes=stat.st_size,
                    age_seconds=max(0, time.time()-stat.st_mtime))
    except FileNotFoundError:
        return None


def save(output, report):
    temp = output / '.latest.tmp'
    temp.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    temp.replace(output / 'latest.json')
    compact = {key: value for key, value in report.items() if key != 'timing'}
    compact['gameplay'] = report.get('timing', {}).get('gameplay', {})
    with (output / 'history.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(compact)+'\n')


def bounded_rows(folder):
    rows, skipped, names = [], 0, []
    for name in ('xr-frame-timing-previous.csv', 'xr-frame-timing.csv'):
        path = folder / name
        if not path.is_file():
            continue
        names.append(name)
        with path.open('rb') as stream:
            header = stream.readline(16384)
            if not header.endswith(b'\n'):
                raise ValueError('Timing header exceeds bound')
            start = stream.tell()
            stream.seek(0, 2)
            end = stream.tell()
            if end - start > 1024 * 1024:
                stream.seek(end - 1024 * 1024)
                stream.readline(16384)  # discard partial first tail row
            else:
                stream.seek(start)
            tail = stream.read(1024 * 1024)
        records = list(csv.DictReader(io.StringIO((header + tail).decode('utf-8'))))
        for index, record in enumerate(records):
            if None in record:
                raise ValueError('Extra timing columns')
            if any(value in (None, '') for value in record.values()):
                if index == len(records)-1:
                    skipped += 1
                    continue
                raise ValueError('Incomplete timing record')
            rows.append({key: int(value) for key, value in record.items()})
    merge_present_boundaries(folder, rows, tail_bytes=1024*1024)
    return rows, dict(timing_chunks=names, incomplete_tail_records=skipped, bounded_tail_bytes_per_chunk=1024*1024)


def run(args):
    process = Process(args.pid)
    output = args.output.resolve()
    try:
        candidate = Candidate(getattr(args, 'candidate_manifest', None))
        identity = candidate.verify(process, args.pid)
        watch = SessionWatch(args.pid, candidate.identity_path.stat().st_mtime_ns, getattr(args, 'stale_seconds', 15))
        output.mkdir(parents=True, exist_ok=False)
        start = time.monotonic()
        previous_counters = None
        previous_recoveries = None
        samples = 0
        reason = 'duration_complete'
        report = dict(pid=args.pid, candidate_manifest=str(candidate.manifest_path), started_utc=utc(), loaded_identity=identity,
                      state='analyzing', seconds=args.seconds, interval_seconds=args.interval,
                      captures_requested=False, game_or_settings_writes=False)
        try:
            while time.monotonic()-start < args.seconds:
                iteration = time.monotonic()
                if stop_requested(output):
                    reason = 'stopped_by_owner'
                    break
                if not process.alive():
                    reason = 'game_exited'
                    break
                try:
                    current_identity = candidate.verify(process, args.pid)
                except (OSError, ValueError):
                    reason = 'loaded_identity_changed'
                    break
                status = read_json(ARTIFACTS / 'xr-live-status.json')
                reason_now, fresh = watch.check(status, ARTIFACTS / 'xr-live-status.json', iteration)
                if reason_now:
                    reason = reason_now
                    break
                if not fresh:
                    time.sleep(min(1, args.seconds - (time.monotonic()-start)))
                    continue
                if (output / 'history.jsonl').exists() and (output / 'history.jsonl').stat().st_size >= 64*1024*1024:
                    reason = 'history_size_limit'
                    break
                report.update(sample_utc=utc(), elapsed_seconds=iteration-start,
                              sources={name: source_info(ARTIFACTS/name) for name in
                                       ('xr-frame-timing.csv', 'xr-frame-timing-previous.csv',
                                        'xr-live-status.json', 'xr-recoveries.log', 'synchronized-diagnostics.jsonl')})
                report.pop('timing', None)
                report.pop('diagnostic_error', None)
                try:
                    if not current_identity or not status:
                        raise ValueError('Identity or status temporarily unavailable; no metrics inferred')
                    counters = counter_values(status)
                    report['status'] = {key: status.get(key) for key in
                                        ('gameplay', 'tracked_render_pair', 'session_state',
                                         'vr_settings_open', 'runtime_should_render', 'tick_ms')}
                    report['counters'] = counters
                    report['counter_deltas'] = ({key: value-previous_counters[key] for key, value in counters.items()
                                                 if key in previous_counters} if previous_counters is not None else None)
                    report['counter_resets'] = [key for key, value in (report['counter_deltas'] or {}).items() if value < 0]
                    previous_counters = counters
                    recovery = ARTIFACTS / 'xr-recoveries.log'
                    if recovery.exists():
                        if recovery.stat().st_size > 8*1024*1024:
                            raise ValueError('Recovery log exceeds 8MiB analysis bound')
                        with recovery.open('rb') as stream:
                            count = sum(block.count(b'\n') for block in iter(lambda: stream.read(65536), b''))
                        report['recovery_log_records'] = count
                        report['recovery_log_record_delta'] = count-previous_recoveries if previous_recoveries is not None else None
                        previous_recoveries = count
                    rows, chunks = bounded_rows(ARTIFACTS)
                    report['timing'] = timing_report(rows, args.window) | chunks
                except (OSError, ValueError, KeyError, TypeError) as error:
                    report['diagnostic_error'] = str(error)
                samples += 1
                report['samples'] = samples
                save(output, report)
                remaining = min(args.interval-(time.monotonic()-iteration), args.seconds-(time.monotonic()-start))
                if remaining > 0:
                    deadline = time.monotonic() + remaining
                    while time.monotonic() < deadline and not stop_requested(output):
                        time.sleep(min(1, deadline - time.monotonic()))
        except KeyboardInterrupt:
            reason = 'analyzer_interrupted'
        except (OSError, ValueError) as error:
            reason = 'identity_or_io_error'
            report['diagnostic_error'] = str(error)
        report.update(state='complete', stop_reason=reason, ended_utc=utc(), duration_seconds=time.monotonic()-start)
        save(output, report)
        print(json.dumps(dict(state='complete', stop_reason=reason, output=str(output))), flush=True)
        return 0
    finally:
        process.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=1200)
    parser.add_argument('--interval', type=float, default=10)
    parser.add_argument('--window', type=float, default=30)
    parser.add_argument('--output', type=Path, required=True, help='New analyzer-owned output directory')
    parser.add_argument('--candidate-manifest', type=Path, help='Verified numbered candidate; default v220')
    parser.add_argument('--stale-seconds', type=float, default=15)
    args = parser.parse_args(argv)
    if args.pid <= 0 or not 1 <= args.seconds <= 86400 or not 1 <= args.interval <= 60 or not 1 <= args.window <= 300 or not 2 <= args.stale_seconds <= 120:
        parser.error('Positive PID, duration 1..86400, interval 1..60, window 1..300 required')
    try:
        low_priority()
        return run(args)
    except (OSError, ValueError, RuntimeError) as error:
        print(json.dumps(dict(state='refused', error=str(error))), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
