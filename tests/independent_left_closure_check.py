"""Independent left native geometry, rigid source lengths and bounded artist search."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from author_independent_left_closure import IndependentLeftClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';r=json.loads((root/'native-sword-block-qualified-v101-20261009/pose-0260.json').read_text());a=verified_arm(r,'left');m=IndependentLeftClosure(a,root/'left-arm.bmd6model',True,True,True);zero,_=m.pose(np.zeros(12));assert max(np.max(np.abs(x-y))for x,y in zip(zero,m.source))<1e-6
pose,local=m.pose(np.tile([.7,.8,.4],4));allowed=set(sum(m.main,[])+m.finger_helpers)
for j in range(69):
 assert np.max(np.abs(pose[j][:3,:3].T@pose[j][:3,:3]-np.eye(3)))<1e-5
 if j in sum(m.main,[]):assert np.max(np.abs(local[j][:3,3]-m.native[j][:3,3]))<1e-8
 if j not in allowed:assert np.max(np.abs(local[j]-m.native[j]))<1e-8
assert np.max(np.abs(pose[m.wrist]-m.source[m.wrist]))<1e-6
for chain in m.main:
 # Native float axes carry a few e-6 of orthogonality error; preserve the source.
 world=[m.source[j][:3,:3]@m.axes[j]for j in chain];assert all(np.linalg.norm(x-world[0])<1e-5 for x in world)
for bad in (np.full(12,np.nan),np.ones(11),np.tile([.7,.8,.71],4),np.full(12,-.1)):
 try:m.pose(bad)
 except ValueError:pass
 else:raise AssertionError('Unbounded authored left curl accepted')
print('PASS: native left identity at zero curl, source-only flex planes, rigid main translations/wrist/SO3, neutral thumb/attachment and finite curl bounds')
