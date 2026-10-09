"""Occupied native axes are evidence only, no endpoint/Empty provenance relabeling."""
import json,sys,copy
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fit_native_axis_empty_thumb import NativeAxisMiddleWrap,LowerNativeAxisMiddleWrap
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import matrices
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';c=json.loads((root/'authored-empty-qualified-middle-contact-refined-v99-20261009/authored-candidate.json').read_text());b=json.loads(Path(c['baseline']).read_text());r=json.loads(Path(c['source_readonly_report']).read_text());sword=json.loads((root.parents[1]/'native-sword-block-source-v89/pose-0036.json').read_text());a=verified_arm(r,'right');o=verified_arm(sword,'right');old=next(x for x in b['arms']if x['side']=='right');item=next(x for x in c['arms']if x['side']=='right');p=np.array(item['parameters']);p[42:44]=[.7,.3]
m=NativeAxisMiddleWrap(a,old['authored_transform_matrices'],root/'right-arm.bmd6model',o);other=locals_of(matrices(o),m.parents)
for j,axis in m.axes.items():
 v=Rotation.from_matrix(m.native[j][:3,:3].T@other[j][:3,:3]).as_rotvec();assert np.allclose(axis,v/np.linalg.norm(v));assert np.linalg.norm(axis-[-1,0,0])<.002
pose=m.pose(p);local=locals_of(pose,m.parents)
for j in sum(m.main,[])+[j for j,n in enumerate(m.names)if'thumb_0'in n and'helper'not in n]:assert np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
assert np.max(np.abs(pose[m.wrist]-m.source[m.wrist]))<1e-6
lower=LowerNativeAxisMiddleWrap(a,old['authored_transform_matrices'],root/'right-arm.bmd6model',o);assert np.linalg.norm(lower.target(p)-m.target(p))>.001
bad=p.copy();bad[43]=.66
try:m.pose(bad)
except ValueError:pass
else:raise AssertionError('Unrestrained distal curl accepted')
wrong=copy.deepcopy(o);wrong['joint_names'][1]+='_wrong'
try:NativeAxisMiddleWrap(a,old['authored_transform_matrices'],root/'right-arm.bmd6model',wrong)
except ValueError:pass
else:raise AssertionError('Mixed native axis rig accepted')
print('PASS: exact native occupied flex-axis derivation, source main lengths/wrist, distinct skin-band target, restrained IP and exact rig rejection')
