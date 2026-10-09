"""Private bounded multi-axis skin-helper correction, measured through partial curl."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from refine_empty_contact_closure import RefinedClosure
from author_empty_contact_closure import locals_of,interpolate_local,DIGITS
from author_native_hand_candidate import fk,skin
from check_authored_hand_contacts import evaluate
from render_body_rig import skin_positions
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

class WebClosure(RefinedClosure):
    def pose(self,parameters,index_release=False):
        p=np.asarray(parameters)
        if len(p)!=39 or not np.isfinite(p).all() or np.any(np.abs(p[27:])>15):raise ValueError('39 bounded finite parameters required')
        local=locals_of(super().pose(p[:27],index_release),self.parents)
        bases=np.array([self.source[c[0]][:3,3]for c in self.main]);forward=bases.mean(axis=0)-self.source[self.wrist][:3,3];forward/=np.linalg.norm(forward)
        normal=np.cross(forward,bases[0]-bases[-1]);normal/=np.linalg.norm(normal)
        for digit,chain in enumerate(self.main):
            if index_release and digit==0:continue
            # Axial finger twist couples skin web spacing without changing joint offsets.
            for joint in [chain[0],*[j for j in self.finger_helpers if ('helper_'+('index','middle','ring','pinky')[digit]+'_')in self.names[j]]]:
                helper=joint!=chain[0];top='_top_'in self.names[joint]
                degrees=p[27+digit*3+(1 if top else 2)]if helper else p[27+digit*3]
                world_axis=normal if helper else forward
                parent=self.parents[joint];parent_axis=self.source[parent][:3,:3].T@world_axis
                local[joint][:3,:3]=Rotation.from_rotvec(parent_axis*np.deg2rad(degrees)).as_matrix()@local[joint][:3,:3]
        return fk(local,self.parents)

def assess(model,p):
    base,closed,pose=model.diagnostic(p);_,release,_=model.diagnostic(p,True)
    local=locals_of(pose,model.parents);sweep=[]
    for amount in (.25,.5,.75):
        blended=fk([interpolate_local(a,b,amount)for a,b in zip(model.native,local)],model.parents);pal=skin(model.arm,blended)
        after=np.concatenate([skin_positions(v,pal)[t]for v,t in model.surfaces]);sweep.append(evaluate(model.before,after,model.ids)['introduced_triangle_crossings'])
    # Keep compact curvature, and penalize every transition rather than endpoint alone.
    return base+.5*release['introduced_triangle_crossings']+.5*sum(sweep),closed,release,pose,sweep

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():raise ValueError('Use new output')
    source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());args.output.mkdir(parents=True)
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';result=[]
    for item,old in zip(source['arms'],baseline['arms']):
        arm=verified_arm(report,item['side']);model=WebClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));p=np.array(item['parameters']+[0.]*12);best=assess(model,p);log=[]
        for step in (6.,3.,1.5):
            for index in range(27,39):
                seed=p.copy()
                for value in (seed[index]-step,seed[index]+step):
                    if abs(value)>15:continue
                    trial=seed.copy();trial[index]=value;r=assess(model,trial)
                    if r[0]<best[0]:p=trial;best=r
            log.append(dict(step=step,score=best[0],closed=best[1]['introduced_triangle_crossings'],released=best[2]['introduced_triangle_crossings'],partial=best[4]));print(json.dumps(dict(side=item['side'],**log[-1])),flush=True)
        _,closed,released,pose,sweep=best;indexpose=model.pose(p,True)
        for state,mat in [('closed',pose),('index-released',indexpose)]:render_close(arm,mat,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+state+'.png'),state)
        result.append(dict(side=item['side'],parameters=p.tolist(),closed=closed,index_released=released,partial_crossings=sweep,search=log,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in indexpose]))
    source.update(precursor=str(args.candidate),authoring_model='WebClosure39',arms=result,production_activation=False)
    (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
