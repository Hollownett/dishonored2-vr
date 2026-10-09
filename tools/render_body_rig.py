"""Replay the owned native body/arm skinning offline and draw their attachment.

Private extracted assets stay in artifacts. This does not alter game memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from analyse_arm_mesh import decode_mesh
from rig_fields import GAME_HASH, affine_product


def skin_positions(vertices, palette):
    matrices=np.asarray(palette,dtype=float)
    if matrices.shape not in ((20,3,4),(69,3,4)) or not np.isfinite(matrices).all():
        raise ValueError('Require a finite owned body/arm palette')
    positions=np.asarray([v[0] for v in vertices],dtype=float)
    weights=np.asarray([list(v[1]) for v in vertices],dtype=float)
    joints=np.asarray([list(v[2]) for v in vertices],dtype=int)
    if not 0<len(vertices)<=500000 or positions.shape!=(len(vertices),3) or \
            weights.shape!=(len(vertices),8) or joints.shape!=weights.shape or \
            not np.isfinite(positions).all() or not np.isfinite(weights).all() or \
            np.any(weights<0) or np.any(weights>255) or np.any(weights.sum(axis=1)!=255) or \
            np.any((weights>0)&((joints<0)|(joints>=len(matrices)))):
        raise ValueError('Invalid native eight-weight vertex records')
    owned=np.where(weights>0,joints,0)
    homogeneous=np.concatenate((positions,np.ones((len(vertices),1))),axis=1)
    posed=np.einsum('vkij,vj->vki',matrices[owned],homogeneous)
    result=np.einsum('vki,vk->vi',posed,weights/255.)
    if not np.isfinite(result).all():raise ValueError('Skinning produced nonfinite points')
    return result


def model_to_world(points, rig):
    axes=np.asarray(rig['model_axis_rows'],dtype=float)
    scale=np.asarray(rig['model_scale'],dtype=float)
    origin=np.asarray(rig['model_origin'],dtype=float)
    if axes.shape!=(3,3) or scale.shape!=(3,) or origin.shape!=(3,) or \
            not np.isfinite(axes).all() or not np.isfinite(scale).all() or not np.isfinite(origin).all() or \
            np.any(scale<1e-4) or np.any(scale>16) or \
            np.max(np.abs(axes@axes.T-np.eye(3)))>.01 or np.linalg.det(axes)<.99:
        raise ValueError('Invalid native model frame')
    return (np.asarray(points)*scale)@axes+origin


def palettes(rig):
    count=rig['skeleton_joints'];skin=rig['skinning_layout']
    joints=rig['sampled_joints']
    if count not in (20,69) or rig['pose_state']!=3 or len(joints)!=count or \
            [j['joint'] for j in joints]!=list(range(count)) or \
            not skin['source_unchanged'] or not skin['output_unchanged'] or \
            skin['maximum_absolute_error'] is None or skin['maximum_absolute_error']>1e-4 or \
            len(skin['inverse_bind_rows'])!=count or len(skin['output_rows'])!=count:
        raise ValueError('Native read-only pose is incomplete or changed')
    rows=[]
    for joint,inverse in zip(joints,skin['inverse_bind_rows']):
        columns=joint['rotation_columns']+[joint['position']]
        animated=[columns[c][r] for r in range(3) for c in range(4)]
        rows.append(affine_product(animated,inverse))
    return np.asarray(rows).reshape(count,3,4),np.asarray(skin['output_rows']).reshape(count,3,4)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pose-report',type=Path,required=True)
    args=parser.parse_args();workspace=Path(__file__).resolve().parents[1]
    source=args.pose_report.resolve()
    if not source.is_relative_to(workspace):raise ValueError('Require a workspace pose report')
    report=json.loads(source.read_text(encoding='utf-8-sig'))
    if report['executable_sha256']!=GAME_HASH or report['game_memory_writes'] is not False:
        raise ValueError('Require the pinned read-only game evidence')
    body=report['rig_samples'][0];arms={x['side']:x for x in report['body_samples'][0]['arms']}
    if body['skeleton_joints']!=20 or set(arms)!=set(('left','right')) or \
            any(a['player']!=body['player'] or a['skeleton_joints']!=69 for a in arms.values()):
        raise ValueError('Body and arms do not belong to the same native player')
    root=workspace/'artifacts/research';body_root=root/'native-body-assets';arm_root=root/'native-arm-assets'
    body_hash=json.loads((body_root/'extraction.json').read_text())['asset_sha256']
    arm_hashes={e['side']:e['sha256'] for e in json.loads((arm_root/'extraction.json').read_text())['entries']}
    entries=[('body',body,body_root/'body-emily.bmd6model',body_hash)]
    entries.extend((side,arms[side],arm_root/(side+'-arm.bmd6model'),arm_hashes[side]) for side in ('left','right'))
    output=root/'native-body-replay';output.mkdir(exist_ok=True)
    meshes=[];stats=[];body_axes=np.asarray(body['model_axis_rows']);origin=np.asarray(body['model_origin'])
    for name,rig,path,digest in entries:
        data=path.read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Extracted private asset changed')
        mesh=decode_mesh(data,rig['skeleton_joints']);cpu,native=palettes(rig)
        if len(mesh['surfaces'])!=len(rig['geometry_layout']['surfaces']):raise ValueError('Surface owner count differs')
        maximum=0.;vertices=0;indices=0
        for surface,live in zip(mesh['surfaces'],rig['geometry_layout']['surfaces']):
            if surface['vertices_count']!=live['vertices'] or surface['indices_count']!=live['indices']:
                raise ValueError('Native surface counts differ')
            posed=skin_positions(surface['vertices'],cpu)
            actual=skin_positions(surface['vertices'],native)
            maximum=max(maximum,float(np.max(np.abs(posed-actual))))
            points=(model_to_world(posed,rig)-origin)@body_axes.T
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
            meshes.append((name,points,triangles))
            vertices+=len(points);indices+=triangles.size
        if maximum>1e-4:raise ValueError('Offline vertices disagree with captured native palette')
        stats.append(dict(part=name,vertices=vertices,indices=indices,
            maximum_cpu_vs_native_vertex_error_metres=maximum,asset_sha256=digest))
    spine=next(j for j in body['sampled_joints'] if j['name']=='center_spine03_jnt')
    spine_world=model_to_world([spine['position']],body)[0]
    attachment={side:float(np.linalg.norm(np.asarray(arms[side]['model_origin'])-spine_world)) for side in arms}
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    figure=plt.figure(figsize=(12,5));colours={'body':'#8094ab','left':'#38a0ae','right':'#bc7857'}
    for panel,(elevation,azimuth,label) in enumerate(((8,180,'Front'),(8,90,'Side'),(15,140,'Three quarter')),1):
        axis=figure.add_subplot(1,3,panel,**({'projection':'3d'} if panel==3 else {}))
        for name,points,triangles in meshes:
            if panel==3:axis.add_collection3d(Poly3DCollection(points[triangles],facecolors=colours[name],linewidths=0))
            else:axis.add_collection(PolyCollection(points[:,[1 if panel==1 else 0,2]][triangles],facecolors=colours[name],linewidths=0))
        if panel==3:
            axis.set(xlim=(-.85,.85),ylim=(-.85,.85),zlim=(-.1,1.8),title=label,xlabel='Forward (m)',ylabel='Left (m)',zlabel='Up (m)')
            axis.set_box_aspect((1.7,1.7,1.9));axis.view_init(elev=elevation,azim=azimuth)
        else:
            axis.set(xlim=(-.85,.85),ylim=(-.1,1.8),title=label,xlabel=('Left' if panel==1 else 'Forward')+' (m)',ylabel='Up (m)')
            axis.set_aspect('equal');axis.grid(alpha=.3)
    figure.suptitle('Native Emily body and arms — offline unchanged pose')
    figure.tight_layout();image=output/'native-body-and-arms.png';figure.savefig(image,dpi=160);plt.close(figure)
    result=dict(pose_report=str(source),game_memory_writes=False,private_diagnostic_only=True,
        snapshot_frame_synchronised=False,tracked_controller_alignment_verified=False,
        headset_body_visibility_verified=False,parts=stats,arm_root_to_spine3_distance_metres=attachment,
        image=str(image))
    (output/'replay.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result));return 0


if __name__=='__main__':raise SystemExit(main())
