"""Synthetic Iggy ownership guards, with no engine calls or input."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_menu_fields import native_menu_snapshot
base=0x7ff700000000
root,callback,batches,wrapper,file,player=0x100000,0x200000,0x300000,0x400000,0x500000,0x600000
manager=root+0x18
memory={}
def put(at,data):memory.update({at+i:b for i,b in enumerate(data)})
def q(at,value):put(at,struct.pack('<Q',value))
def read(at,size):
    try:return bytes(memory[at+i] for i in range(size))
    except KeyError as error:raise OSError('Unreadable fixture') from error
q(base+0x2274270,root);q(manager+0x828,callback)
put(callback,struct.pack('<3Q',base+0x1abe800,manager,base+0x32d40))
put(manager+8,struct.pack('<QII',batches,1,1<<8));put(batches+0x10,struct.pack('<QII',wrapper,1,1<<8))
q(wrapper,file);q(file+0x1618,player);put(wrapper+0xb08,struct.pack('<2f',.5,.25))
put(wrapper+0x878,struct.pack('<QII',wrapper+0x900,2,2<<8))
assert native_menu_snapshot(read,base)['files'][0]['iggy_player']==player
put(wrapper+0xb08,struct.pack('<2I',0x7149f2ca,0x7149f2ca))
assert native_menu_snapshot(read,base)['files'][0]['cursor_cache_reset']
put(wrapper+0xb08,struct.pack('<2f',.5,.25))
def reject(reader=read):
    try:native_menu_snapshot(reader,base)
    except (ValueError,OSError):return
    raise AssertionError('Invalid menu owner accepted')
q(callback+8,manager+8);reject();q(callback+8,manager)
put(batches+0x18,struct.pack('<I',65));reject();put(batches+0x18,struct.pack('<I',1))
put(wrapper+0xb08,struct.pack('<f',float('nan')));reject();put(wrapper+0xb08,struct.pack('<f',.5))
put(wrapper+0xb08,struct.pack('<2f',1e20,1e20));reject()
put(wrapper+0xb08,struct.pack('<If',0x7149f2ca,.25));reject();put(wrapper+0xb08,struct.pack('<2f',.5,.25))
def recycled(at,size):
    value=read(at,size)
    if at==wrapper+0xb08:q(file+0x1618,player+8)
    return value
reject(recycled)
print('PASS: Iggy callback owner, bounded wrapper/member lists, finite cursor and replaced file rejection; no native calls or writes')
