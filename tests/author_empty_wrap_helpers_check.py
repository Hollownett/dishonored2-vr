"""Private helper arcs preserve native main bones and rotate skin coherently."""
import json,sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from refine_empty_wrap_helpers import WrapHelperClosure
from fit_empty_thumb_wrap import WrapThumbClosure
from fit_empty_wrap_clearance import segment_distances
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';c=json.loads((root/'authored-empty-qualified-wrap-helpers-v98-20261009/authored-candidate.json').read_text());b=json.loads(Path(c['baseline']).read_text());r=json.loads(Path(c['source_readonly_report']).read_text())
for item,old in zip(c['arms'],b['arms']):
 arm=verified_arm(r,item['side']);m=WrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));p=np.array(item['parameters']);pose=m.pose(p);local=locals_of(pose,m.parents);base=locals_of(WrapThumbClosure.pose(m,p[:45]),m.parents)
 for j in sum(m.main,[])+[j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.max(np.abs(local[j]-base[j]))<1e-6
 for mat in pose:assert np.max(np.abs(mat[:3,:3].T@mat[:3,:3]-np.eye(3)))<1e-5
 q=p.copy();q[45:]=[1,1,1];assert np.max(np.abs(m.target(q)-m.target(p)))<1e-10
 assert np.max(np.abs(pose[m.wrist]-m.source[m.wrist]))<1e-6
 for j in m.thumb:
  if '_top_'in m.names[j]or'_bot_'in m.names[j]:
   child=m.names.index(item['side']+('_thumb_03_jnt'if'_2_'in m.names[j]else'_thumb_02_jnt'));pivot=m.native[child][:3,3]
   assert abs(np.linalg.norm(local[j][:3,3]-pivot)-np.linalg.norm(m.native[j][:3,3]-pivot))<1e-6
 for bad in (np.full(48,np.nan),np.r_[p[:45],[1.01,.5,.5]],p[:47]):
  try:m.pose(bad)
  except ValueError:pass
  else:raise AssertionError('Unbounded helper response accepted')
points=np.array([[.5,2,0],[2,0,0]]);start=np.array([[0,0,0]]);end=np.array([[1,0,0]]);assert np.allclose(segment_distances(points,start,end).ravel(),[2,1])
rot=Rotation.from_rotvec([.2,.5,-.1]).as_matrix();shift=np.array([4,5,6]);assert np.allclose(segment_distances(points@rot.T+shift,start@rot.T+shift,end@rot.T+shift),segment_distances(points,start,end))
print('PASS: coherent native helper arcs, main/wrist invariants, independent wrap target, finite bounds, segment envelope rigid-frame invariance')
