"""Offline authoring invariant checks, with no game/process access."""
import json
import sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from author_empty_contact_closure import Closure,locals_of,interpolate_local
from refine_empty_contact_closure import RefinedClosure
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
baseline=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());report=json.loads(Path(baseline['source_readonly_report']).read_text())
for item in baseline['arms']:
    arm=verified_arm(report,item['side']);model=Closure(arm,item['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
    params=np.array([.8]*12+[1.,1.,.9]);pose=model.pose(params);local=locals_of(pose,model.parents)
    for joint in range(69):
        assert np.max(np.abs(pose[joint][:3,:3].T@pose[joint][:3,:3]-np.eye(3)))<1e-5
        assert abs(np.linalg.det(pose[joint][:3,:3])-1)<1e-5
    main=sum(model.main,[])
    for joint in main:assert np.linalg.norm(local[joint][:3,3]-model.native[joint][:3,3])<1e-6
    # Neutral arm/wrist/end transforms remain local-source exact; helpers are allowed to deform skin.
    allowed=set(main+model.finger_helpers+model.thumb)
    for joint in range(69):
        if joint not in allowed:assert np.max(np.abs(local[joint]-model.native[joint]))<1e-5
    released=locals_of(model.pose(params,True),model.parents)
    for joint in model.main[0]:assert np.max(np.abs(released[joint]-model.native[joint]))<1e-5
    refined=RefinedClosure(arm,item['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
    compact=np.array([1.25,.8,1.2]*4+[1.,1.3,1.]+[0.]*4+[1.,1.3]*4)
    bent=locals_of(refined.pose(compact),refined.parents)
    for joint in main:assert np.linalg.norm(bent[joint][:3,3]-refined.native[joint][:3,3])<1e-6
    # MCP abduction uses the source palm-normal axis before flexion, not the already curled axis.
    spaced=compact.copy();spaced[15]=4
    posed=refined.pose(spaced);local_spaced=locals_of(posed,refined.parents)
    chain=refined.main[0];bases=[refined.source[c[0]][:3,3] for c in refined.main]
    normal=np.cross(np.mean(bases,axis=0)-refined.source[refined.wrist][:3,3],bases[0]-bases[-1]);normal/=np.linalg.norm(normal)
    parent=refined.parents[chain[0]];expected=refined.source[parent][:3,:3].T@normal
    actual=Rotation.from_matrix(local_spaced[chain[0]][:3,:3]@bent[chain[0]][:3,:3].T).as_rotvec()
    assert np.linalg.norm(actual-np.deg2rad(4)*expected)<1e-5
    bad=compact.copy();bad[15]=9
    try:refined.pose(bad)
    except ValueError:pass
    else:raise AssertionError('Excess finger spread accepted')
    for bad in (np.zeros(15),[float('nan')]*15,[1]*14,np.array([2.1]+[1.]*14),np.array([1.,1.2]+[1.]*13)):
        try:model.pose(bad)
        except ValueError:pass
        else:raise AssertionError('Invalid parameters accepted')
print('PASS: rigid native FK, preserved main lengths/neutral locals, exact index release and bounded parameters')
