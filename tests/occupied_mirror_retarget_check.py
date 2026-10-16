"""Historical OFFLINE mirror geometry, not newly qualified carry evidence."""
import json,sys,copy,hashlib
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from read_occupied_source_evidence import occupied_source_evidence
from retarget_native_occupied_fingers import mirrored_locals
from read_empty_source_evidence import source_evidence,run_evidence,validate_inactive_carry
from export_native_hand_poses import verified_arm
from author_native_hand_candidate import matrices,fk
from author_empty_contact_closure import locals_of
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';ep=root/'native-empty-carryqualified-v96-20261009/pose-0172.json';sp=root/'native-sword-block-qualified-v101-20261009/pose-0260.json'
original={path:hashlib.sha256(path.read_bytes()).hexdigest()for path in (ep,sp,ep.parent/'run-provenance.json',sp.parent/'run-provenance.json')}
assert original[ep]=='15af42a502788b42cffe7a052d2ddffff34586d47cafb6318b2fefa0aac02927'and original[sp]=='5df8ed47326d48bfa98fdf7136e8661a08f3bf82d0ae3f135f9b744eec8b0a86'
e=json.loads(ep.read_text());s=json.loads(sp.read_text())
for path in (ep,sp):
 q=run_evidence(path,path.parent/'run-provenance.json');assert q['tracked_arm_writes_absent_by_archived_run']
 assert [state['index']for state in json.loads(path.read_text())['carry_before']['states']]==[49,45]
try:occupied_source_evidence(sp,sp.parent/'run-provenance.json')
except ValueError:pass
else:raise AssertionError('Legacy unobserved state50 accepted as qualified occupied source')
assert s['authored_pose_confirmed']is False and s['tracked_arm_writes_absent_verified']is False
carry=dict(typed_owner_verified=True,carrying=False,active=[False,False,False],player=0x20000,body=0x30000,machine=0x40000,pool=0x50000,body_identity='synthetic',states=[dict(index=index,state=0x60000+offset*0x1000,declaration=0x70000+offset*0x1000)for offset,index in enumerate((49,45,50))])
synthetic=dict(synthetic_fixture=True,sustained_owned_state_bits=3,carry_consistent=True,carry_before=copy.deepcopy(carry),carry_after=copy.deepcopy(carry),native_hand_state=dict(player=0x20000,body=0x30000,body_identity='synthetic'))
assert validate_inactive_carry(synthetic)
synthetic['carry_before']['active'][2]=True;synthetic['carry_after']=copy.deepcopy(synthetic['carry_before'])
try:validate_inactive_carry(synthetic)
except ValueError:pass
else:raise AssertionError('Active synthetic HoldObject50 accepted')
try:source_evidence(sp,sp.parent/'run-provenance.json')
except ValueError:pass
else:raise AssertionError('Occupied Sword accepted as Empty')
l=verified_arm(e,'left');r=verified_arm(e,'right');o=verified_arm(s,'right');local,proof=mirrored_locals(l,r,o,True);native=locals_of(matrices(l),l['skeleton_layout']['parent_indices']);pose=fk(local,l['skeleton_layout']['parent_indices'])
assert proof['determinant']<-.999 and proof['maximum_bind_point_error_native_model_units']<1e-4 and proof['maximum_authored_main_point_mirror_error_native_model_units']<1e-4
for j,n in enumerate(l['joint_names']):
 if any(d in n for d in ('thumb','index','medium','ring','pinky'))and'helper'not in n:assert np.max(np.abs(local[j][:3,3]-native[j][:3,3]))<1e-8
 assert np.max(np.abs(pose[j][:3,:3].T@pose[j][:3,:3]-np.eye(3)))<1e-5
for n in ('left_wrist_jnt','left_hand_attachment_jnt'):
 j=l['joint_names'].index(n);assert np.max(np.abs(local[j]-native[j]))<1e-8
bad=copy.deepcopy(o);bad['joint_names'][6]+='_bad'
try:mirrored_locals(l,r,bad)
except ValueError:pass
else:raise AssertionError('Mixed occupied rig accepted')
bad=copy.deepcopy(l);bad['skinning_layout']['inverse_bind_rows'][16][3]+=.01
try:mirrored_locals(bad,r,o)
except ValueError:pass
else:raise AssertionError('Unverified bind reflection accepted')
assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in original.items())
print('PASS: OFFLINE historical mirror geometry only; legacy qualification rejected, synthetic three-state guards, unchanged archive hashes')
