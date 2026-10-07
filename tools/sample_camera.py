"""Read-only live sampling of an unverified camera candidate. Never writes memory."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import time

from probe_game import SIGNATURES
from camera_fields import camera_field_candidates, changed_words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True, help="Completed game-probe JSON")
    parser.add_argument("--pid", type=int, required=True, help="Running Dishonored 2 process ID")
    parser.add_argument("--samples", type=int, default=30, help="1 to 100 samples, 100 ms apart")
    parser.add_argument("--find-objects", action="store_true", help="Bounded read-only search for camera/render-view vtables")
    parser.add_argument("--object-report", type=Path, help="Sample camera objects from a prior scan of this same process")
    parser.add_argument("--discover-fields", action="store_true", help="Find plausible FoV/XYZ/orthogonal-basis layouts inside verified objects")
    parser.add_argument("--diff-objects", action="store_true", help="Record changing words in each candidate's bounded render-view prefix")
    parser.add_argument("--wait-for-input", action="store_true", help="Wait up to 60 seconds for game focus and a movement key before sampling")
    parser.add_argument("--scan-start", type=lambda value: int(value, 0), default=0,
                        help="Continue a bounded object scan at a previous report's next_scan_address")
    parser.add_argument("--output", type=Path, required=True, help="JSON report inside project workspace")
    args = parser.parse_args()
    if os.name != "nt" or c.sizeof(c.c_void_p) != 8:
        parser.error("Use 64-bit Python on Windows")
    if not 1 <= args.samples <= 100 or args.pid <= 0:
        parser.error("Samples must be 1..100 and PID must be positive")
    if not 0 <= args.scan_start < 0x0000800000000000:
        parser.error("Scan start must be inside the user address space")
    workspace = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if not output.is_relative_to(workspace):
        parser.error("Output must be inside the development workspace")
    report = json.loads(args.report.read_text(encoding="utf-8-sig"))
    executable = report.get("executable", {})
    if report.get("status") != "binary_research_available" or not executable:
        parser.error("Run the completed-installation game probe first")
    path = Path(executable["path"]).resolve()
    if path.name.lower() != "dishonored2.exe" or output.is_relative_to(path.parent):
        parser.error("Only Dishonored2.exe is supported; never write reports into its folder")
    candidate = executable["camera_research"]["ansel_camera_xyz_store"]
    if not candidate["unique_candidate"] or "candidate_camera_struct" not in candidate:
        parser.error("No unique camera structure candidate in the report")
    target_rva = candidate["candidate_camera_struct"]["rva"]
    code_rva = candidate["matches"][0]["rva"]
    if not 0 <= target_rva <= executable["image_size"] - 32:
        parser.error("Camera candidate is outside the main image")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != executable["sha256"]:
        parser.error("Executable hash changed; rerun the game probe")

    kernel = c.WinDLL("kernel32", use_last_error=True)
    user = c.WinDLL("user32", use_last_error=True)
    user.GetForegroundWindow.argtypes = []
    user.GetForegroundWindow.restype = w.HWND
    user.GetWindowThreadProcessId.argtypes = [w.HWND,c.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype = w.DWORD
    user.GetAsyncKeyState.argtypes = [c.c_int]
    user.GetAsyncKeyState.restype = c.c_short

    def input_context():
        foreground_pid = w.DWORD()
        user.GetWindowThreadProcessId(user.GetForegroundWindow(), c.byref(foreground_pid))
        focused = foreground_pid.value == args.pid
        # Record only a boolean for game movement, and only while that game is
        # foreground. No typed text, other-app titles, or global input history.
        moving = focused and any(user.GetAsyncKeyState(key) & 0x8000 for key in (0x57,0x41,0x53,0x44,0x25,0x26,0x27,0x28))
        return dict(game_focused=focused, movement_key_down=bool(moving))

    class Module(c.Structure):
        _fields_ = [("dwSize", w.DWORD), ("th32ModuleID", w.DWORD),
                    ("th32ProcessID", w.DWORD), ("GlblcntUsage", w.DWORD),
                    ("ProccntUsage", w.DWORD), ("modBaseAddr", c.c_void_p),
                    ("modBaseSize", w.DWORD), ("hModule", w.HMODULE),
                    ("szModule", w.WCHAR * 256), ("szExePath", w.WCHAR * 260)]

    class MemoryInfo(c.Structure):
        _fields_ = [("BaseAddress", c.c_void_p), ("AllocationBase", c.c_void_p),
                    ("AllocationProtect", w.DWORD), ("PartitionId", w.WORD),
                    ("RegionSize", c.c_size_t), ("State", w.DWORD),
                    ("Protect", w.DWORD), ("Type", w.DWORD)]

    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(Module)]
    kernel.Module32FirstW.restype = w.BOOL
    kernel.Module32NextW.argtypes = [w.HANDLE, c.POINTER(Module)]
    kernel.Module32NextW.restype = w.BOOL
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.ReadProcessMemory.restype = w.BOOL
    kernel.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MemoryInfo), c.c_size_t]
    kernel.VirtualQueryEx.restype = c.c_size_t
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.QueryFullProcessImageNameW.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL

    def failed(operation):
        raise OSError(c.get_last_error(), operation)

    snapshot = kernel.CreateToolhelp32Snapshot(0x8 | 0x10, args.pid)
    if snapshot == c.c_void_p(-1).value:
        failed("Snapshot failed; verify game PID and process permissions")
    modules = []
    try:
        module = Module()
        module.dwSize = c.sizeof(Module)
        if not kernel.Module32FirstW(snapshot, c.byref(module)):
            failed("Cannot enumerate game modules")
        while True:
            modules.append(dict(name=module.szModule, path=module.szExePath,
                                base=module.modBaseAddr, size=module.modBaseSize))
            if not kernel.Module32NextW(snapshot, c.byref(module)):
                if c.get_last_error() != 18:  # ERROR_NO_MORE_FILES
                    failed("Module enumeration interrupted")
                break
    finally:
        kernel.CloseHandle(snapshot)
    matches = [m for m in modules if Path(m["path"]).resolve() == path]
    if len(matches) != 1 or matches[0]["size"] != executable["image_size"]:
        parser.error("PID is not running the probed executable/image")
    process = kernel.OpenProcess(0x10 | 0x400, False, args.pid)  # VM_READ | QUERY_INFORMATION
    if not process:
        failed("Cannot open game process for reading")
    try:
        name = c.create_unicode_buffer(32768)
        name_size = w.DWORD(len(name))
        if not kernel.QueryFullProcessImageNameW(process, 0, name, c.byref(name_size)):
            failed("Cannot verify process executable")
        if Path(name.value).resolve() != path:
            parser.error("Process identity changed while opening; rerun")
        base = matches[0]["base"]

        def read(address, size):
            buffer = c.create_string_buffer(size)
            count = c.c_size_t()
            if not kernel.ReadProcessMemory(process, address, buffer, size, c.byref(count)) or count.value != size:
                failed("Memory read failed; game may have exited")
            return buffer.raw

        tokens = SIGNATURES["ansel_camera_xyz_store"].split()
        live_code = read(base + code_rva, len(tokens))
        if any(t != "??" and live_code[i] != int(t, 16) for i, t in enumerate(tokens)):
            parser.error("Loaded code differs from candidate signature; no sampling performed")
        live_target = code_rva + 8 + struct.unpack_from("<i", live_code, 4)[0]
        if live_target != target_rva:
            parser.error("Loaded camera reference differs from probed executable")
        object_search = None
        if args.find_objects:
            # A pointer occurrence can be allocator/type metadata. Require both
            # primary and secondary RTTI vtables at their published subobject offsets.
            types = {name: executable.get("engine_type_candidates", {}).get(name, [])
                     for name in ("arkCpntPlayerCamera", "arkCpntRenderView")}
            patterns = [(name, struct.pack("<Q", base+t["vtable_rva"]), t)
                        for name, tables in types.items() for t in tables
                        if t["subobject_offset"] == 0]
            info = MemoryInfo()
            address = args.scan_start
            next_address = address
            scanned = 0
            scan_limit = 512 * 1024 * 1024
            deadline = time.monotonic() + 10
            objects = []
            seen = set()
            read_failures = 0
            pointer_hits = {name: 0 for name in types}
            exhausted = False
            while address < 0x0000800000000000 and scanned < scan_limit and time.monotonic() < deadline:
                size = kernel.VirtualQueryEx(process, address, c.byref(info), c.sizeof(info))
                if not size:
                    if c.get_last_error() != 87:  # End of user address space: ERROR_INVALID_PARAMETER.
                        failed("Cannot query process memory map")
                    exhausted = True
                    break
                region_start = info.BaseAddress or 0
                region_end = region_start + info.RegionSize
                if region_end <= address:
                    raise ValueError("Non-advancing process memory map")
                # Only committed, readable private allocations; no images or mapped files.
                if info.State == 0x1000 and info.Type == 0x20000 and not info.Protect & 0x100 and \
                        info.Protect & 0xFF in (2, 4, 8, 0x20, 0x40, 0x80):
                    chunk_start = max(region_start, address)
                    while chunk_start < region_end and scanned < scan_limit and time.monotonic() < deadline:
                        chunk_size = min(256*1024, region_end-chunk_start, scan_limit-scanned)
                        try:
                            block = read(chunk_start, chunk_size)
                        except OSError:
                            read_failures += 1
                            chunk_start += chunk_size
                            scanned += chunk_size
                            next_address = chunk_start
                            continue
                        scanned += chunk_size
                        next_address = chunk_start + chunk_size
                        for type_name, pointer, table in patterns:
                            at = block.find(pointer)
                            while at >= 0 and len(objects) < 32:
                                pointer_hits[type_name] += 1
                                object_address = chunk_start + at
                                identity = (type_name, object_address)
                                if object_address % 8 == 0 and identity not in seen:
                                    secondary = [t for t in types[type_name] if t["subobject_offset"] > 0]
                                    try:
                                        consistent = bool(secondary) and all(
                                            read(object_address+t["subobject_offset"], 8) ==
                                            struct.pack("<Q", base+t["vtable_rva"]) for t in secondary)
                                    except OSError:
                                        consistent = False
                                    if consistent:
                                        seen.add(identity)
                                        objects.append(dict(type=type_name, address=object_address,
                                            primary_vtable_rva=table["vtable_rva"],
                                            both_vtables_consistent=True, active_gameplay_role_verified=False))
                                at = block.find(pointer, at+1)
                        # Seven-byte overlap covers pointers crossing a chunk boundary.
                        chunk_start += max(1, chunk_size-7) if chunk_start+chunk_size < region_end else chunk_size
                        next_address = chunk_start
                    if chunk_start < region_end:
                        break
                address = region_end
                next_address = address
            object_search = dict(bytes_scanned=scanned, byte_limit=scan_limit, time_limit_seconds=10,
                                 address_map_exhausted=exhausted, read_failures=read_failures,
                                 scan_start=args.scan_start, next_scan_address=next_address,
                                 primary_pointer_hits=pointer_hits,
                                 candidates=objects, live_roles_verified=False)
        targets = []
        if args.object_report:
            previous = json.loads(args.object_report.read_text(encoding="utf-8-sig"))
            if previous.get("pid") != args.pid or previous.get("executable_sha256") != digest:
                parser.error("Object report belongs to a different process/build; rescan")
            targets = previous.get("object_search", {}).get("candidates", [])
            if not targets or len(targets) > 32:
                parser.error("Object report must contain 1..32 candidates")
            for target in targets:
                if target.get("type") not in ("arkCpntPlayerCamera", "arkCpntRenderView"):
                    parser.error("Unexpected object type in report")
                if not isinstance(target.get("address"), int) or not 0x10000 <= target["address"] < 0x0000800000000000:
                    parser.error("Invalid object address")
                target["field_offsets"] = [0x3A70,0x3AA0]
                if args.discover_fields:
                    # Constructor and pool-stride evidence establishes the shared
                    # render-view prefix as 0x25150 bytes. No arbitrary heap dumps.
                    target["field_offsets"] = camera_field_candidates(read(target["address"], 0x25150))
        object_samples = []
        if args.wait_for_input:
            wait_deadline = time.monotonic()+60
            print("Waiting up to 60 seconds for Dishonored 2 focus and a movement key...", flush=True)
            while not input_context()["movement_key_down"]:
                if time.monotonic() >= wait_deadline:
                    raise ValueError("No focused movement input arrived; sample was not started")
                time.sleep(0.05)
            print("Focused movement detected; recording now.", flush=True)
        diffs = {}
        if args.diff_objects:
            for target in targets:
                initial = read(target["address"], 0x25150)
                diffs[target["address"]] = dict(initial=initial, previous=initial, last=initial,
                                                counts={}, captured=0)

        def sample_object(target):
            tables = executable["engine_type_candidates"][target["type"]]
            try:
                valid_type = all(read(target["address"]+table["subobject_offset"], 8) ==
                                 struct.pack("<Q", base+table["vtable_rva"]) for table in tables)
                if not valid_type:
                    return dict(type=target["type"], address=target["address"], type_still_matches=False)
                if target["address"] in diffs:
                    diff = diffs[target["address"]]
                    latest = read(target["address"], 0x25150)
                    for offset in changed_words(diff["previous"], latest):
                        diff["counts"][offset] = diff["counts"].get(offset, 0)+1
                    diff["previous"] = diff["last"] = latest
                    diff["captured"] += 1
                views = []
                # Exact-build disassembly: arkCpntRenderView's constructor initializes
                # an embedded view at +0x30. Ansel reads view FoV/XYZ/basis at
                # +0x3A70/+0x3A78/+0x3A9C. Both offsets below remain hypotheses.
                for field_offset in target["field_offsets"]:
                    values = struct.unpack("<20f", read(target["address"]+field_offset, 80))
                    finite = all(math.isfinite(value) for value in values)
                    basis = list(values[11:20]) if finite else None
                    orthogonal = False
                    if basis:
                        axes = [basis[i:i+3] for i in (0,3,6)]
                        norms = [sum(v*v for v in axis) for axis in axes]
                        dots = [sum(a*b for a,b in zip(axes[i], axes[j])) for i,j in ((0,1),(0,2),(1,2))]
                        orthogonal = all(abs(n-1) < 0.02 for n in norms) and all(abs(v) < 0.02 for v in dots)
                    views.append(dict(field_offset=field_offset,
                        layout_source="geometric_scan" if args.discover_fields else "constructor_hypothesis",
                        finite=finite, fov_radians_candidate=values[0] if finite else None,
                        position_candidate=list(values[2:5]) if finite else None,
                        orientation_basis_candidate=basis, orientation_basis_orthogonal=orthogonal,
                        gameplay_role_verified=False))
                return dict(type=target["type"], address=target["address"], type_still_matches=True,
                            view_hypotheses=views)
            except OSError as error:
                return dict(type=target["type"], address=target["address"], type_still_matches=False, error=str(error))

        samples = []
        begin = time.monotonic()
        for index in range(args.samples):
            values = struct.unpack("<8f", read(base + target_rva, 32))
            valid = all(math.isfinite(value) for value in values)
            samples.append(dict(elapsed_seconds=round(time.monotonic()-begin, 4),
                                position=list(values[:3]) if valid else None,
                                quaternion_xyzw=list(values[3:7]) if valid else None,
                                fov=values[7] if valid else None, finite=valid,
                                input_context=input_context()))
            if targets:
                object_samples.append(dict(elapsed_seconds=round(time.monotonic()-begin, 4),
                                           objects=[sample_object(target) for target in targets]))
            if index < args.samples-1:
                time.sleep(0.1)
        changed = len({tuple(s["position"] or []) + tuple(s["quaternion_xyzw"] or [])
                       for s in samples}) > 1
        object_summaries = []
        for target in targets:
            for field_offset in target["field_offsets"]:
                views = [view for sample in object_samples for obj in sample["objects"]
                         if obj["address"] == target["address"] for view in obj.get("view_hypotheses", [])
                         if view["field_offset"] == field_offset and view["finite"]]
                object_summaries.append(dict(type=target["type"], address=target["address"],
                    field_offset=field_offset, samples=len(views),
                    orthogonal_basis_samples=sum(view["orientation_basis_orthogonal"] for view in views),
                    distinct_positions=len({tuple(view["position_candidate"]) for view in views}),
                    distinct_orientation_bases=len({tuple(view["orientation_basis_candidate"]) for view in views}),
                    first=views[0] if views else None, last=views[-1] if views else None,
                    gameplay_role_verified=False))
        object_diffs = []
        for target in targets:
            if target["address"] not in diffs:
                continue
            diff = diffs[target["address"]]
            fields = []
            for offset, changes in sorted(diff["counts"].items())[:512]:
                first, last = struct.unpack_from("<f", diff["initial"], offset)[0], struct.unpack_from("<f", diff["last"], offset)[0]
                fields.append(dict(offset=offset, changes=changes,
                    first_float=first if math.isfinite(first) else None,
                    last_float=last if math.isfinite(last) else None,
                    first_bits=hex(struct.unpack_from("<I", diff["initial"], offset)[0]),
                    last_bits=hex(struct.unpack_from("<I", diff["last"], offset)[0])))
            object_diffs.append(dict(type=target["type"], address=target["address"],
                bytes_per_snapshot=0x25150, captured_samples=diff["captured"],
                distinct_changed_words=len(diff["counts"]), first_changed_fields=fields,
                field_report_limit=512, game_memory_writes=False, gameplay_role_verified=False))
        result = dict(pid=args.pid, executable_sha256=digest, camera_rva=target_rva,
                      game_memory_writes=False, gameplay_camera_verified=False,
                      candidate_changed_during_sample=changed,
                      interpretation="Changes are a research clue, not proof of a gameplay camera or safe hook.",
                      loaded_modules=[dict(name=m["name"], size=m["size"]) for m in modules],
                      object_search=object_search, object_samples=object_samples,
                      object_summaries=object_summaries, discovered_object_fields=targets, samples=samples)
        result["object_diffs"] = object_diffs
        result["focused_samples"] = sum(sample["input_context"]["game_focused"] for sample in samples)
        result["movement_key_samples"] = sum(sample["input_context"]["movement_key_down"] for sample in samples)
    finally:
        kernel.CloseHandle(process)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(f"Sampled {len(samples)} poses; candidate changed: {changed}; no game memory writes.")
    if object_search:
        print(f"Bounded camera/render object search: {len(object_search['candidates'])} candidates; roles unverified.")
    print(f"Report: {output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f"CAMERA_SAMPLE_FAILED: {error}") from error
