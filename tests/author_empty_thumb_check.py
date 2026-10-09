"""Offline fitted-thumb FK/identity guards; no source certification or native calls."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fit_empty_thumb import ThumbClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';b=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());r=json.loads(Path(b['source_readonly_report']).read_text())
for old in b['arms']:
 m=ThumbClosure(verified_arm(r,old['side']),old['authored_transform_matrices'],root/(old['side']+'-arm.bmd6model'));p=np.array([1.25,.8,1.2]*4+[1.,1.3,1.]+[0.]*4+[1.,1.3]*4+[0.]*12+[.2,-.1,.1,.4,.5,.024]);pose=m.pose(p);local=locals_of(pose,m.parents);released=m.pose(p,True);released_local=locals_of(released,m.parents)
 for j in sum(m.main,[])+[j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
 for mat in pose:assert np.max(np.abs(mat[:3,:3].T@mat[:3,:3]-np.eye(3)))<1e-5
 # Index release must not change the fitted opposition target or thumb main locals.
 for j in [j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.max(np.abs(local[j]-released_local[j]))<1e-5
 for j in m.main[0]:assert np.max(np.abs(released_local[j]-m.native[j]))<1e-5
 neutral=set(range(69))-set(sum(m.main,[])+m.finger_helpers+m.thumb)
 for j in neutral:assert np.max(np.abs(local[j]-m.native[j]))<1e-5
 for bad in (np.ones(44),np.full(45,float('nan')),np.r_[p[:39],[1.3,0.,0.,.4,.5,.024]],np.r_[p[:44],[.011]]):
  try:m.pose(bad)
  except ValueError:pass
  else:raise AssertionError('Unbounded thumb fit accepted')
print('PASS: native main lengths/SO3/neutral joints, thumb target independent of index release, finite bounds')
