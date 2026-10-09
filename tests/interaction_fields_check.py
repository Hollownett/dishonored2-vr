"""Meaningful owner/lifetime failures for native interaction inspection."""
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from interaction_fields import interaction_snapshot


class Memory:
    def __init__(self):self.data={};self.after_read=None
    def put(self,at,fmt,*values):
        for i,b in enumerate(struct.pack(fmt,*values)):self.data[at+i]=b
    def zero(self,at,count):self.data.update({at+i:0 for i in range(count)})
    def read(self,at,count):
        result=bytes(self.data[at+i] for i in range(count))
        if self.after_read:self.after_read(at,count)
        return result


base=0x140000000;game=0x100000;player=0x200000;interaction=0x300000
camera=0x400000;machine=0x500000;sensor=0x600000;parent=0x700000;target=0x800000;entries=0x900000


def fixture():
    m=Memory()
    m.put(base+0x39a5290,'<Q',game);m.put(game,'<Q',base+0x1c940a8)
    m.put(game+0x3130b0,'<Q',player);m.put(player,'<Q',base+0x1c5e428)
    m.put(player+0x268,'<QII',entries,2,2<<8);m.put(entries,'<2Q',interaction,sensor)
    for at,vt in [(interaction,0x1c72e98),(camera,0x1c72218),(machine,0x1c73d58),(sensor,0x1b403b8),(parent,0x1c5b108),(target,0x1c5c4c8)]:
        m.put(at,'<Q',base+vt);m.put(at+0x10,'<Q',player);m.put(at+0x18,'<iI',12,10)
    m.put(camera+0x20,'<Q',base+0x1c722f8);m.put(machine+0x318,'<Q',interaction)
    m.zero(interaction+0x20,0x50);m.put(interaction+0x20,'<QQ',camera,machine);m.put(interaction+0x30,'<iIQ',-1,0,0)
    m.zero(sensor+0x20,0x78);m.put(sensor+0x20,'<Q',machine);m.put(sensor+0x30,'<Q',parent)
    m.put(sensor+0x38,'<B',1);m.put(sensor+0x40,'<Q',camera)
    m.put(sensor+0x84,'<4I',0x3fff0010,0x3fff0010,0x3fff0010,0)
    return m


def rejected(edit):
    m=fixture();edit(m)
    try:interaction_snapshot(m.read,base)
    except (ValueError,KeyError):return
    raise AssertionError('Invalid native interaction was accepted')


m=fixture();result=interaction_snapshot(m.read,base)
assert result['interaction_available'] and not result['target_reference']['available']
assert result['interaction_sensor']['parent']==parent and result['camera']==camera
m.put(interaction+0x30,'<iIQ',12,0,target)
assert interaction_snapshot(m.read,base)['target_reference']['available']
m.put(target+0x18,'<iI',13,10)
assert not interaction_snapshot(m.read,base)['target_reference']['available']
rejected(lambda m:m.put(camera+0x20,'<Q',base+0x1c72218))
rejected(lambda m:m.put(camera+0x10,'<Q',player+8))
rejected(lambda m:m.put(machine+0x318,'<Q',interaction+8))
rejected(lambda m:m.put(sensor+0x40,'<Q',camera+8))
rejected(lambda m:m.put(sensor+0x30,'<Q',camera))
rejected(lambda m:m.put(interaction+0x18,'<iI',-1,10))
rejected(lambda m:m.put(interaction+0x18,'<iI',12,0))
rejected(lambda m:m.put(player+0x268,'<QII',entries,3,2<<8))
rejected(lambda m:m.put(interaction+0x40,'<B',2))


def recycled(m):
    def after(at,count):
        if at==interaction+0x20 and count==0x50:m.put(interaction+0x18,'<iI',13,10)
    m.after_read=after
rejected(recycled)
print('PASS: native camera/state-machine/sensor ownership, target generation reuse, bounds, secondary vtable and concurrent lifetime rejection; read-only decoder')
