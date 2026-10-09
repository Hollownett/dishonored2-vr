"""Inventory bounded verified entity/component RTTI and authored entity names, read-only."""
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
    parser.add_argument('--radius',type=float,default=40)
    args=parser.parse_args()
    if not math.isfinite(args.radius) or not 0<args.radius<=100:parser.error('Radius must be bounded1..100 native units')
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
        from interaction_fields import interaction_snapshot
        from player_camera_fields import player_camera_snapshot
        game=pointer(read,base+0x39a5290)
        if pointer(read,game)!=base+0x1c940a8:raise ValueError('Game owner changed')
        player=pointer(read,game+0x3130b0)
        if pointer(read,player)!=base+0x1c5e428:raise ValueError('Player RTTI differs')
        player_id=read(player+0xa8,4)
        initial=interaction_snapshot(read,base);camera=initial['camera']
        camera_identity=read(camera+0x18,8)
        k.GetTickCount64.restype=c.c_uint64
        result=dict(player=player,player_id=struct.unpack('<I',player_id)[0],camera=camera,camera_identity=list(struct.unpack('<iI',camera_identity)),samples=[],rejected=[])
        begin=time.monotonic();next_sample=begin
        while time.monotonic()-begin<20:
            tick=k.GetTickCount64()
            try:
                if pointer(read,game+0x3130b0)!=player or read(player+0xa8,4)!=player_id:raise ValueError('Player identity changed')
                feet=struct.unpack('<3f',read(player+0x300,12))
                snap=player_camera_snapshot(read,base,player,camera=camera)
                feet_after=struct.unpack('<3f',read(player+0x300,12))
                if pointer(read,game+0x3130b0)!=player or read(player+0xa8,4)!=player_id or read(camera+0x18,8)!=camera_identity or pointer(read,camera+0x10)!=player:raise ValueError('Player/camera identity changed')
                result['samples'].append(dict(tick_ms=tick,end_tick_ms=k.GetTickCount64(),feet=list(feet),feet_after=list(feet_after),feet_consistent=feet==feet_after,aim_origin=snap['aim_origin'],heading=snap['physics_heading_radians'],pitch=snap['values']['pitch'],mode=snap['current_mode'],target_mode=snap['target_mode'],read_consistent=snap['read_consistent']))
            except (ValueError,OSError) as error:result['rejected'].append(dict(tick_ms=tick,reason=str(error)))
            next_sample+=.05;time.sleep(max(0,next_sample-time.monotonic()))
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False)
    finally: k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-grab-bounds-{args.pid}-readonly.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(samples=len(result['samples']),rejected=len(result['rejected']),first_tick=result['samples'][0]['tick_ms'] if result['samples'] else None,last_tick=result['samples'][-1]['tick_ms'] if result['samples'] else None,output=str(output))))


if __name__=='__main__': main()
