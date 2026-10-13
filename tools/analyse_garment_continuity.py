"""Measure coincident native body/arm seam vertices under actual Driver palettes.

This is geometric evidence only; materials, compositor receipt and headset
appearance are not validated. Run garment_continuity_probe first.
"""
from pathlib import Path
import hashlib
import json
import numpy as np
from scipy.spatial import cKDTree
from analyse_arm_mesh import decode_mesh
from build_player_asset_pack import parse_skeleton
from render_body_rig import skin_positions

ROOT=Path(__file__).resolve().parents[1]
RAW=ROOT/'artifacts/fullbody-assets/raw/models/characters/player'
OUTPUT=ROOT/'artifacts/research/garment-continuity'
OUTPUT.mkdir(exist_ok=True)
body_skeleton=parse_skeleton(RAW/'emily/player_body_emily.md6skl')
arm_skeletons=[parse_skeleton(RAW/f'emily/player_{side}arm_emily.md6skl') for side in ('left','right')]
cases=json.loads((ROOT/'build/garment-continuity-palettes.json').read_text())

def mesh(character,part,count):
    path=RAW/f'{character}/player_{part}_{character}.md6mesh'
    raw=path.read_bytes();decoded=decode_mesh(raw,count)
    vertices=[v for surface in decoded['surfaces'] for v in surface['vertices']]
    triangles=[];offset=0
    for surface in decoded['surfaces']:
        triangles.append(np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)+offset)
        offset+=surface['vertices_count']
    return dict(vertices=vertices,hash=hashlib.sha256(raw).hexdigest(),
                points=np.asarray([v[0] for v in vertices]),
                triangles=np.concatenate(triangles),
                weights=np.asarray([list(v[1]) for v in vertices]),
                joints=np.asarray([list(v[2]) for v in vertices]))

def pose(vertices,transforms,inverse):
    matrices=np.asarray(transforms).reshape(len(inverse),4,4).transpose(0,2,1)
    return skin_positions(vertices,(matrices@inverse)[:,:3,:])

def summary(values):
    return dict(count=len(values),median_metres=float(np.median(values)),p95_metres=float(np.percentile(values,95)),
                maximum_metres=float(values.max()),over_5mm=int((values>.005).sum()),over_20mm=int((values>.020).sum()))

def cuff_experiments(arm,transforms,inverse,body_transform,body_targets,indices):
    matrices=np.asarray(transforms).reshape(len(inverse),4,4).transpose(0,2,1)
    original=skin_positions(arm['vertices'],(matrices@inverse)[:,:3,:])
    cuff_locked=matrices.copy()
    cuff_locked[0]=body_transform
    cuff_locked[1]=body_transform@np.linalg.inv(inverse[1])
    locked=skin_positions(arm['vertices'],(cuff_locked@inverse)[:,:3,:])
    # Best possible one-joint cuff affine, with all fitted centres, arm bend,
    # elbow/wrist targets and rigid limb palettes held fixed. Unconstrained
    # least squares is a lower bound, not a production-safe matrix proposal.
    local=(np.c_[arm['points'][indices],np.ones(len(indices))]@inverse[1].T)
    weights=(arm['weights'][indices]*(arm['joints'][indices]==1)).sum(axis=1)/255.
    x=local*weights[:,None]
    contribution=original[indices]-(local@matrices[1].T)[:,:3]*weights[:,None]
    coefficient=np.linalg.lstsq(x,body_targets-contribution,rcond=None)[0].T
    best=contribution+x@coefficient.T
    local_upper=(np.c_[arm['points'][indices],np.ones(len(indices))]@inverse[2].T)
    upper_weights=(arm['weights'][indices]*(arm['joints'][indices]==2)).sum(axis=1)/255.
    fixed=contribution-(local_upper@matrices[2].T)[:,:3]*upper_weights[:,None]
    axis=matrices[3,:3,3]-matrices[2,:3,3];axis/=np.linalg.norm(axis)
    cross=np.array([[0,-axis[2],axis[1]],[axis[2],0,-axis[0]],[-axis[1],axis[0],0]])
    roll_best=None
    for angle in np.linspace(-np.pi,np.pi,145):
        rotation=np.eye(3)+np.sin(angle)*cross+(1-np.cos(angle))*(cross@cross)
        upper=matrices[2].copy();upper[:3,:3]=rotation@upper[:3,:3]
        upper_part=(local_upper@upper.T)[:,:3]*upper_weights[:,None]
        fitted_cuff=np.linalg.lstsq(x,body_targets-fixed-upper_part,rcond=None)[0].T
        gap=np.linalg.norm(fixed+upper_part+x@fitted_cuff.T-body_targets,axis=1)
        error=float(np.mean(gap*gap))
        if roll_best is None or error<roll_best[0]:roll_best=(error,angle,gap,fitted_cuff)
    return dict(cuff_locked=summary(np.linalg.norm(locked[indices]-body_targets,axis=1)),
                arbitrary_cuff_affine_lower_bound=summary(np.linalg.norm(best-body_targets,axis=1)),
                arbitrary_cuff_affine_singular_values=np.linalg.svd(coefficient[:,:3],compute_uv=False).tolist(),
                best_upper_roll_and_arbitrary_cuff=summary(roll_best[2]),
                best_upper_roll_degrees=float(np.degrees(roll_best[1])),
                best_roll_cuff_singular_values=np.linalg.svd(roll_best[3][:,:3],compute_uv=False).tolist())

results=[]
for character in ('emily','corvo'):
    body=mesh(character,'body',20);arms=[mesh(character,f'{side}arm',69) for side in ('left','right')]
    tree=cKDTree(body['points']);matches=[]
    for side,arm in enumerate(arms):
        native_world=(np.c_[arm['points'],np.ones(len(arm['points']))]@body_skeleton['model'][4].T)[:,:3]
        distances,nearest=tree.query(native_world)
        seam=np.flatnonzero(distances<1e-5)
        root_weight=(arm['weights']*np.isin(arm['joints'],[0,1])).sum(axis=1)
        mixed=(root_weight[seam]>0)&(root_weight[seam]<255)
        matches.append((seam,nearest[seam],mixed))
    for case in (c for c in cases if c['character']==character):
        body_posed=pose(body['vertices'],case['body'],body_skeleton['inverse'])
        for side,arm in enumerate(arms):
            arm_posed=pose(arm['vertices'],case['arms'][side],arm_skeletons[side]['inverse'])
            indices,body_indices,mixed=matches[side]
            gap=np.linalg.norm(arm_posed[indices]-body_posed[body_indices],axis=1)
            result={k:v for k,v in case.items() if k not in ('body','arms')}
            result.update(side=('left','right')[side],body_sha256=body['hash'],arm_sha256=arm['hash'],
                          native_bind_shared_vertex_tolerance_metres=1e-5,
                          all_shared=summary(gap),mixed_shared=summary(gap[mixed]),
                          headset_verified=False,material_seams_verified=False)
            body_matrix=np.asarray(case['body']).reshape(20,4,4).transpose(0,2,1)[4]
            result['mixed_cuff_corrections']=cuff_experiments(arm,case['arms'][side],arm_skeletons[side]['inverse'],
                                                           body_matrix,body_posed[body_indices[mixed]],indices[mixed])
            worst=np.argsort(gap[mixed])[-5:][::-1];mi=indices[mixed];mb=body_indices[mixed]
            result['worst_mixed_vertices']=[dict(arm_vertex=int(mi[i]),body_vertex=int(mb[i]),gap_metres=float(gap[mixed][i]),
                arm_weights=[dict(joint=int(j),weight=int(w)) for j,w in zip(arm['joints'][mi[i]],arm['weights'][mi[i]]) if w],
                body_weights=[dict(joint=int(j),weight=int(w)) for j,w in zip(body['joints'][mb[i]],body['weights'][mb[i]]) if w]) for i in worst]
            results.append(result)

(OUTPUT/'seam-separation.json').write_text(json.dumps(results,indent=2)+'\n')
for result in results:
    print(result['character'],result['profile'],result['posture'],result['side'],'fit' if result['fitted'] else 'baseline',
          'mixed mm median/p95/max',*[round(result['mixed_shared'][key]*1000,2) for key in ('median_metres','p95_metres','maximum_metres')])

# Inspect actual cloth surfaces, not the stick skeleton. Native coincident seam
# points are established above; red lines connect those same vertices after fit.
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
figure=plt.figure(figsize=(15,9))
for row,character in enumerate(('emily','corvo')):
    body=mesh(character,'body',20);arms=[mesh(character,f'{side}arm',69) for side in ('left','right')]
    tree=cKDTree(body['points'])
    for column,(title,profile,posture) in enumerate((('Native bind garment',None,0),('Independent fitted T pose',4,0),('Same fit, raised arms',4,2))):
        axis=figure.add_subplot(2,3,row*3+column+1,projection='3d')
        case=next((c for c in cases if c['character']==character and c['profile']==profile and c['posture']==posture and c['fitted']),None)
        bp=body['points'] if case is None else pose(body['vertices'],case['body'],body_skeleton['inverse'])
        positions=[bp]
        for side,arm in enumerate(arms):
            native=(np.c_[arm['points'],np.ones(len(arm['points']))]@body_skeleton['model'][4].T)[:,:3]
            ap=native if case is None else pose(arm['vertices'],case['arms'][side],arm_skeletons[side]['inverse'])
            positions.append(ap)
            if case is not None:
                distances,indices=tree.query(native);seam=np.flatnonzero(distances<1e-5)
                gaps=np.linalg.norm(ap[seam]-bp[indices[seam]],axis=1)
                for i in np.argsort(gaps)[-10:]:
                    segment=np.stack((ap[seam[i]],bp[indices[seam[i]]]))
                    axis.plot(*segment.T,color='#c93535',linewidth=1.2)
        for part,points,colour in zip([body,*arms],positions,('#8394a7','#bd8c62','#bd8c62')):
            tri=part['triangles'];centres=points[tri].mean(axis=1)
            visible=(centres[:,2]>1.08)&(centres[:,2]<2.05)&(np.abs(centres[:,1])<.55)
            axis.add_collection3d(Poly3DCollection(points[tri[visible]],facecolors=colour,edgecolors='none',alpha=.88))
        axis.set(xlim=(-.3,.3),ylim=(-.5,.5),zlim=(1.08,2.05),title=f'{character.title()}: {title}')
        axis.set_box_aspect((.6,1,.97));axis.view_init(elev=10,azim=20)
        axis.set_xlabel('Forward m');axis.set_ylabel('Left m');axis.set_zlabel('Up m')
figure.suptitle('Actual native cloth meshes: matched bind vertices separate under tracked IK. Red lines are paired seam gaps.\nOffline geometry only; materials and headset appearance remain unverified.')
figure.tight_layout();figure.savefig(OUTPUT/'native-versus-fitted-seams.png',dpi=150);plt.close(figure)
