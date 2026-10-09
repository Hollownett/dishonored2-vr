"""Synthetic ownership/bounds checks for read-only native mesh inspection."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from rig_fields import md6_surfaces

base=0x7ff600000000
primary,entries,surface,geometry,fmt,ranges=(0x100000+i*0x10000 for i in range(6))
memory={}
def put(address,data): memory.update({address+i:v for i,v in enumerate(data)})
def pack(address,fmt,*values): put(address,struct.pack(fmt,*values))
def read(address,size):
    try: return bytes(memory[address+i] for i in range(size))
    except KeyError as error: raise OSError('Unreadable synthetic mesh') from error
def reject(action):
    try: action()
    except (ValueError,OSError): return
    raise AssertionError('Unsafe mesh accepted')

put(primary,b'\0'*0x80);pack(primary,'<Q',base+0x1b0d118)
pack(primary+0x58,'<QII',entries,1,1<<8)
pack(entries,'<Q',surface)
put(surface,b'\0'*0xb8);pack(surface+0x90,'<Q',geometry)
pack(surface+0xa8,'<QII',ranges,1,1<<8);pack(ranges,'<3I',0,0,6)
put(geometry,b'\0'*0xa0);pack(geometry,'<Q',base+0x1ab3a20)
pack(geometry+0x10,'<3f',-1,-1,-1);pack(geometry+0x20,'<3f',1,1,1)
pack(geometry+0x34,'<2I',6,4);pack(geometry+0x90,'<Q',fmt)
put(fmt,b'\0'*0x80);pack(fmt+0x10,'<H',44)
attributes,names=0x170000,0x180000
pack(fmt,'<QII',attributes,1,1<<8);pack(attributes,'<I4B',0,0,0,0,0)
pack(base+0x32d55a0+0xd0,'<Q',fmt);pack(base+0x229ab40,'<Q',names);put(names,b'POSITION'+b'\0'*24)
result=md6_surfaces(read,primary,base)[0]
assert result['vertices']==4 and result['indices']==6 and result['vertex_stride']==44
assert result['format_role']=='GPU' and result['cpu_vertex_prefix_hex'] is None
assert result['skin_weights_verified'] is False and result['game_memory_writes'] is False
pack(geometry+0x38,'<I',500001);reject(lambda:md6_surfaces(read,primary,base))
pack(geometry+0x38,'<I',4);pack(fmt+0x10,'<H',257);reject(lambda:md6_surfaces(read,primary,base))
pack(fmt+0x10,'<H',44);pack(geometry+0x10,'<f',float('nan'));reject(lambda:md6_surfaces(read,primary,base))
pack(geometry+0x10,'<f',-1);pack(primary,'<Q',base+0x1b0d120);reject(lambda:md6_surfaces(read,primary,base))
pack(primary,'<Q',base+0x1b0d118)
reads=0
def recycled(address,size):
    global reads
    if address==primary+0x58:
        reads+=1
        if reads>2: return struct.pack('<QII',entries,0,1<<8)
    return read(address,size)
reject(lambda:md6_surfaces(recycled,primary,base))
pack(surface+0x90,'<Q',0)
assert md6_surfaces(read,primary,base)[0]['cpu_geometry_available'] is False
print('PASS: native MD6 surface ownership, GPU-format fallback, bounded counts/stride, finite bounds, replaced owner and unavailable CPU geometry; read-only')
