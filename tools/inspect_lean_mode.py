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
        from interaction_fields import interaction_snapshot
        initial=interaction_snapshot(read,base);player=initial['player'];machine=initial['state_machine']
        machine_identity=read(machine+0x18,8)
        if pointer(read,machine)!=base+0x1c73d58 or pointer(read,machine+0x10)!=player:raise ValueError('Machine owner differs')
        cvar=base+0x3995600;cvar_name=pointer(read,cvar+0x30,1)
        if read(cvar_name,13)!=b'g_leanToggle\0':raise ValueError('Lean cvar name differs')
        cvar_value=read(cvar+0x28,4)
        input_root=pointer(read,base+0x2274340)
        device_branch=read(input_root+0xa544,1)[0]
        if device_branch>1:raise ValueError('Input mode flag differs')
        controller=pointer(read,machine+0x2f8);body=pointer(read,machine+0x300)
        if pointer(read,controller)!=base+0x1c72918 or pointer(read,body)!=base+0x1c5a8d8 or pointer(read,body+0x20)!=base+0x1c5a9b0 or pointer(read,controller+0x10)!=player or pointer(read,body+0x10)!=player:raise ValueError('Lean body/controller RTTI ownership differs')
        pool=pointer(read,machine+0x90)
        raw=read(pool+0x10,90*8)
        result=dict(controller=controller,body=body,controller_identity=list(struct.unpack('<iI',read(controller+0x18,8))),body_identity=list(struct.unpack('<iI',read(body+0x18,8))),player=player,state_machine=machine,machine_generation=struct.unpack('<iI',machine_identity)[0],state_pool=pool,g_leanToggle=struct.unpack('<i',cvar_value)[0],cvar_object_rva='0x3995600',cvar_value_rva='0x3995628',native_input_device_branch=device_branch,input_device_root=input_root,states=[])
        for index,state in enumerate(struct.unpack('<'+'Q'*90,raw)):
            if not state:continue
            try:
                if pointer(read,state)!=base+0x1c82b28:continue
                state_raw=read(state,0x60)
                declaration=pointer(read,state+0x18)
                if pointer(read,declaration)!=base+0x1c829f8 or struct.unpack('<i',read(declaration+0x60,4))[0]!=18:raise ValueError('Lean declaration type/index differs')
                own=struct.unpack_from('<Q',state_raw,0x10)[0];native_index=struct.unpack_from('<i',state_raw,8)[0]
                if own!=machine or native_index!=index:continue
                result['states'].append(dict(address=state,index=index,vtable_rva='0x1c82b28',declaration=struct.unpack_from('<Q',state_raw,0x18)[0],active_byte20=state_raw[0x20],lean_action_index30=struct.unpack_from('<I',state_raw,0x30)[0],flags34_37=list(state_raw[0x34:0x38]),floats38_58=list(struct.unpack_from('<9f',state_raw,0x38)),opaque_bytes=state_raw.hex()))
                if pointer(read,state+0x10)!=machine or read(state+8,4)!=state_raw[8:12]:raise ValueError('State owner changed')
            except OSError:continue
        if read(machine+0x18,8)!=machine_identity or pointer(read,machine+0x10)!=player or pointer(read,machine+0x90)!=pool or read(pool+0x10,90*8)!=raw or pointer(read,cvar+0x30,1)!=cvar_name or read(cvar+0x28,4)!=cvar_value or pointer(read,base+0x2274340)!=input_root:raise ValueError('State/cvar roots changed')
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False)
    finally: k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-grab-bounds-{args.pid}-readonly.json'
    if output.exists():raise ValueError('Refuse overwrite')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
