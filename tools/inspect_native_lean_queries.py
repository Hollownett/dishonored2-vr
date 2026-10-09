"""Bounded owned native Lean query state, read-only; no query invocation."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
from pathlib import Path
import struct
import time
from rig_fields import GAME_HASH, pointer, native_list, component_ready
from interaction_fields import interaction_snapshot

def lean_queries(read,base):
    owned=interaction_snapshot(read,base);player=owned['player'];machine=owned['state_machine']
    data,count=native_list(read,player+0x268,8,4096);entries=read(data,count*8)
    candidates=[at for at in struct.unpack('<'+'Q'*count,entries)
                if pointer(read,at)==base+0x1b40648 and pointer(read,at+0x10)==player]
    if len(candidates)!=1:raise ValueError('Require one owned Lean sensor')
    at=candidates[0];identity=read(at+0x18,8);generation,flags=struct.unpack('<iI',identity)
    if generation==-1 or not component_ready(flags,40,0) or pointer(read,at+0x20)!=machine:
        raise ValueError('Lean sensor lifetime or machine differs')
    declaration=pointer(read,at+8);shape=pointer(read,at+0x40)
    decl=read(declaration+0x70,24);half_extents=struct.unpack_from('<3f',decl)
    distance=struct.unpack_from('<f',decl,12)[0];mask=struct.unpack_from('<I',decl,20)[0]
    raw=read(at+0x38,0x50);enabled=raw[0]
    if enabled>1:raise ValueError('Lean enabled flag differs')
    origin=struct.unpack_from('<3f',raw,0x10);axes=struct.unpack_from('<9f',raw,0x1c)
    ids=struct.unpack_from('<2i',raw,0x40);limits=struct.unpack_from('<2f',raw,0x48)
    if not all(math.isfinite(v) for v in half_extents+(distance,)+origin+axes+limits):
        raise ValueError('Non-finite Lean geometry')
    game=pointer(read,base+0x39a5290);manager=game+0x60280;world=pointer(read,manager+0x190)
    # These raw resource words are diagnostics, not decoded shape/world identity proof.
    shape_words=read(shape,32).hex();world_words=read(world,32).hex()
    again=interaction_snapshot(read,base)
    if (again['player']!=player or again['state_machine']!=machine or read(at+0x18,8)!=identity or
        pointer(read,at)!=base+0x1b40648 or pointer(read,at+0x10)!=player or pointer(read,at+0x20)!=machine or
        pointer(read,at+8)!=declaration or pointer(read,at+0x40)!=shape or
        read(declaration+0x70,24)!=decl or pointer(read,base+0x39a5290)!=game or
        pointer(read,manager+0x190)!=world or native_list(read,player+0x268,8,4096)!=(data,count) or
        read(data,count*8)!=entries):raise ValueError('Lean owner/resource changed during read')
    return dict(player=player,state_machine=machine,sensor=at,sensor_generation=generation,sensor_flags=flags,
                declaration=declaration,shape=shape,enabled=bool(enabled),half_extents_native_units=half_extents,
                maximum_query_distance_native_units=distance,collision_mask=mask,origin=origin,axes=axes,
                query_ids=ids,limits=limits,query_manager=manager,query_world=world,
                shape_first32_raw=shape_words,world_first32_raw=world_words,
                resource_abi_verified=False,frame_atomic=False,engine_thread_observed=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True)
    p.add_argument('--seconds',type=float,default=1);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.pid<=0 or not math.isfinite(args.seconds) or not 0<args.seconds<=2 or c.sizeof(c.c_void_p)!=8:p.error('Require x64, positive PID and duration<=2s')
    if args.output.exists():p.error('Refuse overwrite')
    k=c.WinDLL('kernel32',use_last_error=True)
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snap=k.CreateToolhelp32Snapshot(8,args.pid)
    if snap==c.c_void_p(-1).value:raise OSError('Module snapshot failed')
    try:
        module=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snap,c.byref(module)) or module.szModule.lower()!='dishonored2.exe' or module.modBaseSize!=0x41cb000:raise ValueError('Wrong native image')
        if hashlib.sha256(Path(module.szExePath).read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Wrong executable hash')
        base=module.modBaseAddr
    finally:k.CloseHandle(snap)
    process=k.OpenProcess(0x1010,False,args.pid)
    if not process:raise OSError('Read-only process open failed')
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768:raise ValueError('Invalid bounded read')
            raw=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(process,at,raw,size,c.byref(got)) or got.value!=size:raise OSError('Read failed')
            return raw.raw
        start=time.monotonic();samples=[]
        while True:
            try:sample=dict(state=lean_queries(read,base))
            except (OSError,ValueError) as error:sample=dict(error=str(error))
            samples.append(dict(seconds=time.monotonic()-start,**sample))
            if time.monotonic()-start>=args.seconds:break
            time.sleep(.05)
    finally:k.CloseHandle(process)
    args.output.write_text(json.dumps(dict(pid=args.pid,executable_sha256=GAME_HASH,game_memory_writes=False,engine_calls=False,samples=samples),indent=2)+'\n')
    print(json.dumps(dict(output=str(args.output),samples=len(samples),errors=sum('error' in s for s in samples))))
if __name__=='__main__':main()
