"""Exact pinned PE RTTI/base layout and native equipment initializer checks."""
from pathlib import Path
import hashlib
import json
import struct
from probe_game import PE
from rig_fields import GAME_HASH

ROOT=Path(__file__).resolve().parents[1]
def check():
    raw=(ROOT/'artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe').read_bytes()
    assert hashlib.sha256(raw).hexdigest()==GAME_HASH
    pe=PE(raw)
    assert pe.machine==0x8664 and pe.image_base==0x140000000 and pe.image_size==0x41cb000
    def u32(at):return pe.unpack('<I',pe.offset(at,4))[0]
    def text(at):return pe.bytes(pe.offset(at,160),160).split(b'\0',1)[0].decode('ascii')
    types={}
    for name in ('arkDeclInventoryItem','arkCpntEquipment','arkDeclCpntEquipment','arkCpntWeaponMeleePlayer','arkDeclCpntWeaponMeleePlayer'):
        tables=pe.rtti_vtables(name)
        types[name]=tables
        for table in tables:
            col=table['locator_rva']
            ch=u32(col+16);count=u32(ch+8);array=u32(ch+12)
            assert 0<count<64
            bases=[]
            for i in range(count):
                bcd=u32(array+i*4);td=u32(bcd)
                base_name=text(td+16)
                bases.append(dict(name=base_name,pmd=list(pe.unpack('<iii',pe.offset(bcd+8,12)))))
            table['bases']=bases
            if table['subobject_offset']==0:
                table['first32_methods']=[hex(pe.unpack('<Q',pe.offset(table['vtable_rva']+i*8,8))[0]-pe.image_base) for i in range(32)]
    assert any(x['vtable_rva']==0x1b9d508 and any(b['name']=='.?AVarkCpntEquipment@@' and b['pmd']==[0,-1,0] for b in x['bases']) for x in types['arkCpntWeaponMeleePlayer'])
    assert any(x['vtable_rva']==0x1b9d3c8 and any(b['name']=='.?AVarkDeclCpntEquipment@@' and b['pmd']==[0,-1,0] for b in x['bases']) for x in types['arkDeclCpntWeaponMeleePlayer'])
    calls={0x63018d:0x6360a0,0x6360a9:0x61ac10,0x6303a9:0x61aed0,0x62da8b:0x633b30}
    for site,target in calls.items():
        instruction=pe.bytes(pe.offset(site,5),5)
        assert instruction[0]==0xe8 and site+5+struct.unpack_from('<i',instruction,1)[0]==target
    assert text(0x1d5efc8)=='idList < arkInventoryItem , TAG_COMPONENT , HEAP_DEFAULTHEAP >'
    assert text(0x1d98c00)=='m_items'
    return dict(executable_sha256=GAME_HASH,types=types)

if __name__=='__main__':
    result=check()
    target=ROOT/'artifacts/research/equipment-sword-abi-2026-10-09.json'
    target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(target),pinned_pe_abi_checks=True,types=len(result['types']))))
