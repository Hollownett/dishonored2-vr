"""Exact PIP-DIP target selection and main/native helper invariants."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from refine_empty_wrap_helpers import MiddlePhalanxWrapHelperClosure,WrapHelperClosure,NativePlaneWrapHelperClosure
from refine_empty_web_closure import WebClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';c=json.loads((root/'authored-empty-qualified-wrap-helpers-v98-20261009/authored-candidate.json').read_text());b=json.loads(Path(c['baseline']).read_text());r=json.loads(Path(c['source_readonly_report']).read_text())
for item,old in zip(c['arms'],b['arms']):
 arm=verified_arm(r,item['side']);asset=root/(item['side']+'-arm.bmd6model');m=MiddlePhalanxWrapHelperClosure(arm,old['authored_transform_matrices'],asset);prox=WrapHelperClosure(arm,old['authored_transform_matrices'],asset);p=np.array(item['parameters']);target,across,evidence=m.target_geometry(p);posed_fingers=WebClosure.pose(m,p[:39])
 assert np.linalg.norm(target-prox.target(p))>.01
 for cohort,chain in zip(evidence['phalanx_skin'],m.main[:2]):
  assert cohort['segment']=='PIP-DIP'and cohort['vertices']>=10
  a,bone_end=posed_fingers[chain[1]][:3,3],posed_fingers[chain[2]][:3,3]
  assert abs(cohort['bone_length']-np.linalg.norm(bone_end-a))<1e-8
  assert np.allclose(cohort['bone_axis'],(bone_end-a)/np.linalg.norm(bone_end-a))
  assert abs(np.dot(cohort['outward'],cohort['bone_axis']))<1e-8
 pose=m.pose(p);local=locals_of(pose,m.parents);released=locals_of(m.pose(p,True),m.parents)
 for j in sum(m.main,[])+[j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
 for j in [j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.max(np.abs(local[j]-released[j]))<1e-5
 assert np.max(np.abs(pose[m.wrist]-m.source[m.wrist]))<1e-6
 native=NativePlaneWrapHelperClosure(arm,old['authored_transform_matrices'],asset)
 for mat in native.pose(p):assert np.max(np.abs(mat[:3,:3].T@mat[:3,:3]-np.eye(3)))<1e-5
print('PASS: exact middle phalanx skin/axes, independent proximal comparison, rigid main lengths and wrist, thumb stable on index release, native bend-plane SO3')
