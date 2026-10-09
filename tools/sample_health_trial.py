"""20 Hz read-only NPC health/position trial; bounded discovery and exact identity guards."""
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
    parser.add_argument('--entity-id',type=lambda value:int(value,0),action='append',default=[],help='Additional native actor ID (maximum four)')
    args=parser.parse_args()
    if len(args.entity_id)>4:parser.error('At most four explicit entity IDs')
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
        from health_trial_fields import primary_owner,primary_feet,nearest_health_actors,discover_health_actor,health_actor_sample
        k.GetTickCount64.restype=c.c_uint64
        result=dict(samples=[],rejected=[],primary_epochs=[],actor_epochs=[])
        primary=None;primary_index=None;targets={};target_epochs={};retry={};last_discovery=-1.
        begin=time.monotonic();next_sample=begin
        while time.monotonic()-begin<args.seconds:
            tick=k.GetTickCount64();now=time.monotonic()
            try:
                current=primary_owner(read,base)
                if primary!=current:
                    if now-last_discovery<.5:raise ValueError('Awaiting bounded primary/actor reacquisition')
                    last_discovery=now;primary=current;primary_index=len(result['primary_epochs'])
                    result['primary_epochs'].append(dict(tick_ms=tick,**primary))
                    targets={};target_epochs={};retry={}
                    for owner in nearest_health_actors(read,base,primary,extra_ids=tuple(args.entity_id or (189269860,190318452))):
                        eid=owner['entity_id'];targets[eid]=owner;target_epochs[eid]=len(result['actor_epochs'])
                        result['actor_epochs'].append(dict(tick_ms=tick,**owner))
                feet=primary_feet(read,base,primary)
                for eid in list(targets):
                    actor_tick=k.GetTickCount64()
                    try:
                        owner=targets[eid]
                        if owner is None:
                            if now-retry.get(eid,-1)<.5:continue
                            retry[eid]=now;owner=discover_health_actor(read,base,primary['game'],eid);targets[eid]=owner
                            target_epochs[eid]=len(result['actor_epochs']);result['actor_epochs'].append(dict(tick_ms=actor_tick,**owner))
                        sample=health_actor_sample(read,base,owner)
                        end=k.GetTickCount64()
                        if end-actor_tick>100:raise ValueError('Actor read interval exceeded 100 ms')
                        if primary_owner(read,base)!=primary:raise ValueError('Primary changed during actor read')
                        result['samples'].append(dict(tick_ms=actor_tick,end_tick_ms=end,primary_epoch=primary_index,actor_epoch=target_epochs[eid],entity_id=eid,player_feet=feet,distance_native_units=math.dist(feet,sample['position']),**sample))
                    except (ValueError,OSError,UnicodeError) as error:
                        result['rejected'].append(dict(tick_ms=actor_tick,entity_id=eid,reason=str(error)));targets[eid]=None
                primary_feet(read,base,primary)
            except (ValueError,OSError) as error:
                result['rejected'].append(dict(tick_ms=tick,reason=str(error)));primary=None
            next_sample+=.05
            if next_sample<time.monotonic()-.05:next_sample=time.monotonic()
            time.sleep(max(0,next_sample-time.monotonic()))
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False,position_units='native units',frame_synchronised=False,requested_duration_seconds=args.seconds,health_source_reader_rva='0x719a70',base_health_reader_rva='0x717db0',damage_exclusion_consumers=['0x718fe0','0x71a7c0'])
    finally:k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-health-trial-{args.pid}-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")}.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(samples=len(result['samples']),rejected=len(result['rejected']),actors=len(result['actor_epochs']),output=str(output))))

if __name__=='__main__':main()
