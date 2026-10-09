"""Native-derived main-finger preset stays fixed under independent authored thumb fit."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fit_native_preset_empty_thumb import NativePresetThumb
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';c=json.loads((root/'authored-native-rigid-mirror-v101-20261009/authored-retarget.json').read_text());r=json.loads(Path(c['empty_source']).read_text())
for item in c['arms']:
 arm=verified_arm(r,item['side']);m=NativePresetThumb(arm,item['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));p=np.r_[np.zeros(39),[.2,-.1,.1,.7,.3,.03,.7,.7,.3]];pose=m.pose(p);local=locals_of(pose,m.parents);released=locals_of(m.pose(p,True),m.parents)
 for j in sum(m.main,[]):assert np.max(np.abs(local[j]-m.preset[j]))<1e-5 and np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
 for j,n in enumerate(m.names):
  if'index'in n:assert np.max(np.abs(released[j]-m.native[j]))<1e-5
  if'thumb_0'in n and'helper'not in n:assert np.max(np.abs(local[j]-released[j]))<1e-5
 for mat in pose:assert np.max(np.abs(mat[:3,:3].T@mat[:3,:3]-np.eye(3)))<1e-5
 assert np.max(np.abs(pose[m.wrist]-m.source[m.wrist]))<1e-5
 bad=p.copy();bad[43]=.66
 try:m.pose(bad)
 except ValueError:pass
 else:raise AssertionError('Unrestrained preset thumb curl accepted')
print('PASS: native-derived main fingers/lengths fixed, neutral index release, independent thumb target, wrist/SO3 and restrained IP')
