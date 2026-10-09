"""Offline exact native bind/mesh palm audit; no process access or runtime writes."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from analyse_arm_mesh import decode_mesh
from rig_fields import GAME_HASH


def unit(v):
    size=np.linalg.norm(v)
    if not np.isfinite(size) or size<1e-8:raise ValueError('Degenerate axis')
    return v/size


def ray(points,triangles,start,direction):
    t=points[triangles];e1=t[:,1]-t[:,0];e2=t[:,2]-t[:,0]
    h=np.cross(np.broadcast_to(direction,e2.shape),e2);a=np.einsum('ij,ij->i',e1,h)
    okay=np.abs(a)>1e-10;factor=np.zeros(len(a));factor[okay]=1/a[okay]
    delta=start-t[:,0];u=factor*np.einsum('ij,ij->i',delta,h);q=np.cross(delta,e1)
    v=factor*np.einsum('ij,j->i',q,direction);distance=factor*np.einsum('ij,ij->i',e2,q)
    okay &= (u>=0)&(v>=0)&(u+v<=1)&(distance>0)&(distance<.1)
    ids=np.where(okay)[0]
    if not len(ids):raise ValueError('No palm surface intersection')
    i=int(ids[np.argmin(distance[ids])]);return {'distance_native_units':float(distance[i]),'triangle':i,'point':(start+direction*distance[i]).tolist()}


def audit(arm,mesh):
    side=arm['side'];names=arm['joint_names'];inverse=arm['skinning_layout']['inverse_bind_rows']
    if arm['skeleton_joints']!=69 or len(names)!=69 or len(inverse)!=69:raise ValueError('Require complete native69 bind')
    bind=[];source=[]
    for rows,node in zip(inverse,arm['sampled_joints']):
        inv=np.eye(4);inv[:3,:]=np.asarray(rows).reshape(3,4);b=np.linalg.inv(inv)
        if np.max(np.abs(b[:3,:3].T@b[:3,:3]-np.eye(3)))>.01 or np.linalg.det(b[:3,:3])<.99:raise ValueError('Nonrigid bind')
        bind.append(b);m=np.eye(4);m[:3,:3]=np.asarray(node['rotation_columns']).T;m[:3,3]=node['position'];source.append(m)
    wrist=names.index(side+'_wrist_jnt');knuckles=[names.index(side+'_'+d+'_02_jnt') for d in ('index','medium','ring','pinky')]
    def frame(matrices):
        w=matrices[wrist][:3,3];k=np.asarray([matrices[j][:3,3] for j in knuckles]);f=k.mean(axis=0)-w;width=k[0]-k[-1]
        return w,f,unit(width),unit(np.cross(f,unit(width)))
    w,f,width,normal=frame(bind);_,_,_,source_normal=frame(source);curvature=[]
    for digit in ('index','medium','ring','pinky'):
        j=[names.index(side+'_'+digit+'_0'+str(i)+'_jnt') for i in (2,3,4)]
        delta=unit(source[j[2]][:3,3]-source[j[1]][:3,3])-unit(source[j[1]][:3,3]-source[j[0]][:3,3])
        curvature.append(float(np.dot(delta,source_normal)))
    expected=1 if side=='left' else -1
    if not all(expected*c>.02 for c in curvature):raise ValueError('Native source cannot corroborate anatomical normal sign')
    palm=normal*expected;centre=w+.5*f
    surfaces=[s for s in mesh['surfaces'] if s['material'].endswith('/player_emily_hands.material')]
    if len(surfaces)!=1:raise ValueError('Require exact native hand-flesh surface')
    surface=surfaces[0];points=np.asarray([v[0] for v in surface['vertices']]);tri=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
    grid=[]
    for v in (-.015,0,.015):
        for u in (-.015,0,.015):grid.append(dict(width_offset=u,length_offset=v,**ray(points,tri,centre+width*u+unit(f)*v,palm)))
    triangle_points=points[tri];centroids=triangle_points.mean(axis=1);delta=centroids-centre
    area=np.linalg.norm(np.cross(triangle_points[:,1]-triangle_points[:,0],triangle_points[:,2]-triangle_points[:,0]),axis=1)*.5
    # A reproducible central patch, not the anatomically measured closed-fist centroid.
    selected=(np.abs(delta@width)<.015)&(np.abs(delta@unit(f))<.015)&(delta@palm>0)&(area>1e-12)
    if selected.sum()<4:raise ValueError('Insufficient central palm patch')
    patch=np.average(centroids[selected],axis=0,weights=area[selected]);patch_delta=patch-centre
    return {'side':side,'wrist':w.tolist(),'estimated_skeletal_centroid':centre.tolist(),'palm_length_native_units':float(np.linalg.norm(f)),
            'palm_width_native_units':float(np.linalg.norm(bind[knuckles[0]][:3,3]-bind[knuckles[-1]][:3,3])),
            'xr_minus_z_direction':width.tolist(),'xr_plus_x_direction':normal.tolist(),'anatomical_outward_palm_direction':palm.tolist(),
            'native_idle_curvature_dot_xr_plus_x':curvature,'normal_sign_matches_spec':True,'hand_surface_material':surface['material'],
            'palm_surface_central_ray':ray(points,tri,centre,palm),'dorsal_surface_central_ray':ray(points,tri,centre,-palm),'central_30mm_grid':grid,
            'central_surface_patch':{'selection':'triangle centroids in central30mm square, outward palm half; hand flesh only',
                'triangles':int(selected.sum()),'area_native_units_squared':float(area[selected].sum()),'area_weighted_centroid':patch.tolist(),
                'delta_from_skeletal_centroid_native_units':patch_delta.tolist(),'delta_forward_left_up_native_units':[float(patch_delta@width),float(patch_delta@(-normal)),float(patch_delta@unit(np.cross(width,-normal)))]},
            'surface_point_is_controller_grip_origin_proven':False,'production_translation_correction':False}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pose-report',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():raise ValueError('Refuse overwrite')
    data=json.loads(args.pose_report.read_text(encoding='utf-8-sig'))
    if data['executable_sha256']!=GAME_HASH or data['game_memory_writes'] is not False:raise ValueError('Require pinned read-only source')
    root=Path('artifacts/research/native-arm-assets');extraction=json.loads((root/'extraction.json').read_text())['entries'];reports=[]
    for arm in data['body_samples'][0]['arms']:
        source=root/(arm['side']+'-arm.bmd6model');raw=source.read_bytes();sha=hashlib.sha256(raw).hexdigest()
        if not any(e['side']==arm['side'] and e['sha256']==sha for e in extraction):raise ValueError('Mesh provenance mismatch')
        reports.append(dict(mesh_sha256=sha,**audit(arm,decode_mesh(raw))))
    if sorted(a['side'] for a in reports)!=['left','right']:raise ValueError('Require both native hands')
    result={'executable_sha256':GAME_HASH,'pose_report':str(args.pose_report),'pose_sha256':hashlib.sha256(args.pose_report.read_bytes()).hexdigest(),
            'arms':reports,'game_memory_writes':False,'engine_calls':False,'anatomical_sign_is_geometry_inference':True,
            'native_units_per_metre_assumed_for_mm_summary':1,'native_unit_conversion_independently_verified':False,
            'controller_physical_alignment_proven':False,'wizard_measures':['grip_position_span','eye_to_shoulder_position_drop','optional_floor_eye_height'],
            'wizard_does_not_measure':['wrist_orientation_alignment','controller_to_palm_translation','hand_size','finger_lengths']}
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'output':str(args.output),'sides':len(reports),'production_translation_correction':False}))


if __name__=='__main__':main()
