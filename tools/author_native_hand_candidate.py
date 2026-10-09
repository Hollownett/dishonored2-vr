"""Private offline authored native-rig grip. Not a captured/native animation endpoint.

No process access, engine calls, production archive export or activation.
Main finger axes follow native segment/palm geometry; helper/end local poses stay neutral.
"""
import argparse
import copy
import hashlib
import json
import math
import re
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
from analyse_arm_mesh import decode_mesh
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions
from rig_fields import affine_product


def unit(v):
    size=np.linalg.norm(v)
    if size<1e-8:raise ValueError('Degenerate native axis')
    return v/size


def matrices(arm):
    result=[]
    for node in arm['sampled_joints']:
        matrix=np.eye(4);matrix[:3,:3]=np.asarray(node['rotation_columns']).T;matrix[:3,3]=node['position'];result.append(matrix)
    return result


def fk(local,parents):
    output=[None]*69
    def solve(joint):
        if output[joint] is None:
            output[joint]=local[joint] if parents[joint]<0 else solve(parents[joint])@local[joint]
        return output[joint]
    for joint in range(69):solve(joint)
    return output


def authored(arm,index_curl=1.,driven_helpers=False,relaxed_knuckles=False,thumb_clearance=.012,mesh_thumb_fit=False,thumb_parameters=None,fan_fingers=False,finger_splay_degrees=None):
    side=arm['side'];names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices'];source=matrices(arm)
    local=[source[i].copy() if parents[i]<0 else np.linalg.inv(source[parents[i]])@source[i] for i in range(69)]
    wrist=names.index(side+'_wrist_jnt');base=[names.index(f'{side}_{digit}_02_jnt') for digit in ('index','medium','ring','pinky')]
    forward=unit(np.mean([source[i][:3,3] for i in base],axis=0)-source[wrist][:3,3])
    width=unit(source[base[0]][:3,3]-source[base[-1]][:3,3]);normal=unit(np.cross(forward,width))
    # Native idle's existing curvature selects the anatomical palm side independently per hand.
    bend=[]
    for digit in ('index','medium','ring','pinky'):
        chain=[names.index(f'{side}_{digit}_0{s}_jnt') for s in (2,3,4)]
        bend.append(np.dot(unit(source[chain[2]][:3,3]-source[chain[1]][:3,3])-unit(source[chain[1]][:3,3]-source[chain[0]][:3,3]),normal))
    if abs(np.mean(bend))<.02:raise ValueError('Native pose cannot identify signed palm-side curvature')
    normal*=1 if np.mean(bend)>0 else -1
    changed=[];axis_evidence=[];index_full=[]
    angles={'index':(55,75,35),'medium':(60,80,40),'ring':(65,85,40),'pinky':(65,85,45)}
    if relaxed_knuckles:angles={'index':(40,85,45),'medium':(40,90,45),'ring':(45,90,45),'pinky':(45,90,50)}
    for digit,amounts in angles.items():
        chain=[names.index(f'{side}_{digit}_0{s}_jnt') for s in (2,3,4)]
        for segment,(joint,degrees) in enumerate(zip(chain,amounts)):
            children=[i for i,p in enumerate(parents) if p==joint and 'helper' not in names[i]]
            child=chain[segment+1] if segment<2 else next((i for i in children if 'end' in names[i] or '_05_' in names[i]),None)
            if child is None:raise ValueError('Missing native finger end geometry')
            direction=unit(source[child][:3,3]-source[joint][:3,3]);axis=unit(np.cross(direction,normal))
            own=source[joint][:3,:3].T@axis;angle=math.radians(degrees)*(index_curl if digit=='index' else 1.)
            splay=0.
            spread=finger_splay_degrees or ({'ring':-4,'pinky':-8}if fan_fingers else {})
            if any(d not in angles or not math.isfinite(v)or abs(v)>18 for d,v in spread.items()):raise ValueError('Invalid bounded offline finger splay')
            if segment==0 and digit in spread:
                splay=math.radians(spread[digit])*(index_curl if digit=='index'else 1.)*np.sign(np.dot(np.cross(normal,forward),width))
                local[joint][:3,:3]=local[joint][:3,:3]@Rotation.from_rotvec((source[joint][:3,:3].T@normal)*splay).as_matrix()
            local[joint][:3,:3]=local[joint][:3,:3]@Rotation.from_rotvec(own*angle).as_matrix();changed.append(joint)
            if digit=='index':
                matrix=local[joint].copy();matrix[:3,:3]=np.asarray(source[parents[joint]][:3,:3]).T@source[joint][:3,:3]@Rotation.from_rotvec(own*math.radians(degrees)).as_matrix()
                index_full.append((joint,matrix))
            axis_evidence.append(dict(joint=names[joint],axis_in_native_bone_frame=own.tolist(),additional_angle_degrees=math.degrees(angle),splay_degrees=math.degrees(splay)))
    fingers=fk(local,parents);thumb=[names.index(f'{side}_thumb_0{s}_jnt') for s in (1,2,3)];end=names.index(side+'_thumb_end_jnt')
    # Opposition reaches an actual native index proximal joint; no global/thumb axis convention is assumed.
    target_local=[m.copy() for m in local]
    for joint,matrix in index_full:target_local[joint]=matrix
    target_fingers=fk(target_local,parents)
    if not .005<=thumb_clearance<=.04:raise ValueError('Thumb clearance outside offline trial bounds')
    target=target_fingers[names.index(side+'_index_03_jnt')][:3,3]-normal*thumb_clearance
    thumb_base=[local[i].copy() for i in thumb];axes=[]
    for joint,child in zip(thumb[1:],thumb[2:]+[end]):
        direction=unit(source[child][:3,3]-source[joint][:3,3]);towards=unit(target-source[joint][:3,3])
        axes.append(source[joint][:3,:3].T@unit(np.cross(direction,towards)))
    def pose(parameters,contact=False):
        candidate=[m.copy() for m in local]
        if contact:
            for joint,matrix in index_full:candidate[joint]=matrix
        candidate[thumb[0]][:3,:3]=thumb_base[0][:3,:3]@Rotation.from_rotvec(parameters[:3]).as_matrix()
        for i,joint in enumerate(thumb[1:]):
            candidate[joint][:3,:3]=thumb_base[i+1][:3,:3]@Rotation.from_rotvec(axes[i]*parameters[i+3]).as_matrix()
        return candidate,fk(candidate,parents)
    thumb_vertices=[];capsule_start=[];capsule_end=[];clearance=[]
    if mesh_thumb_fit:
        path=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'/(side+'-arm.bmd6model')
        original_palette,_=palettes(arm)
        chain=[names.index(f'{side}_index_0{s}_jnt')for s in (2,3,4)]+[names.index(side+'_index_end_jnt')]
        capsule_start=np.asarray([target_fingers[i][:3,3]for i in chain[:-1]])
        capsule_end=np.asarray([target_fingers[i][:3,3]for i in chain[1:]])
        old_start=np.asarray([source[i][:3,3]for i in chain[:-1]]);old_end=np.asarray([source[i][:3,3]for i in chain[1:]])
        def distance(points,starts,ends):
            segment=ends-starts;offset=points[:,None,:]-starts
            amount=np.clip(np.einsum('vki,ki->vk',offset,segment)/np.einsum('ki,ki->k',segment,segment),0.,1.)
            return np.linalg.norm(offset-amount[:,:,None]*segment,axis=2).min(axis=1)
        for surface in decode_mesh(path.read_bytes())['surfaces']:
            if not any(n in surface['material']for n in ('player_emily_hands.material','player_emily_gloves.material')):continue
            vertices=surface['vertices'];before=skin_positions(vertices,original_palette)
            for vertex,point in zip(vertices,before):
                weight=sum(w for j,w in zip(vertex[2],vertex[1])if 'thumb'in names[j])
                if weight>=190 and np.linalg.norm(point-source[wrist][:3,3])<.25:thumb_vertices.append(vertex)
        old_points=skin_positions(thumb_vertices,original_palette)
        # Native web contact is retained; additional surface intrusion into the index core is penalized.
        clearance=np.minimum(.011,distance(old_points,old_start,old_end)-.0001)
    def residual(parameters):
        _,global_pose=pose(parameters,contact=True)
        values=[global_pose[end][:3,3]-target,parameters*.0015]
        if mesh_thumb_fit:
            points=skin_positions(thumb_vertices,skin(arm,global_pose))
            values.append(np.maximum(0.,clearance-distance(points,capsule_start,capsule_end))*.4)
        return np.concatenate(values)
    if thumb_parameters is None:
        parameters=least_squares(residual,np.array([0.,0.,0.,.3,.3]),bounds=([-1.,-1.,-1.,0.,0.],[1.,1.,1.,1.2,1.2])).x
    else:
        parameters=np.asarray(thumb_parameters,dtype=float)
        if parameters.shape!=(5,)or not np.isfinite(parameters).all()or np.any(parameters<[-1.,-1.,-1.,0.,0.])or np.any(parameters>[1.,1.,1.,1.2,1.2]):raise ValueError('Invalid bounded authored thumb parameters')
    local,output=pose(parameters);changed+=thumb;helper_evidence=[]
    original=[source[i] if parents[i]<0 else np.linalg.inv(source[parents[i]])@source[i] for i in range(69)]
    if driven_helpers:
        for joint,name in enumerate(names):
            if 'helper'not in name or not any(d in name for d in ('thumb','index','middle','ring','pinky')):continue
            match=re.search(r'helper_(index|middle|ring|pinky)_(\d)_',name)
            if match:
                digit='medium'if match[1]=='middle'else match[1];child=names.index(f'{side}_{digit}_0{int(match[2])+2}_jnt')
            elif 'helper_thumb_2_'in name:child=thumb[2]
            elif 'helper_thumb_1_'in name or 'flexmuscle'in name or 'skinpalm'in name:child=thumb[1]
            elif '1stdorsal'in name or 'tendon'in name:child=thumb[0]
            else:continue
            parent=parents[joint]
            if parents[child]!=parent:raise ValueError('Helper/native main hierarchy relationship differs')
            neutral_child=output[parent]@original[child]
            delta=output[child][:3,:3]@neutral_child[:3,:3].T
            # Native sprint top knuckle helper rotates about70% of MCP, bottom50%.
            factor=.7 if '_top_'in name else .5
            if 'thumb'in name:
                # Native partial-bend capture: thumb top helpers retain neutral rotations;
                # bottom1 rotates about10%, bottom2 about20%; palm skin primarily translates.
                factor=0. if '_top_'in name or '1stdorsal'in name or 'skinpalm'in name or 'flexmuscle'in name else .2 if '_2_'in name else .1
                if 'tendon'in name:factor=.16
            rotation=Rotation.from_matrix(delta).as_rotvec()
            half=Rotation.from_rotvec(rotation*factor).as_matrix()
            helper=output[parent]@original[joint]
            helper[:3,:3]=half@helper[:3,:3]
            # Native top helpers have nearly neutral local positions; lower skin follows the bend.
            if '_bot_'in name:
                pivot=neutral_child[:3,3];helper[:3,3]=pivot+half@(helper[:3,3]-pivot)
            if 'skinpalm'in name or 'flexmuscle'in name:
                old_root=output[wrist]@original[thumb[0]];new_root=output[thumb[0]]
                root_delta=new_root[:3,:3]@old_root[:3,:3].T
                portion=Rotation.from_rotvec(Rotation.from_matrix(root_delta).as_rotvec()*.3).as_matrix()
                neutral=old_root@original[joint]
                helper[:3,3]=old_root[:3,3]+portion@(neutral[:3,3]-old_root[:3,3])
            local[joint]=np.linalg.inv(output[parent])@helper;changed.append(joint)
            helper_evidence.append(dict(joint=name,main_child=names[child],rotation_factor=factor,
                helper_local_offset_change_native_units=float(np.linalg.norm(local[joint][:3,3]-original[joint][:3,3]))))
        output=fk(local,parents)
    # Exact neutral helper/end locals; all other locals remain native authored source.
    neutral=max(float(np.max(np.abs(local[i]-original[i]))) for i in range(69) if i not in changed)
    offset=max(float(np.linalg.norm(local[i][:3,3]-original[i][:3,3])) for i in range(69))
    main_offset=max(float(np.linalg.norm(local[i][:3,3]-original[i][:3,3]))for i in changed if 'helper'not in names[i])
    return output,dict(side=side,index_curl=index_curl,fan_fingers=fan_fingers,mesh_thumb_fit=mesh_thumb_fit,thumb_fit_mesh_vertices=len(thumb_vertices),thumb_clearance_native_units=thumb_clearance,relaxed_knuckles=relaxed_knuckles,helper_drivers_enabled=driven_helpers,helper_evidence=helper_evidence,changed_bones=[names[i] for i in changed],
        axis_evidence=axis_evidence,thumb_opposition_rotation_vector_radians=parameters[:3].tolist(),thumb_flex_radians=parameters[3:].tolist(),
        thumb_target_error_native_units=float(np.linalg.norm(output[end][:3,3]-target)),
        maximum_neutral_local_error=neutral,maximum_main_local_offset_change_native_units=main_offset,maximum_local_offset_change_native_units=offset)


def skin(arm,pose):
    rows=[]
    for matrix,inverse in zip(pose,arm['skinning_layout']['inverse_bind_rows']):rows.append(affine_product(matrix[:3,:4].reshape(-1).tolist(),inverse))
    return np.asarray(rows).reshape(69,3,4)


def render(arm,poses,asset,target):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    mesh=decode_mesh(asset.read_bytes(),69);names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices'];wrist=names.index(arm['side']+'_wrist_jnt')
    descendants=[]
    for joint in range(69):
        at=joint
        for _ in range(69):
            if at==wrist:descendants.append(joint);break
            at=parents[at]
            if at<0:break
    meshes=[];stats=[];source,_=palettes(arm)
    for label,pose in poses:
        palette=skin(arm,pose);origin=pose[wrist][:3,3];axes=pose[wrist][:3,:3];pieces=[];surfaces=[]
        for number,surface in enumerate(mesh['surfaces']):
            v=surface['vertices'];points=skin_positions(v,palette);before=skin_positions(v,source);weights=np.asarray([list(x[1]) for x in v]);joints=np.asarray([list(x[2]) for x in v])
            handweights=(weights*np.isin(joints,descendants)).sum(axis=1);hand=(handweights>=128)&(np.linalg.norm(before-origin,axis=1)<.25)
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3);visible=triangles[np.all(hand[triangles],axis=1)]
            if len(visible):pieces.append(((points-origin)@axes,visible))
            displacement=np.linalg.norm(points-before,axis=1);nonhand=handweights==0
            edge=triangles[:,[[0,1],[1,2],[2,0]]].reshape(-1,2);old=np.linalg.norm(before[edge[:,0]]-before[edge[:,1]],axis=1);new=np.linalg.norm(points[edge[:,0]]-points[edge[:,1]],axis=1);valid=old>1e-5
            worst=int(np.argmax(np.where(valid,new/np.maximum(old,1e-5),0.)));ends=edge[worst]
            surfaces.append(dict(surface=number,material=surface['material'],maximum_vertex_displacement_native_units=float(displacement.max()),
                maximum_zero_hand_weight_displacement_native_units=float(displacement[nonhand].max()) if np.any(nonhand) else None,
                maximum_edge_length_ratio=float(np.max(new[valid]/old[valid])),worst_edge_before_native_units=float(old[worst]),
                worst_edge_after_native_units=float(new[worst]),worst_edge_vertex_weights=[[(names[j],int(w))for j,w in zip(v[i][2],v[i][1])if w]for i in ends]))
        meshes.append((label,pieces));stats.append(dict(state=label,surfaces=surfaces))
    visible=np.concatenate([p[t].reshape(-1,3) for _,pieces in meshes for p,t in pieces]);centre=(visible.min(axis=0)+visible.max(axis=0))/2;radius=max(visible.max(axis=0)-visible.min(axis=0))*.55
    figure=plt.figure(figsize=(13,12))
    for row,(label,pieces) in enumerate(meshes):
        for col,(elev,azim,view) in enumerate(((10,40,'Oblique'),(85,0,'Palm plane'),(0,90,'Side'))):
            axis=figure.add_subplot(len(meshes),3,row*3+col+1,projection='3d')
            for p,t in pieces:axis.add_collection3d(Poly3DCollection(p[t],facecolor='#ac9785',edgecolor='#493c36',linewidth=.06))
            axis.set(xlim=(centre[0]-radius,centre[0]+radius),ylim=(centre[1]-radius,centre[1]+radius),zlim=(centre[2]-radius,centre[2]+radius),title=f'{label}: {view}')
            axis.set_box_aspect((1,1,1));axis.view_init(elev=elev,azim=azim);axis.set_axis_off()
    figure.suptitle(arm['side'].capitalize()+' native-rig authored candidate — mesh review\nOffline artist/solver pose; NOT a captured native fist or headset acceptance')
    figure.tight_layout();figure.savefig(target,dpi=170);plt.close(figure);return stats


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--driven-helpers',action='store_true',help='Authored bounded main-relative helper drivers, informed by native partial bend')
    parser.add_argument('--relaxed-knuckles',action='store_true',help='Artist trial: distribute closure toward PIP/DIP while preserving a relaxed MCP')
    parser.add_argument('--thumb-clearance',type=float,default=.012,help='Offline thumb-bone target clearance in native model units, not verified metres')
    parser.add_argument('--mesh-thumb-fit',action='store_true',help='Penalize actual thumb skin entering the native index-bone capsule')
    parser.add_argument('--thumb-parameters',type=Path,help='Private authored per-side parameter JSON from offline contact search')
    parser.add_argument('--fan-fingers',action='store_true',help='Artist trial: ring/pinky MCP abduction4/8degrees about native palm normal')
    args=parser.parse_args();report=json.loads(args.source.read_text());args.output.mkdir(parents=True,exist_ok=True);result=[]
    parameters=json.loads(args.thumb_parameters.read_text())['parameters']if args.thumb_parameters else {}
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    for side in ('left','right'):
        arm=verified_arm(report,side);full,evidence=authored(arm,driven_helpers=args.driven_helpers,relaxed_knuckles=args.relaxed_knuckles,thumb_clearance=args.thumb_clearance,mesh_thumb_fit=args.mesh_thumb_fit,thumb_parameters=parameters.get(side),fan_fingers=args.fan_fingers);index,index_evidence=authored(arm,0.,args.driven_helpers,args.relaxed_knuckles,args.thumb_clearance,args.mesh_thumb_fit,parameters.get(side),args.fan_fingers)
        stats=render(arm,[('Native idle',matrices(arm)),('Authored grip candidate',full),('Authored index released',index)],root/(side+'-arm.bmd6model'),args.output/(side+'-authored-candidate.png'))
        result.append(dict(side=side,evidence=evidence,index_evidence=index_evidence,authored_transform_matrices=[m.tolist() for m in full],surfaces=stats))
    data=dict(pose_origin='deliberately_authored_installed_native_69_rig',source_readonly_report=str(args.source.resolve()),
        source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),game_memory_access=False,game_memory_writes=False,
        captured_fist=False,production_archive_exported=False,visual_review_required=True,headset_acceptance=False,arms=result)
    (args.output/'authored-candidate.json').write_text(json.dumps(data,indent=2)+'\n');print(json.dumps(dict(output=str(args.output),
        evidence=[{k:a['evidence'][k]for k in ('side','helper_drivers_enabled','maximum_main_local_offset_change_native_units','thumb_target_error_native_units')}for a in result])))


if __name__=='__main__':main()
