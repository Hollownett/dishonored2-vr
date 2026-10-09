"""Matched native Empty/Sword/candidate skin cohorts; occupied Sword stays occupied.
Counts exact noncoplanar crossings and fixed source seams separately, not acceptance.
"""
import argparse,json,hashlib
from pathlib import Path
from collections import Counter
import numpy as np
from scipy.spatial import cKDTree
from author_empty_contact_closure import Closure
from author_native_hand_candidate import matrices,skin
from check_authored_hand_contacts import crossing,evaluate
from export_native_hand_poses import verified_arm
from render_body_rig import skin_positions
from render_authored_hand_close import render_close
from read_occupied_source_evidence import occupied_source_evidence

def strain(before,after):
 old_edges=np.linalg.norm(before[:,[1,2,0]]-before,axis=2);new_edges=np.linalg.norm(after[:,[1,2,0]]-after,axis=2);valid=old_edges>1e-8;edge=new_edges[valid]/old_edges[valid]
 old_area=np.linalg.norm(np.cross(before[:,1]-before[:,0],before[:,2]-before[:,0]),axis=1);new_area=np.linalg.norm(np.cross(after[:,1]-after[:,0],after[:,2]-after[:,0]),axis=1);valid=old_area>1e-12;area=new_area[valid]/old_area[valid]
 return dict(edge_length_ratio=dict(minimum=float(edge.min()),p5=float(np.percentile(edge,5)),p95=float(np.percentile(edge,95)),maximum=float(edge.max())),triangle_area_ratio=dict(minimum=float(area.min()),p1=float(np.percentile(area,1)),p5=float(np.percentile(area,5)),p95=float(np.percentile(area,95)),maximum=float(area.max())),triangles_below_one_tenth_source_area=int((area<.1).sum()),skin_fold_acceptance_certified=False)

def census(before,after,ids):
 centre=after.mean(axis=1);radius=np.linalg.norm(after-centre[:,None,:],axis=2).max(axis=1);pairs=cKDTree(centre).query_pairs(2*radius.max(),output_type='ndarray');seam=[];kept=[]
 for k in range(0,len(pairs),20000):
  a,b=pairs[k:k+20000].T;valid=np.linalg.norm(centre[a]-centre[b],axis=1)<=radius[a]+radius[b]
  a,b=a[valid],b[valid];valid=np.all(after[a].max(axis=1)>=after[b].min(axis=1),axis=1)&np.all(after[b].max(axis=1)>=after[a].min(axis=1),axis=1);a,b=a[valid],b[valid]
  hit=crossing(after[a],after[b]);a,b=a[hit],b[hit]
  source_seam=np.any(np.linalg.norm(before[a,:,None,:]-before[b,None,:,:],axis=3)<2e-5,axis=(1,2))
  seam.extend((int(x),int(y))for x,y in zip(a[source_seam],b[source_seam]));kept.extend((int(x),int(y))for x,y in zip(a[~source_seam],b[~source_seam]))
 def labels(pairs):return dict(region_pairs=dict(Counter('/'.join(sorted((ids[a]['region'],ids[b]['region'])))for a,b in pairs)),surface_pairs=dict(Counter('/'.join(map(str,sorted((ids[a]['surface'],ids[b]['surface']))))for a,b in pairs)),same_surface=sum(ids[a]['surface']==ids[b]['surface']for a,b in pairs),different_surface=sum(ids[a]['surface']!=ids[b]['surface']for a,b in pairs))
 return dict(raw_noncoplanar_crossings=len(seam)+len(kept),fixed_source_seam_excluded_crossings=len(seam),source_seam_excluded_details=labels(seam),kept_crossings=len(kept),kept_details=labels(kept),coplanar_contacts_certified=False)

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--candidate',type=Path,required=True);a.add_argument('--sword',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--sword-companion',type=Path);args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 c=json.loads(args.candidate.read_text());opened=json.loads(Path(c['source_readonly_report']).read_text());occupied=json.loads(args.sword.read_text());qualification=occupied_source_evidence(args.sword,args.sword_companion)if args.sword_companion else None;baseline=json.loads(Path(c['baseline']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);results=[]
 for item,old in zip(c['arms'],baseline['arms']):
  side=item['side'];arm=verified_arm(opened,side);sword=verified_arm(occupied,side)
  if arm['joint_names']!=sword['joint_names']or arm['skeleton_layout']['parent_indices']!=sword['skeleton_layout']['parent_indices']or arm['skinning_layout']['inverse_bind_rows']!=sword['skinning_layout']['inverse_bind_rows']:raise ValueError('Exact native rig differs')
  model=Closure(arm,old['authored_transform_matrices'],root/(side+'-arm.bmd6model'));native=matrices(arm);sword_pose=matrices(sword);wrist=model.wrist
  align=native[wrist]@np.linalg.inv(sword_pose[wrist]);sword_aligned=[align@p for p in sword_pose]
  candidate=[np.asarray(x)for x in item['authored_transform_matrices']];states=[]
  for label,pose in (('native_Empty_open',native),('native_occupied_Sword_snapshot',sword_aligned),('private_authored_Empty_candidate',candidate)):
   palette=skin(arm,pose);after=np.concatenate([skin_positions(v,palette)[tri]for v,tri in model.surfaces]);metric=evaluate(model.before,after,model.ids);raw=census(model.before,after,model.ids)
   states.append(dict(label=label,matched_checker=metric,crossing_census=raw,matched_skin_strain=strain(model.before,after)));render_close(arm,pose,root/(side+'-arm.bmd6model'),args.output/(side+'-'+label+'.png'),label)
  hand=next(h for h in occupied['native_hand_state']['hands']if h['side']==side)
  results.append(dict(side=side,sword_slot=hand['slot'],sword_equip_name=hand['equip_name'],same_exact_hierarchy_inverse_bind=True,matched_triangle_count=len(model.ids),asset_sha256=hashlib.sha256((root/(side+'-arm.bmd6model')).read_bytes()).hexdigest(),states=states))
 result=dict(candidate=str(args.candidate.resolve()),candidate_sha256=hashlib.sha256(args.candidate.read_bytes()).hexdigest(),native_open=str(Path(c['source_readonly_report']).resolve()),native_sword=str(args.sword.resolve()),sword_sha256=hashlib.sha256(args.sword.read_bytes()).hexdigest(),source_cohort='Identical Empty-source flesh/glove triangles within0.25 native model units of source wrist',seam_policy='Same source-position coincidences within2e-5 native model units excluded in every state',sword_qualification=qualification,sword_full_interval_writer_absence_certified=bool(qualification),sword_is_empty_fist=False,geometry_acceptance=False,production_activation=False,arms=results)
 (args.output/'matched-baselines.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps([dict(side=a['side'],states=[dict(label=s['label'],raw=s['crossing_census']['raw_noncoplanar_crossings'],excluded=s['crossing_census']['fixed_source_seam_excluded_crossings'],kept=s['crossing_census']['kept_crossings'],new=s['matched_checker']['introduced_triangle_crossings'])for s in a['states']])for a in results]))
if __name__=='__main__':main()
