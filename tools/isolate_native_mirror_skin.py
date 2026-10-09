"""Matched left native idle/main/helper isolation across authored interpolation stages."""
import argparse,json
from pathlib import Path
from collections import Counter
import numpy as np
from export_native_hand_poses import verified_arm
from author_empty_contact_closure import Closure,locals_of,interpolate_local
from author_native_hand_candidate import matrices,fk,skin
from check_authored_hand_contacts import evaluate
from compare_empty_hand_contact_baselines import strain
from render_body_rig import skin_positions
from render_authored_hand_close import render_close
from read_empty_source_evidence import source_evidence
from read_occupied_source_evidence import occupied_source_evidence

def collapse_regions(before,after,ids):
 old=np.linalg.norm(np.cross(before[:,1]-before[:,0],before[:,2]-before[:,0]),axis=1);new=np.linalg.norm(np.cross(after[:,1]-after[:,0],after[:,2]-after[:,0]),axis=1);collapsed=(old>1e-12)&(new<old*.1)
 return dict(region_counts=dict(Counter(ids[i]['region']for i in np.flatnonzero(collapsed))),surface_counts=dict(Counter(str(ids[i]['surface'])for i in np.flatnonzero(collapsed))),triangles=[ids[i]for i in np.flatnonzero(collapsed)])

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--retarget',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--segments',action='store_true');args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 c=json.loads(args.retarget.read_text());ep=Path(c['empty_source']);sp=Path(c['occupied_source']);eq=source_evidence(ep,c['empty_qualification']['companion']);sq=occupied_source_evidence(sp,c['occupied_qualification']['companion']);report=json.loads(ep.read_text());arm=verified_arm(report,'left');item=next(x for x in c['arms']if x['side']=='left');parents=arm['skeleton_layout']['parent_indices'];native=locals_of(matrices(arm),parents);mirror=locals_of([np.asarray(x)for x in item['authored_transform_matrices']],parents);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';model=Closure(arm,item['authored_transform_matrices'],root/'left-arm.bmd6model');main=[];helpers=[]
 for j,n in enumerate(arm['joint_names']):
  if any(d in n for d in ('thumb','index','medium','middle','ring','pinky')):(helpers if'helper'in n else main).append(j)
 args.output.mkdir(parents=True);results=[]
 cases=[('native_Empty_idle',[],[]),('authored_main_rotations',main,[]),('authored_helper_rotations',helpers,[]),('authored_helper_offsets',[],helpers),('authored_all_rotations',main+helpers,[]),('authored_full_mirror',main+helpers,helpers)]
 if args.segments:
  for number in (1,2,3,4):cases.append(('authored_main_segment_'+str(number),[j for j in main if'_0'+str(number)+'_jnt'in arm['joint_names'][j]],[]))
  for digit in ('thumb','index','medium','ring','pinky'):cases.append(('authored_main_digit_'+digit,[j for j in main if digit in arm['joint_names'][j]],[]))
 for label,rotations,translations in cases:
  target=[x.copy()for x in native]
  for j in rotations:target[j][:3,:3]=mirror[j][:3,:3]
  for j in translations:target[j][:3,3]=mirror[j][:3,3]
  stages=[]
  for amount in (0.,.25,.5,.75,1.):
   pose=fk([interpolate_local(x,y,amount)for x,y in zip(native,target)],parents);after=np.concatenate([skin_positions(v,skin(arm,pose))[t]for v,t in model.surfaces]);stages.append(dict(amount=amount,contacts=evaluate(model.before,after,model.ids),skin_strain=strain(model.before,after),collapse=collapse_regions(model.before,after,model.ids)))
  render_close(arm,fk(target,parents),root/'left-arm.bmd6model',args.output/(label+'.png'),label+'; diagnostic authored isolation')
  result=dict(label=label,main_translation_error=max(float(np.linalg.norm(target[j][:3,3]-native[j][:3,3]))for j in main),stages=stages);results.append(result)
  print(json.dumps(dict(label=label,new_crossings=[s['contacts']['introduced_triangle_crossings']for s in stages],collapsed=[s['skin_strain']['triangles_below_one_tenth_source_area']for s in stages],closed_regions=stages[-1]['collapse']['region_counts'])),flush=True)
 (args.output/'isolation.json').write_text(json.dumps(dict(empty_qualification=eq,occupied_qualification=sq,retarget=str(args.retarget.resolve()),source_cohorts_identical=True,production_activation=False,native_Empty_closed_captured=False,anatomical_acceptance=False,results=results),indent=2)+'\n')
if __name__=='__main__':main()
