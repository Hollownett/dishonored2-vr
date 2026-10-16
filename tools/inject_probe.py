"""Load this workspace's temporary DXGI diagnostic into the exact supported game.

This writes a DLL path into a new process allocation and patches Present through
MinHook. Head-tracking mode temporarily overrides render-view inputs and restores
them after each engine Setup call. The disabled DLL stays until game exit.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
from datetime import datetime, timezone
import json
import math
import os
import re
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import time

from probe_game import PE
from game_state import game_logic_mask
from simulated_gameplay_script import compile_script, validate_script_profile
from native_weapon_attachment_profile import validate_native_weapon_attachment
from archive_build_source import matching_provenance

GAME_HASH = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"
PROBE_OUTPUTS = (
    "synchronized-diagnostics.jsonl", "native-grab-decisions.jsonl", "native-combat.csv",
    "render-perf-cpu.csv", "render-perf-gpu.csv",
    "left-weapon-bind-receipts.jsonl", "left-weapon-bind-summary.json",
    "native-assassination-receipts.jsonl",
    "probe-launch-config.json", "dxgi-probe-result.json", "dxgi-probe-failure.txt", "dxgi-camera-probe.csv", "xr-live-status.json",
    "dxgi-gpu-trace.csv", "dxgi-native-hand-draw-candidates.csv", "native-view-setup.csv", "native-eye-boundary.csv", "native-gameplay-camera.csv", "native-turning-feedback.csv", "native-menu-input.csv", "paired-bind-counts.csv",
    "rendered-head-anchor.csv", "controller-trace.csv", "xr-frame-timing.csv", "xr-frame-timing-previous.csv", "present-boundaries.csv", "present-boundaries-previous.csv", "xr-frame-timing.json", "native-interaction-rays.csv", "native-interaction-targets.csv", "native-physical-grab.csv", "native-physical-grab-loss.txt",
    "dxgi-mono.rgba", "dxgi-left.rgba", "dxgi-stereo.rgba", "dxgi-mono.png", "dxgi-left.png", "dxgi-stereo.png",
    "dxgi-mono-views.json", "dxgi-left-views.json", "dxgi-right-views.json", "dxgi-stereo-views.json",
    "xr-source-left.rgba", "xr-source-right.rgba", "xr-destination-left.rgba", "xr-destination-right.rgba",
    "xr-transfer-left.json", "xr-transfer-right.json",
    "xr-native-source-left.rgba", "xr-native-source-right.rgba",
    "xr-settings.rgba", "xr-settings-transfer.json",
    "native-prompt-probe.jsonl", "native-wrist-diagnostics.json", "wrist-hud-left.rgba", "wrist-hud-left.json", "wrist-hud-right.rgba", "wrist-hud-right.json",
    "native-marker-probe.csv", "native-marker-probe-summary.txt", "native-crosshair.csv",
    "xr-settings-composed.rgba", "xr-settings-composed.json",
    "world-ui-1.rgba", "world-ui-1.json", "world-ui-2.rgba", "world-ui-2.json", "world-ui-3.rgba", "world-ui-3.json",
    "world-ui-target-context.jsonl",
    "xr-source-left.png", "xr-source-right.png", "xr-destination-left.png", "xr-destination-right.png",
    "native-arm-upload.csv", "native-arm-palettes.bin", "avatar-fit-events.csv", "stereo-geometry.json", "native-grab-acquisition.csv", "native-gameplay-aim.csv", "native-left-aim.csv",
    "xr-menu-source-left.rgba", "xr-menu-source-right.rgba", "xr-menu-destination-left.rgba", "xr-menu-destination-right.rgba",
    "xr-menu-transfer-left.json", "xr-menu-transfer-right.json", "xr-lifecycle.csv",
    "dxgi-settled-views.json", "settled-render-state.json",
    "headset-preflight.txt",
    "scene-resolution.json",
    "native-exception-context.txt", "native-exception-context.dmp",
    "simulated-gameplay-script.json", "simulated-gameplay-commands.csv",
    "simulated-cleanup-exception.json",
    "authored-native-left.rgba", "authored-native-right.rgba", "authored-native-views.json", "authored-native-pair.json",
    "native-context-dispatch-change.json",
    "native-context-dispatch-change.jsonl",
    "native-weapon-attachment.csv",
    "native-lean-query-trace.csv",
    "native-lean-lifecycle.csv",
    "native-weapon-model-base.csv",
    "native-weapon-dry-lease.csv",
)


def wait_for_headset(root, seconds=300):
    """No XR session or game writes: wait for the standalone system/device probe."""
    executable = root/'build'/'xr_probe.exe'
    loader = Path(r'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\openxr_loader.dll')
    if not executable.is_file() or not loader.is_file():
        raise RuntimeError('The built headset probe or configured OpenXR loader is missing')
    deadline = time.monotonic()+seconds
    print('Waiting for headset availability using the read-only OpenXR probe...', flush=True)
    while True:
        result = subprocess.run([str(executable), str(loader)], capture_output=True, text=True,
                                timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        output = result.stdout+result.stderr
        (root/'artifacts'/'headset-preflight.txt').write_text(output, encoding='utf-8')
        if result.returncode == 0:
            print('Headset and matching graphics device available; continuing launch.', flush=True)
            return output
        if result.returncode != 3:
            raise RuntimeError('Read-only headset preflight failed: '+output[-2000:])
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Headset stayed unavailable; no diagnostic session was started')
        time.sleep(min(5, remaining))


def preserve_probe_outputs(root, label, identity=None, extra_files=()):
    """Retain actual pixels and logs before another test clears shared outputs."""
    sources = [root/'artifacts'/name for name in PROBE_OUTPUTS if (root/'artifacts'/name).is_file()]
    if not sources:
        return None
    # Timed scripted eye captures and the session recovery log move with the run.
    timed = sorted((root/'artifacts').glob('xr-t*-left.rgba')) + sorted((root/'artifacts').glob('xr-t*-right.rgba'))
    recoveries = root/'artifacts'/'xr-recoveries.log'
    sources += timed + ([recoveries] if recoveries.is_file() else [])
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    destination = root/'artifacts'/'runs'/(stamp+'-'+label)
    destination.mkdir(parents=True, exist_ok=False)
    if identity and identity.is_file():
        sources.append(identity)
    for source in sources:
        shutil.copy2(source, destination/source.name)
    for source, name in extra_files:
        shutil.copy2(source, destination/name)
    for source in timed:
        source.unlink(missing_ok=True)  # Never attribute stale timed frames to the next run.
    return destination


def validate_native_left_aim(args, loaded=None):
    """Reject an unsafe first-experiment profile before any remote writes."""
    if not args.native_left_aim_experiment:
        return
    if not (args.paired and args.vr_preview and args.head_tracking and args.controllers and args.dll_name):
        raise ValueError("Native left aim experiment requires paired XR, --head-tracking, --controllers and --dll-name")
    if not args.native_eye_capture:
        raise ValueError("Native left aim experiment requires --native-eye-capture for verified paired-eye startup and left-pose publication")
    # The camera and weapon traces hook the same native aim producers (0x7f4b30).
    if args.native_camera_trace or getattr(args,'native_weapon_attachment_trace',False):
        raise ValueError("Native left aim is exclusive with --native-camera-trace and --native-weapon-attachment-trace")


def validate_simulated_cleanup_exception(args):
    if not args.simulated_cleanup_exception:
        return
    if not (args.simulated_vr and args.vr_preview and args.paired and args.native_eye_capture and
            args.head_tracking and args.controllers and args.dll_name):
        raise ValueError("Cleanup exception fixture requires --simulated-vr, paired native-eye XR, head tracking, controllers and --dll-name")
    if args.until_stopped or not 10 <= args.seconds <= 120 or args.native_left_aim_experiment or args.saved_settings:
        raise ValueError("Cleanup exception fixture requires a timed 10 to 120 second session without left aim or saved settings")


def probe_config(args, camera_address, mode, turning_mode, menu_mode, simulated_mode):
    # Native parser consumes this append-only positional format.
    fields = (args.pid, camera_address, mode, int(args.pipeline or args.paired),
              int(args.skip_temporal_aa), int(args.full_render_scale), int(args.symmetric_frustum),
              int(args.controllers), int(args.headset_size), int(args.eye_boundary), int(args.native_eye_capture),
              args.seconds, int(args.arm_upload_observer), int(args.optical_density), int(args.arm_palette_copy),
              int(args.tracked_arms), int(args.stable_camera), args.resolution_multiplier, int(args.eye_antialiasing),
              int(args.native_camera_trace), turning_mode, args.snap_degrees, args.smooth_degrees_per_second,
              int(args.saved_settings), menu_mode, simulated_mode, int(args.controller_aim), int(args.physical_grab),
              int(args.native_left_aim_experiment), int(args.simulated_cleanup_exception), int(bool(args.simulated_gameplay_script)), int(getattr(args,'native_weapon_attachment_trace',False)))
    return ' '.join(map(str, fields))+'\n'


def json_args(args):
    return {name: str(value) if isinstance(value, Path) else value
            for name, value in vars(args).items()}


def write_launch_config(root, args, requested_args, game, dll, dll_hash, config_text,
                        started_unix, ended_unix, diagnostic_exit=None, error=None,
                        error_unix=None):
    run_config = {
        'schema': 'D2VR-probe-launch-config-v1',
        'pid': args.pid,
        'executable': str(game['path']),
        'executable_sha256': GAME_HASH,
        'dll_name': dll.name,
        'dll_sha256': dll_hash,
        'started_unix': started_unix,
        'ended_unix': ended_unix,
        'tracked_arms_requested': bool(requested_args.get('tracked_arms')),
        'arm_palette_copy_requested': bool(requested_args.get('arm_palette_copy')),
        'requested_flags': {name+'_requested': value for name, value in requested_args.items()
                            if isinstance(value, bool)},
        'requested_config': requested_args,
        'launcher_argv': sys.argv,
        'probe_config': config_text,
        'probe_config_sha256': hashlib.sha256(config_text.encode('ascii')).hexdigest(),
        'diagnostic_exit': diagnostic_exit,
        'status': 'remote_call_failed' if error else 'diagnostic_failed' if diagnostic_exit else 'completed',
    }
    if error:
        run_config['error'] = {'type': type(error).__name__, 'message': str(error)}
        run_config['launcher_error_unix'] = error_unix
    (root/'artifacts'/'probe-launch-config.json').write_text(
        json.dumps(run_config, indent=2)+'\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--camera-report", type=Path,
                        help="Successful focused movement capture from this process")
    parser.add_argument("--stereo", action="store_true", help="Temporary native side-by-side stereo and frame capture")
    parser.add_argument("--single-eye", action="store_true", help="Compare full-viewport left/right native projections over successive frames")
    parser.add_argument("--candidate", action="store_true", help="Use the separately built candidate after checking the previous diagnostic restored cleanly")
    parser.add_argument("--pipeline", action="store_true", help="Use the separately built pipeline diagnostic to trace D3D11 viewports and render targets")
    parser.add_argument("--paired", action="store_true", help="Use full-viewport native stereo and capture both completed eyes before one Present")
    parser.add_argument("--view-setup", action="store_true", help="Observe native idRenderView::Setup without writing camera or stereo fields")
    parser.add_argument("--wait-for-game", action="store_true", help="Wait up to five minutes for verified native gameplay state before starting")
    parser.add_argument("--wait-for-headset", action="store_true", help="Wait up to five minutes with a read-only OpenXR system/device probe before starting VR")
    parser.add_argument("--dll-name", help="Separately built workspace diagnostic filename; avoids replacing a loaded DLL")
    parser.add_argument("--vr-preview", action="store_true", help="Game-image OpenXR preview; defaults to 30 seconds, or use --until-stopped")
    parser.add_argument("--head-tracking", action="store_true", help="Apply experimental render-only head rotation and headset optical FOV during VR preview")
    parser.add_argument("--skip-temporal-aa", action="store_true", help="Temporarily enable the verified native skip-TAA flag, then restore it; requires --head-tracking")
    parser.add_argument("--full-render-scale", action="store_true", help="Temporarily force native X/Y render scale to 100 percent and restore both values; requires --vr-preview")
    parser.add_argument("--symmetric-frustum", action="store_true", help="Compare a symmetric projection and matching XR FOV to isolate native frustum consumers; requires --head-tracking")
    parser.add_argument("--controllers", action="store_true", help="Route Quest actions into the native XInput 9.1.0 gamepad path during the preview; requires --head-tracking")
    parser.add_argument("--headset-size", action="store_true", help="Request the runtime's recommended eye size through native game output resizing; implies --full-render-scale and requires --head-tracking")
    parser.add_argument("--eye-boundary", action="store_true", help="Trace exact native eye-begin calls against source render-view identity; requires --paired")
    parser.add_argument("--native-eye-capture", action="store_true", help="Capture a matched native eye pair at the second eye boundary; implies --eye-boundary")
    parser.add_argument("--seconds", type=int, help="Diagnostic duration, 10 to 300 seconds; experimental profiles retain their shorter limits; default 30 for XR, 10 for desktop")
    parser.add_argument("--until-stopped", action="store_true", help="Keep a paired session running until --stop, Ctrl+C, game exit or a rendering/runtime failure")
    parser.add_argument("--stop", action="store_true", help="Request clean shutdown of the exact loaded --dll-name; does not alter configuration or outputs")
    parser.add_argument("--arm-upload-observer", action="store_true", help="Read the owned player arm palettes at their verified native consumer; requires --paired")
    parser.add_argument("--optical-density", action="store_true", help="Enlarge a symmetric render frustum to retain the runtime's recommended visible pixel density; requires --headset-size and --symmetric-frustum")
    parser.add_argument("--arm-palette-copy", action="store_true", help="Pass an unchanged owned arm palette through the native consumer; implies --arm-upload-observer")
    parser.add_argument("--tracked-arms", action="store_true", help="Apply experimental grip-driven native arm palettes; requires --controllers and implies --arm-upload-observer")
    parser.add_argument("--stable-camera", action="store_true", help="Session-only guarded native bob/shake cache overrides with atomic restoration; requires --head-tracking")
    parser.add_argument("--eye-antialiasing", action="store_true", help="SMAA 1x Ultra on each completed game eye, no sharpening or temporal history; requires --vr-preview")
    parser.add_argument("--native-camera-trace", action="store_true", help="Passive before/after native gameplay camera and owned physics heading trace; no camera writes")
    parser.add_argument("--vr-turning", choices=('native','snap','smooth'), default='native', help="Experimental native physics turning; requires --controllers. Native preserves the previous gamepad path.")
    parser.add_argument("--snap-degrees", type=float, default=45, help="Fixed snap angle, 5 to 180 degrees; 30 or 45 recommended for the demo")
    parser.add_argument("--smooth-degrees-per-second", type=float, default=90, help="Time-based native smooth turning, 15 to 360 degrees per second")
    parser.add_argument("--resolution-multiplier", type=float, default=1.0, help="Initial linear headset scale, 0.5 to 2.0; reports effective scale after limits. New builds provide paused-menu Y settings with --controllers and --headset-size.")
    parser.add_argument("--saved-settings", action="store_true", help="Load and save build/vr-settings.cfg; saved resolution, AA and turning override initial command-line settings")
    parser.add_argument("--native-menu-observer", action="store_true", help="Observe the owned Iggy cursor update without replacing input; requires --paired")
    parser.add_argument("--native-menu-pointer", action="store_true", help="Experimental controller ray and trigger into the native Iggy menus; requires tracked controllers")
    parser.add_argument("--simulated-vr", action="store_true", help="Development-only scripted XR backend using actual game D3D11 textures; no headset/compositor; timed sessions only")
    parser.add_argument("--simulated-input", action="store_true", help="With --simulated-vr, scripted pause/Resume, walk, jump and turn; restores pause if initially paused; requires at least 10 seconds")
    parser.add_argument("--controller-aim", action="store_true", help="Experimental right-controller aim in owned native interaction query arguments; preserves native ranges/filters, no shared camera writes")
    parser.add_argument("--physical-grab", action="store_true", help="Experimental right-grip near-object hold/drop/throw using an owned native dynamic-body velocity motor")
    parser.add_argument("--simulated-gameplay-script", type=Path, help="Developer-only workspace JSON finite gameplay controller commands; requires timed simulated input/native-eye XR; explicit physical-grab/tracked-arms/controller-aim use production paths without fixed fixtures; does not certify outcomes")
    parser.add_argument("--native-weapon-attachment-trace", action="store_true", help="Default-off passive owned-arm native equipment attachment observation; timed 10..120 seconds, paired native-eye XR head/controllers; no native output writes or weapon outcome certification")
    parser.add_argument("--native-left-aim-experiment", action="store_true", help="Default-off native left weapon/power aim experiment; first injection only, paired XR with --native-eye-capture and explicit snap/smooth, maximum 30 seconds; simulated VR is development evidence only")
    parser.add_argument("--simulated-cleanup-exception", action="store_true", help="Default-off controlled worker C++ exception after at least three submitted simulated XR gameplay frames; exercises actual cleanup, expected diagnostic exit 1; requires timed simulated paired native-eye XR with head/controllers, no left aim or saved settings")
    args = parser.parse_args()
    requested_args = json_args(args)
    if args.stop and (not args.dll_name or any((args.camera_report,args.stereo,args.single_eye,args.candidate,args.pipeline,
            args.paired,args.view_setup,args.wait_for_game,args.vr_preview,args.head_tracking,args.skip_temporal_aa,
            args.full_render_scale,args.symmetric_frustum,args.controllers,args.headset_size,args.eye_boundary,
            args.native_eye_capture,args.seconds is not None,args.until_stopped,args.arm_upload_observer,args.optical_density,args.wait_for_headset,args.arm_palette_copy,args.tracked_arms,args.stable_camera,args.eye_antialiasing,args.native_camera_trace,args.vr_turning!='native',args.snap_degrees!=45,args.smooth_degrees_per_second!=90,args.resolution_multiplier!=1.0,args.saved_settings,args.native_menu_observer,args.native_menu_pointer,args.simulated_vr,args.simulated_input,args.controller_aim,args.physical_grab,args.native_left_aim_experiment,args.simulated_cleanup_exception,args.simulated_gameplay_script,args.native_weapon_attachment_trace))):
        parser.error("Use --stop with only --pid and the exact --dll-name")
    if args.until_stopped and (not args.paired or not args.dll_name or args.seconds is not None):
        parser.error("Continuous mode requires --paired, --dll-name, and no --seconds")
    if args.view_setup and any((args.stereo,args.single_eye,args.paired,args.pipeline,args.candidate,args.camera_report,args.vr_preview)):
        parser.error("View setup observation uses its own DLL and no other diagnostic mode")
    if sum((args.stereo,args.single_eye,args.paired))>1:
        parser.error("Choose one native rendering diagnostic")
    if args.paired and (args.pipeline or args.candidate):
        parser.error("Paired rendering uses its own DLL output")
    if args.pipeline and (args.candidate or not args.stereo):
        parser.error("Pipeline tracing requires --stereo and its own DLL output")
    if args.vr_preview and (not args.paired or not args.dll_name):
        parser.error("VR preview requires --paired and its separately built --dll-name")
    if args.head_tracking and not args.vr_preview:
        parser.error("Head tracking requires --vr-preview")
    if args.stable_camera and not args.head_tracking:
        parser.error("Stable camera requires --head-tracking")
    if args.eye_antialiasing and not args.vr_preview:
        parser.error("Eye antialiasing requires --vr-preview")
    if args.wait_for_headset and not args.vr_preview:
        parser.error("Headset readiness waiting requires --vr-preview")
    if args.simulated_vr and (not args.vr_preview or args.until_stopped or args.wait_for_headset or args.saved_settings):
        parser.error("Simulated VR requires a timed --vr-preview session without headset waiting or saved preferences")
    if args.simulated_input and not (args.simulated_vr and args.controllers):
        parser.error("Simulated input requires --simulated-vr and --controllers")
    if args.controller_aim and not (args.head_tracking and args.controllers):
        parser.error("Controller interaction aim requires --head-tracking and --controllers")
    if args.physical_grab and not (args.head_tracking and args.controllers):
        parser.error("Physical grabbing requires --head-tracking and --controllers")
    if args.skip_temporal_aa and not args.head_tracking:
        parser.error("The temporal antialiasing comparison requires --head-tracking")
    if args.full_render_scale and not args.vr_preview:
        parser.error("The full render-scale comparison requires --vr-preview")
    if args.symmetric_frustum and not args.head_tracking:
        parser.error("The symmetric frustum comparison requires --head-tracking")
    if args.controllers and not args.head_tracking:
        parser.error("Controller input requires --head-tracking")
    if args.vr_turning!='native' and not args.controllers:
        parser.error("Native VR snap/smooth turning requires --controllers")
    if not math.isfinite(args.snap_degrees) or not 5<=args.snap_degrees<=180 or \
            not math.isfinite(args.smooth_degrees_per_second) or not 15<=args.smooth_degrees_per_second<=360:
        parser.error("Snap angle must be 5 to 180 degrees; smooth speed must be 15 to 360 degrees per second")
    if args.headset_size and not args.head_tracking:
        parser.error("Recommended headset sizing requires --head-tracking")
    if args.saved_settings and not (args.headset_size and args.head_tracking and args.controllers):
        parser.error("Saved VR settings require headset sizing, head tracking and controllers")
    if args.native_menu_observer and (not args.paired or args.native_menu_pointer):
        parser.error("Choose passive native-menu observation with --paired or a tracked native-menu pointer")
    if args.native_menu_pointer and not (args.controllers and args.head_tracking):
        parser.error("Native menu pointer requires tracked controllers")
    if not math.isfinite(args.resolution_multiplier) or not .5<=args.resolution_multiplier<=2 or (args.resolution_multiplier!=1.0 and not args.headset_size):
        parser.error("Resolution multiplier requires --headset-size and range 0.5 to 2.0")
    if args.native_eye_capture:
        args.eye_boundary = True
    if args.eye_boundary and not args.paired:
        parser.error("Native eye boundary observation requires --paired")
    if args.arm_upload_observer and not args.paired:
        parser.error("Native arm palette observation requires --paired")
    if args.arm_palette_copy and args.tracked_arms:
        parser.error("Choose unchanged palette copy or tracked arms")
    if (args.arm_palette_copy or args.tracked_arms) and not args.paired:
        parser.error("Arm palette modes require --paired")
    if args.tracked_arms and not args.controllers:
        parser.error("Tracked arm palettes require --controllers")
    if args.arm_palette_copy or args.tracked_arms:
        args.arm_upload_observer = True
    if args.optical_density and not (args.headset_size and args.symmetric_frustum):
        parser.error("Optical density requires --headset-size and --symmetric-frustum")
    if args.headset_size:
        args.full_render_scale = True
    if args.until_stopped:
        args.seconds = 0
    elif args.seconds is None:
        args.seconds = 30 if args.vr_preview else 10
    if not args.until_stopped and not 10 <= args.seconds <= 300:
        parser.error("Diagnostic duration must be 10 to 300 seconds")
    try:
        validate_native_left_aim(args)
        validate_simulated_cleanup_exception(args)
        validate_script_profile(args)
        validate_native_weapon_attachment(args)
        if args.native_weapon_attachment_trace:
            args.arm_upload_observer=True
    except ValueError as error:
        parser.error(str(error))
    native_rendering = args.stereo or args.single_eye or args.paired
    if os.name != "nt" or c.sizeof(c.c_void_p) != 8 or args.pid <= 0:
        parser.error("Use x64 Python on Windows and a positive game PID")
    root = Path(__file__).resolve().parents[1]
    script_source=script_transport=None
    if args.simulated_gameplay_script:
        try:
            script_source,script_transport=compile_script(args.simulated_gameplay_script,root,args.seconds)
        except (OSError,ValueError) as error:
            parser.error(str(error))
    variant = "-view-setup" if args.view_setup else "-paired" if args.paired else "-pipeline" if args.pipeline else "-candidate" if args.candidate else ""
    dll_name = args.dll_name or "d2vr_render_probe"+variant.replace("-","_")+".dll"
    if not re.fullmatch(r"d2vr_render_probe(?:_[a-z0-9_]+)?\.dll",dll_name):
        parser.error("DLL name must be a lowercase workspace diagnostic filename")
    variant=dll_name[len("d2vr_render_probe"):-4].replace("_","-")
    dll = root / "build" / dll_name
    pe = PE(dll.read_bytes())
    if pe.machine != 0x8664:
        parser.error("The diagnostic must be x64")
    entry_rva = pe.code_export("StartProbe")
    camera_address = 0
    if not args.stop and not native_rendering and not args.view_setup:
        if not args.camera_report:
            parser.error("Camera mode requires --camera-report; native stereo mode uses --stereo")
        camera_report = json.loads(args.camera_report.read_text(encoding="utf-8-sig"))
        if camera_report.get("pid") != args.pid or camera_report.get("executable_sha256") != GAME_HASH:
            parser.error("Movement capture belongs to a different process/build")
        if camera_report.get("focused_samples", 0) < 10 or camera_report.get("movement_key_samples", 0) < 10:
            parser.error("Capture did not establish focused gameplay movement")
        objects = camera_report["object_samples"][-1]["objects"]
        cameras = [o for o in objects if o.get("type") == "arkCpntPlayerCamera"]
        if len(cameras) != 1:
            parser.error("Capture must contain one typed player camera")
        camera_address = cameras[0]["address"]
    kernel = c.WinDLL("kernel32", use_last_error=True)

    class Module(c.Structure):
        _fields_ = [("dwSize", w.DWORD), ("th32ModuleID", w.DWORD), ("th32ProcessID", w.DWORD),
                    ("GlblcntUsage", w.DWORD), ("ProccntUsage", w.DWORD), ("modBaseAddr", c.c_void_p),
                    ("modBaseSize", w.DWORD), ("hModule", w.HMODULE),
                    ("szModule", w.WCHAR*256), ("szExePath", w.WCHAR*260)]

    def api(name, result, arguments):
        function = getattr(kernel, name)
        function.restype, function.argtypes = result, arguments
        return function

    snapshot = api("CreateToolhelp32Snapshot", w.HANDLE, [w.DWORD, w.DWORD])
    first = api("Module32FirstW", w.BOOL, [w.HANDLE, c.POINTER(Module)])
    next_module = api("Module32NextW", w.BOOL, [w.HANDLE, c.POINTER(Module)])
    close = api("CloseHandle", w.BOOL, [w.HANDLE])
    open_process = api("OpenProcess", w.HANDLE, [w.DWORD, w.BOOL, w.DWORD])
    read_memory = api("ReadProcessMemory", w.BOOL,
                      [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)])
    write_memory = api("WriteProcessMemory", w.BOOL,
                       [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)])
    allocate = api("VirtualAllocEx", c.c_void_p, [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD])
    free = api("VirtualFreeEx", w.BOOL, [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD])
    remote_thread = api("CreateRemoteThread", w.HANDLE,
                        [w.HANDLE, c.c_void_p, c.c_size_t, c.c_void_p, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)])
    wait = api("WaitForSingleObject", w.DWORD, [w.HANDLE, w.DWORD])
    thread_exit = api("GetExitCodeThread", w.BOOL, [w.HANDLE, c.POINTER(w.DWORD)])
    query_path = api("QueryFullProcessImageNameW", w.BOOL, [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)])
    get_module = api("GetModuleHandleW", w.HMODULE, [w.LPCWSTR])
    get_proc = api("GetProcAddress", c.c_void_p, [w.HMODULE, c.c_char_p])

    def checked(value, operation):
        if not value:
            raise OSError(c.get_last_error(), operation)
        return value

    def modules(pid):
        handle = snapshot(0x8 | 0x10, pid)
        if handle == c.c_void_p(-1).value:
            raise OSError(c.get_last_error(), "Cannot enumerate process modules")
        try:
            item = Module(dwSize=c.sizeof(Module))
            checked(first(handle, c.byref(item)), "Cannot read first module")
            result = []
            while True:
                result.append(dict(path=Path(item.szExePath).resolve(), base=item.modBaseAddr, size=item.modBaseSize))
                if not next_module(handle, c.byref(item)):
                    if c.get_last_error() != 18:
                        raise OSError(c.get_last_error(), "Module enumeration interrupted")
                    return result
        finally:
            close(handle)

    loaded = modules(args.pid)
    try:
        validate_native_left_aim(args, loaded)
    except ValueError as error:
        parser.error(str(error))
    games = [m for m in loaded if m["path"].name.lower() == "dishonored2.exe"]
    if len(games) != 1 or games[0]["size"] != 68988928:
        parser.error("PID is not the supported Dishonored 2 image")
    game = games[0]
    with game["path"].open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != GAME_HASH:
            parser.error("Unsupported executable hash; no DLL loaded")
    prior = [m for m in loaded if m["path"].name.lower() == dll.name.lower()]
    identity_file = root / "artifacts" / ("loaded-render-probe"+variant+".json")
    dll_hash = hashlib.sha256(dll.read_bytes()).hexdigest()
    provenance_files = []
    try:
        provenance = matching_provenance(root, dll.name, dll_hash)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"No build-source provenance will be attached: {error}", flush=True)
        provenance = None
    if provenance:
        provenance_files = [
            (provenance['pointer'], 'build-source-provenance.json'),
            (provenance['manifest'], 'build-source-manifest.json'),
            (provenance['zip'], 'build-source.zip'),
        ]
    if prior:
        identity = json.loads(identity_file.read_text()) if identity_file.exists() else {}
        if (len(prior)!=1 or prior[0]["path"]!=dll or prior[0]["size"]!=pe.image_size or
                identity.get("pid")!=args.pid or identity.get("base")!=prior[0]["base"] or identity.get("sha256")!=dll_hash):
            parser.error("Loaded diagnostic identity differs; restart the game before testing a rebuilt DLL")
    if args.stop and not prior:
        parser.error("The specified diagnostic is not loaded in this game process")
    if variant and not prior and any(m["path"].name.lower()=="d2vr_render_probe.dll" for m in loaded):
        old_identity = json.loads((root/"artifacts"/"loaded-render-probe.json").read_text())
        restored = json.loads((root/"artifacts"/"dxgi-probe-result.json").read_text())
        if old_identity.get("pid") != args.pid or not all(restored.get(key) is True for key in
                ("hook_disabled","backend_hook_disabled","mode_restored","values_restored")):
            parser.error("Previous diagnostic has not confirmed clean restoration; restart the game")
        if restored.get("gpu_trace_requested") and restored.get("gpu_hooks_disabled") is not True:
            parser.error("Previous GPU trace hooks have not confirmed clean restoration; restart the game")
        if restored.get("view_setup_requested") and restored.get("view_setup_hook_disabled") is not True:
            parser.error("Previous native view observer has not confirmed clean restoration; restart the game")
    # Resolve forwarded LoadLibraryW by its actual owning local module, then
    # use that module's base in the target. Never treat a DWORD exit as HMODULE.
    # Steam hooks kernel32's forwarding thunk in this process. Resolve the same
    # system implementation exported by KernelBase, and still require matching
    # owning-module identity and live instructions before calling it.
    loader = checked(get_proc(get_module("kernelbase.dll"), b"LoadLibraryW"), "Cannot resolve LoadLibraryW implementation")
    owners = [m for m in modules(os.getpid()) if m["base"] <= loader < m["base"]+m["size"]]
    remote_owners = [m for m in loaded if len(owners) == 1 and m["path"] == owners[0]["path"]]
    if len(owners) != 1 or len(remote_owners) != 1 or owners[0]["size"] != remote_owners[0]["size"]:
        parser.error("Cannot establish the remote loader module identity")
    remote_loader = remote_owners[0]["base"]+loader-owners[0]["base"]
    process = checked(open_process(0x2 | 0x8 | 0x10 | 0x20 | 0x400, False, args.pid), "Cannot open game for diagnostic injection")
    allocation = None
    path_thread_may_run = False
    stop_address = None
    try:
        path = c.create_unicode_buffer(32768)
        size = w.DWORD(len(path))
        checked(query_path(process, 0, path, c.byref(size)), "Cannot verify opened process")
        if Path(path.value).resolve() != game["path"]:
            raise ValueError("Process identity changed")

        def read(address, size):
            buffer = c.create_string_buffer(size)
            count = c.c_size_t()
            checked(read_memory(process, address, buffer, size, c.byref(count)), "Process memory read failed")
            if count.value != size:
                raise ValueError("Incomplete process read")
            return buffer.raw

        def run_thread(start, parameter, milliseconds):
            handle = checked(remote_thread(process, None, 0, start, parameter, 0, None), "Cannot create diagnostic thread")
            try:
                try:
                    if milliseconds is None:
                        while (status := wait(handle, 500)) == 0x102:
                            pass
                    else:
                        status = wait(handle, milliseconds)
                except KeyboardInterrupt:
                    if milliseconds is not None or stop_address is None:
                        raise
                    print("Stopping on Ctrl+C; waiting for native restoration...", flush=True)
                    stop_code = run_thread(stop_address, None, 5000)
                    if stop_code:
                        raise RuntimeError(f"Probe could not accept stop ({stop_code}); do not rerun")
                    status = wait(handle, 10000)
                if status == 0x102:
                    raise TimeoutError("Diagnostic thread is still running; DLL/allocation retained, do not rerun")
                if status != 0:
                    raise OSError(c.get_last_error(), "Diagnostic thread wait failed; do not rerun")
                code = w.DWORD()
                checked(thread_exit(handle, c.byref(code)), "Cannot read diagnostic thread status")
                return code.value
            finally:
                close(handle)

        def export_address(module_base, module_pe, name):
            rva = module_pe.code_export(name)
            address = module_base+rva
            if read(address, 16) != module_pe.bytes(module_pe.offset(rva, 16), 16):
                raise ValueError(f"Loaded {name} entry differs from the built DLL")
            return address

        if args.stop:
            stop_address = export_address(prior[0]['base'], pe, 'StopProbe')
            code = run_thread(stop_address, None, 5000)
            print(json.dumps(dict(pid=args.pid, stop_requested=code == 0, stop_exit=code,
                                  restoration_pending=code == 0)))
            if code:
                raise RuntimeError("Probe is inactive, starting or already stopping; no new session was started")
            return
        # A running session must not lose its shared configuration or frame evidence.
        for item in loaded:
            if item['path'].parent != dll.parent or not re.fullmatch(r'd2vr_render_probe(?:_[a-z0-9_]+)?\.dll', item['path'].name):
                continue
            existing = PE(item['path'].read_bytes())
            try:
                existing.code_export('ProbeStatus')
            except ValueError:
                continue  # Older timed diagnostics have no status export.
            if run_thread(export_address(item['base'], existing, 'ProbeStatus'), None, 5000):
                raise RuntimeError(f"A diagnostic is still active: {item['path'].name}; stop it before another test")

        remote_bytes = read(remote_loader, 16)
        local_bytes = c.string_at(loader, 16)
        if remote_bytes != local_bytes:
            raise ValueError(f"Remote loader instructions differ: module={owners[0]['path'].name} "
                             f"RVA={loader-owners[0]['base']:#x} local={local_bytes.hex()} remote={remote_bytes.hex()}")
        if args.view_setup:
            game_pe = PE(game["path"].read_bytes())
            if read(game["base"]+0x374e40,32) != game_pe.bytes(game_pe.offset(0x374e40,32),32):
                raise ValueError("Live native view setup entry differs from supported executable")
        elif native_rendering:
            game_pe = PE(game["path"].read_bytes())
            if read(game["base"]+0x5bf00,16) != game_pe.bytes(game_pe.offset(0x5bf00,16),16):
                raise ValueError("Live stereo construction code differs from the supported executable")
            if struct.unpack("<Q",read(game["base"]+0x2be6910,8))[0] != game["base"]+0x1aca0b8:
                raise ValueError("The native renderer must start in single-camera mode")
        else:
            camera = read(camera_address, 0x120)
            if (struct.unpack_from("<Q", camera, 0)[0] != game["base"]+0x1c72218 or
                    struct.unpack_from("<Q", camera, 0x20)[0] != game["base"]+0x1c722f8):
                raise ValueError("Player camera is stale; capture it again")
        headset_preflight = wait_for_headset(root) if args.wait_for_headset else None
        if args.wait_for_game:
            print("Waiting up to five minutes for a loaded game (playing or paused)...", flush=True)
            deadline = time.monotonic()+300
            while game_logic_mask(read, game['base']) not in (2, 4):
                if time.monotonic() >= deadline:
                    raise TimeoutError("Game remained paused, loading, in a menu, or unverifiable; diagnostic not started")
                time.sleep(.1)
        mode = 6 if args.head_tracking else 5 if args.vr_preview else 4 if args.view_setup else 3 if args.paired else 2 if args.single_eye else int(args.stereo)
        turning_mode={'native':0,'snap':1,'smooth':2}[args.vr_turning]
        menu_mode=2 if args.native_menu_pointer else int(args.native_menu_observer)
        required_capabilities = int(args.arm_upload_observer) | (2 if args.optical_density else 0) | (4 if args.arm_palette_copy else 0) | (8 if args.tracked_arms else 0) | (16 if args.stable_camera else 0) | (32 if args.resolution_multiplier!=1.0 else 0) | (64 if args.eye_antialiasing else 0) | (256 if args.native_camera_trace else 0) | (512 if turning_mode else 0) | (1024 if args.saved_settings else 0) | (2048 if menu_mode else 0) | (4096 if args.simulated_vr else 0) | (8192 if args.controller_aim else 0) | (16384 if args.physical_grab else 0) | (32768 if args.native_left_aim_experiment else 0) | (65536 if args.simulated_cleanup_exception else 0) | (131072 if args.simulated_gameplay_script else 0) | (262144 if args.native_weapon_attachment_trace else 0)
        if required_capabilities:
            pe.code_export('ProbeCapabilities')  # Reject older DLLs before changing shared configuration.
        simulated_mode=2 if args.simulated_input else int(args.simulated_vr)
        positional_config = probe_config(args,camera_address,mode,turning_mode,menu_mode,simulated_mode)
        (root / "build" / "dxgi-probe.cfg").write_text(positional_config, encoding="ascii")
        (root / "artifacts").mkdir(exist_ok=True)
        preserve_probe_outputs(root, 'previous-artifacts')
        for name in PROBE_OUTPUTS:
            try:
                (root / "artifacts" / name).unlink(missing_ok=True)
            except PermissionError:  # still open in an earlier, stopped DLL of this game (v451 kept its controller trace open)
                open(root / "artifacts" / name, 'w').close()
        if script_transport is not None:
            (root/'build'/'simulated-gameplay-script.cfg').write_text(script_transport,encoding='ascii')
            (root/'artifacts'/'simulated-gameplay-script.json').write_text(script_source,encoding='utf-8')
        if headset_preflight is not None:
            (root/'artifacts'/'headset-preflight.txt').write_text(headset_preflight, encoding='utf-8')

        duration = 'until stopped' if args.until_stopped else f'for {args.seconds} seconds'
        presentation = 'SIMULATED VR (no headset)' if args.simulated_vr else 'game-image XR preview' if args.vr_preview else 'sampling Present'
        print(f"Loading exact-build diagnostic; {presentation} {duration}...", flush=True)
        if not prior:
            payload = (str(dll)+"\0").encode("utf-16-le")
            allocation = checked(allocate(process, None, len(payload), 0x3000, 0x4), "Cannot allocate DLL path")
            written = c.c_size_t()
            checked(write_memory(process, allocation, payload, len(payload), c.byref(written)), "Cannot write DLL path")
            if written.value != len(payload):
                raise ValueError("Incomplete DLL-path write")
            # On any uncertain wait, keep the tiny path allocation until game exit.
            path_thread_may_run = True
            run_thread(remote_loader, allocation, 10000)
            path_thread_may_run = False
        diagnostic = [m for m in modules(args.pid) if m["path"] == dll]
        if len(diagnostic) != 1 or diagnostic[0]["size"] != pe.image_size:
            raise ValueError("DLL did not load with the expected identity")
        start = diagnostic[0]["base"]+entry_rva
        if read(start, 16) != pe.bytes(pe.offset(entry_rva, 16), 16):
            raise ValueError("Loaded diagnostic entry instructions differ from built DLL")
        if args.until_stopped:
            stop_address = export_address(diagnostic[0]['base'], pe, 'StopProbe')
        if required_capabilities:
            capabilities = run_thread(export_address(diagnostic[0]['base'], pe, 'ProbeCapabilities'), None, 5000)
            if capabilities & required_capabilities != required_capabilities:
                raise ValueError('The exact built DLL does not implement the requested features')
        identity_file.write_text(json.dumps(dict(pid=args.pid,base=diagnostic[0]["base"],sha256=dll_hash)))
        if args.stereo:
            print("Waiting up to 60 seconds for game focus and movement input, then capturing...", flush=True)
        run_started_unix = time.time()
        try:
            code = run_thread(start, None, None if args.until_stopped else args.seconds*1000+10000+(60000 if args.stereo else 0))
        except BaseException as error:
            failure_unix = time.time()
            write_launch_config(root,args,requested_args,game,dll,dll_hash,positional_config,
                                run_started_unix,None,error=error,error_unix=failure_unix)
            evidence = preserve_probe_outputs(root, f'pid{args.pid}{variant}-failed', identity_file, provenance_files)
            print(json.dumps(dict(pid=args.pid, run_config_archived=str(evidence) if evidence else None,
                                  build_source_provenance=bool(provenance_files), status='remote_call_failed')))
            raise
        run_ended_unix = time.time()
        write_launch_config(root,args,requested_args,game,dll,dll_hash,positional_config,
                            run_started_unix,run_ended_unix,diagnostic_exit=code)
        archive_label = f'pid{args.pid}{variant}' if code == 0 else f'pid{args.pid}{variant}-failed'
        evidence = preserve_probe_outputs(root, archive_label, identity_file, provenance_files)
        print(json.dumps(dict(pid=args.pid, diagnostic_exit=code,
                              preserved_artifacts=str(evidence) if evidence else None,
                              build_source_provenance=bool(provenance_files),
                              dll_retained_until_game_exit=True, camera_object_writes=None if args.native_left_aim_experiment else False,
                              camera_object_writes_requested=args.native_left_aim_experiment,
                              native_left_aim_experiment_requested=args.native_left_aim_experiment,
                              simulated_cleanup_exception_requested=args.simulated_cleanup_exception,
                              simulated_gameplay_script_requested=bool(args.simulated_gameplay_script),
                              expected_diagnostic_exit=1 if args.simulated_cleanup_exception else 0,
                              native_left_aim_backend=('simulated-development' if args.simulated_vr else 'xr') if args.native_left_aim_experiment else None,
                              native_stereo_settings_requested=native_rendering)))
        if code:
            raise RuntimeError(f"Diagnostic failed ({code}); inspect artifacts/dxgi-probe-failure.txt or result JSON")
        if not (root/'artifacts'/'dxgi-probe-result.json').is_file():
            raise RuntimeError("Game exited before the diagnostic wrote a restoration report; clean shutdown is unverified")
    finally:
        if allocation and not path_thread_may_run:
            free(process, allocation, 0, 0x8000)
        close(process)


if __name__ == "__main__":
    main()
