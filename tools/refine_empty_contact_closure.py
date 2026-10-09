"""Private offline per-finger helper/splay refinement with compact closure retained."""
import argparse
import json
import re
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from author_empty_contact_closure import Closure,locals_of,interpolate_local
from author_native_hand_candidate import fk,render,matrices
from export_native_hand_poses import verified_arm

class RefinedClosure(Closure):
    def pose(self,parameters,index_release=False):
        if len(parameters)!=27 or not np.isfinite(parameters).all():raise ValueError('27 finite refinement parameters required')
        if np.any(np.abs(parameters[15:19])>8) or np.any(np.asarray(parameters)[19:]<.1) or np.any(np.asarray(parameters)[19:]>np.tile([1.35,1.8],4)):raise ValueError('Refinement exceeds offline bound')
        pose=super().pose(parameters[:15],index_release);local=locals_of(pose,self.parents)
        bases=[self.source[chain[0]][:3,3] for chain in self.main]
        forward=np.mean(bases,axis=0)-self.source[self.wrist][:3,3];width=bases[0]-bases[-1]
        normal=np.cross(forward,width);normal/=np.linalg.norm(normal)
        for digit,chain in enumerate(self.main):
            if index_release and digit==0:continue
            axis=self.source[chain[0]][:3,:3].T@normal
            native=self.native[chain[0]][:3,:3]
            spread=native@Rotation.from_rotvec(axis*np.deg2rad(parameters[15+digit])).as_matrix()@native.T
            local[chain[0]][:3,:3]=spread@local[chain[0]][:3,:3]
        for joint in self.finger_helpers:
            match=re.search(r'helper_(index|middle|ring|pinky)_(\d)_',self.names[joint])
            digit=('index','middle','ring','pinky').index(match[1]);segment=int(match[2]);top='_top_' in self.names[joint]
            response=parameters[19+digit*2+(0 if top else 1)]
            amount=0. if index_release and digit==0 else parameters[digit*3+segment]*response
            local[joint]=interpolate_local(self.native[joint],self.closed[joint],amount)
        return fk(local,self.parents)

def refine(model,parameters):
    def assess(trial):
        score,metric,pose=model.diagnostic(trial)
        _,released,_=model.diagnostic(trial,True)
        return score+.5*released['introduced_triangle_crossings'],metric,pose
    best=assess(parameters);log=[]
    # Genuine finger spacing, not mesh offsets. Choose both signs from exact contact measurements.
    for index in range(15,19):
        for value in (-8,-4,-2,2,4,8):
            trial=parameters.copy();trial[index]=value;result=assess(trial)
            if result[0]<best[0]:parameters=trial;best=result
    for step in (.25,.1,.05):
        for index in [0,3,6,9,*range(19,27),14]:
            base=parameters.copy();lo,hi=(.5,2.) if index<12 else (.5,1.1) if index==14 else (.1,1.35) if (index-19)%2==0 else (.1,1.8)
            for value in (base[index]-step,base[index]+step):
                if not lo<=value<=hi:continue
                trial=base.copy();trial[index]=value;result=assess(trial)
                if result[0]<best[0]:parameters=trial;best=result
        log.append(dict(step=step,crossings=best[1]['introduced_triangle_crossings'],score=best[0]))
    return parameters,best,log

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():raise ValueError('Use new private output directory')
    source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());args.output.mkdir(parents=True)
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';result=[]
    for item,old in zip(source['arms'],baseline['arms']):
        arm=verified_arm(report,item['side']);model=RefinedClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
        parameters=np.array(item['parameters'] if len(item['parameters'])==27 else item['parameters']+[0.]*4+[item['parameters'][12],item['parameters'][13]]*4)
        initial=model.diagnostic(parameters);parameters,best,log=refine(model,parameters);_,closed,pose=best
        _,released,index_pose=model.diagnostic(parameters,True)
        stats=render(arm,[('Native Empty source',matrices(arm)),('Compact precursor',[np.asarray(x)for x in item['authored_transform_matrices']]),('Refined authored closure',pose),('Index released',index_pose)],root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-authored-candidate.png'))
        result.append(dict(side=item['side'],parameters=parameters.tolist(),prior_compact_crossings=initial[1]['introduced_triangle_crossings'],closed=closed,index_released=released,search=log,evaluations=model.evaluations,authored_transform_matrices=[x.tolist()for x in pose],index_released_transform_matrices=[x.tolist()for x in index_pose],surfaces=stats))
        print(json.dumps(dict(side=item['side'],closed=closed['introduced_triangle_crossings'],released=released['introduced_triangle_crossings'],compact=closed['compact_closure_constraint'])),flush=True)
    source.update(candidate_origin='offline_authored_Empty_compact_contact_refinement',precursor=str(args.candidate),arms=result,production_activation=False)
    (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n')
if __name__=='__main__':main()
