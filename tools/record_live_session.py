#!/usr/bin/env python3
"""Record an already running VR session without controlling the game.

This saves stereo projection images and diagnostics, not scene geometry,
controller-to-palm pose replay, or the headset compositor. Capture readbacks
can perturb frame timing. No game launch, input, injection or settings writes.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import re
import time

import request_vr_capture as capture

ROOT = Path(__file__).resolve().parent.parent
ARTIFACTS = ROOT / 'artifacts'
IDENTITY = ARTIFACTS / 'loaded-render-probe-arm-v220.json'
DLL = ROOT / 'build' / 'd2vr_render_probe_arm_v220.dll'
PINNED_GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\Dishonored2.exe')
MAX_CAPTURES = 12
MAX_PROJECTION_PIXELS = 16 * 1024 * 1024
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_CAPTURE_BYTES = 1536 * 1024 * 1024
MAX_STATUS_BYTES = 64 * 1024 * 1024
DIAGNOSTICS = (
    'xr-live-status.json', 'xr-frame-timing.csv', 'xr-frame-timing-previous.csv', 'present-boundaries.csv', 'present-boundaries-previous.csv',
    'xr-recoveries.log', 'dxgi-probe-failure.txt', 'dxgi-probe-result.json',
    'avatar-fit-events.csv', 'native-arm-upload.csv', 'native-arm-palettes.bin',
    'native-grab-acquisition.csv', 'native-gameplay-aim.csv', 'native-left-aim.csv',
    'probe-launch-config.json', 'stereo-geometry.json',
    'synchronized-diagnostics.jsonl', 'native-grab-decisions.jsonl', 'native-combat.csv',
    'render-perf-cpu.csv', 'render-perf-gpu.csv',
    'left-weapon-bind-receipts.jsonl', 'left-weapon-bind-summary.json',
    'native-assassination-receipts.jsonl',
)
CONFIGS = ('dxgi-probe.cfg', 'vr-settings.cfg', 'full-body', 'fsr4', 'render-perf', 'native-gpu-timing', 'fsr4-compute-timing', 'assassination-receipts')


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def read_json(path: Path) -> dict | None:
    try:
        if path.stat().st_size > 1024 * 1024:
            raise ValueError(f'oversized JSON: {path.name}')
        value = json.loads(path.read_text(encoding='utf-8'))
        return value if isinstance(value, dict) else None
    except (FileNotFoundError, json.JSONDecodeError, PermissionError):
        return None


def low_priority():
    if hasattr(ctypes, 'WinDLL'):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.SetPriorityClass.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.SetPriorityClass.restype = wintypes.BOOL
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x40):
            raise OSError(ctypes.get_last_error(), 'Cannot set helper idle priority')


class Candidate:
    def __init__(self, manifest=None):
        self.manifest_path = Path(manifest).resolve() if manifest else ARTIFACTS / 'repair-v220-test-candidate.json'
        self.manifest = read_json(self.manifest_path)
        if not self.manifest:
            raise ValueError('Candidate manifest is missing or invalid')
        name = self.manifest.get('dll_name', '')
        if not re.fullmatch(r'd2vr_render_probe_arm_v[0-9]+\.dll', name):
            raise ValueError('Candidate must select a numbered arm DLL filename')
        self.dll = ROOT / 'build' / name
        self.identity_path = ARTIFACTS / ('loaded-render-probe-arm-' + name.removeprefix('d2vr_render_probe_arm_').removesuffix('.dll') + '.json')
        self.hash = self.manifest.get('sha256', '')
        self.game_hash = self.manifest.get('game_sha256', '')
        if not all(re.fullmatch('[0-9a-f]{64}', value) for value in (self.hash, self.game_hash)):
            raise ValueError('Candidate requires lowercase DLL and game SHA256 hashes')
        with self.dll.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != self.hash:
                raise ValueError('Candidate DLL differs from manifest')

    def verify(self, process, pid):
        identity = read_json(self.identity_path)
        if not identity or identity.get('pid') != pid or identity.get('sha256') != self.hash:
            raise ValueError('Loaded candidate identity changed or does not match PID/build')
        identity_mtime_ns = self.identity_path.stat().st_mtime_ns
        if hasattr(self, 'identity_mtime_ns') and identity_mtime_ns != self.identity_mtime_ns:
            raise ValueError('Loaded identity was republished; selected session ended')
        self.identity_mtime_ns = identity_mtime_ns
        if hasattr(self, 'loaded_identity') and identity != self.loaded_identity:
            raise ValueError('Loaded candidate identity changed during selected session')
        self.loaded_identity = identity
        if time.monotonic() - getattr(process, "last_build_check", -1e9) >= 10:
            process.verify_build(self, identity)
            process.last_build_check = time.monotonic()
        return identity


class SessionWatch:
    """Never infer fresh metrics from an unchanged or ended same-PID session."""
    def __init__(self, pid, identity_mtime_ns, stale_seconds=15):
        self.pid, self.identity_mtime_ns, self.stale_seconds = pid, identity_mtime_ns, stale_seconds
        self.signature, self.last_change = None, time.monotonic()

    def check(self, status, path, now):
        try:
            stat = path.stat()
        except FileNotFoundError:
            stat = None
        if status and status.get('pid') != self.pid:
            return 'live_pid_changed', False
        result_path = path.parent / 'dxgi-probe-result.json'
        result = read_json(result_path)
        if result and result.get('pid') == self.pid and (result.get('stop_requested') or result.get('hook_disabled')):
            if result_path.stat().st_mtime_ns >= self.identity_mtime_ns and (not stat or result_path.stat().st_mtime_ns >= stat.st_mtime_ns):
                return 'session_stopped', False
        if status and (status.get('stop_requested') or status.get('session_state') in (6, 8)):
            return 'session_stopped', False
        signature = (status.get('tick_ms', stat.st_mtime_ns), status.get('game_layers_submitted'), status.get('session_state'), status.get('pose_epoch')) if stat and status else None
        fresh = signature is not None and signature != self.signature
        if fresh:
            self.signature, self.last_change = signature, now
        if stat and time.time() - stat.st_mtime > self.stale_seconds:
            return 'status_stale', False
        if now - self.last_change >= self.stale_seconds:
            return 'status_no_progress', False
        return None, fresh


def stop_requested(output):
    return (output / 'stop.request').is_file()


class Process:
    """A persistent read-only process handle also prevents PID reuse confusion."""
    def __init__(self, pid: int):
        if not hasattr(ctypes, 'WinDLL'):
            raise RuntimeError('This live recorder requires Windows')
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.handle = self.kernel.OpenProcess(0x100000 | 0x0410, False, pid)
        if not self.handle:
            raise OSError(ctypes.get_last_error(), 'Cannot open expected game process read-only')
        buffer = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(buffer))
        if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, buffer, ctypes.byref(length)):
            self.close()
            raise OSError(ctypes.get_last_error(), 'Cannot identify expected game process')
        self.image = buffer.value
        if Path(self.image).name.lower() != 'dishonored2.exe':
            self.close()
            raise ValueError('Expected PID is not Dishonored2.exe')

    def verify_build(self, candidate, identity):
        if Path(self.image).resolve() != PINNED_GAME.resolve():
            raise ValueError('Running executable is not the pinned installed game path')
        if not getattr(self, 'game_hash_verified', False):
            with Path(self.image).open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != candidate.game_hash:
                    raise ValueError('Running executable differs from candidate manifest')
            self.game_hash_verified = True
        psapi = ctypes.WinDLL('psapi', use_last_error=True)
        psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
        psapi.EnumProcessModulesEx.restype = wintypes.BOOL
        psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
        psapi.GetModuleFileNameExW.restype = wintypes.DWORD
        modules = (wintypes.HMODULE * 2048)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(self.handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3) or needed.value > ctypes.sizeof(modules):
            raise OSError(ctypes.get_last_error(), 'Cannot enumerate loaded modules within bound')
        matches = []
        for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            buffer = ctypes.create_unicode_buffer(32768)
            count = psapi.GetModuleFileNameExW(self.handle, module, buffer, len(buffer))
            if not count or count >= len(buffer):
                raise OSError(ctypes.get_last_error(), 'Cannot identify loaded module')
            if Path(buffer.value).name.lower() == candidate.dll.name.lower():
                matches.append((Path(buffer.value).resolve(), module))
        if len(matches) != 1 or matches[0][0] != candidate.dll.resolve() or matches[0][1] != identity.get('base'):
            raise ValueError('Exact workspace candidate DLL path/base is not loaded')

    def alive(self) -> bool:
        result = self.kernel.WaitForSingleObject(self.handle, 0)
        if result == 0xffffffff:
            raise OSError(ctypes.get_last_error(), 'Process liveness query failed')
        return result == 258

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class Recorder:
    def __init__(self, pid: int, output: Path, seconds: float, projection_interval: float, candidate_manifest=None, stale_seconds=15):
        self.pid, self.output = pid, output.resolve()
        self.seconds, self.projection_interval = seconds, projection_interval
        self.process = Process(pid)
        try:
            self.candidate = Candidate(candidate_manifest)
            self.identity = self.candidate.verify(self.process, pid)
            self.identity_mtime_ns = self.candidate.identity_path.stat().st_mtime_ns
            self.expected_hash = self.candidate.hash
            self.watch = SessionWatch(pid, self.identity_mtime_ns, stale_seconds)
        except Exception:
            self.process.close()
            raise
        try:
            status = read_json(ARTIFACTS / 'xr-live-status.json')
            reason, _ = self.watch.check(status, ARTIFACTS / 'xr-live-status.json', time.monotonic())
            if not status or reason or status.get('pid') != pid:
                raise ValueError('Current live status is absent, ended, stale or does not identify the expected PID')
            self.output.mkdir(parents=True, exist_ok=False)
        except Exception:
            self.process.close()
            raise
        self.archive_bytes = self.capture_bytes = self.samples = self.capture_attempts = 0
        self.status_bytes = 0
        self.next_projection = 0.0
        self.pending: tuple[str, float] | None = None
        self.pending_timeout_reported = False
        self.rotation_signatures = {}
        self.rotations = {}
        self.manifest = dict(schema_version=1, pid=pid, game_image=self.process.image,
                             loaded_identity=self.identity, candidate_manifest=str(self.candidate.manifest_path), started_utc=utc(), state='recording',
                             requested_seconds=seconds, projection_interval_seconds=projection_interval,
                             limits=dict(max_captures=MAX_CAPTURES, max_file_bytes=MAX_FILE_BYTES,
                                         archive_bytes=MAX_ARCHIVE_BYTES, capture_raw_bytes=MAX_CAPTURE_BYTES,
                                         status_bytes=MAX_STATUS_BYTES),
                             evidence=dict(stereo_projection=True, telemetry=True, scene_geometry=False,
                                           raw_controller_and_rendered_palm_pose_replay=False,
                                           headset_compositor=False, headset_visual_acceptance=False),
                             limitations=['Projection images exclude wrist/other compositor quad layers.',
                                          'GPU readbacks can perturb timing; capture frames must be excluded from benchmarks.',
                                          'Status is sampled at approximately 1 Hz; source file timestamps are preserved.',
                                          'Live CSV copies can include an incomplete final row; completed samples remain usable.'],
                             captures=[], snapshots=[])

    def verify_identity(self):
        return self.candidate.verify(self.process, self.pid)

    def event(self, kind: str, **data):
        with (self.output / 'events.jsonl').open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(dict(sample_utc=utc(), kind=kind, **data)) + '\n')

    def save_manifest(self):
        self.manifest.update(samples=self.samples, archive_bytes=self.archive_bytes,
                             capture_raw_bytes=self.capture_bytes, status_bytes=self.status_bytes,
                             capture_attempts=self.capture_attempts, rotation_counts=dict(self.rotations),
                             pending_capture_id=self.pending[0] if self.pending else None,
                             updated_utc=utc())
        temp = self.output / '.manifest.tmp'
        temp.write_text(json.dumps(self.manifest, indent=2), encoding='utf-8')
        temp.replace(self.output / 'manifest.json')

    def copy_file(self, source: Path, destination: Path) -> bool:
        try:
            size = source.stat().st_size
            if size > MAX_FILE_BYTES or self.archive_bytes + size > MAX_ARCHIVE_BYTES:
                self.event('archive_limit', source=str(source), bytes=size)
                return False
            destination.parent.mkdir(parents=True, exist_ok=True)
            # Read at most the validated size even while an append-only log grows.
            with source.open('rb') as incoming, destination.open('xb') as outgoing:
                remaining = size
                while remaining:
                    block = incoming.read(min(1024 * 1024, remaining))
                    if not block:
                        break
                    outgoing.write(block)
                    remaining -= len(block)
            copied = destination.stat().st_size
            self.archive_bytes += copied
            self.manifest['snapshots'].append(dict(source=str(source), path=str(destination.relative_to(self.output)),
                                                    bytes=copied, source_mtime_ns=source.stat().st_mtime_ns,
                                                    predates_live_identity=source.stat().st_mtime_ns < self.identity_mtime_ns,
                                                    sample_utc=utc()))
            return True
        except FileNotFoundError:
            return False

    def snapshot(self, name: str):
        for filename in DIAGNOSTICS:
            self.copy_file(ARTIFACTS / filename, self.output / name / 'artifacts' / filename)
        for source in (self.candidate.identity_path, self.candidate.manifest_path):
            self.copy_file(source, self.output / name / 'candidate' / source.name)
        for filename in CONFIGS:
            self.copy_file(ROOT / 'build' / filename, self.output / name / 'build' / filename)

    def preserve_rotation(self):
        for filename, folder in [('xr-frame-timing-previous.csv', 'timing-rotations'),
                                 ('present-boundaries-previous.csv', 'boundary-rotations')]:
            path = ARTIFACTS / filename
            try:
                stat = path.stat()
                with path.open('rb') as stream:
                    prefix = stream.read(4096)
                signature = (stat.st_size, stat.st_mtime_ns, hashlib.sha256(prefix).hexdigest())
                if signature != self.rotation_signatures.get(filename):
                    counter = self.rotations.get(filename, 0) + 1
                    destination = self.output / folder / f'{counter:04d}.csv'
                    while destination.exists():
                        counter += 1
                        destination = self.output / folder / f'{counter:04d}.csv'
                    if self.copy_file(path, destination):
                        self.rotation_signatures[filename] = signature
                        self.rotations[filename] = counter
            except FileNotFoundError:
                pass

    def projections(self, status: dict | None, elapsed: float):
        if self.pending:
            nonce, requested = self.pending
            records = capture.read_pair(ARTIFACTS, nonce)
            if records:
                if any(metadata['width'] * metadata['height'] > MAX_PROJECTION_PIXELS for _, metadata in records):
                    raise ValueError('Projection exceeds bounded conversion memory budget')
                actual = sum(raw.stat().st_size for raw, _ in records)
                if actual > MAX_CAPTURE_BYTES or actual > MAX_CAPTURE_BYTES - (self.capture_bytes - self.pending_estimate):
                    raise ValueError('Completed projection exceeds bounded capture budget')
                self.capture_bytes += actual - self.pending_estimate
                destination = self.output / 'projections' / nonce
                destination.mkdir(parents=True, exist_ok=False)
                local = []
                for raw, metadata in records:
                    shutil.copyfile(raw, destination / raw.name)
                    metadata_path = destination / raw.with_suffix('.json').name
                    metadata_path.write_text(json.dumps(metadata, indent=2), encoding='utf-8')
                    local.append((destination / raw.name, metadata))
                try:
                    outputs = capture.convert_pair(local)
                except ImportError:
                    outputs = [raw for raw, _ in local]
                    self.event('png_conversion_unavailable', request_id=nonce, retained_rgba=True)
                else:
                    for raw, _ in local:
                        raw.unlink()  # Only this recorder's private copies.
                self.manifest['captures'].append(dict(request_id=nonce, sample_utc=utc(),
                                                        paths=[str(p.relative_to(self.output)) for p in outputs],
                                                        frame=records[0][1]['frame'], pose_epoch=records[0][1]['pose_epoch']))
                self.event('projection_complete', request_id=nonce)
                self.pending = None
                self.pending_timeout_reported = False
            elif elapsed - requested > 30 and not self.pending_timeout_reported:
                self.pending_timeout_reported = True
                self.event('projection_wait_timeout', request_id=nonce, request_preserved=True)
            return
        if not self.projection_interval or self.capture_attempts >= MAX_CAPTURES:
            return
        if elapsed < self.next_projection or not status or not status.get('gameplay') or not status.get('tracked_render_pair') or status.get('vr_settings_open'):
            return
        if (ARTIFACTS / 'capture-eye-pair.request').exists():
            return
        estimate = status.get('width', 0) * status.get('height', 0) * 4 * 4
        if estimate <= 0 or estimate // 16 > MAX_PROJECTION_PIXELS or estimate > MAX_CAPTURE_BYTES - self.capture_bytes:
            return
        try:
            nonce = capture.request_capture(ARTIFACTS, 'session-recorder')
        except FileExistsError:
            return
        self.capture_attempts += 1
        self.capture_bytes += estimate
        self.pending_estimate = estimate
        self.next_projection = elapsed + self.projection_interval
        self.pending = (nonce, elapsed)
        self.event('projection_requested', request_id=nonce, estimated_bytes=estimate,
                   timing_may_be_perturbed=True)

    def run(self) -> int:
        start = time.monotonic()
        reason, result = 'duration_complete', 0
        try:
            self.snapshot('initial')
            self.save_manifest()
            print(json.dumps(dict(state='recording', pid=self.pid, output=str(self.output),
                                  scene_geometry=False, headset_compositor=False)), flush=True)
            while time.monotonic() - start < self.seconds:
                iteration = time.monotonic()
                if stop_requested(self.output):
                    reason = 'stopped_by_owner'
                    break
                if not self.process.alive():
                    reason = 'game_exited'
                    break
                self.verify_identity()
                path = ARTIFACTS / 'xr-live-status.json'
                status = read_json(path)
                reason_now, fresh = self.watch.check(status, path, iteration)
                if reason_now:
                    reason = reason_now
                    break
                if not fresh:
                    time.sleep(min(1, self.seconds - (time.monotonic() - start)))
                    continue
                try:
                    mtime = path.stat().st_mtime_ns
                except FileNotFoundError:
                    mtime = None
                with (self.output / 'status.jsonl').open('a', encoding='utf-8') as stream:
                    line = json.dumps(dict(sample_utc=utc(), elapsed_seconds=iteration-start,
                                           source_mtime_ns=mtime, status=status)) + '\n'
                    count = len(line.encode('utf-8'))
                    if self.status_bytes + count > MAX_STATUS_BYTES:
                        reason = 'status_size_limit'
                        break
                    stream.write(line)
                    self.status_bytes += count
                self.samples += 1
                self.preserve_rotation()
                try:
                    self.projections(status, iteration-start)
                except (OSError, ValueError) as error:
                    self.event('projection_error', error=str(error),
                               request_id=self.pending[0] if self.pending else None,
                               request_preserved=True)
                    self.manifest['abandoned_capture_id'] = self.pending[0] if self.pending else None
                    self.projection_interval = 0
                    self.pending = None
                self.save_manifest()
                remaining = min(1 - (time.monotonic()-iteration), self.seconds-(time.monotonic()-start))
                if remaining > 0:
                    time.sleep(remaining)
        except KeyboardInterrupt:
            reason = 'recorder_interrupted'
        except (OSError, ValueError) as error:
            reason, result = 'identity_or_io_error', 2
            self.event(reason, error=str(error))
        finally:
            # A different PID's shared logs must never become this session's final snapshot.
            try:
                final_identity = read_json(self.candidate.identity_path)
                final_status = read_json(ARTIFACTS / 'xr-live-status.json')
                if final_identity and final_identity.get('pid') == self.pid and final_identity.get('sha256') == self.expected_hash and (not final_status or final_status.get('pid') == self.pid):
                    self.snapshot('final')
                self.manifest.update(state='complete', stop_reason=reason, ended_utc=utc(),
                                     duration_seconds=time.monotonic()-start)
                self.save_manifest()
            finally:
                self.process.close()
        print(json.dumps(dict(state='complete', stop_reason=reason, output=str(self.output))), flush=True)
        return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=1200)
    parser.add_argument('--output', type=Path, required=True, help='New recorder-owned output directory')
    parser.add_argument('--projection-interval', type=float, default=0, help='Seconds between gameplay projection requests; 0 disables')
    parser.add_argument('--candidate-manifest', type=Path, help='Verified numbered candidate; default v220')
    parser.add_argument('--stale-seconds', type=float, default=15, help='Stop after status becomes stale or stops changing')
    args = parser.parse_args(argv)
    if args.pid <= 0 or not 1 <= args.seconds <= 86400 or not 0 <= args.projection_interval <= 86400 or not 2 <= args.stale_seconds <= 120:
        parser.error('PID must be positive, duration 1..86400, projection interval 0..86400')
    try:
        low_priority()
        return Recorder(args.pid, args.output, args.seconds, args.projection_interval, args.candidate_manifest, args.stale_seconds).run()
    except (OSError, ValueError, RuntimeError) as error:
        print(json.dumps(dict(state='refused', error=str(error))), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
