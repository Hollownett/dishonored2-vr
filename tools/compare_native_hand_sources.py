"""Offline native finger-chain measurements and private hand mesh comparison."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np
from analyse_arm_mesh import decode_mesh
from export_native_hand_poses import verified_arm, local, dot
from render_body_rig import palettes, skin_positions


def changes(a,b,side):
    names=a['joint_names'];parents=a['skeleton_layout']['parent_indices'];result=[]
    for digit in ('thumb','index','medium','ring','pinky'):
        for segment in range(3):
            name=f'{side}_{digit}_0{segment+(1 if digit=="thumb" else 2)}_jnt';joint=names.index(name)
            x,px=local(a['sampled_joints'],parents,joint);y,py=local(b['sampled_joints'],parents,joint)
            trace=sum(dot(v,w) for v,w in zip(x,y))
            result.append(dict(name=name,relative_angle_degrees=math.degrees(math.acos(max(-1.,min(1.,(trace-1)/2)))),
                               local_offset_change_native_units=math.dist(px,py),flexed=trace<2.98))
    return result


def draw(opened,closed,side,asset,out,candidate_label='Sprint candidate'):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    data=asset.read_bytes();mesh=decode_mesh(data,69);meshes=[];stats=[]
    for label,arm in (('Idle',opened),(candidate_label,closed)):
        cpu,native=palettes(arm);names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices'];wrist=names.index(side+'_wrist_jnt')
        pose=arm['sampled_joints'];origin=np.asarray(pose[wrist]['position']);axes=np.asarray(pose[wrist]['rotation_columns'])
        descendants=[]
        for joint in range(69):
            at=joint
            for _ in range(69):
                if at==wrist:descendants.append(joint);break
                at=parents[at]
                if at<0:break
        pieces=[];maximum=0.
        for surface in mesh['surfaces']:
            vertices=surface['vertices'];points=skin_positions(vertices,cpu);actual=skin_positions(vertices,native)
            maximum=max(maximum,float(np.max(np.abs(points-actual))))
            joints=np.asarray([list(v[2]) for v in vertices]);weights=np.asarray([list(v[1]) for v in vertices])
            hand=((weights*np.isin(joints,descendants)).sum(axis=1)>=128)&(np.linalg.norm(points-origin,axis=1)<.25)
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
            triangles=triangles[np.all(hand[triangles],axis=1)]
            if len(triangles):pieces.append(((points-origin)@axes.T,triangles))
        if maximum>1e-4:raise ValueError('Private mesh does not match native render palette')
        meshes.append((label,pieces));stats.append(dict(state=label,maximum_cpu_vs_native_vertex_error_native_units=maximum,
                                                         rendered_hand_triangles=sum(len(t) for _,t in pieces)))
    visible=np.concatenate([p[t].reshape(-1,3) for _,pieces in meshes for p,t in pieces]);lo=visible.min(axis=0);hi=visible.max(axis=0)
    centre=(lo+hi)/2;radius=max(hi-lo)*.55
    figure=plt.figure(figsize=(13,8))
    for row,(label,pieces) in enumerate(meshes):
        for column,(elevation,azimuth,view) in enumerate(((10,40,'Oblique'),(85,0,'Palm plane'),(0,90,'Side'))):
            axis=figure.add_subplot(2,3,row*3+column+1,projection='3d')
            for points,triangles in pieces:
                axis.add_collection3d(Poly3DCollection(points[triangles],facecolor='#ac9785',edgecolor='#493c36',linewidth=.06,alpha=1))
            axis.set(xlim=(centre[0]-radius,centre[0]+radius),ylim=(centre[1]-radius,centre[1]+radius),
                     zlim=(centre[2]-radius,centre[2]+radius),title=f'{label}: {view}')
            axis.set_box_aspect((1,1,1));axis.view_init(elev=elevation,azim=azimuth);axis.set_axis_off()
    figure.suptitle(f'{side.capitalize()} native 69-joint hand — unchanged source mesh, wrist aligned\nPrivate offline diagnostic; action label does not certify a fist')
    figure.tight_layout();target=out/(side+('-idle-sprint.png'if candidate_label=='Sprint candidate'else '-idle-action.png'));figure.savefig(target,dpi=170);plt.close(figure)
    return dict(asset_sha256=hashlib.sha256(data).hexdigest(),image=str(target),states=stats)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--idle',required=True);parser.add_argument('--candidate',required=True)
    parser.add_argument('--candidate-label',default='Sprint candidate')
    args=parser.parse_args();root=args.capture.resolve().parent;capture=json.loads(args.capture.read_text())
    idle=json.loads((root/args.idle).read_text());candidate=json.loads((root/args.candidate).read_text());summaries=[]
    for sample in capture['samples']:
        if 'pose_report' not in sample:continue
        report=json.loads((root/sample['pose_report']).read_text());arms=[]
        for side in ('left','right'):
            arm=next(a for a in report['body_samples'][0]['arms'] if a['side']==side)
            skin=arm['skinning_layout'];baseline=next(a for a in idle['body_samples'][0]['arms'] if a['side']==side)
            delta=changes(baseline,arm,side)
            arms.append(dict(side=side,error=skin['maximum_absolute_error'],source_unchanged=skin['source_unchanged'],
                output_unchanged=skin['output_unchanged'],flexed_main_bones=sum(d['flexed'] for d in delta),joint_changes=delta))
        summaries.append(dict(seconds=sample['seconds'],report=sample['pose_report'],arms=arms))
    assets=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';renders=[];endpoints=[]
    for side in ('left','right'):
        a=verified_arm(idle,side);b=verified_arm(candidate,side)
        same=a['joint_names']==b['joint_names'] and a['skeleton_layout']['parent_indices']==b['skeleton_layout']['parent_indices'] and \
             a['skinning_layout']['inverse_bind_rows']==b['skinning_layout']['inverse_bind_rows']
        if not same:raise ValueError('Endpoint rigs differ')
        endpoints.append(dict(side=side,same_rig=True,joint_changes=changes(a,b,side)))
        renders.append(draw(a,b,side,assets/(side+'-arm.bmd6model'),root,args.candidate_label))
    matching=sum(all(a['error'] is not None and a['error']<=1e-4 and a['source_unchanged'] and a['output_unchanged']
                     for a in sample['arms']) for sample in summaries)
    result=dict(capture=str(args.capture),idle=args.idle,candidate=args.candidate,game_memory_writes=False,
        render_source_matching_samples=matching,total_pose_samples=len(summaries),
        authored_fist_certified=False,headset_acceptance=False,metric_scale_verified=False,endpoints=endpoints,samples=summaries,renders=renders)
    (root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(output=str(root/'comparison.json'),renders=renders)))


if __name__=='__main__':main()
