"""Private coherent thumb skin-helper arcs; rigid native main bones unchanged."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from fit_empty_thumb_wrap import WrapThumbClosure
from fit_empty_thumb import NativePlaneThumbClosure
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import fk
from refine_empty_web_closure import assess
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

class WrapHelperClosure(WrapThumbClosure):
 def pose(self,p,index_release=False):
  p=np.asarray(p)
  if len(p)!=48 or not np.isfinite(p).all() or np.any(p[45:]<0) or np.any(p[45:]>1):raise ValueError('48 bounded finite wrap/helper parameters required')
  posed=super().pose(p[:45],index_release);local=locals_of(posed,self.parents)
  main=[self.names.index(self.arm['side']+'_thumb_0'+str(i)+'_jnt')for i in (1,2,3)]
  wrist_map=posed[self.wrist]@np.linalg.inv(self.source[self.wrist]);oldroot=wrist_map@self.source[main[0]]
  rootdelta=posed[main[0]][:3,:3]@oldroot[:3,:3].T
  for j in self.thumb:
   name=self.names[j]
   if 'helper_thumb'not in name:continue
   child=main[2]if'helper_thumb_2_'in name else main[1]if'helper_thumb_1_'in name or'flexmuscle'in name or'skinpalm'in name else main[0]
   if self.parents[j]!=self.parents[child]:raise ValueError('Native helper sibling hierarchy changed')
   factor=p[45]if'_top_'in name else p[46]
   delta=local[child][:3,:3]@self.native[child][:3,:3].T
   part=Rotation.from_rotvec(Rotation.from_matrix(delta).as_rotvec()*factor).as_matrix()
   local[j]=self.native[j].copy();local[j][:3,:3]=part@self.native[j][:3,:3]
   pivot=self.native[child][:3,3];local[j][:3,3]=pivot+part@(self.native[j][:3,3]-pivot)
   if 'skinpalm'in name or'flexmuscle'in name:
    portion=Rotation.from_rotvec(Rotation.from_matrix(rootdelta).as_rotvec()*p[47]).as_matrix()
    native_world=wrist_map@self.source[j];target=native_world.copy()
    target[:3,3]=oldroot[:3,3]+portion@(native_world[:3,3]-oldroot[:3,3]);target[:3,:3]=portion@native_world[:3,:3]
    local[j]=np.linalg.inv(posed[self.parents[j]])@target
  return fk(local,self.parents)

class NativePlaneWrapHelperClosure(WrapHelperClosure):
 def flex_axis(self,j,child,target):
  return NativePlaneThumbClosure.flex_axis(self,j,child,target)

class MiddlePhalanxWrapHelperClosure(WrapHelperClosure):
 target_segment=1

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);m=WrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));best=None;trials=[]
  for top,bot,palm in ((.3,.3,.3),(.6,.6,.3),(.9,.9,.3),(.6,.6,.6),(.9,.9,.6),(.9,.9,1.)):
   p=np.r_[item['parameters'],top,bot,palm];score=assess(m,p);entry=dict(helper_top=top,helper_bottom=bot,palm_root_portion=palm,closed=score[1],index_released=score[2],partial_crossings=score[4]);trials.append(entry)
   if best is None or score[0]<best[0]:best=(score[0],p,entry,score[3])
   print(json.dumps(dict(side=item['side'],top=top,bottom=bot,palm=palm,closed=score[1]['introduced_triangle_crossings'],partial=score[4])),flush=True)
  _,p,entry,pose=best;entry=dict(entry);entry.update(side=item['side'],parameters=p.tolist(),trials=trials,target_evidence=m.target_geometry(p)[2],authored_transform_matrices=[x.tolist()for x in pose],index_released_transform_matrices=[x.tolist()for x in m.pose(p,True)]);arms.append(entry)
  render_close(arm,pose,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-closed.png'),'Coherent native thumb helper arcs')
 source.update(precursor=str(args.candidate),authoring_model='WrapHelperClosure48',arms=arms,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n')
if __name__=='__main__':main()
