"""Private full-fist refinement includes pad/palm and thumb/index skin gap proxies."""
import argparse,json
from pathlib import Path
import numpy as np
from refine_empty_thumb_offsets import OffsetThumbClosure
from refine_empty_web_closure import assess
from measure_hand_closure_shape import measure
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());report=json.loads(Path(source['source_readonly_report']).read_text());baseline=json.loads(Path(source['baseline']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);asset=root/(item['side']+'-arm.bmd6model');model=OffsetThumbClosure(arm,old['authored_transform_matrices'],asset);parameters=np.array(item['parameters']);log=[]
  if parameters[14]<.9:raise ValueError('Require the stronger closed-thumb precursor')
  def objective(trial):
   scored=assess(model,trial);shape=measure(arm,scored[3],asset);gaps=[v['minimum_pad_to_palm_vertex_distance_native_model_units']for k,v in shape['pad_to_palm_proxies'].items()if k!='thumb'];gaps.append(shape['thumb_pad_to_index_vertex_distance_native_model_units']);penalty=sum(max(0.,d-.006)for d in gaps)*2000
   return scored[0]+penalty,scored,shape
  best=objective(parameters)
  for step in (.1,.05):
   for i in [*range(12),*range(19,27),*range(39,45)]:
    seed=parameters.copy();lo,hi=(.5,2.)if i<12 and i%3==0 else(.45,1.1)if i<12 and i%3==1 else(.5,1.6)if i<12 else(-18.,18.)if i>=39 else(.1,1.35)if(i-19)%2==0 else(.1,1.8);increment=3. if i>=39 and step==.1 else 1.5 if i>=39 else step
    for value in (seed[i]-increment,seed[i]+increment):
     if not lo<=value<=hi:continue
     trial=seed.copy();trial[i]=value;r=objective(trial)
     if r[0]<best[0]:parameters=trial;best=r
   log.append(dict(step=step,objective=best[0],closed=best[1][1]['introduced_triangle_crossings'],released=best[1][2]['introduced_triangle_crossings'],partial=best[1][4],shape=best[2]));print(json.dumps(dict(side=item['side'],**log[-1])),flush=True)
  _,(_,closed,released,pose,partial),shape=best;index=model.pose(parameters,True)
  for label,mat in [('closed',pose),('index-released',index)]:render_close(arm,mat,asset,args.output/(item['side']+'-'+label+'.png'),'Full-fist contact/shape trial '+label)
  arms.append(dict(side=item['side'],parameters=parameters.tolist(),closed=closed,index_released=released,partial_crossings=partial,shape_measurements=shape,search=log,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in index]))
 source.update(authoring_model='OffsetThumbClosure45',precursor=str(args.candidate),arms=arms,artist_skin_gap_proxy_target_native_model_units=.006,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()

