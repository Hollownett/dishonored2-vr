"""Private left-glove helper response search with mirrored native main bones fixed."""
import argparse,json
from pathlib import Path
import numpy as np
from author_native_hand_candidate import matrices,fk,skin
from author_empty_contact_closure import Closure,locals_of,interpolate_local
from export_native_hand_poses import verified_arm
from compare_empty_hand_contact_baselines import strain
from check_authored_hand_contacts import evaluate
from render_body_rig import skin_positions
from render_authored_hand_close import render_close

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--candidate',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 c=json.loads(args.candidate.read_text());report=json.loads(Path(c['empty_source']).read_text());arm=verified_arm(report,'left');item=next(x for x in c['arms']if x['side']=='left');parents=arm['skeleton_layout']['parent_indices'];native=locals_of(matrices(arm),parents);closed=locals_of([np.asarray(m)for m in item['authored_transform_matrices']],parents);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';model=Closure(arm,item['authored_transform_matrices'],root/'left-arm.bmd6model');helpers=[j for j,n in enumerate(arm['joint_names'])if'helper'in n and any(d in n for d in ('thumb','index','middle','ring','pinky'))];args.output.mkdir(parents=True);trials=[];best=None
 for top in (.35,.7,1.):
  for bottom in (.35,.7,1.):
   local=[m.copy()for m in closed]
   for j in helpers:local[j]=interpolate_local(native[j],closed[j],top if'_top_'in arm['joint_names'][j]else bottom)
   pose=fk(local,parents);after=np.concatenate([skin_positions(v,skin(arm,pose))[t]for v,t in model.surfaces]);contacts=evaluate(model.before,after,model.ids);fold=strain(model.before,after);score=contacts['introduced_triangle_crossings']+fold['triangles_below_one_tenth_source_area']*50;entry=dict(top=top,bottom=bottom,closed_contacts=contacts,strain=fold);trials.append(entry)
   if best is None or score<best[0]:best=(score,local,pose,entry)
   print(json.dumps(dict(top=top,bottom=bottom,crossings=contacts['introduced_triangle_crossings'],collapse=fold['triangles_below_one_tenth_source_area'])),flush=True)
 _,local,pose,entry=best;stages=[]
 for amount in (0.,.25,.5,.75,1.):
  stage=fk([interpolate_local(x,y,amount)for x,y in zip(native,local)],parents);after=np.concatenate([skin_positions(v,skin(arm,stage))[t]for v,t in model.surfaces]);stages.append(dict(amount=amount,contacts=evaluate(model.before,after,model.ids),strain=strain(model.before,after)))
 render_close(arm,pose,root/'left-arm.bmd6model',args.output/'left-authored-helper-retarget.png','Authored mirrored main bones; native left helper response refit')
 result=dict(precursor=str(args.candidate.resolve()),empty_qualification=c['empty_qualification'],occupied_qualification=c['occupied_qualification'],main_bones_changed_from_precursor=False,production_activation=False,native_Empty_closed_captured=False,anatomical_acceptance=False,side='left',winner=entry,trials=trials,stages=stages,authored_transform_matrices=[m.tolist()for m in pose]);(args.output/'authored-helper-retarget.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
