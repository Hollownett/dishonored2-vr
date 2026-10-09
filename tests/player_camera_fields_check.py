"""Synthetic ownership/field checks; no real camera writer is exercised."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from player_camera_fields import player_camera_snapshot

base=0x7ff700000000
player,equipment,camera,body,controller=0x100000,0x200000,0x300000,0x400000,0x500000
physics,delegate=0x600000,0x700000
memory={}
def put(at,data):memory.update({at+i:b for i,b in enumerate(data)})
def q(at,value):put(at,struct.pack('<Q',value))
def read(at,size):
    try:return bytes(memory[at+i] for i in range(size))
    except KeyError as e:raise OSError('Unreadable fixture') from e
for at,table in [(equipment,0x1b9ae68),(camera,0x1c72218),(camera+0x20,0x1c722f8),(body,0x1c5a8d8),(controller,0x1c72918)]:q(at,base+table)
for at in [equipment,camera,body,controller]:q(at+0x10,player)
q(equipment+0x48,camera);put(camera+0x18,struct.pack('<iI',17,0x0a))
put(camera+0x25150,bytes(0x200));q(camera+0x25158,controller);q(camera+0x25170,body)
put(camera+0x25198,struct.pack('<6h',8,6,5,2,3,4))
put(camera+0x25264,struct.pack('<6f',.1,.2,.3,.4,.5,.6))
put(camera+0x2527c,struct.pack('<3f',10,20,30));put(camera+0x25288,struct.pack('<9f',1,0,0,0,1,0,0,0,1))
put(controller+0xd0,struct.pack('<2f8B',.4,-.8,0,0,0,0,1,0,0,0))
for offset,table in ((0,0x1c73268),(0x20,0x1c73340),(0x30,0x1c73378)):q(physics+offset,base+table)
q(physics+0x10,player);put(physics+0x18,struct.pack('<iI',18,0x0a));q(physics+0x48,delegate)
q(camera+0x25160,physics);put(delegate+0xd0,struct.pack('<f',1.5))
q(delegate+0x160,base+0x1b02768);q(delegate+0x168,physics);q(delegate+0x120,player+0x300);q(delegate+0x1e0,delegate)
put(delegate+0xbc,struct.pack('<f',0));put(delegate+0x40,struct.pack('<f',0));put(player+0x30c,struct.pack('<9f',1,0,0,0,1,0,0,0,1))
sample=lambda:player_camera_snapshot(read,base,player,equipment)
state=sample()
assert player_camera_snapshot(read,base,player,camera=camera)==state
assert state['control_joint_indices']==[8,6,5,2,3,4]
assert abs(state['values']['yaw']-.5)<1e-6 and state['aim_origin']==[10,20,30]
assert state['read_consistent'] and state['angular_units_verified'] and not state['gameplay_heading_writer_verified']
assert state['physics_heading_radians']==1.5 and state['heading_delegate_type_verified']
def reject():
    try:sample()
    except (ValueError,OSError):return
    raise AssertionError('Replaced camera identity accepted')
q(camera+0x20,base+1);reject();q(camera+0x20,base+0x1c722f8)
q(camera+0x10,player+8);reject();q(camera+0x10,player)
put(camera+0x18,struct.pack('<iI',-1,0x0a));reject();put(camera+0x18,struct.pack('<iI',17,0x0a))
q(controller+0x10,player+8);reject();q(controller+0x10,player)
put(camera+0x25274,struct.pack('<f',float('nan')));reject();put(camera+0x25274,struct.pack('<f',.5))
put(delegate+0xd0,struct.pack('<f',float('nan')));reject();put(delegate+0xd0,struct.pack('<f',1.5))
q(physics+0x10,player+8);reject();q(physics+0x10,player)
def changing(at,size):
    value=read(at,size)
    if at==controller+0xd0:q(equipment+0x48,camera+8)
    return value
try:player_camera_snapshot(changing,base,player,equipment)
except ValueError:pass
else:raise AssertionError('Recycled camera was accepted')
print('PASS: owned camera links, dual RTTI, native fields, generation/owner/NaN/replacement rejection; no engine calls or writes')
