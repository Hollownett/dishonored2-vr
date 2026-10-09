"""Read-only native hand slot/equip-graph diagnostic; never changes game state."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time
from rig_fields import GAME_HASH, pointer, native_list, component_ready
from native_carry_fields import carry_snapshot, carry_current


def hand_state(read, base):
    game=pointer(read,base+0x39a5290)
    if pointer(read,game)!=base+0x1c940a8:raise ValueError('Game type differs')
    player=pointer(read,game+0x3130b0)
    if pointer(read,player)!=base+0x1c5e428:raise ValueError('Player type differs')
    data,count=native_list(read,player+0x268,8,4096)
    addresses=read(data,count*8)
    bodies=[at for at in struct.unpack('<'+'Q'*count,addresses) if pointer(read,at)==base+0x1c5a8d8 and pointer(read,at+0x10)==player]
    if len(bodies)!=1:raise ValueError('Ambiguous player body')
    body=bodies[0];carry_before=carry_snapshot(read,base,game,player,body);inventory=pointer(read,body+0x60)
    if pointer(read,inventory)!=base+0x1b9a878 or pointer(read,inventory+0x10)!=player:raise ValueError('Inventory type/owner differs')
    identities={at:read(at+0x18,8) for at in (body,inventory)}
    for raw in identities.values():
        generation,flags=struct.unpack('<iI',raw)
        if generation==-1 or not component_ready(flags,40,0):raise ValueError('Native component is not started')
    slots=read(inventory+0x68,16);right,left=struct.unpack('<QQ',slots)
    result=dict(player=player,body=body,inventory=inventory,inventory_identity=identities[inventory].hex(),
        body_identity=identities[body].hex(),inventory_vtable_rva=hex(pointer(read,inventory)-base),hands=[])
    for side,part,slot in (('left',0x370,left),('right',0x680,right)):
        animation=pointer(read,body+part+8)
        if pointer(read,animation)!=base+0x1b80408:raise ValueError('Arm animation type differs')
        identities[animation]=read(animation+0x18,8)
        generation,flags=struct.unpack('<iI',identities[animation])
        if generation==-1 or not component_ready(flags,40,0):raise ValueError('Arm animation not started')
        model=pointer(read,animation+0x38);descriptor=pointer(read,model+0x1fe8)
        if pointer(read,model)!=base+0x1b0d628:raise ValueError('Arm model type differs')
        decl=pointer(read,descriptor+0x138)
        embedded_decl=struct.unpack('<Q',read(body+part+0x18,8))[0]
        if (pointer(read,decl)!=base+0x1c4d448 or embedded_decl!=decl+(0x190 if side=='left' else 0x1d0) or
                pointer(read,embedded_decl)!=base+0x1c4d438):raise ValueError('Player locomotion root/arm node type differs')
        index=struct.unpack('<b',read(body+part+0x21,1))[0]
        names,n=native_list(read,decl+(0xa0 if side=='left' else 0xb0),8,128)
        all_names=[]
        for j in range(n):
            text=pointer(read,names+j*8);raw=read(text,64).split(b'\0',1)[0]
            if any(v<32 or v>126 for v in raw):raise ValueError('Invalid equip graph list name')
            all_names.append(raw.decode('ascii'))
        graph=None
        if 0<=index<n:
            text=pointer(read,names+index*8);raw=read(text,64).split(b'\0',1)[0]
            if not raw or any(v<32 or v>126 for v in raw):raise ValueError('Invalid equip graph name')
            graph=raw.decode('ascii')
        result['hands'].append(dict(side=side,slot=slot,animation=animation,model=model,descriptor=descriptor,
            graph_decl=decl,embedded_decl=embedded_decl,node_ownership_verified=True,equip_index=index,equip_name=graph,
            animation_identity=identities[animation].hex(),equip_name_list=names,
            graph_vtable_rva=hex(pointer(read,decl)-base),embedded_vtable_rva=hex(pointer(read,embedded_decl)-base),equip_states=all_names,
            empty_candidate=slot==0 and graph=='Empty' and not carry_before['carrying']))
    if read(inventory+0x68,16)!=slots or pointer(read,body+0x60)!=inventory or pointer(read,game+0x3130b0)!=player or \
            native_list(read,player+0x268,8,4096)!=(data,count) or read(data,count*8)!=addresses or \
            any(read(at+0x18,8)!=identity for at,identity in identities.items()):raise ValueError('Ownership/state changed during snapshot')
    carry_after=carry_current(read,base,carry_before)
    if carry_after!=carry_before:raise ValueError('Carry transition during hand snapshot')
    result.update(native_carry=carry_before,carrying=carry_before['carrying'],carry_before=carry_before,carry_after=carry_after,carry_consistent=True)
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
            try:samples.append(dict(seconds=time.monotonic()-start,state=hand_state(read,base)))
            except (OSError,ValueError) as error:samples.append(dict(seconds=time.monotonic()-start,error=str(error)))
            if time.monotonic()-start>=args.seconds:break
            time.sleep(.1)
    finally:k.CloseHandle(process)
    result=dict(pid=args.pid,base=hex(base),executable_sha256=GAME_HASH,game_memory_writes=False,engine_calls=False,
        authored_fist_captured=False,empty_eligibility_proven=False,samples=samples)
    target=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'/f'hand-state-{args.pid}-readonly.json'
    target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(target),samples=len(samples),first=samples[0],last=samples[-1])))


if __name__=='__main__':main()
