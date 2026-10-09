"""Inspect owned native player interaction without writes, game calls or input."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
from interaction_fields import interaction_snapshot
from rig_fields import GAME_HASH
from game_state import game_logic_mask
from object_fields import nearby_bodies


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--nearby-bodies', action='store_true')
    args=parser.parse_args()
    if c.sizeof(c.c_void_p)!=8 or args.pid<=0:parser.error('Require x64 Python and a positive PID')
    k=c.WinDLL('kernel32',use_last_error=True)
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),
            ('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),
            ('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)];k.QueryFullProcessImageNameW.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snapshot=k.CreateToolhelp32Snapshot(0x8,args.pid)
    if snapshot==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        m=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snapshot,c.byref(m)):raise OSError(c.get_last_error(),'Module lookup failed')
        if m.szModule.lower()!='dishonored2.exe' or m.modBaseSize!=0x041cb000:raise ValueError('Require pinned Dishonored 2 layout')
        path=Path(m.szExePath).resolve();base=m.modBaseAddr
        if hashlib.sha256(path.read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Unsupported game hash')
    finally:k.CloseHandle(snapshot)
    handle=k.OpenProcess(0x1010,False,args.pid) # QUERY_LIMITED_INFORMATION | VM_READ only.
    if not handle:raise OSError(c.get_last_error(),'Read-only process open failed')
    try:
        name=c.create_unicode_buffer(32768);length=w.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(handle,0,name,c.byref(length)) or Path(name.value).resolve()!=path:
            raise ValueError('Process executable identity changed')
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768:raise ValueError('Bounded read rejected')
            b=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(handle,at,b,size,c.byref(got)) or got.value!=size:
                raise OSError(c.get_last_error(),'Interaction read failed')
            return b.raw
        result=interaction_snapshot(read,base)
        if args.nearby_bodies: result['nearby_physics'] = nearby_bodies(read,base)
        result.update(pid=args.pid,base=hex(base),executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),logic_mask=game_logic_mask(read,base))
    finally:k.CloseHandle(handle)
    output=Path(__file__).resolve().parents[1]/'artifacts'/f'native-interaction-{args.pid}-readonly.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result));return 0


if __name__=='__main__':raise SystemExit(main())
