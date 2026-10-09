"""Private thumb opposition targets the exposed posed index/middle proximal skin."""
import argparse,json
from functools import lru_cache
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from fit_empty_thumb import ThumbClosure,unit
from refine_empty_web_closure import WebClosure,assess
from author_native_hand_candidate import skin
from render_body_rig import skin_positions
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close

class WrapThumbClosure(ThumbClosure):
 root_limit=1.8
 target_segment=0
 target_fraction=(.25,.85)
 @lru_cache(maxsize=32)
 def wrap_geometry(self,base):
  base=np.asarray(base);pose=self.posed_fingers(base);palette=skin(self.arm,pose);wrist=pose[self.wrist][:3,3];bases=np.array([pose[c[0]][:3,3]for c in self.main]);palm=wrist+(bases.mean(axis=0)-wrist)*.5;points=[];front=[];normals=[]
  for d,chain in zip(('index','medium'),self.main[:2]):
   a,b=pose[chain[self.target_segment]][:3,3],pose[chain[self.target_segment+1]][:3,3];axis=unit(b-a);mid=(a+b)*.5;outward=mid-palm;outward-=axis*np.dot(outward,axis);outward=unit(outward);verts=[]
   for vertices,_ in self.surfaces:
    verts.extend([v for v in vertices if sum(w for w,j in zip(v[1],v[2])if d in self.names[j]or(d=='medium'and'middle'in self.names[j]))>=190])
   skinpoints=skin_positions(verts,palette);t=(skinpoints-a)@axis/np.linalg.norm(b-a);radial=(skinpoints-mid)@outward;near=(t>self.target_fraction[0])&(t<self.target_fraction[1])
   if near.sum()<10:raise ValueError('Insufficient target phalanx skin')
   visible=near&(radial>=np.percentile(radial[near],75));surface=np.median(skinpoints[visible],axis=0);front.append(surface);normals.append(outward);points.append(dict(digit=d,segment='MCP-PIP'if self.target_segment==0 else'PIP-DIP',vertices=int(visible.sum()),surface_point=surface.tolist(),bone_axis=axis.tolist(),outward=outward.tolist(),bone_length=float(np.linalg.norm(b-a))))
  end=self.names.index(self.arm['side']+'_thumb_end_jnt');thumbverts=[v for vs,_ in self.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if'thumb'in self.names[j])>=190];sourcepoints=skin_positions(thumbverts,skin(self.arm,self.source));dist=np.linalg.norm(sourcepoints-self.source[end][:3,3],axis=1);tipradius=float(np.median(np.sort(dist)[:10]))
  return front,normals,points,tipradius
 def target_geometry(self,p):
  front,normals,points,radius=self.wrap_geometry(tuple(p[:39]));blend=(p[44]-.012)/.028;normal=unit(normals[0]*(1-blend)+normals[1]*blend);surface=front[0]*(1-blend)+front[1]*blend;target=surface+normal*(radius+.002);across=unit(front[1]-front[0]);root=self.names.index(self.arm['side']+'_thumb_01_jnt');second=self.names.index(self.arm['side']+'_thumb_02_jnt');original=unit(self.source[second][:3,3]-self.source[root][:3,3]);toward=unit(target-self.source[root][:3,3]);angle=float(np.arccos(np.clip(np.dot(original,toward),-1,1)))
  evidence=dict(target=target.tolist(),surface_target=surface.tolist(),outward=normal.tolist(),cross_finger_axis=across.tolist(),index_middle_blend=blend,source_thumb_tip_radius_native_model_units=radius,source_metacarpal_to_target_direction_degrees=float(np.degrees(angle)),phalanx_skin=points,search_root_component_bound_radians=self.root_limit)
  if self.target_segment==0:evidence['proximal_skin']=points # Historical proximal-model report field.
  return target,across,evidence
 def target(self,p):return self.target_geometry(p)[0]

def fit_wrap(model,parameters):
 target,across,evidence=model.target_geometry(parameters);root=model.names.index(model.arm['side']+'_thumb_01_jnt');second=model.names.index(model.arm['side']+'_thumb_02_jnt');end=model.names.index(model.arm['side']+'_thumb_end_jnt');ip=model.names.index(model.arm['side']+'_thumb_03_jnt');native=unit(model.source[second][:3,3]-model.source[root][:3,3]);desired=unit(target-model.source[root][:3,3]);axis=unit(np.cross(native,desired));angle=np.arccos(np.clip(np.dot(native,desired),-1,1));seed=model.source[root][:3,:3].T@axis*angle
 def residual(q):
  p=parameters.copy();p[39:44]=q;pose=model.pose(p);tip=pose[end][:3,3];direction=unit(tip-pose[ip][:3,3]);return np.r_[tip-target,(direction-across)*.025,q*.001, np.dot(q[:3],model.source[root][:3,:3].T@native)*.004]
 result=least_squares(residual,np.r_[np.clip(seed,-1.79,1.79),.3,.5],bounds=([-1.8]*3+[0.,0.],[1.8]*3+[1.4,1.4]),max_nfev=160);parameters[39:44]=result.x;pose=model.pose(parameters);evidence.update(target_error_native_model_units=float(np.linalg.norm(pose[end][:3,3]-target)),root_rotation_norm_degrees=float(np.degrees(np.linalg.norm(result.x[:3]))),distal_direction_alignment=float(np.dot(unit(pose[end][:3,3]-pose[ip][:3,3]),across)))
 return parameters,evidence

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());report=json.loads(Path(source['source_readonly_report']).read_text());baseline=json.loads(Path(source['baseline']).read_text());args.output.mkdir(parents=True);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);model=WrapThumbClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));best=None;variants=[]
  for blend in (.4,.7,1.):
   parameters=np.r_[item['parameters'][:39],[0.,0.,0.,.3,.5,.012+.028*blend]];parameters,goal=fit_wrap(model,parameters);scored=assess(model,parameters);entry=dict(blend=blend,target_evidence=goal,closed=scored[1],index_released=scored[2],partial_crossings=scored[4],parameters=parameters.tolist());variants.append(entry);render_close(arm,scored[3],root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-wrap-'+str(blend)+'.png'),'Across posed finger skin '+str(blend))
   # Prefer wrap-target accuracy/direction over contact count alone; every failure remains explicit.
   objective=goal['target_error_native_model_units']*1000+(1-goal['distal_direction_alignment'])*20+scored[0]*.1
   if best is None or objective<best[0]:best=(objective,parameters,entry,scored[3])
   print(json.dumps(dict(side=item['side'],blend=blend,closed=scored[1]['introduced_triangle_crossings'],partial=scored[4],target_error=goal['target_error_native_model_units'],source_metacarpal_to_target_degrees=goal['source_metacarpal_to_target_direction_degrees'],root=goal['root_rotation_norm_degrees'],alignment=goal['distal_direction_alignment'])),flush=True)
  _,parameters,entry,pose=best;entry=dict(entry);entry.update(side=item['side'],variants=variants,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in model.pose(parameters,True)]);arms.append(entry)
 source.update(authoring_model='WrapThumbClosure45',precursor=str(args.candidate),arms=arms,production_activation=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
