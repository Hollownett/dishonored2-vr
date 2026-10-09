"""Private right Empty authoring uses occupied native hinge-axis evidence only.
Never relabels Sword endpoint; restrained IP and skin-band targets remain experiments.
"""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from refine_empty_wrap_helpers import MiddlePhalanxWrapHelperClosure
from fit_empty_wrap_clearance import refit
from author_native_hand_candidate import matrices
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
from refine_empty_web_closure import assess
from render_authored_hand_close import render_close

class NativeAxisMiddleWrap(MiddlePhalanxWrapHelperClosure):
 def __init__(self,arm,baseline,asset,occupied):
  if arm['side']!='right'or occupied['side']!='right':raise ValueError('Only right native occupied hinge evidence exists')
  if arm['joint_names']!=occupied['joint_names']or arm['skeleton_layout']['parent_indices']!=occupied['skeleton_layout']['parent_indices']or arm['skinning_layout']['inverse_bind_rows']!=occupied['skinning_layout']['inverse_bind_rows']:raise ValueError('Exact native rig differs')
  super().__init__(arm,baseline,asset);other=locals_of(matrices(occupied),self.parents);self.axes={};self.axis_evidence=[]
  for s in (2,3):
   j=self.names.index('right_thumb_0'+str(s)+'_jnt');v=Rotation.from_matrix(self.native[j][:3,:3].T@other[j][:3,:3]).as_rotvec();angle=np.linalg.norm(v)
   if angle<.05:raise ValueError('Insufficient occupied flex-axis evidence')
   self.axes[j]=v/angle;self.axis_evidence.append(dict(joint=self.names[j],source_local_delta_degrees=float(angle*180/np.pi),local_postrotation_axis=self.axes[j].tolist()))
 def flex_axis(self,j,child,target):return self.axes[j]
 def pose(self,p,index_release=False):
  if len(p)!=48 or not np.isfinite(p).all()or p[43]>.65:raise ValueError('Restrained native-axis IP fit required')
  return super().pose(p,index_release)

class LowerNativeAxisMiddleWrap(NativeAxisMiddleWrap):
 target_fraction=(.55,.95)

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--candidate',type=Path,required=True);a.add_argument('--occupied',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--maximum-ip',type=float,default=.65);args=a.parse_args()
 if not np.isfinite(args.maximum_ip)or not .05<=args.maximum_ip<=.65:raise ValueError('Restrained IP bound0.05–0.65 radians required')
 if args.output.exists():raise ValueError('Use new output')
 c=json.loads(args.candidate.read_text());b=json.loads(Path(c['baseline']).read_text());r=json.loads(Path(c['source_readonly_report']).read_text());occupied_report=json.loads(args.occupied.read_text());occupied=verified_arm(occupied_report,'right');state=next(h for h in occupied_report['native_hand_state']['hands']if h['side']=='right')
 if state['equip_name']!='Sword'or not state['slot']or not state['node_ownership_verified']:raise ValueError('Require owned occupied Sword axis reference')
 root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';arm=verified_arm(r,'right');old=next(x for x in b['arms']if x['side']=='right');item=next(x for x in c['arms']if x['side']=='right');args.output.mkdir(parents=True);variants=[]
 for label,cls in (('middle',NativeAxisMiddleWrap),('lower_middle',LowerNativeAxisMiddleWrap)):
  model=cls(arm,old['authored_transform_matrices'],root/'right-arm.bmd6model',occupied)
  for blend in (.4,.7,1.):
   p=np.array(item['parameters']);p[44]=.012+.028*blend;p[42:44]=[.7,min(.3,args.maximum_ip)]
   p,evidence=refit(model,p,.15,maximum_ip=args.maximum_ip)
   result=assess(model,p);entry=dict(band=label,target_fraction=model.target_fraction,blend=blend,parameters=p.tolist(),axis_evidence=model.axis_evidence,target_evidence=evidence,closed=result[1],index_released=result[2],partial_crossings=result[4],authored_transform_matrices=[m.tolist()for m in result[3]]);variants.append(entry)
   render_close(arm,result[3],root/'right-arm.bmd6model',args.output/(label+'-'+str(blend)+'.png'),'Native right hinge axes; restrained IP; '+label+' skin band')
   print(json.dumps(dict(band=label,blend=blend,closed=result[1]['introduced_triangle_crossings'],partial=result[4],error=evidence['target_error_native_model_units'],alignment=evidence['distal_direction_alignment'],MCP=p[42],IP=p[43])),flush=True)
 result=dict(source=c['source_readonly_report'],source_qualification=c.get('source_qualification'),occupied_axis_source=str(args.occupied.resolve()),occupied_sha256=hashlib.sha256(args.occupied.read_bytes()).hexdigest(),occupied_reference_full_interval_writer_absence_certified=False,occupied_endpoint_relabelled_Empty=False,IP_additional_flex_limit_radians=args.maximum_ip,production_activation=False,anatomical_acceptance=False,variants=variants)
 (args.output/'experiment.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
