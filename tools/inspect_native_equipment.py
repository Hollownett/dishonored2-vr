"""Bounded read-only equipment ownership diagnostic; never invokes engine code."""
import json
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
from pathlib import Path
import time
import struct
from rig_fields import GAME_HASH, pointer, native_list, component_ready
from object_fields import entity_for_id
from inspect_native_hands import hand_state


def equipment_snapshot(read, base):
    hands = hand_state(read, base)
    game = pointer(read, base+0x39a5290)
    guards = {}
    def take(at, size):
        raw = read(at, size)
        guards[(at, size)] = raw
        return raw
    def q(at):
        return struct.unpack('<Q', take(at, 8))[0]
    def typename(at):
        table = q(at)
        if not base <= table < base+0x41cb000:
            raise ValueError('RTTI vtable outside pinned image')
        locator = q(table-8)
        sig, offset, ctor, td, ch, selfr = struct.unpack('<6I', take(locator,24))
        if sig != 1 or offset != 0 or base+selfr != locator or td >= 0x41cb000:
            raise ValueError('RTTI locator differs')
        name = take(base+td+16, 160).split(b'\0',1)[0].decode('ascii')
        return dict(name=name, vtable_rva=hex(table-base), type_descriptor_rva=hex(td), locator_rva=hex(selfr))
    def components(entity):
        header=take(entity+0x268,16)
        data,count=native_list(read,entity+0x268,8,4096)
        addresses=take(data,count*8) if count else b''
        result=[]
        for at in struct.unpack('<'+'Q'*count,addresses):
            table=q(at)
            if table in (base+0x1b9ae68,base+0x1b9d508):
                identity=take(at+0x18,8)
                generation,flags=struct.unpack('<iI',identity)
                if generation==-1 or not component_ready(flags,40,0):
                    raise ValueError('Equipment component not started')
                decl=q(at+8)
                result.append(dict(component=at,type=typename(at),identity=identity.hex(),owner=q(at+0x10),
                    declaration=decl,declaration_type=typename(decl),slot_enum=struct.unpack('<i',take(decl+0x68,4))[0],
                    player_body=q(at+0x38),player_sensor=q(at+0x40),player_camera=q(at+0x48)))
        return dict(header=header.hex(),entries=data,count=count,equipment=result)
    result=dict(player=hands['player'],body=hands['body'],inventory=hands['inventory'],hands=hands['hands'],slots=[])
    for hand in hands['hands']:
        slot=hand['slot']
        if not slot:
            result['slots'].append(dict(side=hand['side'],slot=0))
            continue
        raw=take(slot,0x80)
        record=dict(side=hand['side'],slot=slot,raw=raw.hex())
        try:record['type']=typename(slot)
        except (ValueError,OSError,UnicodeError) as error:record['type_error']=str(error)
        # Diagnostic only: decode candidate pointer fields without claiming their ABI.
        candidates=[]
        child_id=struct.unpack_from('<I',raw,8)[0]
        child=entity_for_id(read,game,child_id)
        if child:
            candidates.append(dict(source='slot+8 native entity ID (548be0)',entity=child,entity_id=hex(child_id),
                type=typename(child),registry_matches=True,components=components(child)))
            child_data,child_count=native_list(read,child+0x268,8,4096)
            record['child_component_types']=[dict(component=at,type=typename(at),declaration=q(at+8),
                identity=take(at+0x18,8).hex()) for at in struct.unpack('<'+'Q'*child_count,take(child_data,child_count*8))]
            record['slot_root_type']=typename(q(slot))
        for off in (0,8,0x10,0x18,0x20,0x28,0x30):
            at=struct.unpack_from('<Q',raw,off)[0]
            if not 0x10000<=at<0x800000000000 or at%8:continue
            try:
                typ=typename(at)
                if 'arkEntity' in typ['name'] or 'arkPlayer' in typ['name']:
                    eid=struct.unpack('<I',take(at+0xa8,4))[0]
                    candidates.append(dict(offset=hex(off),entity=at,type=typ,entity_id=hex(eid),
                        registry_matches=entity_for_id(read,game,eid)==at,components=components(at)))
            except (ValueError,OSError,UnicodeError):pass
        record['candidate_entities']=candidates
        result['slots'].append(record)
    result['player_components']=components(hands['player'])
    if any(read(at,size)!=raw for (at,size),raw in guards.items()) or hand_state(read,base)!=hands:
        raise ValueError('Equipment ownership changed during read')
    result['guard_count']=len(guards)
    result['guards_revalidated']=True
    result['equipment_link_proven']=False
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--seconds',type=float,default=2);args=parser.parse_args()
    if args.pid<=0 or not 0<=args.seconds<=10 or c.sizeof(c.c_void_p)!=8:parser.error('Require positive PID, x64 Python and0..10seconds')
    k=c.WinDLL('kernel32',use_last_error=True)
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),
            ('modBaseAddr',c.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snapshot=k.CreateToolhelp32Snapshot(8,args.pid)
    if snapshot==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        module=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snapshot,c.byref(module)) or module.szModule.lower()!='dishonored2.exe' or module.modBaseSize!=0x41cb000:
            raise ValueError('Require pinned Dishonored2 image')
        if hashlib.sha256(Path(module.szExePath).read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Game executable hash differs')
        base=module.modBaseAddr
    finally:k.CloseHandle(snapshot)
    process=k.OpenProcess(0x1010,False,args.pid)
    if not process:raise OSError(c.get_last_error(),'Read-only open failed')
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768:raise ValueError('Invalid bounded read')
            raw=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(process,at,raw,size,c.byref(got)) or got.value!=size:raise OSError(c.get_last_error(),'Native read failed')
            return raw.raw
        samples=[];start=time.monotonic()
        while True:
            try:samples.append(dict(seconds=time.monotonic()-start,state=equipment_snapshot(read,base)))
            except (OSError,ValueError) as error:samples.append(dict(seconds=time.monotonic()-start,error=str(error)))
            if time.monotonic()-start>=args.seconds:break
            time.sleep(.1)
    finally:k.CloseHandle(process)
    result=dict(pid=args.pid,base=hex(base),executable_sha256=GAME_HASH,game_memory_writes=False,engine_calls=False,
        authored_fist_captured=False,empty_eligibility_proven=False,samples=samples)
    target=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'/f'equipment-state-{args.pid}-readonly.json'
    target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(target),samples=len(samples),first=samples[0],last=samples[-1])))



if __name__=="__main__":main()
