"""Offline multi-axis helper and anatomical render invariants."""
import json,sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from refine_empty_web_closure import WebClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
baseline=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());report=json.loads(Path(baseline['source_readonly_report']).read_text())
for item in baseline['arms']:
    arm=verified_arm(report,item['side']);model=WebClosure(arm,item['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
    p=np.array([1.25,.8,1.2]*4+[1.,1.3,1.]+[0.]*4+[1.,1.3]*4+[0.]*12)
    original=locals_of(model.pose(p),model.parents);p[27:]=np.tile([3.,-6.,9.],4);pose=model.pose(p);local=locals_of(pose,model.parents)
    for joint in range(69):
        assert np.linalg.norm(local[joint][:3,3]-original[joint][:3,3])<1e-6
        assert np.max(np.abs(pose[joint][:3,:3].T@pose[joint][:3,:3]-np.eye(3)))<1e-5
    released=locals_of(model.pose(p,True),model.parents)
    for joint in model.main[0]:assert np.max(np.abs(released[joint]-model.native[joint]))<1e-5
    for j in model.finger_helpers:
        if 'helper_index_'in model.names[j]:assert np.max(np.abs(released[j]-model.native[j]))<1e-5
    neutral=set(range(69))-set(sum(model.main,[])+model.finger_helpers+model.thumb)
    for j in neutral:assert np.max(np.abs(local[j]-model.native[j]))<1e-5
    # Exact source-normal helper axis: only its assigned top response changes.
    p[27:]=0.;p[28]=6.;test=locals_of(model.pose(p),model.parents)
    bases=np.array([model.source[c[0]][:3,3]for c in model.main]);forward=bases.mean(axis=0)-model.source[model.wrist][:3,3]
    normal=np.cross(forward,bases[0]-bases[-1]);normal/=np.linalg.norm(normal)
    joint=next(j for j in model.finger_helpers if 'helper_index_'in model.names[j]and'_top_'in model.names[j])
    expected=model.source[model.parents[joint]][:3,:3].T@normal*np.deg2rad(6)
    actual=Rotation.from_matrix(test[joint][:3,:3]@original[joint][:3,:3].T).as_rotvec()
    assert np.linalg.norm(actual-expected)<1e-5
    for invalid in [np.ones(38),np.full(39,float('nan')),np.r_[p[:27],[16.]*12]]:
        try:model.pose(invalid)
        except ValueError:pass
        else:raise AssertionError('Unbounded helper correction accepted')
print('PASS: exact multi-axis helper frame, unchanged native translations/neutral joints, released index and bounds')

