"""Offline position-only garment patch correction; never touches native buffers.

Coincident bind positions are overlap correspondences, not assumed weld edges.
Harmonic displacement joins these correspondences to the body while a geodesic
band returns to the original arm surface. Native joint palettes/weights stay fixed.
"""
from pathlib import Path
import hashlib
import argparse
import json
import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra
from scipy.sparse.linalg import spsolve
from analyse_arm_mesh import decode_mesh
from build_player_asset_pack import parse_skeleton
from render_body_rig import skin_positions

ROOT=Path(__file__).resolve().parents[1]
RAW=ROOT/'artifacts/fullbody-assets/raw/models/characters/player'
OUT=ROOT/'artifacts/research/garment-continuity'

def load(character,part,joints):
    path=RAW/f'{character}/player_{part}_{character}.md6mesh'
    data=path.read_bytes(); decoded=decode_mesh(data,joints)
    vertices=[];triangles=[]
    for surface in decoded['surfaces']:
        triangles.extend(np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)+len(vertices))
        vertices.extend(surface['vertices'])
    return dict(vertices=vertices,points=np.asarray([v[0] for v in vertices]),
                weights=np.asarray([list(v[1]) for v in vertices])/255.,
                joints=np.asarray([list(v[2]) for v in vertices]),triangles=np.asarray(triangles),
                sha256=hashlib.sha256(data).hexdigest())

def matrices(values,inverse):
    return np.asarray(values).reshape(len(inverse),4,4).transpose(0,2,1)@inverse

def stats(values):
    return dict(median=float(np.median(values)),p95=float(np.percentile(values,95)),maximum=float(np.max(values)))

def topology(mesh,seam,band):
    points,groups=np.unique(np.round(mesh['points'],7),axis=0,return_inverse=True)
    faces=groups[mesh['triangles']]
    edges=np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[2,0]]])
    edges=np.unique(np.sort(edges,axis=1),axis=0)
    edges=edges[edges[:,0]!=edges[:,1]]
    lengths=np.linalg.norm(points[edges[:,0]]-points[edges[:,1]],axis=1)
    rows=np.r_[edges[:,0],edges[:,1]];cols=np.r_[edges[:,1],edges[:,0]]
    graph=coo_matrix((np.r_[lengths,lengths],(rows,cols)),shape=(len(points),len(points))).tocsr()
    anchors=np.unique(groups[seam])
    distance=dijkstra(graph,indices=anchors,min_only=True)
    # Uniform graph Laplacian avoids tiny bevel edges dominating the field.
    adjacency=coo_matrix((np.ones(len(rows)),(rows,cols)),shape=graph.shape).tocsr()
    lap=coo_matrix((np.asarray(adjacency.sum(axis=1)).ravel(),(np.arange(len(points)),np.arange(len(points)))),shape=graph.shape).tocsr()-adjacency
    unknown=np.flatnonzero((distance<band)&~np.isin(np.arange(len(points)),anchors))
    return groups,anchors,unknown,lap,edges,distance

def distortion(before,after,faces,affected):
    p=before[faces];q=after[faces]
    a=np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]);b=np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0])
    al=np.linalg.norm(a,axis=1);bl=np.linalg.norm(b,axis=1)
    valid=(al>1e-10)&np.any(affected[faces],axis=1)
    cosine=np.clip((a[valid]*b[valid]).sum(axis=1)/np.maximum(al[valid]*bl[valid],1e-20),-1,1)
    edges=np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[2,0]]])
    edges=np.unique(np.sort(edges,axis=1),axis=0);edges=edges[np.any(affected[edges],axis=1)]
    old=np.linalg.norm(before[edges[:,0]]-before[edges[:,1]],axis=1)
    new=np.linalg.norm(after[edges[:,0]]-after[edges[:,1]],axis=1)
    ratios=new[old>1e-8]/old[old>1e-8]
    area=bl[valid]/al[valid]
    return dict(affected_triangles=int(valid.sum()),normal_change_degrees=stats(np.degrees(np.arccos(cosine))),
                reversed_relative_to_original_normals=int((cosine<0).sum()),
                area_ratio_minimum=float(area.min()),area_ratio=stats(area),
                edge_ratio_minimum=float(ratios.min()),edge_ratio=stats(ratios))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--boundary-only',action='store_true',help='Anchor shared arm boundary positions rather than the whole overlap patch')
    args=parser.parse_args()
    cases=json.loads((ROOT/'build/garment-continuity-palettes.json').read_text())
    body_skeleton=parse_skeleton(RAW/'emily/player_body_emily.md6skl')
    rows=[]
    for character in ('emily','corvo'):
        body=load(character,'body',20);tree=cKDTree(body['points'])
        for side,name in enumerate(('left','right')):
            arm=load(character,f'{name}arm',69)
            skeleton=parse_skeleton(RAW/f'emily/player_{name}arm_emily.md6skl')
            world=(np.c_[arm['points'],np.ones(len(arm['points']))]@body_skeleton['model'][4].T)[:,:3]
            distances,nearest=tree.query(world);seam=np.flatnonzero(distances<1e-5)
            if args.boundary_only:
                _,groups=np.unique(np.round(arm['points'],7),axis=0,return_inverse=True)
                faces=groups[arm['triangles']]
                edges=np.sort(np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[2,0]]]),axis=1)
                unique,counts=np.unique(edges,axis=0,return_counts=True)
                boundary=np.unique(unique[counts==1])
                seam=seam[np.isin(groups[seam],boundary)]
            for band in (.06,.12,.20):
                groups,anchors,unknown,lap,edges,distance=topology(arm,seam,band)
                for case in (c for c in cases if c['character']==character):
                    body_pose=skin_positions(body['vertices'],matrices(case['body'],body_skeleton['inverse'])[:,:3,:])
                    palette=matrices(case['arms'][side],skeleton['inverse'])
                    before=skin_positions(arm['vertices'],palette[:,:3,:])
                    displacement=np.zeros((lap.shape[0],3));counts=np.zeros(lap.shape[0])
                    np.add.at(displacement,groups[seam],body_pose[nearest[seam]]-before[seam])
                    np.add.at(counts,groups[seam],1)
                    displacement[anchors]/=counts[anchors,None]
                    if len(unknown):
                        displacement[unknown]=spsolve(lap[unknown][:,unknown],-lap[unknown][:,anchors]@displacement[anchors])
                    target=before+displacement[groups]
                    blended=np.einsum('vk,vkij->vij',arm['weights'],palette[arm['joints']])
                    condition=np.linalg.cond(blended[:,:3,:3])
                    # Position-only replacement: inverse blended native shader map.
                    corrected=np.linalg.solve(blended[:,:3,:3],(target-blended[:,:3,3])[...,None])[...,0]
                    replay=np.einsum('vij,vj->vi',blended[:,:3,:3],corrected)+blended[:,:3,3]
                    affected=np.linalg.norm(displacement[groups],axis=1)>1e-9
                    row={k:v for k,v in case.items() if k not in ('body','arms')}
                    row.update(side=name,band_metres=band,shared_patch_vertices=len(seam),affected_vertices=int(affected.sum()),
                               body_sha256=body['sha256'],arm_sha256=arm['sha256'],
                               remaining_patch_distance_metres=stats(np.linalg.norm(replay[seam]-body_pose[nearest[seam]],axis=1)),
                               source_position_change_metres=stats(np.linalg.norm(corrected-arm['points'],axis=1)[affected]),
                               shader_replay_error_maximum=float(np.linalg.norm(replay-target,axis=1).max()),
                               blended_matrix_condition_maximum=float(condition.max()),
                               outside_band_position_error_maximum=float(np.linalg.norm(replay[distance[groups]>=band]-before[distance[groups]>=band],axis=1).max()),
                               distortion=distortion(before,replay,arm['triangles'],affected))
                    rows.append(row)
    OUT.mkdir(exist_ok=True)
    filename='structural-boundary-band.json' if args.boundary_only else 'structural-seam-band.json'
    (OUT/filename).write_text(json.dumps(dict(scope='offline shared arm boundary' if args.boundary_only else 'offline shared overlap patch',
                                           native_buffer_writes=False,shader_normal_semantics_verified=False,cases=rows),indent=2))
    for character in ('emily','corvo'):
        for band in (.06,.12,.20):
            selected=[r for r in rows if r['character']==character and r['band_metres']==band]
            print(character,band,'gap',max(r['remaining_patch_distance_metres']['maximum'] for r in selected),
                  'edge',max(r['distortion']['edge_ratio']['maximum'] for r in selected),
                  'normal',max(r['distortion']['normal_change_degrees']['maximum'] for r in selected),
                  'reversed',max(r['distortion']['reversed_relative_to_original_normals'] for r in selected))

if __name__=='__main__': main()
