"""Export selected generated type records from local Ghidra evidence, without running game code."""
import hashlib
import json
from pathlib import Path
import re
import sys

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from probe_game import PE

sha='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
analysis=root/'artifacts/re-toolchain/direct-ghidra'
data=(analysis/'Dishonored2-steam17576867.exe').read_bytes()
if hashlib.sha256(data).hexdigest()!=sha:
    raise ValueError('Executable differs from the pinned Steam build')
pe=PE(data)
def string(va):
    at=pe.offset(va-pe.image_base)
    end=pe.data.index(b'\0',at,min(at+512,len(pe.data)))
    return pe.data[at:end].decode('ascii')

owners={
    'arkCpntAnimBase':('anim-evolved/function-141596200.txt',0x3b3a0a0,0x410),
    'arkCpntAnimEvolved':('anim-fields/function-1415f0320.txt',0x3b69670,0x2d40),
    'arkCpntEquipment':('query-exports/function-141598000.txt',0x3b3da60,0x68),
    'arkCpntPlayerBody':('query-exports/function-1416d64d0.txt',0x3bf88e0,0xf40),
    'arkCpntPlayerBody::arkBodyBase':('body-base/function-14171d5d0.txt',0x3c27770,0x2b0),
    'arkCpntPlayerBody::arkBodyLoco':('body-base/function-14171e350.txt',0x3c27e40,0x2b8),
    'arkCpntPlayerBody::arkBodyPart':('rig-and-output/function-14171e740.txt',0x3c28170,0x310),
}
classes={}
for owner,(relative,table_rva,size) in owners.items():
    text=(analysis/relative).read_text()
    if not text.startswith('sha256='+sha) or 'decompilation_completed=true' not in text:
        raise ValueError('Ghidra export is incomplete or belongs to another executable')
    slots={int(a,16):string(int(b,16)) for a,b in re.findall(r'FUN_140131f90\(&DAT_([0-9a-f]+),&DAT_([0-9a-f]+),0\)',text)}
    values={int(a,16):int(v,0) for a,v in re.findall(r'_DAT_([0-9a-f]+) = (0x[0-9a-f]+|[0-9]+);',text)}
    fields=[]
    address=pe.image_base+table_rva
    while address in slots and address+16 in slots:
        offset=values[address+24];field_size=values[address+28]
        if field_size==0xffffffff:
            address+=0x48
            continue # Generated accessor property, not a direct storage field.
        if not 0<=offset<offset+field_size<=size:raise ValueError('Field outside declaring class')
        fields.append({'name':slots[address+16],'type':slots[address],
            'offset':hex(offset),'size':hex(field_size),'record_rva':hex(address-pe.image_base)})
        address+=0x48
    classes[owner]={'size':hex(size),'fields':fields,'decompiled_initializer':relative,
        'limit':'Static generated metadata only; no live instance or bone writes validated'}
    print(owner, len(fields),'fields')
    for field in fields:
        if any(w in (field['name']+' '+field['type']).lower() for w in ('model','joint','skeleton','arm','playerbody')):
            print(json.dumps(field))
(root/'artifacts/research/d2-body-field-metadata.json').write_text(json.dumps(
    {'sha256':sha,'classes':classes,'live_verified':False},indent=2))
