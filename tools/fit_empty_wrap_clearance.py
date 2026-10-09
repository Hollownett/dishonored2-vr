"""Private opposed thumb refit with measured finger-segment skin envelopes.
Capsules are optimization proxies; independent triangle checks remain required.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from refine_empty_wrap_helpers import WrapHelperClosure,NativePlaneWrapHelperClosure,MiddlePhalanxWrapHelperClosure
from fit_empty_thumb import unit
from refine_empty_web_closure import WebClosure,assess
from author_native_hand_candidate import skin
from render_body_rig import skin_positions
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

def segment_distances(points,start,end):
 segment=end-start;offset=points[:,None,:]-start
 t=np.clip(np.einsum('vki,ki->vk',offset,segment)/np.einsum('ki,ki->k',segment,segment),0.,1.)
 return np.linalg.norm(offset-t[:,:,None]*segment,axis=2)

def refit(model,p,strength,maximum_ip=1.4):
 if not np.isfinite(maximum_ip)or not 0<maximum_ip<=1.4:raise ValueError('Bounded positive IP limit required')
 fingerpose=model.posed_fingers(p[:39]);palette=skin(model.arm,fingerpose);starts=[];ends=[];radii=[]
 for digit,chain in zip(('index','medium'),model.main[:2]):
  chain=chain+[model.names.index(model.arm['side']+'_'+digit+'_end_jnt')]
  start=np.array([fingerpose[j][:3,3]for j in chain[:-1]]);end=np.array([fingerpose[j][:3,3]for j in chain[1:]])
  verts=[v for vs,_ in model.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if digit in model.names[j]or(digit=='medium'and'middle'in model.names[j]))>=190]
  points=skin_positions(verts,palette);distance=segment_distances(points,start,end);nearest=np.argmin(distance,axis=1)
  for k in range(len(start)):
   cohort=distance[nearest==k,k]
   if len(cohort)<10:raise ValueError('Insufficient skin envelope cohort')
   radii.append(float(np.percentile(cohort,95))+.001)
  starts.extend(start);ends.extend(end)
 start=np.array(starts);end=np.array(ends);radii=np.array(radii)
 vertices=[v for vs,_ in model.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if'thumb'in model.names[j])>=190]
 target,across,evidence=model.target_geometry(p);tip=model.names.index(model.arm['side']+'_thumb_end_jnt');ip=model.names.index(model.arm['side']+'_thumb_03_jnt')
 def residual(q):
  trial=p.copy();trial[39:44]=q;pose=model.pose(trial);points=skin_positions(vertices,skin(model.arm,pose));distance=segment_distances(points,start,end)
  penetration=np.maximum(0.,(radii-distance).max(axis=1))
  return np.r_[(pose[tip][:3,3]-target)*2,(unit(pose[tip][:3,3]-pose[ip][:3,3])-across)*.025,penetration*strength,q*.001]
 fit=least_squares(residual,p[39:44],bounds=([-1.8]*3+[0.,0.],[1.8]*3+[1.4,maximum_ip]),max_nfev=120)
 p=p.copy();p[39:44]=fit.x;pose=model.pose(p);points=skin_positions(vertices,skin(model.arm,pose));distance=segment_distances(points,start,end)
 evidence.update(envelope_radius_p95_native_model_units=radii.tolist(),envelope_margin_native_model_units=.001,envelope_penalty_strength=strength,maximum_capsule_penetration_proxy_native_model_units=float(np.maximum(0.,(radii-distance).max(axis=1)).max()),target_error_native_model_units=float(np.linalg.norm(pose[tip][:3,3]-target)),distal_direction_alignment=float(np.dot(unit(pose[tip][:3,3]-pose[ip][:3,3]),across)),capsules_certify_actual_contacts=False)
 return p,evidence

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--candidate',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--native-planes',action='store_true');a.add_argument('--middle-phalanx',action='store_true');args=a.parse_args()
 if args.native_planes and args.middle_phalanx:raise ValueError('Choose one isolated model comparison')
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);m=(MiddlePhalanxWrapHelperClosure if args.middle_phalanx else NativePlaneWrapHelperClosure if args.native_planes else WrapHelperClosure)(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));best=None;variants=[]
  for strength in (.15,.5,1.5):
   p,evidence=refit(m,np.array(item['parameters']),strength);score=assess(m,p);entry=dict(parameters=p.tolist(),fit_evidence=evidence,closed=score[1],index_released=score[2],partial_crossings=score[4]);variants.append(entry)
   render_close(arm,score[3],root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+str(strength)+'.png'),'Opposed thumb with measured skin clearance '+str(strength))
   # Do not erase the wrap target to win contact counts.
   objective=score[0]+max(0.,evidence['target_error_native_model_units']-.01)*20000+max(0.,.85-evidence['distal_direction_alignment'])*500
   if best is None or objective<best[0]:best=(objective,p,entry,score[3])
   print(json.dumps(dict(side=item['side'],strength=strength,closed=score[1]['introduced_triangle_crossings'],partial=score[4],error=evidence['target_error_native_model_units'],alignment=evidence['distal_direction_alignment'])),flush=True)
  _,p,entry,pose=best;entry=dict(entry);entry.update(side=item['side'],variants=variants,authored_transform_matrices=[x.tolist()for x in pose],index_released_transform_matrices=[x.tolist()for x in m.pose(p,True)]);arms.append(entry)
 source.update(precursor=str(args.candidate),authoring_model='MiddlePhalanxWrapHelperClosure48'if args.middle_phalanx else 'NativePlaneWrapHelperClosure48'if args.native_planes else 'WrapHelperClosure48',arms=arms,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n')
if __name__=='__main__':main()
