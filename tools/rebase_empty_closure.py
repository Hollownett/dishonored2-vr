"""Private native-source rebase; preserves uncertified provenance fields unchanged."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np
from refine_empty_web_closure import WebClosure,assess
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close
from read_empty_source_evidence import source_evidence

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--source-companion',type=Path);args=p.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 previous=json.loads(args.candidate.read_text());baseline=json.loads(args.baseline.read_text());source=Path(baseline['source_readonly_report']);report=json.loads(source.read_text());qualification=source_evidence(source,args.source_companion)if args.source_companion else None;args.output.mkdir(parents=True);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';arms=[]
 for item,old in zip(previous['arms'],baseline['arms']):
  if item['side']!=old['side']:raise ValueError('Mixed side')
  arm=verified_arm(report,item['side']);model=WebClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));raw=item['parameters'];raw=raw+[0.]*4+[raw[12],raw[13]]*4 if len(raw)==15 else raw;params=np.asarray(raw+[0.]*(39-len(raw)));score,closed,released,pose,partial=assess(model,params);index=model.pose(params,True)
  for label,mat in [('closed',pose),('index-released',index)]:render_close(arm,mat,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+label+'.png'),'Fresh source '+label)
  arms.append(dict(side=item['side'],parameters=params.tolist(),closed=closed,index_released=released,partial_crossings=partial,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in index]))
  print(json.dumps(dict(side=item['side'],closed=closed['introduced_triangle_crossings'],partial=partial,released=released['introduced_triangle_crossings'])),flush=True)
 result=dict(pose_origin='private_fresh_native_source_rebase',authoring_model='WebClosure39',source_readonly_report=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),baseline=str(args.baseline),baseline_sha256=hashlib.sha256(args.baseline.read_bytes()).hexdigest(),precursor=str(args.candidate),source_tracked_writes_absence_certified=report.get('tracked_arm_writes_absent_verified')is True,source_authored_endpoint_certified=report.get('authored_pose_confirmed')is True,source_per_pose_carry_certified=False,source_qualification=qualification,game_memory_access=False,game_memory_writes=False,captured_fist=False,production_archive_exported=False,production_activation=False,headset_acceptance=False,arms=arms)
 (args.output/'authored-candidate.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
