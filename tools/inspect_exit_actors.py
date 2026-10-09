"""Read verified static-prop render bounds; never write memory or call the engine."""
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
    args=parser.parse_args()
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
        game=pointer(read,base+0x39a5290)
        if pointer(read,game)!=base+0x1c940a8: raise ValueError('Game owner differs')
        player=pointer(read,game+0x3130b0)
        origin=struct.unpack('<3f',read(player+0x300,12))
        cache={}
        def typename(table):
            if table in cache:return cache[table]
            if not base<=table<base+0x041cb000:raise ValueError('Table outside pinned image')
            locator=pointer(read,table-8)
            if not base<=locator<base+0x041cb000:raise ValueError('Locator outside image')
            sig,offset,ctor,td,ch,selfr=struct.unpack('<6I',read(locator,24))
            if sig!=1 or base+selfr!=locator or offset!=0 or td>=0x041cb000:raise ValueError('RTTI REV1 owner differs')
            name=read(base+td+16,160).split(b'\0',1)[0].decode('ascii')
            if not name.startswith('.?AV'):raise ValueError('RTTI typename differs')
            cache[table]=dict(name=name,vtable_rva=hex(table-base),type_descriptor_rva=hex(td),locator_rva=hex(selfr))
            return cache[table]
        result=dict(player=player,origin=list(origin),radius_native_units=40,actors=[],rejected=0)
        for start in range(0,0x3fff,2048):
            n=min(2048,0x3fff-start)
            registry=struct.unpack('<'+'Q'*n,read(game+0x3130b0+start*8,n*8))
            for idx,entity in enumerate(registry,start):
                if not entity or entity==player:continue
                try:
                    eid=struct.unpack('<I',read(entity+0xa8,4))[0]
                    if eid>>16!=idx or entity_for_id(read,game,eid)!=entity:continue
                    pos=struct.unpack('<3f',read(entity+0x300,12));distance=math.dist(pos,origin)
                    if not math.isfinite(distance) or distance>40:continue
                    data,count=native_list(read,entity+0x268,8,4096)
                    raw=read(data,count*8) if count else b''; components=[]
                    for at in struct.unpack('<'+'Q'*count,raw):
                        try:
                            typ=typename(pointer(read,at))
                            if not any(word in typ['name'].lower() for word in ('window','door','exit','objective','mission')):continue
                            identity=read(at+0x18,8);gen,flags=struct.unpack('<iI',identity)
                            if pointer(read,at+0x10)!=entity or gen==-1:continue
                            if read(at+0x18,8)!=identity:continue
                            components.append(dict(address=at,generation=gen,flags=flags,started=component_ready(flags,40,0),**typ))
                        except (ValueError,OSError,UnicodeError):continue
                    if components and entity_for_id(read,game,eid)==entity and native_list(read,entity+0x268,8,4096)==(data,count) and (read(data,count*8) if count else b'')==raw:
                        result['actors'].append(dict(entity=entity,entity_id=eid,position=list(pos),distance_native_units=distance,components=components))
                except (ValueError,OSError):result['rejected']+=1
        if pointer(read,game+0x3130b0)!=player:raise ValueError('Player changed')
        result['actors'].sort(key=lambda x:x['distance_native_units'])
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False)
    finally: k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-grab-bounds-{args.pid}-readonly.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
