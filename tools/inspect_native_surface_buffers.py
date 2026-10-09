"""Pinned read-only MD6 surface to stored D3D buffer ownership; no engine calls."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
from inspect_native_hands import hand_state
from rig_fields import GAME_HASH,pointer,native_list,model_geometry_layout

def resource_name(read,address):
    # Base constructor0x05e740 + pathname consumer0x05e890 both use +8.
    text=pointer(read,address+8,1);raw=read(text,512);end=raw.find(b'\0')
    if end<=0 or any(b<32 or b>126 for b in raw[:end]):raise ValueError('Invalid bounded resource name')
    if pointer(read,address+8,1)!=text or read(text,end+1)!=raw[:end+1]:raise ValueError('Resource name changed')
    return raw[:end].decode('ascii')

def buffer_list(read,geometry,offset,kind):
    header=read(geometry+offset,16);data,count=native_list(read,geometry+offset,32,32)
    entries=read(data,count*32)if count else b'';result=[]
    for slot in range(count):
        wrapper,owned,base_offset,stride=struct.unpack_from('<QB7xQQ',entries,slot*32)
        if not wrapper:continue
        if wrapper<0x10000 or wrapper%8:raise ValueError('Invalid native buffer wrapper')
        raw=read(wrapper,32);com=struct.unpack_from('<Q',raw)[0]
        byte_width,structure_stride,elements,usage,flags=struct.unpack_from('<5I',raw,12)
        if not com or com%8 or not 0<byte_width<=128*1024*1024 or usage>3 or flags!=(1 if kind=='vertex'else 2):
            raise ValueError('Native buffer type/size/usage differs')
        # Constructor0x2d3420; CreateBuffer0x2d3aa0 stores ID3D11Buffer at+0.
        # Never invokes its methods; collected getter pointer supplies equality.
        table=pointer(read,com)
        if read(wrapper,32)!=raw:raise ValueError('Native COM wrapper changed during read')
        result.append(dict(slot=slot,wrapper=wrapper,com_buffer=com,com_vtable=table,byte_width=byte_width,
            structure_stride=structure_stride,elements=elements,usage=usage,bind_flags=flags,
            entry_owned=owned,entry_base_offset=base_offset,entry_stride=stride))
    if read(geometry+offset,16)!=header or (count and read(data,count*32)!=entries):raise ValueError('Native buffer list identity changed')
    return result

def inspect(read,base):
    state=hand_state(read,base);arms=[]
    for hand in state['hands']:
        skeleton=pointer(read,hand['descriptor']+0x50)
        layout=model_geometry_layout(read,dict(model=hand['model'],skeleton=skeleton),base)
        layout['primary_resource_name_method_rva']=hex(pointer(read,base+0x1b0d118+0x38)-base)
        layout['primary_resource_name']=resource_name(read,layout['primary_geometry'])
        for surface in layout.get('surfaces',[]):
            surface['native_material_declaration']=pointer(read,surface['surface']+0x28)
            surface['native_material_vtable']=pointer(read,surface['native_material_declaration'])
            if surface['native_material_vtable']!=base+0x1b17928:raise ValueError('Native material declaration type differs')
            surface['native_material_name']=resource_name(read,surface['native_material_declaration'])
            geometry=surface.get('geometry')
            if not geometry:continue
            prefix=read(geometry,0xa0)
            if struct.unpack_from('<Q',prefix)[0]!=base+0x1ab3a20:raise ValueError('Geometry type differs')
            surface['native_vertex_buffers']=buffer_list(read,geometry,0xa0,'vertex')
            surface['native_index_buffers']=buffer_list(read,geometry,0x298,'index')
            surface['native_index_element_size']=2 if prefix[0x9c]==2 else 4
            if read(geometry,0xa0)!=prefix:raise ValueError('Native geometry identity changed')
            if pointer(read,surface['surface']+0x90)!=geometry or pointer(read,surface['surface']+0x28)!=surface['native_material_declaration']:raise ValueError('Native surface geometry/material owner changed')
        fresh=model_geometry_layout(read,dict(model=hand['model'],skeleton=skeleton),base)
        for field in ('descriptor','source','primary_geometry','geometry_list_count'):
            if fresh[field]!=layout[field]:raise ValueError('Native model resource owner changed across buffer snapshot')
        if [(s['surface'],s['geometry'])for s in fresh.get('surfaces',[])]!=[(s['surface'],s['geometry'])for s in layout.get('surfaces',[])]:raise ValueError('Native surface list changed across buffer snapshot')
        arms.append(dict(side=hand['side'],model=hand['model'],descriptor=hand['descriptor'],geometry_layout=layout))
    if hand_state(read,base)!=state:raise ValueError('Native arm owner/equip identity changed')
    return arms

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.pid<=0 or c.sizeof(c.c_void_p)!=8:p.error('Require x64 and positive PID')
    k=c.WinDLL('kernel32',use_last_error=True)
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    snap=k.CreateToolhelp32Snapshot(8,args.pid)
    if snap==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        module=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snap,c.byref(module))or module.szModule.lower()!='dishonored2.exe'or module.modBaseSize!=0x41cb000:raise ValueError('Pinned module differs')
        if hashlib.sha256(Path(module.szExePath).read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Pinned executable differs')
        base=module.modBaseAddr
    finally:k.CloseHandle(snap)
    process=k.OpenProcess(0x1010,False,args.pid)
    if not process:raise OSError(c.get_last_error(),'Read-only open failed')
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768:raise ValueError('Invalid bounded read')
            raw=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(process,at,raw,size,c.byref(got))or got.value!=size:raise OSError(c.get_last_error(),'Native read failed')
            return raw.raw
        arms=inspect(read,base)
    finally:k.CloseHandle(process)
    result=dict(pid=args.pid,base=base,executable_sha256=GAME_HASH,game_memory_writes=False,engine_calls=False,
        animation_pose_capture=False,exact_visible_draw_identity_proven=False,arms=arms)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(output=str(args.output),arms=[dict(side=a['side'],surfaces=len(a['geometry_layout'].get('surfaces',[])))for a in arms])))

if __name__=='__main__':main()
