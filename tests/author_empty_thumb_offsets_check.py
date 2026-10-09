"""Offline bounded thumb offsets preserve finger curl, neutral rig and native local lengths."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from refine_empty_thumb_offsets import OffsetThumbClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';b=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());r=json.loads(Path(b['source_readonly_report']).read_text())
for old in b['arms']:
 m=OffsetThumbClosure(verified_arm(r,old['side']),old['authored_transform_matrices'],root/(old['side']+'-arm.bmd6model'));p=np.array([1.25,.8,1.2]*4+[1.,1.3,1.]+[0.]*4+[1.,1.3]*4+[0.]*12+[0.]*6);original=locals_of(m.pose(p),m.parents);p[39:]=[12.,-6.,3.,6.,-12.,9.];pose=m.pose(p);local=locals_of(pose,m.parents);thumb=[m.names.index(m.arm['side']+'_thumb_0'+str(i)+'_jnt')for i in (1,2,3)]
 for j in range(69):
  assert np.max(np.abs(pose[j][:3,:3].T@pose[j][:3,:3]-np.eye(3)))<1e-5
  assert np.linalg.norm(local[j][:3,3]-original[j][:3,3])<1e-6
  if j not in thumb:assert np.max(np.abs(local[j]-original[j]))<1e-5
 released=locals_of(m.pose(p,True),m.parents)
 for j in m.main[0]:assert np.max(np.abs(released[j]-m.native[j]))<1e-5
 for j in thumb:assert np.max(np.abs(released[j]-local[j]))<1e-5
 for bad in (np.ones(44),np.full(45,float('nan')),np.r_[p[:39],[19.]*6]):
  try:m.pose(bad)
  except ValueError:pass
  else:raise AssertionError('Unbounded thumb offset accepted')
print('PASS: exact unchanged finger curl/neutral/offsets, SO3 and native lengths; index release, finite thumb bounds')
