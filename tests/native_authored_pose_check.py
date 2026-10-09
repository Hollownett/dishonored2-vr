"""Opt-in private rig checks for the offline artist/solver trial; no game access."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from author_native_hand_candidate import authored,matrices,skin
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions
from analyse_arm_mesh import decode_mesh


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path,required=True);parser.add_argument('--candidate',type=Path);args=parser.parse_args()
    report=json.loads(args.source.read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    candidate_document=json.loads(args.candidate.read_text())if args.candidate else None
    for side in ('left','right'):
        arm=verified_arm(report,side)
        if candidate_document:
            item=next(a for a in candidate_document['arms']if a['side']==side);config=item['evidence']
            parameters=config['thumb_opposition_rotation_vector_radians']+config['thumb_flex_radians']
            options=dict(driven_helpers=config['helper_drivers_enabled'],relaxed_knuckles=config['relaxed_knuckles'],
                thumb_clearance=config['thumb_clearance_native_units'],mesh_thumb_fit=config['mesh_thumb_fit'],
                thumb_parameters=parameters,fan_fingers=config.get('fan_fingers',False),finger_splay_degrees=config.get('offline_finger_splay_degrees'))
            full,e=authored(arm,**options);released,re=authored(arm,0.,**options)
            assert np.max(np.abs(np.asarray(full)-np.asarray(item['authored_transform_matrices'])))<1e-10
        else:
            full,e=authored(arm,driven_helpers=True,relaxed_knuckles=True);released,re=authored(arm,0.,True,True)
        source=matrices(arm);parents=arm['skeleton_layout']['parent_indices'];names=arm['joint_names']
        for invalid in ({'ring':19},{'ring':float('nan')},{'missing':2}):
            try:authored(arm,finger_splay_degrees=invalid)
            except ValueError:pass
            else:raise AssertionError('Invalid offline finger spread accepted')
        assert e['maximum_main_local_offset_change_native_units']==0
        assert re['maximum_main_local_offset_change_native_units']==0
        assert e['maximum_local_offset_change_native_units']<.025
        assert re['maximum_local_offset_change_native_units']<.025
        for joint,parent in enumerate(parents):
            before=source[joint]if parent<0 else np.linalg.inv(source[parent])@source[joint]
            closed=full[joint]if parent<0 else np.linalg.inv(full[parent])@full[joint]
            opened=released[joint]if parent<0 else np.linalg.inv(released[parent])@released[joint]
            assert abs(np.linalg.det(closed[:3,:3])-1)<2e-5
            assert np.max(np.abs(closed[:3,:3].T@closed[:3,:3]-np.eye(3)))<2e-5
            if 'helper'not in names[joint]:assert np.linalg.norm(before[:3,3]-closed[:3,3])<1e-10
            if 'index'not in names[joint]:assert np.max(np.abs(closed-opened))<1e-7
        original,_=palettes(arm);candidate=skin(arm,full);wrist=names.index(side+'_wrist_jnt')
        descendants=[]
        for joint in range(69):
            at=joint
            for _ in range(69):
                if at==wrist:descendants.append(joint);break
                at=parents[at]
                if at<0:break
        for surface in decode_mesh((root/(side+'-arm.bmd6model')).read_bytes())['surfaces']:
            v=surface['vertices'];weights=np.array([list(x[1])for x in v]);joints=np.array([list(x[2])for x in v])
            unchanged=(weights*np.isin(joints,descendants)).sum(axis=1)==0
            if np.any(unchanged):assert np.max(np.abs(skin_positions(v,original)[unchanged]-skin_positions(v,candidate)[unchanged]))<1e-10
    print('PASS: private authored FK rigid rotations, main segment lengths, bounded helpers, independent index/thumb stability and zero-hand-weight surface preservation; visual/collision acceptance pending')


if __name__=='__main__':main()
