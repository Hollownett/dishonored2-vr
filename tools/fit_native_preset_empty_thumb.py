"""Private native-derived finger preset plus independently authored restrained thumb.
The occupied Sword posture is a source reference, never native Empty-closed proof.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from refine_empty_wrap_helpers import MiddlePhalanxWrapHelperClosure
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import fk
from export_native_hand_poses import verified_arm
from read_empty_source_evidence import source_evidence
from read_occupied_source_evidence import occupied_source_evidence
from fit_empty_wrap_clearance import refit
from refine_empty_web_closure import assess
from render_authored_hand_close import render_close

class NativePresetThumb(MiddlePhalanxWrapHelperClosure):
 def __init__(self,arm,preset,asset):
  super().__init__(arm,preset,asset);self.preset=locals_of([np.asarray(x)for x in preset],self.parents);self.axes={}
  for segment in (2,3):
   j=self.names.index(arm['side']+'_thumb_0'+str(segment)+'_jnt');v=Rotation.from_matrix(self.native[j][:3,:3].T@self.preset[j][:3,:3]).as_rotvec()
   if np.linalg.norm(v)<.05:raise ValueError('Insufficient native-derived hinge evidence')
   self.axes[j]=v/np.linalg.norm(v)
 def posed_fingers(self,p,index_release=False):
  if len(p)!=39 or not np.isfinite(p).all():raise ValueError('Finite fixed-preset parameter key required')
  local=[x.copy()for x in self.preset]
  if index_release:
   for j,n in enumerate(self.names):
    if'index'in n:local[j]=self.native[j].copy()
  return fk(local,self.parents)
 def flex_axis(self,j,child,target):return self.axes[j]
 def pose(self,p,index_release=False):
  if len(p)!=48 or not np.isfinite(p).all()or p[43]>.65:raise ValueError('Restrained preset thumb IP required')
  return super().pose(p,index_release)

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--retarget',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 c=json.loads(args.retarget.read_text());ep=Path(c['empty_source']);sp=Path(c['occupied_source']);r=json.loads(ep.read_text());eq=source_evidence(ep,c['empty_qualification']['companion']);sq=occupied_source_evidence(sp,c['occupied_qualification']['companion']);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item in c['arms']:
  arm=verified_arm(r,item['side']);m=NativePresetThumb(arm,item['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));variants=[]
  for blend,ip in ((.4,.35),(.7,.35),(.7,.65),(1.,.65)):
   p=np.r_[np.zeros(39),[0.,0.,0.,.7,.3,.012+.028*blend,.7,.7,.3]];p,evidence=refit(m,p,.15,maximum_ip=ip);score=assess(m,p);entry=dict(blend=blend,IP_limit=ip,parameters=p.tolist(),hinge_axes={m.names[j]:v.tolist()for j,v in m.axes.items()},target_evidence=evidence,closed=score[1],index_released=score[2],partial_crossings=score[4],authored_transform_matrices=[x.tolist()for x in score[3]],index_released_transform_matrices=[x.tolist()for x in m.pose(p,True)]);variants.append(entry)
   render_close(arm,score[3],root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+str(blend)+'-'+str(ip)+'.png'),'Native-derived fingers, independently authored modest-IP thumb')
   print(json.dumps(dict(side=item['side'],blend=blend,IP_limit=ip,closed=score[1]['introduced_triangle_crossings'],partial=score[4],error=evidence['target_error_native_model_units'],alignment=evidence['distal_direction_alignment'],MCP=p[42],IP=p[43])),flush=True)
  arms.append(dict(side=item['side'],variants=variants))
 (args.output/'experiment.json').write_text(json.dumps(dict(precursor=str(args.retarget.resolve()),empty_qualification=eq,occupied_qualification=sq,occupied_posture_relabelled_Empty=False,native_Empty_closed_captured=False,production_activation=False,accepted=False,arms=arms),indent=2)+'\n')
if __name__=='__main__':main()
