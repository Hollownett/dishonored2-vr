"""Mesh silhouette proxies are invariant under rigid owner-frame changes."""
import copy,json,sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from measure_hand_closure_shape import measure
from author_native_hand_candidate import matrices
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';candidate=json.loads((root/'authored-empty-qualified-full-thumb-v96-20261009/authored-candidate.json').read_text());report=json.loads(Path(candidate['source_readonly_report']).read_text());transform=np.eye(4);transform[:3,:3]=Rotation.from_rotvec([.2,-.3,.4]).as_matrix();transform[:3,3]=[4.,-2.,3.]
for item in candidate['arms']:
 arm=verified_arm(report,item['side']);pose=[np.asarray(m)for m in item['authored_transform_matrices']];asset=root/(item['side']+'-arm.bmd6model');original=measure(arm,pose,asset);moved=copy.deepcopy(arm)
 for node,mat in zip(moved['sampled_joints'],matrices(arm)):
  changed=transform@mat;node['rotation_columns']=changed[:3,:3].T.tolist();node['position']=changed[:3,3].tolist()
 transformed=measure(moved,[transform@mat for mat in pose],asset)
 assert original['palm_vertices']==transformed['palm_vertices']
 for digit in original['pad_to_palm_proxies']:
  a,b=original['pad_to_palm_proxies'][digit],transformed['pad_to_palm_proxies'][digit];assert a['pad_vertices']==b['pad_vertices']
  for key in ('minimum_pad_to_palm_vertex_distance_native_model_units','median_pad_to_palm_vertex_distance_native_model_units'):assert abs(a[key]-b[key])<1e-6
 assert abs(original['thumb_pad_to_index_vertex_distance_native_model_units']-transformed['thumb_pad_to_index_vertex_distance_native_model_units'])<1e-6
 assert original['contact_certified']is False and original['silhouette_acceptance']is False
print('PASS: native mesh cohorts/distances invariant under owner rigid transform; no proxy contact certification')
