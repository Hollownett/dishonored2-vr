"""Private true-triangle refinement of a fixed posed middle-phalanx wrap target."""
import argparse,json
from pathlib import Path
import numpy as np
from refine_empty_wrap_helpers import MiddlePhalanxWrapHelperClosure
from refine_empty_web_closure import assess
from fit_empty_thumb import unit
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

def objective(model,p):
 score=assess(model,p);target,across,_=model.target_geometry(p);tip=model.names.index(model.arm['side']+'_thumb_end_jnt');ip=model.names.index(model.arm['side']+'_thumb_03_jnt');pose=score[3]
 error=float(np.linalg.norm(pose[tip][:3,3]-target));alignment=float(np.dot(unit(pose[tip][:3,3]-pose[ip][:3,3]),across))
 # Fixed surface target is preserved, including while fingers change only by index release.
 penalty=max(0.,error-.006)*100000+max(0.,.8-alignment)*2000
 return score[0]+penalty+error*2000,score,error,alignment

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--experiment',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.experiment.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);m=MiddlePhalanxWrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));entry=next(v for v in item['variants']if v['blend']==(.4 if item['side']=='left'else .7)and v['strength']==.15);p=np.array(entry['parameters']);best=objective(m,p);steps=[]
  for step in (.12,.06):
   for index in range(39,48):
    if index==44:continue # Never move the fixed finger-surface target to win counts.
    seed=p.copy()
    for delta in (-step,step):
     trial=seed.copy();trial[index]+=delta
     try:r=objective(m,trial)
     except ValueError:continue
     if r[0]<best[0]:p=trial;best=r
   steps.append(dict(step=step,objective=best[0],closed=best[1][1]['introduced_triangle_crossings'],partial=best[1][4],target_error_native_model_units=best[2],alignment=best[3]));print(json.dumps(dict(side=item['side'],**steps[-1])),flush=True)
  _,scored,error,alignment=best;pose=scored[3];target=m.target_geometry(p)[2];target.update(target_error_native_model_units=error,distal_direction_alignment=alignment)
  arms.append(dict(side=item['side'],parameters=p.tolist(),closed=scored[1],index_released=scored[2],partial_crossings=scored[4],search=steps,target_evidence=target,authored_transform_matrices=[x.tolist()for x in pose],index_released_transform_matrices=[x.tolist()for x in m.pose(p,True)]))
  for state,mats in (('closed',pose),('index-released',m.pose(p,True))):render_close(arm,mats,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+state+'.png'),'Fixed middle phalanx skin wrap '+state)
 source.update(precursor=str(args.experiment),authoring_model='MiddlePhalanxWrapHelperClosure48',arms=arms,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n')
if __name__=='__main__':main()
