"""Qualified occupied provenance and exact native mirror invariants, no Empty relabel."""
import json,sys,copy
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from read_occupied_source_evidence import occupied_source_evidence
from retarget_native_occupied_fingers import mirrored_locals
from read_empty_source_evidence import source_evidence
from export_native_hand_poses import verified_arm
from author_native_hand_candidate import matrices,fk
from author_empty_contact_closure import locals_of
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';ep=root/'native-empty-carryqualified-v96-20261009/pose-0172.json';sp=root/'native-sword-block-qualified-v101-20261009/pose-0260.json';e=json.loads(ep.read_text());s=json.loads(sp.read_text());q=occupied_source_evidence(sp,sp.parent/'run-provenance.json');assert q['per_pose_occupied_sword_and_inactive_carry_verified']and not q['occupied_endpoint_is_Empty_fist'];assert s['authored_pose_confirmed']is False and s['tracked_arm_writes_absent_verified']is False
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
print('PASS: qualified occupied-only source, Empty rejection, bind reflection/main point fidelity, native lengths/wrist/SO3, exact rig mismatch guards')
