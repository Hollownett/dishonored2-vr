"""Private bounded thumb offsets scored on actual skin contacts at all grip stages."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from refine_empty_web_closure import WebClosure,assess
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import fk
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

class OffsetThumbClosure(WebClosure):
 def pose(self,p,index_release=False):
  p=np.asarray(p)
  if len(p)!=45 or not np.isfinite(p).all()or np.any(np.abs(p[39:])>18):raise ValueError('45 finite bounded offset parameters required')
  local=locals_of(super().pose(p[:39],index_release),self.parents);bases=np.array([self.source[c[0]][:3,3]for c in self.main]);forward=bases.mean(axis=0)-self.source[self.wrist][:3,3];forward/=np.linalg.norm(forward);width=bases[0]-bases[-1];width/=np.linalg.norm(width);normal=np.cross(forward,width);normal/=np.linalg.norm(normal)
  for k,j in enumerate([self.names.index(self.arm['side']+'_thumb_0'+str(i)+'_jnt')for i in (1,2,3)]):
   parent=self.parents[j];axis=self.source[parent][:3,:3].T@(normal*p[39+k*2]+width*p[40+k*2]);local[j][:3,:3]=Rotation.from_rotvec(np.deg2rad(axis)).as_matrix()@local[j][:3,:3]
  return fk(local,self.parents)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--minimum-thumb-curl',type=float);args=parser.parse_args()
 if args.minimum_thumb_curl is not None and not .8<=args.minimum_thumb_curl<=1.1:raise ValueError('Closed-thumb minimum outside bound')
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());report=json.loads(Path(source['source_readonly_report']).read_text());baseline=json.loads(Path(source['baseline']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);model=OffsetThumbClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));p=np.array(item['parameters']+[0.]*(45-len(item['parameters'])))
  if args.minimum_thumb_curl is not None:p[14]=max(p[14],args.minimum_thumb_curl)
  best=assess(model,p);log=[]
  for step in (12.,6.,3.):
   for i in range(39,45):
    seed=p.copy()
    for value in (seed[i]-step,seed[i]+step):
     if abs(value)>18:continue
     trial=seed.copy();trial[i]=value;r=assess(model,trial)
     if r[0]<best[0]:p=trial;best=r
   log.append(dict(step=step,score=best[0],closed=best[1]['introduced_triangle_crossings'],released=best[2]['introduced_triangle_crossings'],partial=best[4]));print(json.dumps(dict(side=item['side'],**log[-1])),flush=True)
  _,closed,released,pose,partial=best;index=model.pose(p,True)
  for label,mat in [('closed',pose),('index-released',index)]:render_close(arm,mat,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+label+'.png'),'Bounded thumb offsets '+label)
  arms.append(dict(side=item['side'],parameters=p.tolist(),closed=closed,index_released=released,partial_crossings=partial,search=log,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in index]))
 source.update(authoring_model='OffsetThumbClosure45',precursor=str(args.candidate),arms=arms,minimum_thumb_curl=args.minimum_thumb_curl,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
