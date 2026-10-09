"""Synthetic exact-layout carry guards and source-run provenance rejection checks."""
import copy
import struct
import sys
import json
import subprocess
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_carry_fields import carry_snapshot,carry_current
from verify_native_hand_source_run import verify
from rig_fields import GAME_HASH


def fixture():
    base=0x140000000;game=0x200000;player=0x300000;body=0x400000;machine=0x500000;pool=0x600000;data=0x700000;memory={}
    def put(at,fmt,*values):
        for i,v in enumerate(struct.pack(fmt,*values)):memory[at+i]=v
    def read(at,n):return bytes(memory[at+i] for i in range(n))
    for at,vt in ((game,0x1c940a8),(player,0x1c5e428),(body,0x1c5a8d8),(machine,0x1c73d58)):put(at,'<Q',base+vt)
    put(base+0x39a5290,'<Q',game);put(game+0x3130b0,'<Q',player);put(player+0x268,'<QII',data,2,2<<8);put(data,'<QQ',body,machine)
    put(body+0x10,'<Q',player);put(body+0x18,'<iI',1,10);put(body+0x20,'<Q',base+0x1c5a9b0)
    put(machine+0x10,'<Q',player);put(machine+0x18,'<iI',52,10);put(machine+0x90,'<Q',pool);put(machine+0x300,'<Q',body)
    for i,(index,vt,declvt) in enumerate(((49,0x1c7a998,0x1c7a868),(45,0x1c77748,0x1c77618))):
        state=0x800000+i*0x1000;decl=0x900000+i*0x1000
        put(pool+0x10+index*8,'<Q',state);put(state,'<Q',base+vt);put(state+8,'<i',index);put(state+0x10,'<QQ',machine,decl);put(state+0x20,'<B',0);put(decl,'<Q',base+declvt);put(decl+0x60,'<i',index)
    return read,put,base,game,player,body


def rejects(fn):
    try:fn()
    except (ValueError,KeyError):return
    raise AssertionError('Accepted invalid evidence')


def main():
    read,put,base,game,player,body=fixture();old=carry_snapshot(read,base,game,player,body);assert not old['carrying'] and old['typed_owner_verified']
    for i in range(2):
        at=old['states'][i]['state'];put(at+0x20,'<B',1);assert carry_current(read,base,old)['carrying'];put(at+0x20,'<B',0)
    for at,fmt,bad in ((old['machine']+0x10,'<Q',player+8),(old['states'][0]['declaration']+0x60,'<i',45),(old['states'][1]['state']+0x20,'<B',2),(old['body']+0x20,'<Q',base)):
        original=read(at,struct.calcsize(fmt));put(at,fmt,bad);rejects(lambda:carry_current(read,base,old))
        for i,v in enumerate(original):put(at+i,'<B',v)
    calls=0;activeat=old['states'][0]['state']+0x20
    def transition(at,n):
        nonlocal calls
        if at==activeat:calls+=1;return bytes([calls>1])
        return read(at,n)
    rejects(lambda:carry_current(transition,base,old))
    capture=dict(pid=1,executable_sha256=GAME_HASH,started_unix=10,ended_unix=12,samples=[dict(pose_report='p.json',state=dict(carry_consistent=True,native_carry=old))])
    config=dict(pid=1,executable_sha256=GAME_HASH,started_unix=9,ended_unix=13,tracked_arms_requested=False,arm_palette_copy_requested=False)
    diag=dict(pid=1,tracked_arms_requested=False,arm_palette_copy_requested=False,native_arm_pose_calls=[0,0],native_arm_copy_calls=[0,0],hook_disabled=True,first_chance_fault_records=0)
    assert verify(capture,config,diag)
    for key,value in (('pid',2),('native_arm_copy_calls',[0,1]),('tracked_arms_requested',True),('hook_disabled',False)):
        bad=dict(diag);bad[key]=value;rejects(lambda:verify(capture,config,bad))
    bad=dict(config,ended_unix=11);rejects(lambda:verify(capture,bad,diag))
    legacy=dict(capture,samples=[dict(pose_report='p.json',state={})]);rejects(lambda:verify(legacy,config,diag))
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);pose=dict(pid=1,executable_sha256=GAME_HASH,captured_unix=11,carry_consistent=True,carry_before=old,carry_after=old)
        for name,value in (('capture.json',capture),('config.json',config),('diag.json',diag),('p.json',pose)):(root/name).write_text(json.dumps(value))
        command=[sys.executable,str(Path(__file__).resolve().parents[1]/'tools/verify_native_hand_source_run.py'),'--capture',str(root/'capture.json'),'--run-config',str(root/'config.json'),'--diagnostics',str(root/'diag.json'),'--output',str(root/'companion.json')]
        completed=subprocess.run(command,capture_output=True,text=True);assert completed.returncode==0,completed.stderr
        companion=json.loads((root/'companion.json').read_text());assert len(companion['poses'])==1 and companion['authored_pose_confirmed'] is False
        assert json.loads((root/'p.json').read_text())==pose  # Companion does not mutate source evidence.
        pose['carry_after']=dict(old,carrying=True);(root/'p.json').write_text(json.dumps(pose));command[-1]=str(root/'bad-companion.json')
        assert subprocess.run(command,capture_output=True,text=True).returncode!=0 and not (root/'bad-companion.json').exists()
    print('PASS: carry ownership, both active types, declaration identity, transition and provenance interval/writer rejection')

if __name__=='__main__':main()
