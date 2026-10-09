"""Read-only capture hysteresis must never label unknown/occupied hands empty."""
import copy
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from capture_native_hand_source import EmptyWindow,OwnedStateWindow

def state():
    return dict(player=1,body=2,body_identity='1',inventory=3,inventory_identity='1',carry_consistent=True,native_carry=dict(typed_owner_verified=True,carrying=False,active=[False,False]),hands=[
        dict(animation=i,animation_identity='1',model=i+10,graph_decl=4,equip_name_list=i+20,empty_candidate=True) for i in (5,6)])

def main():
    gate=EmptyWindow();sample=state()
    for i in range(20):assert gate.observe(sample,i*.05,True)==0
    assert gate.observe(sample,1.,True)==3
    armed=copy.deepcopy(sample);armed['hands'][0]['empty_candidate']=False
    assert gate.observe(armed,1.,True)==2
    recycled=copy.deepcopy(sample);recycled['hands'][1]['animation_identity']='2'
    assert gate.observe(recycled,1.05,True)==0
    assert gate.observe(sample,2.,True)==0
    assert gate.observe(sample,2.05,False)==0
    assert gate.observe(None,2.1,True)==0
    for i in range(21):value=gate.observe(sample,3.+i*.05,True)
    assert value==3
    carry=copy.deepcopy(sample);carry['native_carry'].update(carrying=True,active=[True,False])
    assert gate.observe(carry,4.05,True)==0
    assert gate.observe(sample,4.1,True)==0  # A release starts a new stable interval.
    unknown=copy.deepcopy(sample);unknown.pop('native_carry')
    assert gate.observe(unknown,4.15,True)==0
    assert gate.observe(sample,4.3,True)==0
    diagnostic=OwnedStateWindow();weapon=state();weapon['hands'][0].update(empty_candidate=False,slot=100,equip_index=1)
    empty_gate=EmptyWindow()
    for i in range(21):
        stable=diagnostic.observe(weapon,i*.05,True);empty=empty_gate.observe(weapon,i*.05,True)
    assert stable==3 and empty==2  # Diagnostic weapon stability never enables left empty bit.
    transition=copy.deepcopy(weapon);transition['hands'][0]['slot']=101
    assert diagnostic.observe(transition,1.05,True)==0
    print('native hand capture hysteresis checks passed')

if __name__=='__main__':main()
