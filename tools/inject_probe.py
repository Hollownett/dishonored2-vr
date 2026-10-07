"""Load this workspace's temporary DXGI diagnostic into the exact supported game.

This writes a DLL path into a new process allocation and patches Present through
MinHook. It does not write camera values. The disabled DLL stays until game exit.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import struct
import time

from probe_game import PE

GAME_HASH = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--camera-report", type=Path,
                        help="Successful focused movement capture from this process")
    parser.add_argument("--stereo", action="store_true", help="Temporary native side-by-side stereo and frame capture")
    parser.add_argument("--single-eye", action="store_true", help="Compare full-viewport left/right native projections over successive frames")
    parser.add_argument("--candidate", action="store_true", help="Use the separately built candidate after checking the previous diagnostic restored cleanly")
    parser.add_argument("--wait-for-game", action="store_true", help="Wait up to five minutes outside the game for focused movement before starting")
    args = parser.parse_args()
    if args.stereo and args.single_eye:
        parser.error("Choose one native rendering diagnostic")
    native_rendering = args.stereo or args.single_eye
    if os.name != "nt" or c.sizeof(c.c_void_p) != 8 or args.pid <= 0:
        parser.error("Use x64 Python on Windows and a positive game PID")
    if args.wait_for_game:
        user = c.WinDLL("user32", use_last_error=True)
        user.GetForegroundWindow.restype = w.HWND
        user.GetWindowThreadProcessId.argtypes = [w.HWND,c.POINTER(w.DWORD)]
        user.GetAsyncKeyState.argtypes = [c.c_int]
        user.GetAsyncKeyState.restype = c.c_short
        print("Waiting up to five minutes for Dishonored 2 foreground movement...", flush=True)
        deadline = time.monotonic()+300
        while True:
            foreground_pid = w.DWORD()
            user.GetWindowThreadProcessId(user.GetForegroundWindow(),c.byref(foreground_pid))
            if foreground_pid.value == args.pid and any(user.GetAsyncKeyState(key)&0x8000 for key in (0x57,0x41,0x53,0x44,0x25,0x26,0x27,0x28)):
                break
            if time.monotonic() >= deadline:
                parser.error("No focused movement detected; diagnostic not started")
            time.sleep(.05)
    root = Path(__file__).resolve().parents[1]
    dll = root / "build" / ("d2vr_render_probe_candidate.dll" if args.candidate else "d2vr_render_probe.dll")
    pe = PE(dll.read_bytes())
    if pe.machine != 0x8664:
        parser.error("The diagnostic must be x64")
    entry_rva = pe.code_export("StartProbe")
    camera_address = 0
    if not native_rendering:
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
    games = [m for m in loaded if m["path"].name.lower() == "dishonored2.exe"]
    if len(games) != 1 or games[0]["size"] != 68988928:
        parser.error("PID is not the supported Dishonored 2 image")
    game = games[0]
    with game["path"].open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != GAME_HASH:
            parser.error("Unsupported executable hash; no DLL loaded")
    prior = [m for m in loaded if m["path"].name.lower() == dll.name.lower()]
    identity_file = root / "artifacts" / ("loaded-render-probe-candidate.json" if args.candidate else "loaded-render-probe.json")
    dll_hash = hashlib.sha256(dll.read_bytes()).hexdigest()
    if prior:
        identity = json.loads(identity_file.read_text()) if identity_file.exists() else {}
        if (len(prior)!=1 or prior[0]["path"]!=dll or prior[0]["size"]!=pe.image_size or
                identity.get("pid")!=args.pid or identity.get("base")!=prior[0]["base"] or identity.get("sha256")!=dll_hash):
            parser.error("Loaded diagnostic identity differs; restart the game before testing a rebuilt DLL")
    if args.candidate and not prior and any(m["path"].name.lower()=="d2vr_render_probe.dll" for m in loaded):
        old_identity = json.loads((root/"artifacts"/"loaded-render-probe.json").read_text())
        restored = json.loads((root/"artifacts"/"dxgi-probe-result.json").read_text())
        if old_identity.get("pid") != args.pid or not all(restored.get(key) is True for key in
                ("hook_disabled","backend_hook_disabled","mode_restored","values_restored")):
            parser.error("Previous diagnostic has not confirmed clean restoration; restart the game")
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

        remote_bytes = read(remote_loader, 16)
        local_bytes = c.string_at(loader, 16)
        if remote_bytes != local_bytes:
            raise ValueError(f"Remote loader instructions differ: module={owners[0]['path'].name} "
                             f"RVA={loader-owners[0]['base']:#x} local={local_bytes.hex()} remote={remote_bytes.hex()}")
        if native_rendering:
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
        (root / "build" / "dxgi-probe.cfg").write_text(f"{args.pid} {camera_address} {2 if args.single_eye else int(args.stereo)}\n", encoding="ascii")
        (root / "artifacts").mkdir(exist_ok=True)
        for name in ("dxgi-probe-result.json", "dxgi-probe-failure.txt", "dxgi-camera-probe.csv",
                     "dxgi-mono.rgba", "dxgi-left.rgba", "dxgi-stereo.rgba",
                     "dxgi-mono.png", "dxgi-left.png", "dxgi-stereo.png",
                     "dxgi-mono-views.json", "dxgi-left-views.json", "dxgi-right-views.json", "dxgi-stereo-views.json"):
            (root / "artifacts" / name).unlink(missing_ok=True)

        def run_thread(start, parameter, milliseconds):
            handle = checked(remote_thread(process, None, 0, start, parameter, 0, None), "Cannot create diagnostic thread")
            try:
                status = wait(handle, milliseconds)
                if status == 0x102:
                    raise TimeoutError("Diagnostic thread is still running; DLL/allocation retained, do not rerun")
                if status != 0:
                    raise OSError(c.get_last_error(), "Diagnostic thread wait failed; do not rerun")
                code = w.DWORD()
                checked(thread_exit(handle, c.byref(code)), "Cannot read diagnostic thread status")
                return code.value
            finally:
                close(handle)

        print("Loading exact-build diagnostic; sampling Present for 10 seconds...", flush=True)
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
        identity_file.write_text(json.dumps(dict(pid=args.pid,base=diagnostic[0]["base"],sha256=dll_hash)))
        if args.stereo:
            print("Waiting up to 60 seconds for game focus and movement input, then capturing...", flush=True)
        code = run_thread(start, None, 80000 if native_rendering else 20000)
        print(json.dumps(dict(pid=args.pid, diagnostic_exit=code,
                              dll_retained_until_game_exit=True, camera_object_writes=False,
                              native_stereo_settings_requested=native_rendering)))
        if code:
            raise RuntimeError(f"Diagnostic failed ({code}); inspect artifacts/dxgi-probe-failure.txt or result JSON")
    finally:
        if allocation and not path_thread_may_run:
            free(process, allocation, 0, 0x8000)
        close(process)


if __name__ == "__main__":
    main()
