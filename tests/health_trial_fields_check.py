"""Synthetic health ownership and damage/death evidence checks; no process access."""
import copy
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from health_trial_fields import discover_health_actor,health_actor_sample,primary_owner,primary_feet
B=0x140000000;G=0x200000000;P=0x210000000;E=0x220000000;H=0x230000000;D=0x240000000;L=0x250000000;NAME=0x260000000;EID=(2<<16)|17
class Memory:
    def __init__(self):self.bytes={};self.mutate=None
    def put(self,at,fmt,*v):
        for i,b in enumerate(struct.pack(fmt,*v)):self.bytes[at+i]=b
    def __call__(self,at,size):
        try:raw=bytes(self.bytes[at+i] for i in range(size))
        except KeyError:raise OSError('Unmapped test read')
        if self.mutate:self.mutate(self,at)
        return raw
m=Memory();m.put(B+0x39a5290,'<Q',G);m.put(G,'<Q',B+0x1c940a8);m.put(G+0x3130b0,'<Q',P)
m.put(P,'<Q',B+0x1c5e428);m.put(P+0xa8,'<I',16);m.put(P+0x300,'<3f',1,2,3)
m.put(G+0x3130b0+2*8,'<Q',E);m.put(E,'<Q',B+0x1c5e108);m.put(E+0xa8,'<I',EID);m.put(E+0x20,'<Q',NAME)
name=b'models/characters/def/generic/dunwall_guard_male_small_01.def_1892\0';m.put(NAME,f'<{len(name)}s',name)
m.put(E+0x268,'<QII',L,1,1<<8);m.put(L,'<Q',H);m.put(E+0x300,'<3f',4,5,6);m.put(E+0xd1,'<B',128)
m.put(H,'<Q',B+0x1c51588);m.put(H+8,'<Q',D);m.put(H+0x10,'<Q',E);m.put(H+0x18,'<iI',51,10);m.put(H+0x20,'<f',40);m.put(H+0x70,'<Q',0)
m.put(D,'<Q',B+0x1c50000);m.put(D+0x68,'<f',40)
owner=discover_health_actor(m,B,G,EID);before=health_actor_sample(m,B,owner);assert before['health_source']==40 and before['exact_getter_value_verified']
primary=primary_owner(m,B);assert primary_feet(m,B,primary)==[1,2,3]
m.put(H+0x20,'<f',12);assert health_actor_sample(m,B,owner)['health_source']==12
m.put(H+0x20,'<f',0);m.put(E+0xd1,'<B',136);after=health_actor_sample(m,B,owner);assert after['health_source']==0 and after['damage_exclusion_bit8']
m.put(H+0x70,'<Q',0x270000000);assert not health_actor_sample(m,B,owner)['exact_getter_value_verified'];m.put(H+0x70,'<Q',0)
def rejected(f):
    try:f()
    except (ValueError,OSError):return
    raise AssertionError('Recycled/unowned actor accepted')
for at,fmt,value in ((E+0xa8,'<I',EID+1),(H+0x10,'<Q',P),(H+0x18,'<i',52),(H+8,'<Q',D+8),(E,'<Q',B+0x1c5e428),(D,'<Q',B+0x1c50008),(NAME,'<B',ord('x'))):
    bad=copy.deepcopy(m);bad.put(at,fmt,value);rejected(lambda:health_actor_sample(bad,B,owner))
# A health change during the read is explicitly marked inconsistent.
changing=copy.deepcopy(m);changing.mutate=lambda mem,at:mem.put(H+0x20,'<f',2) if at==H+0x20 else None
assert not health_actor_sample(changing,B,owner)['health_fields_consistent']
replaced=copy.deepcopy(m);replaced.put(G+0x3130b0,'<Q',P+8);rejected(lambda:primary_feet(replaced,B,primary))
# Cached samples no longer scan the list.
for i in range(16):m.bytes.pop(E+0x268+i)
assert health_actor_sample(m,B,owner)['health_source']==0
print('PASS: exact owned health sources, damage/death transitions, auxiliary caveat, mixed health and recycled actor rejection')
