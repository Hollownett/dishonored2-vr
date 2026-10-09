"""Synthetic guarded-memory tests; never opens a game process."""
import copy
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from lean_trial_fields import discover_lean_trial_owner,verify_lean_trial_owner,lean_trial_state
B=0x140000000;G=0x200000000;P=0x210000000;M=0x220000000;C=0x230000000;T=0x240000000;BODY=0x250000000;L=0x260000000;POOL=0x270000000;S=0x280000000;D=0x290000000;DEVICE=0x300000000
class Memory:
    def __init__(self):self.bytes={};self.mutate=None
    def put(self,at,fmt,*values):
        for i,v in enumerate(struct.pack(fmt,*values)):self.bytes[at+i]=v
    def __call__(self,at,size):
        try:raw=bytes(self.bytes[at+i] for i in range(size))
        except KeyError:raise OSError('Unmapped test memory')
        if self.mutate:self.mutate(self,at)
        return raw
m=Memory();m.put(B+0x39a5290,'<Q',G);m.put(G,'<Q',B+0x1c940a8);m.put(G+0x3130b0,'<Q',P)
m.put(P,'<Q',B+0x1c5e428);m.put(P+0xa8,'<I',16);m.put(P+0x268,'<QII',L,2,2<<8);m.put(L,'<QQ',M,C)
for at,vt,gen in ((M,0x1c73d58,10),(C,0x1c72218,11),(T,0x1c72918,12),(BODY,0x1c5a8d8,13)):
    m.put(at,'<Q',B+vt);m.put(at+0x10,'<Q',P);m.put(at+0x18,'<iI',gen,10)
m.put(C+0x20,'<Q',B+0x1c722f8);m.put(BODY+0x20,'<Q',B+0x1c5a9b0)
m.put(M+0x2f8,'<Q',T);m.put(M+0x300,'<Q',BODY);m.put(C+0x25158,'<Q',T);m.put(C+0x25170,'<Q',BODY)
m.put(M+0x90,'<Q',POOL);m.put(POOL+0x10+18*8,'<Q',S);m.put(S,'<Q',B+0x1c82b28);m.put(S+8,'<i',18);m.put(S+0x10,'<Q',M);m.put(S+0x18,'<Q',D);m.put(S+0x20,'<B',1);m.put(S+0x30,'<I',11)
m.put(D,'<Q',B+0x1c829f8);m.put(D+0x60,'<i',18);m.put(B+0x2274340,'<Q',DEVICE);m.put(DEVICE+0xa544,'<B',1);m.put(B+0x3995628,'<i',1);m.put(B+0x3995630,'<Q',B+0x1c82bf8)
owner=discover_lean_trial_owner(m,B);assert lean_trial_state(m,B,owner)['active']
# Steady-state verification never requires the component registry.
cached=copy.deepcopy(m)
for i in range(16):cached.bytes.pop(P+0x268+i)
assert lean_trial_state(cached,B,owner)['active']
def rejected(f):
    try:f()
    except (ValueError,OSError):return
    raise AssertionError('Mixed owner/state was accepted')
for at,fmt,value in ((P+0xa8,'<I',17),(G+0x3130b0,'<Q',P+8),(M+0x18,'<i',99),(C+0x10,'<Q',P+8),(C+0x25170,'<Q',BODY+8),(S+0x10,'<Q',M+8),(S+0x20,'<B',2),(D+0x60,'<i',17)):
    bad=copy.deepcopy(m);bad.put(at,fmt,value);rejected(lambda:lean_trial_state(bad,B,owner))
changed=copy.deepcopy(m)
changed.mutate=lambda mem,at:mem.put(S+0x20,'<B',0) if at==S+0x20 else None
rejected(lambda:lean_trial_state(changed,B,owner))
recycled=copy.deepcopy(m);recycled.put(C+0x18,'<iI',42,10)
rejected(lambda:verify_lean_trial_owner(recycled,B,owner))
new=discover_lean_trial_owner(recycled,B);assert new['identities']['camera']!=owner['identities']['camera']
assert lean_trial_state(recycled,B,new)['active']
print('PASS: cached fixed-field polling, primary/component ownership, torn state rejection and camera generation reacquisition')
