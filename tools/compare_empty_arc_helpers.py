"""Offline geometric arc helper driver comparison; not native-animation evidence."""
import argparse,json,re
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from refine_empty_web_closure import WebClosure,assess
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import fk
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

class ArcWebClosure(WebClosure):
    def pose(self,parameters,index_release=False):
        local=locals_of(super().pose(parameters,index_release),self.parents)
        for joint in self.finger_helpers:
            match=re.search(r'helper_(index|middle|ring|pinky)_(\d)_',self.names[joint]);digit=('index','middle','ring','pinky').index(match[1]);child=self.main[digit][int(match[2])];top='_top_'in self.names[joint]
            if index_release and digit==0:local[joint]=self.native[joint].copy();continue
            if self.parents[child]!=self.parents[joint]:raise ValueError('Helper/main sibling relationship changed')
            delta=local[child][:3,:3]@self.native[child][:3,:3].T
            response=parameters[19+digit*2+(0 if top else 1)]*(.7 if top else .5)
            portion=Rotation.from_rotvec(Rotation.from_matrix(delta).as_rotvec()*response).as_matrix()
            local[joint]=self.native[joint].copy();local[joint][:3,:3]=portion@local[joint][:3,:3]
            if not top:
                pivot=self.native[child][:3,3];local[joint][:3,3]=pivot+portion@(local[joint][:3,3]-pivot)
            # Independent palm-normal correction remains bounded and rotational only.
            bases=np.array([self.source[c[0]][:3,3]for c in self.main]);normal=np.cross(bases.mean(axis=0)-self.source[self.wrist][:3,3],bases[0]-bases[-1]);normal/=np.linalg.norm(normal)
            axis=self.source[self.parents[joint]][:3,:3].T@normal;degrees=parameters[27+digit*3+(1 if top else 2)]
            local[joint][:3,:3]=Rotation.from_rotvec(axis*np.deg2rad(degrees)).as_matrix()@local[joint][:3,:3]
        return fk(local,self.parents)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():raise ValueError('Use new output')
    source=json.loads(args.candidate.read_text());report=json.loads(Path(source['source_readonly_report']).read_text());baseline=json.loads(Path(source['baseline']).read_text());args.output.mkdir(parents=True);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';out=[]
    for item,old in zip(source['arms'],baseline['arms']):
        arm=verified_arm(report,item['side']);model=ArcWebClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));parameters=np.array(item['parameters']+[0.]*(39-len(item['parameters'])));score,closed,released,pose,partial=assess(model,parameters);index=model.pose(parameters,True)
        render_close(arm,pose,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-closed.png'),'Geometric arc helper trial')
        out.append(dict(side=item['side'],parameters=parameters.tolist(),closed=closed,index_released=released,partial_crossings=partial,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in index]))
        print(json.dumps(dict(side=item['side'],closed=closed['introduced_triangle_crossings'],released=released['introduced_triangle_crossings'],partial=partial)),flush=True)
    source.update(authoring_model='ArcWebClosure39',arms=out,production_activation=False)
    (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
