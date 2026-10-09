"""Read verified static-prop render bounds; never write memory or call the engine."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import struct
from object_fields import nearby_bodies
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
        result=nearby_bodies(read,base)
        for body in result['bodies']:
            try:
                model=pointer(read,body['entity']+0xf0)
                if pointer(read,model)!=base+0x1b0dd88: continue
                bounds=struct.unpack('<8f',read(model+0x1840,32))
                mode,motion=struct.unpack('<II',read(body['record']+0x40,8))
                root,primary=struct.unpack('<II',read(body['record']+0x70,8))
                parent=struct.unpack('<Q',read(body['component']+0xc8,8))[0]
                entity_type=pointer(read,body['entity'])-base
                failures=[]
                if entity_type not in (0x1c886a8,0x1c87d58): failures.append('entity_type')
                if motion&7!=2: failures.append('motion_type')
                if not 0<mode<0x100000: failures.append('motion_index')
                if root!=body['handle'] or primary!=body['handle']: failures.append('primary_handle')
                if parent: failures.append('native_attachment')
                if body['temporary_activation_state']: failures.append('temporary_activation')
                if body['type']=='throwable' and body.get('throwable_flags',0)&8: failures.append('throwable_attachment')
                body['grab_eligibility']=dict(eligible=not failures,failures=failures,parent=parent,root=root,primary=primary,entity_vtable_rva=hex(entity_type))
                body['render_geometry']=dict(model=model,world_min=list(bounds[:3]),world_max=list(bounds[4:7]),
                    origin=list(struct.unpack('<3f',read(model+0x1860,12))),
                    axis=list(struct.unpack('<9f',read(model+0x186c,36))),
                    scale=list(struct.unpack('<3f',read(model+0x1890,12))))
            except (ValueError,OSError): continue
        result.update(pid=args.pid,executable_sha256=GAME_HASH,time_utc=datetime.now(timezone.utc).isoformat(),game_memory_writes=False,engine_calls=False)
    finally: k.CloseHandle(process)
    output=args.output or Path(__file__).resolve().parents[1]/'artifacts'/f'native-grab-bounds-{args.pid}-readonly.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
