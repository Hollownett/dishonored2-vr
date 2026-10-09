"""Private authored mirror/retarget from exact qualified occupied native finger locals."""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from author_native_hand_candidate import matrices,fk,skin
from author_empty_contact_closure import locals_of,Closure,interpolate_local
from export_native_hand_poses import verified_arm
from read_empty_source_evidence import source_evidence
from read_occupied_source_evidence import occupied_source_evidence
from render_body_rig import skin_positions
from check_authored_hand_contacts import evaluate
from compare_empty_hand_contact_baselines import census,strain
from render_authored_hand_close import render_close

def rigid_rotation(matrix):
 u,_,v=np.linalg.svd(matrix);rotation=u@v
 if np.linalg.det(rotation)<.999 or np.max(np.abs(rotation-matrix))>1e-4:raise ValueError('Native frame exceeds bounded rotation rounding tolerance')
 return rotation
def bind_matrices(arm):
 result=[np.linalg.inv(np.vstack([row,[0,0,0,1]]))for row in np.asarray(arm['skinning_layout']['inverse_bind_rows']).reshape(69,3,4)]
 for matrix in result:matrix[:3,:3]=rigid_rotation(matrix[:3,:3])
 return result
def mirrored_locals(left,right,occupied,absolute=False):
 if right['joint_names']!=occupied['joint_names']or right['skeleton_layout']['parent_indices']!=occupied['skeleton_layout']['parent_indices']or right['skinning_layout']['inverse_bind_rows']!=occupied['skinning_layout']['inverse_bind_rows']:raise ValueError('Occupied source exact rig changed')
 ln,rn=left['joint_names'],right['joint_names'];lp,rp=left['skeleton_layout']['parent_indices'],right['skeleton_layout']['parent_indices'];mapping=[ln.index(n.replace('right_','left_',1))for n in rn]
 if any(lp[mapping[j]]!=(-1 if rp[j]<0 else mapping[rp[j]])for j in range(69)):raise ValueError('Mirror hierarchy differs')
 lb,rb=bind_matrices(left),bind_matrices(right);w=rn.index('right_wrist_jnt');mask=[j for j,n in enumerate(rn)if any(d in n for d in ('thumb','index','medium','ring','pinky','middle'))];main=[j for j in mask if'helper'not in rn[j]]
 R=np.array([rb[j][:3,3]-rb[w][:3,3]for j in main]);L=np.array([lb[mapping[j]][:3,3]-lb[mapping[w]][:3,3]for j in main]);u,sv,v=np.linalg.svd(R.T@L);mirror=v.T@u.T;error=np.linalg.norm(R@mirror.T-L,axis=1)
 if np.linalg.det(mirror)>-.999 or error.max()>1e-4 or sv.min()<1e-5:raise ValueError('Native bind reflection not demonstrated')
 a=locals_of(matrices(right),rp);b=locals_of(matrices(occupied),rp);native_left=locals_of(matrices(left),lp);out=[x.copy()for x in native_left]
 left_pose=matrices(left);right_pose=matrices(occupied);world_rotations=[x[:3,:3].copy()for x in left_pose];wrist_frame=lb[mapping[w]][:3,:3].T@mirror@rb[w][:3,:3];world_mirror=left_pose[mapping[w]][:3,:3]@wrist_frame@right_pose[w][:3,:3].T
 if absolute:
  for j in mask:
   i=mapping[j];K=lb[i][:3,:3].T@mirror@rb[j][:3,:3];world_rotations[i]=world_mirror@right_pose[j][:3,:3]@K.T
 for j in mask:
  i=mapping[j];K=lb[i][:3,:3].T@mirror@rb[j][:3,:3];delta=a[j][:3,:3].T@b[j][:3,:3];out[i][:3,:3]=world_rotations[lp[i]].T@world_rotations[i]if absolute else native_left[i][:3,:3]@K@delta@K.T
  out[i][:3,:3]=rigid_rotation(out[i][:3,:3])
  if'helper'in rn[j]:
   parent=rp[j];P=lb[mapping[parent]][:3,:3].T@mirror@rb[parent][:3,:3]
   if absolute:out[i][:3,3]=P@b[j][:3,3]
   else:out[i][:3,3]+=P@(b[j][:3,3]-a[j][:3,3])
 derived=fk(out,lp);point_error=max(float(np.linalg.norm(derived[mapping[j]][:3,3]-(left_pose[mapping[w]][:3,3]+world_mirror@(right_pose[j][:3,3]-right_pose[w][:3,3]))))for j in main)
 return out,dict(bind_reflection=mirror.tolist(),determinant=float(np.linalg.det(mirror)),maximum_bind_point_error_native_model_units=float(error.max()),absolute_occupied_pose_mirror=absolute,maximum_authored_main_point_mirror_error_native_model_units=point_error,rotation_mirror_is_conjugation=not absolute,main_translations_preserved=True,derived_authored_not_native_Empty_closed=True)

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--empty',type=Path,required=True);a.add_argument('--sword',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--absolute',action='store_true');args=a.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 empty=json.loads(args.empty.read_text());sword=json.loads(args.sword.read_text());eq=source_evidence(args.empty,args.empty.parent/'run-provenance.json');sq=occupied_source_evidence(args.sword,args.sword.parent/'run-provenance.json');arms={s:verified_arm(empty,s)for s in ('left','right')};occupied=verified_arm(sword,'right');mirror,evidence=mirrored_locals(arms['left'],arms['right'],occupied,args.absolute);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);results=[]
 for side in ('left','right'):
  arm=arms[side];parents=arm['skeleton_layout']['parent_indices'];native=locals_of(matrices(arm),parents);local=mirror if side=='left'else locals_of(matrices(occupied),parents)
  if side=='right':
   mask=[j for j,n in enumerate(arm['joint_names'])if any(d in n for d in ('thumb','index','medium','ring','pinky','middle'))];local=[local[j]if j in mask else native[j]for j in range(69)]
  pose=fk(local,parents);model=Closure(arm,pose,root/(side+'-arm.bmd6model'));states=[]
  for amount in (0.,.25,.5,.75,1.):
   stage=fk([interpolate_local(x,y,amount)for x,y in zip(native,local)],parents);after=np.concatenate([skin_positions(v,skin(arm,stage))[tri]for v,tri in model.surfaces]);states.append(dict(amount=amount,contacts=evaluate(model.before,after,model.ids),census=census(model.before,after,model.ids),strain=strain(model.before,after)))
  render_close(arm,pose,root/(side+'-arm.bmd6model'),args.output/(side+'-authored-retarget.png'),'Authored '+('native-bind mirror'if side=='left'else'occupied-local retarget')+'; not native Empty fist')
  main=[j for j,n in enumerate(arm['joint_names'])if any(d in n for d in ('thumb','index','medium','ring','pinky'))and'helper'not in n];length=max(float(np.linalg.norm(local[j][:3,3]-native[j][:3,3]))for j in main)
  results.append(dict(side=side,maximum_main_translation_error_native_model_units=length,authored_transform_matrices=[m.tolist()for m in pose],stages=states))
 result=dict(empty_source=str(args.empty.resolve()),empty_qualification=eq,occupied_source=str(args.sword.resolve()),occupied_qualification=sq,occupied_sha256=hashlib.sha256(args.sword.read_bytes()).hexdigest(),retarget_evidence=evidence,production_activation=False,native_Empty_closed_captured=False,anatomical_acceptance=False,arms=results)
 (args.output/'authored-retarget.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps([dict(side=x['side'],length_error=x['maximum_main_translation_error_native_model_units'],crossings=[s['contacts']['introduced_triangle_crossings']for s in x['stages']],collapse=x['stages'][-1]['strain']['triangles_below_one_tenth_source_area'])for x in results]))
if __name__=='__main__':main()
