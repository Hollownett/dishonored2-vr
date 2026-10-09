"""Offline native Empty-hand authoring by bounded skin-contact search.
No game access, production bank export, engine calls or activation.
"""
import argparse
import copy
import hashlib
import json
import math
import re
import time
from collections import Counter
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from author_native_hand_candidate import matrices,fk,skin,render
from analyse_arm_mesh import decode_mesh
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions
from check_authored_hand_contacts import evaluate

DIGITS=('index','medium','ring','pinky')
def locals_of(pose,parents):
    return [p.copy() if parents[i]<0 else np.linalg.inv(pose[parents[i]])@p for i,p in enumerate(pose)]

def interpolate_local(before,after,amount):
    result=before.copy()
    result[:3,:3]=before[:3,:3]@Rotation.from_rotvec(Rotation.from_matrix(before[:3,:3].T@after[:3,:3]).as_rotvec()*amount).as_matrix()
    result[:3,3]=before[:3,3]+amount*(after[:3,3]-before[:3,3])
    return result

class Closure:
    def __init__(self,arm,baseline,asset):
        self.arm=arm;self.names=arm['joint_names'];self.parents=arm['skeleton_layout']['parent_indices'];self.source=matrices(arm)
        self.native=locals_of(self.source,self.parents);self.closed=locals_of([np.asarray(x) for x in baseline],self.parents)
        self.wrist=self.names.index(arm['side']+'_wrist_jnt')
        self.main=[[self.names.index(f'{arm["side"]}_{digit}_0{s}_jnt') for s in (2,3,4)] for digit in DIGITS]
        self.finger_helpers=[i for i,n in enumerate(self.names) if 'helper' in n and any(d in n for d in ('index','middle','ring','pinky'))]
        self.thumb=[i for i,n in enumerate(self.names) if 'thumb' in n or 'skinpalm' in n or 'flexmuscle' in n or '1stdorsal' in n or 'tendon' in n]
        original,_=palettes(arm);self.surfaces=[];self.ids=[];before=[]
        origin=self.source[self.wrist][:3,3]
        for number,surface in enumerate(decode_mesh(asset.read_bytes())['surfaces']):
            if not any(n in surface['material'] for n in ('player_emily_hands.material','player_emily_gloves.material')):continue
            vertices=surface['vertices'];points=skin_positions(vertices,original)
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
            visible=np.all(np.linalg.norm(points[triangles]-origin,axis=2)<.25,axis=1);triangles=triangles[visible]
            self.surfaces.append((vertices,triangles))
            for tri in triangles:
                regions=Counter()
                for vertex in tri:
                    for joint,weight in zip(vertices[vertex][2],vertices[vertex][1]):
                        if not weight:continue
                        name=self.names[joint];region=next((d for d in ('thumb',*DIGITS) if d in name),None)
                        if region is None:region='medium' if 'middle' in name else 'palm'
                        regions[region]+=weight
                self.ids.append(dict(surface=number,vertices=[int(x) for x in tri],region=regions.most_common(1)[0][0]))
            before.extend(points[triangles])
        self.before=np.asarray(before);self.evaluations=0
    def pose(self,parameters,index_release=False):
        if len(parameters)!=15 or not np.isfinite(parameters).all():raise ValueError('15 finite bounded authoring parameters required')
        if np.any(np.asarray(parameters)[:12]<np.tile([.5,.45,.5],4)) or np.any(np.asarray(parameters)[:12]>np.tile([2.,1.1,1.6],4)) or not .1<=parameters[12]<=1.8 or not .1<=parameters[13]<=1.8 or not .5<=parameters[14]<=1.1:raise ValueError('Authoring parameter outside bound')
        local=[x.copy() for x in self.native]
        for digit,chain in enumerate(self.main):
            for segment,joint in enumerate(chain):
                amount=0. if index_release and digit==0 else parameters[digit*3+segment]
                local[joint]=interpolate_local(self.native[joint],self.closed[joint],amount)
        for joint in self.finger_helpers:
            match=re.search(r'helper_(index|middle|ring|pinky)_(\d)_',self.names[joint])
            if not match:raise ValueError('Unknown native finger helper hierarchy')
            digit=('index','middle','ring','pinky').index(match[1]);segment=int(match[2])
            amount=0. if index_release and digit==0 else parameters[digit*3+segment]*parameters[12 if '_top_' in self.names[joint] else 13]
            local[joint]=interpolate_local(self.native[joint],self.closed[joint],amount)
        for joint in self.thumb:local[joint]=interpolate_local(self.native[joint],self.closed[joint],parameters[14])
        return fk(local,self.parents)
    def diagnostic(self,parameters,index_release=False):
        pose=self.pose(parameters,index_release);palette=skin(self.arm,pose)
        after=np.concatenate([skin_positions(v,palette)[tri] for v,tri in self.surfaces])
        measured=evaluate(self.before,after,self.ids);self.evaluations+=1
        # Prevent the optimizer from winning simply by opening the fingers.
        ratios=[]
        for digit,chain in zip(DIGITS,self.main):
            end=self.names.index(self.arm['side']+'_'+digit+'_end_jnt')
            ratios.append(float(np.linalg.norm(pose[end][:3,3]-pose[chain[0]][:3,3])/np.linalg.norm(self.source[end][:3,3]-self.source[chain[0]][:3,3])))
        measured['fingertip_to_MCP_distance_ratios_vs_idle']=dict(zip(DIGITS,ratios))
        measured['compact_closure_constraint']=all(r<=.51 for r in ratios)
        penalty=float(np.maximum(0.,np.asarray(ratios)-.5).sum())*2000
        return measured['introduced_triangle_crossings']+penalty,measured,pose

def search(model):
    start=time.monotonic();best=None;log=[]
    for amounts in ((1.,1.,1.),(1.25,.8,1.2),(1.5,.75,1.3),(1.75,.7,1.4)):
        parameters=np.array(list(amounts)*4+[1.,1.,1.])
        score,metric,_=model.diagnostic(parameters)
        log.append(dict(stage='uniform',amounts=amounts,score=score,crossings=metric['introduced_triangle_crossings']))
        if best is None or score<best[0]:best=(score,parameters,metric)
    # Bounded coordinate search: each anatomical curl and top/bottom helper response.
    for step in (.2,.1,.05):
        for index in range(15):
            lower,upper=((.5,2.),(.45,1.1),(.5,1.6))[index%3] if index<12 else (.5,1.1) if index==14 else (.1,1.8)
            base=best[1].copy()
            for amount in (base[index]-step,base[index]+step):
                if not lower<=amount<=upper:continue
                parameters=base.copy();parameters[index]=amount;score,metric,_=model.diagnostic(parameters)
                if score<best[0]:best=(score,parameters,metric)
        log.append(dict(stage='coordinate',step=step,score=best[0],crossings=best[2]['introduced_triangle_crossings']))
    return best[1],log,time.monotonic()-start

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():raise ValueError('Use a new private output directory')
    baseline=json.loads(args.baseline.read_text());source=Path(baseline['source_readonly_report']);report=json.loads(source.read_text());args.output.mkdir(parents=True)
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';result=[]
    for item in baseline['arms']:
        side=item['side'];arm=verified_arm(report,side);model=Closure(arm,item['authored_transform_matrices'],root/(side+'-arm.bmd6model'))
        before=model.diagnostic(np.ones(15))[1];parameters,log,seconds=search(model)
        _,closed,pose=model.diagnostic(parameters);_,released,index_pose=model.diagnostic(parameters,True)
        stats=render(arm,[('Native Empty source',matrices(arm)),('Prior rejected closure',[np.asarray(x) for x in item['authored_transform_matrices']]),('Contact-optimized closure',pose),('Index released',index_pose)],root/(side+'-arm.bmd6model'),args.output/(side+'-authored-candidate.png'))
        result.append(dict(side=side,parameters=parameters.tolist(),search=log,search_seconds=seconds,evaluations=model.evaluations,prior_crossings=before['introduced_triangle_crossings'],closed=closed,index_released=released,authored_transform_matrices=[x.tolist() for x in pose],index_released_transform_matrices=[x.tolist() for x in index_pose],surfaces=stats))
        print(json.dumps(dict(side=side,prior=before['introduced_triangle_crossings'],closed=closed['introduced_triangle_crossings'],released=released['introduced_triangle_crossings'],seconds=seconds)),flush=True)
    data=dict(pose_origin='offline_authored_Empty_closure_contact_search',source_readonly_report=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),baseline=str(args.baseline),baseline_sha256=hashlib.sha256(args.baseline.read_bytes()).hexdigest(),game_memory_access=False,game_memory_writes=False,captured_fist=False,production_archive_exported=False,production_activation=False,visual_review_required=True,headset_acceptance=False,arms=result)
    (args.output/'authored-candidate.json').write_text(json.dumps(data,indent=2)+'\n')
if __name__=='__main__':main()
