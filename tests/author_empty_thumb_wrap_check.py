"""Private native-skin wrap target and bounded FK invariants, no game access."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fit_empty_thumb_wrap import WrapThumbClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
c=json.loads((root/'authored-empty-qualified-thumb-wrap-v97-20261009/authored-candidate.json').read_text())
b=json.loads(Path(c['baseline']).read_text());r=json.loads(Path(c['source_readonly_report']).read_text())
for item,old in zip(c['arms'],b['arms']):
 m=WrapThumbClosure(verified_arm(r,item['side']),old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));p=np.array(item['parameters']);target,across,evidence=m.target_geometry(p)
 assert all(x['vertices']>=10 for x in evidence['proximal_skin'])
 assert evidence['source_metacarpal_to_target_direction_degrees']>18
 assert np.linalg.norm(np.asarray(evidence['outward']))-1<1e-8
 assert np.dot(target-np.asarray(evidence['surface_target']),np.asarray(evidence['outward']))>0
 assert abs(np.linalg.norm(across)-1)<1e-8
 local=locals_of(m.pose(p),m.parents);released=locals_of(m.pose(p,True),m.parents)
 main=sum(m.main,[])+[j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]
 for j in main:assert np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
 for j in m.thumb:
  if 'helper'not in m.names[j]:assert np.max(np.abs(local[j]-released[j]))<1e-5
 for mat in m.pose(p):assert np.max(np.abs(mat[:3,:3].T@mat[:3,:3]-np.eye(3)))<1e-5
 for j in m.main[0]:assert np.max(np.abs(released[j]-m.native[j]))<1e-5
 for j in set(range(69))-set(main+m.finger_helpers+m.thumb):assert np.max(np.abs(local[j]-m.native[j]))<1e-5
 for bad in (np.full(45,np.nan),np.r_[p[:39],[1.81,0,0,.4,.5,.024]],np.r_[p[:44],[.041]]):
  try:m.pose(bad)
  except ValueError:pass
  else:raise AssertionError('Unbounded opposition accepted')
print('PASS: skin-derived opposed target, expanded opposition search bounds, rigid main translations/SO3, neutral joints, index release, finite bounds')
