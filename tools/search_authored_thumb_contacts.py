"""Bounded offline artist thumb search against real flesh/glove triangle crossings.

Generates parameters only; no captured-pose labels, archive or production activation.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import numpy as np
from scipy.optimize import differential_evolution
from scipy.spatial import cKDTree
from analyse_arm_mesh import decode_mesh
from author_native_hand_candidate import authored,skin
from check_authored_hand_contacts import crossing
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions


def model(arm,root):
    vertices=[];triangles=[];regions=[]
    for surface in decode_mesh((root/(arm['side']+'-arm.bmd6model')).read_bytes())['surfaces']:
        if not any(s in surface['material']for s in ('player_emily_hands.material','player_emily_gloves.material')):continue
        offset=len(vertices);vertices.extend(surface['vertices']);t=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)+offset;triangles.extend(t)
        for tri in t:
            weights=Counter()
            for vertex in tri:
                for joint,weight in zip(vertices[vertex][2],vertices[vertex][1]):
                    if not weight:continue
                    name=arm['joint_names'][joint];region=next((d for d in ('thumb','index','medium','ring','pinky')if d in name),None)
                    weights[region or ('medium'if 'middle'in name else 'palm')]+=weight
            regions.append(weights.most_common(1)[0][0])
    palette,_=palettes(arm);before=skin_positions(vertices,palette)[np.array(triangles)]
    return vertices,np.array(triangles),np.array(regions),before


def thumb_score(before,after,regions):
    centres=after.mean(axis=1);radius=np.linalg.norm(after-centres[:,None,:],axis=2).max(axis=1)
    thumbs=np.flatnonzero(regions=='thumb');near=cKDTree(centres).query_ball_point(centres[thumbs],radius[thumbs]+radius.max())
    a=np.repeat(thumbs,[len(n)for n in near]);b=np.concatenate(near).astype(int)
    valid=(a!=b)&((regions[b]!='thumb')|(a<b))&(np.linalg.norm(centres[a]-centres[b],axis=1)<=radius[a]+radius[b])
    a,b=a[valid],b[valid]
    valid=np.all(after[a].max(axis=1)>=after[b].min(axis=1),axis=1)&np.all(after[b].max(axis=1)>=after[a].min(axis=1),axis=1)
    a,b=a[valid],b[valid]
    same=np.any(np.linalg.norm(before[a,:,None,:]-before[b,None,:,:],axis=3)<2e-5,axis=(1,2));a,b=a[~same],b[~same]
    introduced=crossing(after[a],after[b])&~crossing(before[a],before[b]);a,b=a[introduced],b[introduced]
    depth=np.zeros(len(a))
    for tri,plane in ((after[a],after[b]),(after[b],after[a])):
        normal=np.cross(plane[:,1]-plane[:,0],plane[:,2]-plane[:,0]);normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
        signed=np.einsum('vki,vi->vk',tri-plane[:,0,None,:],normal)
        depth=np.maximum(depth,np.maximum(0.,np.minimum(signed.max(axis=1),-signed.min(axis=1))))
    return float(np.sum(depth*depth)*1e6+len(a)*.02),int(len(a)),float(depth.max(initial=0.))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--iterations',type=int,default=12);args=parser.parse_args()
    if not 1<=args.iterations<=30:parser.error('Require bounded1..30iterations')
    report=json.loads(args.source.read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';results=[];parameters={}
    for side in ('left','right'):
        arm=verified_arm(report,side);vertices,triangles,regions,before=model(arm,root)
        initial,evidence=authored(arm,driven_helpers=True,relaxed_knuckles=True,thumb_clearance=.025)
        initial_parameters=np.array(evidence['thumb_opposition_rotation_vector_radians']+evidence['thumb_flex_radians'])
        end=arm['joint_names'].index(side+'_thumb_end_jnt');goal=initial[end][:3,3];calls=0;best=None
        def objective(values):
            nonlocal calls,best
            score=0.;states=[]
            for amount in (1.,0.):
                pose,_=authored(arm,amount,True,True,.025,False,values);after=skin_positions(vertices,skin(arm,pose))[triangles]
                contact,count,depth=thumb_score(before,after,regions);score+=contact;states.append(dict(index_curl=amount,contacts=count,max_plane_overlap_proxy_native_units=depth))
            # Keep the thumb at the grasp, rather than solving collisions by opening it.
            displacement=np.linalg.norm(pose[end][:3,3]-goal);score+=max(0.,displacement-.008)**2*1e5
            score+=np.sum((values-initial_parameters)**2)*.2;calls+=1
            if best is None or score<best['score']:
                best=dict(score=float(score),parameters=values.tolist(),states=states,tip_displacement_native_units=float(displacement))
            return score
        bounds=[(-.2,.7),(-.35,.35),(-.65,.25),(.05,1.),(.05,1.)]
        initial_score=objective(initial_parameters)
        fit=differential_evolution(objective,bounds,popsize=5,maxiter=args.iterations,seed=7,polish=False,x0=initial_parameters,workers=1)
        parameters[side]=best['parameters'];results.append(dict(side=side,initial_score=initial_score,calls=calls,best=best,solver_success=bool(fit.success)))
        print(json.dumps(results[-1]),flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(dict(pose_origin='offline_authored_contact_search',
        captured_fist=False,production_activation=False,no_clipping_certified=False,parameters=parameters,results=results),indent=2)+'\n')


if __name__=='__main__':main()
