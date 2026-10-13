"""Test shared rigid/affine skin transforms; no vertex correction or runtime use."""
import json
import hashlib
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from experiment_garment_seam_band import load, distortion, stats, RAW, ROOT
from build_player_asset_pack import parse_skeleton
from render_body_rig import skin_positions

OUT=ROOT/'artifacts/research/garment-continuity'
INPUT=OUT/'driver-palettes-20261011.json'

def animated(values,n):
    return np.asarray(values).reshape(n,4,4).transpose(0,2,1)

def skin(mesh,matrix,skeleton):
    return skin_positions(mesh['vertices'],(matrix@skeleton['inverse'])[:,:3,:])

def main():
    body_skeleton=parse_skeleton(RAW/'emily/player_body_emily.md6skl')
    arms_skeleton=[parse_skeleton(RAW/f'emily/player_{side}arm_emily.md6skl') for side in ('left','right')]
    evidence=dict(input_sha256=hashlib.sha256(INPUT.read_bytes()).hexdigest(),
                  hierarchy=dict(body=list(zip(body_skeleton['names'],body_skeleton['parents'])),
                                 arms=[list(zip(s['names'],s['parents'])) for s in arms_skeleton]),
                  native_buffer_writes=False,headset_verified=False,cases=[])
    for character in ('emily','corvo'):
        body=load(character,'body',20);tree=cKDTree(body['points'])
        arms=[load(character,f'{side}arm',69) for side in ('left','right')]
        correspondences=[]
        for arm in arms:
            bind_world=(np.c_[arm['points'],np.ones(len(arm['points']))]@body_skeleton['model'][4].T)[:,:3]
            distance,nearest=tree.query(bind_world);indices=np.flatnonzero(distance<1e-5)
            root=(arm['weights']*np.isin(arm['joints'],[0,1])).sum(axis=1)
            mixed=(root[indices]>1e-9)&(root[indices]<1-1e-9)
            body_spine=(body['weights'][nearest[indices]]*(body['joints'][nearest[indices]]==4)).sum(axis=1)
            arm_shared=(arm['weights'][indices]*np.isin(arm['joints'][indices],[0,1,2,68])).sum(axis=1)
            compatible=(body_spine>1-1e-9)&(arm_shared>1-1e-9)
            correspondences.append((indices,nearest[indices],mixed,compatible))
        for case in (c for c in json.loads(INPUT.read_text()) if c['character']==character):
            body_matrix=animated(case['body'],20);arm_matrix=[animated(a,69) for a in case['arms']]
            original_body=skin(body,body_matrix,body_skeleton)
            proposed_shared=[dict(chest=body_matrix[4],
                                  clavicle=arm_matrix[i][1]@arms_skeleton[i]['inverse'][1],
                                  upper_arm=arm_matrix[i][2]@arms_skeleton[i]['inverse'][2]) for i in range(2)]
            # The body has a single spine03 transform, not a left/right chest pair.
            probes=np.c_[body['points'],np.ones(len(body['points']))]@body_skeleton['inverse'][4].T
            for mode in ('chest','clavicle','upper_arm'):
                conflict=np.linalg.norm((probes@proposed_shared[0][mode].T)[:,:3]-(probes@proposed_shared[1][mode].T)[:,:3],axis=1)
                for side,arm in enumerate(arms):
                    shared=proposed_shared[side][mode]
                    new_body=body_matrix.copy();new_body[4]=shared
                    new_arm=arm_matrix[side].copy()
                    # Arm bind model is local to native body spine03. Setting
                    # these animated transforms gives joints0/1/2 one skin map.
                    for joint in (0,1,2,68):new_arm[joint]=shared@arms_skeleton[side]['model'][joint]
                    before=skin(arm,arm_matrix[side],arms_skeleton[side])
                    after=skin(arm,new_arm,arms_skeleton[side]);body_after=skin(body,new_body,body_skeleton)
                    indices,body_indices,mixed,compatible=correspondences[side]
                    separation=np.linalg.norm(after[indices]-body_after[body_indices],axis=1)
                    affected_arm=np.linalg.norm(after-before,axis=1)>1e-9
                    affected_body=np.linalg.norm(body_after-original_body,axis=1)>1e-9
                    # Unmodified forearm/wrist palettes keep their centres, but
                    # no longer connect the changed shoulder/upper-arm frame.
                    local_elbow=arms_skeleton[side]['inverse'][2]@np.r_[arms_skeleton[side]['model'][3,:3,3],1]
                    carried_elbow=(new_arm[2]@local_elbow)[:3]
                    original_carried_elbow=(arm_matrix[side][2]@local_elbow)[:3]
                    row={k:v for k,v in case.items() if k not in ('body','arms')}
                    row.update(side=('left','right')[side],mode=mode,body_sha256=body['sha256'],arm_sha256=arm['sha256'],
                               all_correspondence_distance=stats(separation),mixed_correspondence_distance=stats(separation[mixed]),
                               compatible_correspondences=int(compatible.sum()),
                               compatible_correspondence_distance=stats(separation[compatible]),
                               left_right_spine03_conflict=stats(conflict),
                               left_right_spine03_centre_conflict_metres=float(np.linalg.norm(proposed_shared[0][mode][:3,3]-proposed_shared[1][mode][:3,3])),
                               shoulder_centre_shift_metres=float(np.linalg.norm(new_arm[1,:3,3]-arm_matrix[side][1,:3,3])),
                               upper_arm_centre_shift_metres=float(np.linalg.norm(new_arm[2,:3,3]-arm_matrix[side][2,:3,3])),
                               upper_arm_carried_elbow_error_metres=float(np.linalg.norm(carried_elbow-arm_matrix[side][3,:3,3])),
                               original_carried_elbow_error_metres=float(np.linalg.norm(original_carried_elbow-arm_matrix[side][3,:3,3])),
                               carried_elbow_change_metres=float(np.linalg.norm(carried_elbow-original_carried_elbow)),
                               shared_linear_singular_values=np.linalg.svd(shared[:3,:3],compute_uv=False).tolist(),
                               arm_distortion=distortion(before,after,arm['triangles'],affected_arm) if affected_arm.any() else None,
                               body_distortion=distortion(original_body,body_after,body['triangles'],affected_body) if affected_body.any() else None)
                    evidence['cases'].append(row)
    (OUT/'shared-garment-transform.json').write_text(json.dumps(evidence,indent=2)+'\n')
    for mode in ('chest','clavicle','upper_arm'):
        selected=[r for r in evidence['cases'] if r['mode']==mode]
        print(mode,'mixed separation',max(r['mixed_correspondence_distance']['maximum'] for r in selected),
              'shoulder shift',max(r['shoulder_centre_shift_metres'] for r in selected),
              'upper shift',max(r['upper_arm_centre_shift_metres'] for r in selected),
              'carried elbow error',max(r['upper_arm_carried_elbow_error_metres'] for r in selected),
              'spine conflict',max(r['left_right_spine03_conflict']['maximum'] for r in selected))

if __name__=='__main__':main()
