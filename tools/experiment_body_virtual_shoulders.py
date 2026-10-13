"""Offline extended body skin influences; arms/palms remain original.

Diffuse nonnegative virtual arm-joint weights from coincident body/arm bind
positions across three body-surface geodesic bands. Never modifies assets.
"""
import hashlib
import json
import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse.linalg import spsolve
from experiment_garment_seam_band import load, topology, distortion, stats, ROOT, RAW
from build_player_asset_pack import parse_skeleton
from render_body_rig import skin_positions

OUT=ROOT/'artifacts/research/garment-continuity'
INPUT=OUT/'driver-palettes-20261011.json'

def animated(values,n):
    return np.asarray(values).reshape(n,4,4).transpose(0,2,1)

def main():
    cases=json.loads(INPUT.read_text())
    bs=parse_skeleton(RAW/'emily/player_body_emily.md6skl')
    skeletons=[parse_skeleton(RAW/f'emily/player_{side}arm_emily.md6skl') for side in ('left','right')]
    report=dict(input_sha256=hashlib.sha256(INPUT.read_bytes()).hexdigest(),native_asset_changes=False,
                headset_verified=False,scope='coincident bind positions are overlap correspondences, not assumed boundary welds',cases=[])
    for character in ('emily','corvo'):
        body=load(character,'body',20);arms=[load(character,f'{side}arm',69) for side in ('left','right')]
        tree=cKDTree(body['points']);matches=[]
        for arm in arms:
            bind_world=(np.c_[arm['points'],np.ones(len(arm['points']))]@bs['model'][4].T)[:,:3]
            distances,nearest=tree.query(bind_world);indices=np.flatnonzero(distances<1e-5)
            # Restrict virtual branches to the actual shoulder/upper-arm patch.
            # Exact coincident points elsewhere (including leg-influenced body
            # vertices) do not establish an anatomical garment correspondence.
            shoulder_weights=(arm['weights']*np.isin(arm['joints'],[0,1,2,68])).sum(axis=1)
            leg_weights=(body['weights']*np.isin(body['joints'],range(9,19))).sum(axis=1)
            indices=indices[(shoulder_weights[indices]>1-1e-9)&(leg_weights[nearest[indices]]<1e-9)]
            matches.append((indices,nearest[indices]))
        for band in (.06,.12,.20):
            extended=[]
            for side,arm in enumerate(arms):
                indices,body_indices=matches[side]
                groups,anchors,unknown,lap,_,distance=topology(body,body_indices,band)
                opposite_anchors=np.unique(groups[matches[1-side][1]])
                # One cap's fade cannot modify the opposite cap's independent
                # seed. A seed shared by both sides remains an explicit conflict.
                unknown=unknown[~np.isin(unknown,opposite_anchors)]
                influence=np.zeros((lap.shape[0],69));counts=np.zeros(lap.shape[0])
                # Exact authored arm weight mix at each coincident body seed.
                for slot in range(8):
                    np.add.at(influence,(groups[body_indices],arm['joints'][indices,slot]),arm['weights'][indices,slot])
                np.add.at(counts,groups[body_indices],1)
                influence[anchors]/=counts[anchors,None]
                if len(unknown):
                    influence[unknown]=spsolve(lap[unknown][:,unknown],-lap[unknown][:,anchors]@influence[anchors])
                if influence.min()<-1e-10 or influence.sum(axis=1).max()>1+1e-10:
                    raise ValueError('Virtual weight field violates nonnegative normalized bounds')
                extended.append(np.maximum(influence[groups],0))
            influence=np.stack(extended)
            total=influence.sum(axis=(0,2))
            # Crossed body-side bands share available weight; no negative base weights.
            influence/=np.maximum(total,1)[None,:,None]
            retained=1-influence.sum(axis=(0,2))
            active=retained<1-1e-9
            shared_seed_groups=np.intersect1d(np.unique(groups[matches[0][1]]),np.unique(groups[matches[1][1]]))
            for case in (c for c in cases if c['character']==character):
                bm=animated(case['body'],20)
                before=skin_positions(body['vertices'],(bm@bs['inverse'])[:,:3,:])
                after=before*retained[:,None];arm_pose=[]
                for side,arm in enumerate(arms):
                    am=animated(case['arms'][side],69)
                    arm_pose.append(skin_positions(arm['vertices'],(am@skeletons[side]['inverse'])[:,:3,:]))
                    virtual_map=am@skeletons[side]['inverse']@bs['inverse'][4]
                    virtual_positions=np.einsum('kij,vj->vki',virtual_map[:,:3,:],np.c_[body['points'],np.ones(len(body['points']))])
                    after+=np.einsum('vk,vki->vi',influence[side],virtual_positions)
                row={k:v for k,v in case.items() if k not in ('body','arms')}
                displacement=np.linalg.norm(after-before,axis=1)
                neck=(body['weights']*np.isin(body['joints'],[5,6,7,8])).sum(axis=1)>1e-9
                legs=(body['weights']*np.isin(body['joints'],range(9,19))).sum(axis=1)>1e-9
                side_gap=[]
                for side,(arm_indices,body_indices) in enumerate(matches):
                    side_gap.append(stats(np.linalg.norm(after[body_indices]-arm_pose[side][arm_indices],axis=1)))
                row.update(band_metres=band,body_sha256=body['sha256'],arm_sha256=[a['sha256'] for a in arms],
                           correspondence_counts=[len(m[0]) for m in matches],correspondence_distance=side_gap,
                           active_vertices=int(active.sum()),virtual_weight_maximum=float((1-retained).max()),
                           dual_side_overlap_vertices=int((total>1+1e-9).sum()),
                           shared_body_seed_groups=len(shared_seed_groups),
                           body_displacement_metres=stats(displacement[active]),
                           neck_vertex_displacement_metres=stats(displacement[neck]),
                           leg_vertex_displacement_metres=stats(displacement[legs]),
                           outside_band_position_error_maximum=float(displacement[~active].max()),
                           body_distortion=distortion(before,after,body['triangles'],active))
                report['cases'].append(row)
    (OUT/'body-virtual-shoulder-experiment.json').write_text(json.dumps(report,indent=2)+'\n')
    for character in ('emily','corvo'):
        for band in (.06,.12,.20):
            r=[x for x in report['cases'] if x['character']==character and x['band_metres']==band]
            print(character,band,'maxedge',max(x['body_distortion']['edge_ratio']['maximum'] for x in r),
                  'flips',max(x['body_distortion']['reversed_relative_to_original_normals'] for x in r),
                  'gap',max(g['maximum'] for x in r for g in x['correspondence_distance']),
                  'neck',max(x['neck_vertex_displacement_metres']['maximum'] for x in r),
                  'leg',max(x['leg_vertex_displacement_metres']['maximum'] for x in r))

if __name__=='__main__':main()
