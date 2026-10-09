"""Offline weighted rigid accessory leaf fit; no mesh writes or runtime activation."""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
from analyse_arm_mesh import decode_mesh
from author_native_hand_candidate import skin,matrices,render
from export_native_hand_poses import verified_arm
from render_body_rig import skin_positions

def fit_rigid(points,targets,weights):
    points=np.asarray(points,dtype=float);targets=np.asarray(targets,dtype=float);weights=np.asarray(weights,dtype=float)
    if points.ndim!=2 or points.shape[1]!=3 or targets.shape!=points.shape or weights.shape!=(len(points),)or len(points)<3 or not np.isfinite(points).all()or not np.isfinite(targets).all()or not np.isfinite(weights).all()or np.any(weights<=0):raise ValueError('Invalid or insufficient leaf influences')
    normalized=weights/weights.sum();a=(points*normalized[:,None]).sum(axis=0);b=(targets*normalized[:,None]).sum(axis=0)
    u,singular,vh=np.linalg.svd(((points-a)*normalized[:,None]).T@(targets-b))
    rotation=vh.T@np.diag([1,1,np.linalg.det(vh.T@u.T)])@u.T
    result=np.eye(4);result[:3,:3]=rotation;result[:3,3]=b-rotation@a
    if abs(np.linalg.det(rotation)-1)>1e-8:raise ValueError('Accessory fit is not rigid')
    return result,singular

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);args=p.parse_args()
    document=json.loads(args.candidate.read_text());report=json.loads(Path(document['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    data=(args.candidate.parent/'runtime-sweep-test-output.bin').read_bytes()
    if data[:8]!=b'D2VRSO02'or struct.unpack_from('<I',data,8)[0]!=112:raise ValueError('Invalid actual runtime sweep')
    results=[]
    for side_index,side in enumerate(('left','right')):
        arm=verified_arm(report,side);names=arm['joint_names'];original=np.asarray(matrices(arm));mesh=decode_mesh((root/(side+'-arm.bmd6model')).read_bytes())
        leaf=names.index(side+('_index_end_jnt'if side=='left'else '_thumb_end_jnt'))
        material='player_emily_ring_disappear.material'if side=='left'else 'emily_body.material'
        accessory=next(s for s in mesh['surfaces']if s['material'].endswith(material));vertices=accessory['vertices']
        positions=np.asarray([v[0]for v in vertices]);homogeneous=np.column_stack((positions,np.ones(len(vertices))))
        weight=np.asarray([sum(w for j,w in zip(v[2],v[1])if j==leaf)/255. for v in vertices]);active=weight>0
        inverse=np.eye(4);inverse[:3]=np.asarray(arm['skinning_layout']['inverse_bind_rows'][leaf]).reshape(3,4)
        local_points=(inverse@homogeneous.T).T[:,:3]
        before=skin_positions(vertices,skin(arm,original));flesh=[]
        for s in mesh['surfaces']:
            if any(n in s['material']for n in ('player_emily_hands.material','player_emily_gloves.material','player_emily_sleeves.material')):
                if any(any(j==leaf and w for j,w in zip(v[2],v[1]))for v in s['vertices']):raise ValueError('Accessory leaf affects hand/cuff')
                flesh.append(s)
        poses=[]
        for record in range(112):
            at=12+record*(16+69*64);record_side,mode,grip,index=struct.unpack_from('<IIfI',data,at)
            if record_side!=side_index or mode!=2:continue
            pose=np.frombuffer(data,dtype='<f4',count=69*16,offset=at+16).reshape(69,4,4).transpose(0,2,1).astype(float)
            posed=skin_positions(vertices,skin(arm,pose));target=before.copy()
            if side=='left':
                anchor=names.index('left_index_02_jnt');delta=pose[anchor]@np.linalg.inv(original[anchor]);target=(delta@np.column_stack((before,np.ones(len(before)))).T).T[:,:3]
            leaf_contribution=(pose[leaf]@np.column_stack((local_points,np.ones(len(local_points)))).T).T[:,:3]*weight[:,None]
            other=posed-leaf_contribution;leaf_target=(target[active]-other[active])/weight[active,None]
            transform,singular=fit_rigid(local_points[active],leaf_target,weight[active]**2)
            fitted=pose.copy();fitted[leaf]=transform;after=skin_positions(vertices,skin(arm,fitted))
            error=np.linalg.norm(after-target,axis=1);skin_error=0.
            for s in flesh:skin_error=max(skin_error,float(np.max(np.abs(skin_positions(s['vertices'],skin(arm,pose))-skin_positions(s['vertices'],skin(arm,fitted))))))
            if skin_error!=0:raise ValueError('Accessory fit changes hand/cuff')
            item=dict(side=side,grip=grip,index=index,leaf_vertices=int(active.sum()),
                maximum_target_residual_native_units=float(error.max()),rms_target_residual_native_units=float(np.sqrt(np.mean(error**2))),
                maximum_active_residual_native_units=float(error[active].max()),maximum_uncontrolled_residual_native_units=float(error[~active].max())if np.any(~active)else 0,
                leaf_global_translation_delta_native_units=float(np.linalg.norm(transform[:3,3]-pose[leaf,:3,3])),
                fit_singular_values=singular.tolist(),hand_cuff_skin_difference=skin_error)
            results.append(item)
            if (grip==0 and index==0)or(grip==.5 and index==1)or grip==1:poses.append((f'Rigid leaf trial grip{grip:.2f} index{index}',list(fitted)))
        render(arm,poses,root/(side+'-arm.bmd6model'),args.candidate.parent/(side+'-accessory-rigid-leaf-fit.png'))
    target=args.candidate.parent/'accessory-rigid-leaf-fit.json';target.write_text(json.dumps(dict(pose_origin='offline_authored_rigid_accessory_leaf_trial',game_memory_writes=False,mesh_modified=False,production_activation=False,acceptance=False,
        targets={'left':'Native ring rigidly follows current index02 attachment','right':'Retain native source alternative body vertex positions'},
        limits=['Fit target is an authored hypothesis, not animation capture','Rigid least squares does not certify texture/material/headset appearance'],samples=results),indent=2)+'\n')
    print(json.dumps([dict(side=side,maximum_residual_native_units=max(s['maximum_target_residual_native_units']for s in results if s['side']==side),maximum_leaf_translation=max(s['leaf_global_translation_delta_native_units']for s in results if s['side']==side))for side in ('left','right')]))

if __name__=='__main__':main()
