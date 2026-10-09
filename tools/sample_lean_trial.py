"""20 Hz read-only primary-owner, lean and feet sampler; safely reacquires across reload."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import struct
from object_fields import entity_for_id
from rig_fields import native_list, component_ready
import math
from rig_fields import GAME_HASH, pointer


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--output',type=Path,help='Unique read-only snapshot output path')
    parser.add_argument('--seconds',type=float,default=20)
    args=parser.parse_args()
    if not math.isfinite(args.seconds) or not 0<args.seconds<=180:parser.error('Duration must be bounded to 180 seconds')
    if c.sizeof(c.c_void_p)!=8 or args.pid<=0: parser.error('Require x64 Python and positive PID')
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
    if snapshot==c.c_void_p(-1).value: raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        module=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snapshot,c.byref(module)): raise OSError(c.get_last_error(),'Module lookup failed')
        if module.szModule.lower()!='dishonored2.exe' or module.modBaseSize!=0x041cb000: raise ValueError('Pinned game layout required')
        if hashlib.sha256(Path(module.szExePath).read_bytes()).hexdigest()!=GAME_HASH: raise ValueError('Game hash differs')
        base=module.modBaseAddr
    finally: k.CloseHandle(snapshot)
    process=k.OpenProcess(0x1010,False,args.pid)
    if not process: raise OSError(c.get_last_error(),'Read-only process open failed')
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768: raise ValueError('Bounded read required')
            data=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(process,at,data,size,c.byref(got)) or got.value!=size: raise OSError(c.get_last_error(),'Read failed')
            return data.raw
        import time
        from lean_trial_fields import discover_lean_trial_owner,verify_lean_trial_owner,lean_trial_state
        from player_camera_fields import player_camera_snapshot
        k.GetTickCount64.restype=c.c_uint64
        result=dict(samples=[],rejected=[],owners=[])
        owner=None;owner_index=None;last_discovery=-1.;begin=time.monotonic();next_sample=begin
        while time.monotonic()-begin<args.seconds:
            tick=k.GetTickCount64();now=time.monotonic()
            try:
                if owner is None:
                    if now-last_discovery<.5:raise ValueError('Awaiting bounded owner reacquisition')
                    last_discovery=now;owner=discover_lean_trial_owner(read,base)
                    owner_index=len(result['owners']);result['owners'].append(dict(tick_ms=tick,**owner))
                verify_lean_trial_owner(read,base,owner)
                feet=struct.unpack('<3f',read(owner['player']+0x300,12))
                if not all(math.isfinite(v) and abs(v)<1e7 for v in feet):raise ValueError('Feet position invalid')
                lean=lean_trial_state(read,base,owner)
                snap=player_camera_snapshot(read,base,owner['player'],camera=owner['camera'])
                feet_after=struct.unpack('<3f',read(owner['player']+0x300,12))
                verify_lean_trial_owner(read,base,owner)
                lean_after=lean_trial_state(read,base,owner)
                end_tick=k.GetTickCount64()
                if end_tick-tick>100:raise ValueError('Read interval exceeded 100 ms')
                result['samples'].append(dict(tick_ms=tick,end_tick_ms=end_tick,owner_index=owner_index,feet=list(feet),feet_after=list(feet_after),feet_consistent=feet==feet_after,lean=lean,lean_after=lean_after,lean_consistent=lean==lean_after,aim_origin=snap['aim_origin'],heading=snap['physics_heading_radians'],pitch=snap['values']['pitch'],mode=snap['current_mode'],target_mode=snap['target_mode'],camera_read_consistent=snap['read_consistent']))
            except (ValueError,OSError) as error:
                result['rejected'].append(dict(tick_ms=tick,reason=str(error)));owner=None
            next_sample+=.05
            if next_sample<time.monotonic()-.05:next_sample=time.monotonic()
            time.sleep(max(0,next_sample-time.monotonic()))
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False,position_units='native units',frame_synchronised=False,requested_duration_seconds=args.seconds)
    finally:k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-lean-trial-{args.pid}-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")}.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(samples=len(result['samples']),rejected=len(result['rejected']),owner_epochs=len(result['owners']),output=str(output))))

if __name__=='__main__':main()
