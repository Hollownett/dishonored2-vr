"""Bounded exact-build native logic-manager reads. No writes, input or engine calls."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time
from game_state import game_logic_mask
from rig_fields import GAME_HASH
from player_camera_fields import player_camera_snapshot


def state_snapshot(read, base):
    game = struct.unpack('<Q', read(base+0x39a5290, 8))[0]
    result = dict(game=hex(game), logic_mask=game_logic_mask(read, base))
    if not game:
        return dict(result, reason='null game')
    vtable = struct.unpack('<Q', read(game, 8))[0]
    state = struct.unpack('<i', read(game+0x341058, 4))[0]
    manager = game+0x1f6ec8
    raw = read(manager, 0x8b8)
    result.update(game_vtable=hex(vtable), game_vtable_valid=vtable==base+0x1c940a8,
                  outer_state=state, manager=hex(manager), entries=[], invalid_reads=0)
    seen = set()
    for slot, entry in enumerate(struct.unpack('<'+'Q'*(len(raw)//8), raw)):
        if not entry or entry in seen:
            continue
        seen.add(entry)
        try:
            data = read(entry, 0x41)
        except (OSError, ValueError):
            result['invalid_reads'] += 1
            continue
        backref = struct.unpack_from('<Q', data, 8)[0]
        if backref != manager:
            continue
        result['entries'].append(dict(slot=hex(slot*8), address=hex(entry),
            vtable_rva=hex(struct.unpack_from('<Q',data)[0]-base),
            state=struct.unpack_from('<i',data,0x18)[0], active=data[0x40]))
    result['outer_state_after'] = struct.unpack('<i',read(game+0x341058,4))[0]
    result['game_after'] = hex(struct.unpack('<Q',read(base+0x39a5290,8))[0])
    result['manager_bytes_stable'] = raw==read(manager,len(raw))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--samples',type=int,default=8)
    parser.add_argument('--camera',type=lambda text:int(text,0),help='Known camera address from this same process; verifies primary player ownership before read-only snapshot')
    parser.add_argument('--output',type=Path,help='Unique workspace evidence path; existing files are never replaced')
    args=parser.parse_args()
    if args.pid<=0 or not 1<=args.samples<=20 or c.sizeof(c.c_void_p)!=8:
        parser.error('Require positive PID, x64 Python and 1..20 samples')
    k=c.WinDLL('kernel32',use_last_error=True)
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),
            ('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),
            ('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snapshot=k.CreateToolhelp32Snapshot(8,args.pid)
    if snapshot==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        m=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snapshot,c.byref(m)):raise OSError(c.get_last_error(),'Module lookup failed')
        if m.szModule.lower()!='dishonored2.exe' or m.modBaseSize!=0x041cb000:raise ValueError('Require pinned Dishonored 2 layout')
        base=m.modBaseAddr
        if hashlib.sha256(Path(m.szExePath).read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Unsupported executable hash')
    finally:k.CloseHandle(snapshot)
    handle=k.OpenProcess(0x1010,False,args.pid)  # VM_READ | QUERY_LIMITED_INFORMATION only.
    if not handle:raise OSError(c.get_last_error(),'Read-only process open failed')
    samples=[]
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=0x8b8:raise ValueError('Bounded read rejected')
            raw=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(handle,at,raw,size,c.byref(got)) or got.value!=size:
                raise OSError(c.get_last_error(),'State read failed')
            return raw.raw
        for i in range(args.samples):
            sample=state_snapshot(read,base)
            if args.camera is not None:
                game=int(sample['game'],16)
                if not sample.get('game_vtable_valid'):raise ValueError('Cannot verify game for camera snapshot')
                player=struct.unpack('<Q',read(game+0x3130b0,8))[0]
                if struct.unpack('<Q',read(player,8))[0]!=base+0x1c5e428:raise ValueError('Primary player type differs')
                sample['camera_snapshot']=player_camera_snapshot(read,base,player,camera=args.camera)
                if struct.unpack('<Q',read(game+0x3130b0,8))[0]!=player:raise ValueError('Primary player changed during camera read')
            renderer=struct.unpack('<Q',read(base+0x32e8f38,8))[0]
            context=struct.unpack('<Q',read(renderer+8,8))[0]
            rtv=struct.unpack('<Q',read(renderer+0xb90,8))[0]
            sample['iggy']={'renderer':hex(renderer),'context':hex(context),'context_vtable':hex(struct.unpack('<Q',read(context,8))[0]) if context else None,'rtv':hex(rtv),'rtv_vtable':hex(struct.unpack('<Q',read(rtv,8))[0]) if rtv else None}
            samples.append(sample)
            if i+1<args.samples:time.sleep(.1)
    finally:k.CloseHandle(handle)
    result=dict(pid=args.pid,base=hex(base),executable_sha256=GAME_HASH,game_memory_writes=False,engine_calls=False,samples=samples)
    workspace=Path(__file__).resolve().parents[1]
    output=(args.output or workspace/'artifacts'/f'native-state-{args.pid}-{time.time_ns()}-readonly.json').resolve()
    if not output.is_relative_to(workspace) or output.exists():
        raise ValueError('Require a new evidence file inside the workspace')
    output.parent.mkdir(parents=True,exist_ok=True)
    result['output']=str(output)
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':main()
