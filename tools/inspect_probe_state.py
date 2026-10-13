"""Passive, exact-v220 restoration state read. No threads, writes or game calls."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
from probe_game import PE

GAME_HASH = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
DLL_HASH = '88c1e7ae6e8acce527408318fd2c5d68e488939fcd9693cc2624d637ee2c9d0b'
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    args = parser.parse_args()
    if args.pid <= 0 or c.sizeof(c.c_void_p) != 8:
        parser.error('Require positive PID and x64 Python')
    dll = (ROOT / 'build/d2vr_render_probe_arm_v220.dll').resolve()
    data = dll.read_bytes()
    if hashlib.sha256(data).hexdigest() != DLL_HASH:
        raise ValueError('Exact archived v220 required')
    pe = PE(data)
    status = pe.code_export('ProbeStatus')
    code = data[pe.offset(status, 60):pe.offset(status, 60)+60]
    # Verify the compiled source's started byte load, then its sole call to
    # body_material_restore_pending. This never executes either function.
    if code[:7] != bytes.fromhex('4883ec280fb605') or code[28] != 0xe8:
        raise ValueError('ProbeStatus code differs from pinned v220')
    started_rva = status+11+struct.unpack_from('<i', code, 7)[0]
    pending_rva = status+33+struct.unpack_from('<i', code, 29)[0]
    pending = data[pe.offset(pending_rva, 86):pe.offset(pending_rva, 86)+86]
    if pending[:7] != bytes.fromhex('40534883ec2048') or pending[34:37] != bytes.fromhex('0fb61d'):
        raise ValueError('Pinned material pending read differs')
    active_rva = pending_rva+41+struct.unpack_from('<i', pending, 37)[0]
    k = c.WinDLL('kernel32', use_last_error=True)
    class Module(c.Structure):
        _fields_ = [('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),
                    ('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),
                    ('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    for name in ('Module32FirstW','Module32NextW'):
        fn=getattr(k,name);fn.argtypes=[w.HANDLE,c.POINTER(Module)];fn.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snapshot=k.CreateToolhelp32Snapshot(0x18,args.pid)
    if snapshot==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    modules=[]
    try:
        m=Module(dwSize=c.sizeof(Module));ok=k.Module32FirstW(snapshot,c.byref(m))
        while ok:
            modules.append((Path(m.szExePath).resolve(),m.modBaseAddr,m.modBaseSize))
            ok=k.Module32NextW(snapshot,c.byref(m))
    finally:k.CloseHandle(snapshot)
    game=next((m for m in modules if m[0].name.lower()=='dishonored2.exe'),None)
    loaded=next((m for m in modules if m[0]==dll),None)
    if not game or game[2]!=0x41cb000 or hashlib.sha256(game[0].read_bytes()).hexdigest()!=GAME_HASH or not loaded:
        raise ValueError('Exact native executable/loaded v220 identity missing')
    handle=k.OpenProcess(0x1010,False,args.pid)
    if not handle:raise OSError(c.get_last_error(),'Read-only process open failed')
    try:
        def read(rva,size):
            b=c.create_string_buffer(size);got=c.c_size_t()
            if not 0<=rva<=loaded[2]-size or not k.ReadProcessMemory(handle,loaded[1]+rva,b,size,c.byref(got)) or got.value!=size:
                raise ValueError('Exact loaded module read failed')
            return b.raw
        if read(status,60)!=code or read(pending_rva,86)!=pending:raise ValueError('Loaded code identity differs')
        started=read(started_rva,1)[0];active=read(active_rva,1)[0]
        if started not in (0,1) or active not in (0,1):raise ValueError('Invalid bool layout')
        result=dict(time_utc=datetime.now(timezone.utc).isoformat(),pid=args.pid,game_path=str(game[0]),game_sha256=GAME_HASH,
                    dll_path=str(dll),dll_sha256=DLL_HASH,dll_base=loaded[1],probe_started=bool(started),
                    body_material_restoration_pending=bool(active),read_only=True,game_calls=False,
                    limitation='Two passive byte reads; does not prove complete restoration or headset state')
    finally:k.CloseHandle(handle)
    output=ROOT/'artifacts'/f'probe-state-{args.pid}-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':main()
