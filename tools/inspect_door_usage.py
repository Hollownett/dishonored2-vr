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

HISTORICAL_ENTITY_IDS={88998248,88932711}
OPENABLE_VTABLE_RVA='0x1c569c8'


def filter_actors(actors,nearby_openables_only=False):
    if nearby_openables_only:
        return [actor for actor in actors if any(component.get('vtable_rva')==OPENABLE_VTABLE_RVA
            for component in actor.get('components',()))]
    return [actor for actor in actors if actor.get('entity_id') in HISTORICAL_ENTITY_IDS]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--output',type=Path,help='Unique read-only snapshot output path')
    parser.add_argument('--nearby-openables-only',action='store_true',help='Inspect every nearby typed openable; omit unrelated hand/carry inspection')
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
        def actor_name(entity):
            at=pointer(read,entity+0x20,1)
            chars=bytearray()
            for i in range(256):
                b=read(at+i,1)
                if b==b'\0':break
                chars+=b
            else:raise ValueError('Authored name exceeds bounded read')
            name=chars.decode('utf-8')
            if any(ord(ch)<32 for ch in name) or read(at,len(chars)+1)!=bytes(chars)+b'\0' or pointer(read,entity+0x20,1)!=at:raise ValueError('Authored name changed')
            return name
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
                            # Inventory every verified component type; no semantic offset assumptions.
                            identity=read(at+0x18,8);gen,flags=struct.unpack('<iI',identity)
                            if pointer(read,at+0x10)!=entity or gen==-1:continue
                            if read(at+0x18,8)!=identity:continue
                            components.append(dict(address=at,generation=gen,flags=flags,started=component_ready(flags,40,0),**typ))
                        except (ValueError,OSError,UnicodeError):continue
                    if components and entity_for_id(read,game,eid)==entity and native_list(read,entity+0x268,8,4096)==(data,count) and (read(data,count*8) if count else b'')==raw:
                        name=actor_name(entity)
                        if entity_for_id(read,game,eid)!=entity:continue
                        result['actors'].append(dict(entity=entity,entity_id=eid,position=list(pos),distance_native_units=distance,authored_name=name,entity_type=typename(pointer(read,entity)),components=components))
                except (ValueError,OSError):result['rejected']+=1
        if pointer(read,game+0x3130b0)!=player:raise ValueError('Player changed')
        result['actors'].sort(key=lambda x:x['distance_native_units'])
        result['actors']=filter_actors(result['actors'],args.nearby_openables_only)
        from interaction_fields import interaction_snapshot
        result['interaction']=interaction_snapshot(read,base)
        for actor in result['actors']:
            for cp in actor['components']:
                if cp['vtable_rva']!='0x1c569c8':continue
                at=cp['address'];identity=read(at+0x18,8);declaration=pointer(read,at+8)
                raw=read(at,0x200);decl=read(declaration,0x1b8)
                if pointer(read,at)!=base+0x1c569c8 or pointer(read,at+0x10)!=actor['entity']:raise ValueError('Openable owner/type differs')
                state=struct.unpack_from('<i',raw,0x48)[0]
                allow=bool(decl[0x1b0]);primary_offset=None;secondary_offset=None
                if state==2:primary_offset=0xd0
                elif state==0:
                    primary_offset=0xf8 if raw[0x1e8] else 0x120 if raw[0x118] else 0xa8
                    if allow:secondary_offset=0x148
                cp['verified_usage_branch']=dict(state48=state,flag118=raw[0x118],flag1e8=raw[0x1e8],declaration_allowKeyhole=allow,primary_usage_offset=hex(primary_offset) if primary_offset else None,secondary_usage_offset=hex(secondary_offset) if secondary_offset else None,keyhole_secondary_offered=secondary_offset==0x148,state_d8=struct.unpack_from('<i',raw,0xd8)[0],declaration=declaration,primary_usage_opaque=decl[primary_offset:primary_offset+0x28].hex() if primary_offset else None,secondary_usage_opaque=decl[0x148:0x170].hex(),component_opaque=raw.hex(),declaration_opaque=decl.hex())
                if read(at+0x18,8)!=identity or pointer(read,at+8)!=declaration or read(at,0x200)!=raw or read(declaration,0x1b8)!=decl or entity_for_id(read,game,actor['entity_id'])!=actor['entity']:raise ValueError('Openable identity/state changed')
        machine=result['interaction']['state_machine'];machine_identity=read(machine+0x18,8)
        pool=pointer(read,machine+0x90);slots=read(pool+0x10,90*8);states=[]
        for index,state in enumerate(struct.unpack('<'+'Q'*90,slots)):
            if not state:continue
            if pointer(read,state)!=base+0x1c86ab8:continue
            if pointer(read,state+0x10)!=machine or struct.unpack('<i',read(state+8,4))[0]!=index:raise ValueError('Interact state owner/index differs')
            declaration=pointer(read,state+0x18);decl_table=pointer(read,declaration)
            raw=read(state,0x48);threshold=read(declaration+0x70,4)
            states.append(dict(index=index,state=state,vtable_rva='0x1c86ab8',declaration=declaration,declaration_vtable_rva=hex(decl_table-base),active=raw[0x20],target_generation=struct.unpack_from('<i',raw,0x30)[0],target_component=struct.unpack_from('<Q',raw,0x38)[0],controller_action_index=struct.unpack_from('<I',raw,0x40)[0],elapsed_value=struct.unpack_from('<f',raw,0x44)[0],secondary_hold_threshold=struct.unpack('<f',threshold)[0]))
            if read(state,0x48)!=raw or pointer(read,state+0x18)!=declaration or read(declaration+0x70,4)!=threshold:raise ValueError('Interact state changed')
        if read(machine+0x18,8)!=machine_identity or pointer(read,machine+0x90)!=pool or read(pool+0x10,90*8)!=slots or pointer(read,machine+0x10)!=player:raise ValueError('Interact pool changed')
        result['verified_interact_states']=states
        from player_camera_fields import player_camera_snapshot
        result['camera_snapshot']=player_camera_snapshot(read,base,player,camera=result['interaction']['camera'])
        if args.nearby_openables_only:
            result['hand_state']=None
            result['hand_state_not_requested']=True
        else:
            from inspect_native_hands import hand_state
            result['hand_state']=hand_state(read,base)
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False)
    finally: k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-grab-bounds-{args.pid}-readonly.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
