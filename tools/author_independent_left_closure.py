"""Private left Empty-source closure with source skin targets and sampled area guards.
No right-hand pose mirroring, game access, native-fist certification or activation.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
from author_empty_contact_closure import Closure,interpolate_local
from author_native_hand_candidate import matrices,fk,skin
from export_native_hand_poses import verified_arm
from read_occupied_source_evidence import occupied_source_evidence
from render_body_rig import skin_positions
from render_authored_hand_close import render_close
from check_authored_hand_contacts import evaluate
from compare_empty_hand_contact_baselines import strain
from fit_empty_thumb import unit

class IndependentLeftClosure(Closure):
 limits=np.tile([1.4,1.6,.7],4)
 def __init__(self,arm,asset,use_digit_plane=False,metacarpal_palm=False,plane_palm=False,area_weight=3000.,maximum_pip=1.6,maximum_dip=.7,volume_gain=0.):
  if arm['side']!='left':raise ValueError('Independent left source required')
  if not 1.0<=maximum_pip<=1.9 or not .3<=maximum_dip<=1.2:raise ValueError('Bounded offline joint search required')
  if not np.isfinite(volume_gain)or not 0<=volume_gain<=1:raise ValueError('Bounded authored helper compensation required')
  self.limits=np.tile([1.4,maximum_pip,maximum_dip],4);self.volume_gain=volume_gain
  super().__init__(arm,matrices(arm),asset);self.axes={};self.ends=[];bases=[];self.area_weight=area_weight
  for chain in self.main:
   end=next(i for i,n in enumerate(self.names)if n==self.names[chain[0]].replace('_02_jnt','_end_jnt'));self.ends.append(end);bases.append(self.source[chain[0]][:3,3]);fallback=None
   for j,child in zip(chain,chain[1:]+[end]):
    previous=unit(self.source[j][:3,3]-self.source[self.parents[j]][:3,3]);following=unit(self.source[child][:3,3]-self.source[j][:3,3]);axis=np.cross(previous,following)
    if np.linalg.norm(axis)>.01:fallback=unit(axis)
    if fallback is None:raise ValueError('Native finger plane underdetermined')
    self.axes[j]=self.source[j][:3,:3].T@(unit(axis)if np.linalg.norm(axis)>.01 else fallback)
   if use_digit_plane:
    # Metacarpal splay contaminates the MCP cross-product. The same digit's
    # native PIP curvature supplies an independently measured flex-plane normal.
    j=chain[1];axis=self.source[j][:3,:3]@self.axes[j]
    for member in chain:self.axes[member]=self.source[member][:3,:3].T@axis
  bases=np.array(bases);wrist=self.source[self.wrist][:3,3];forward=unit(bases.mean(axis=0)-wrist);width=unit(bases[0]-bases[-1]);normal=unit(np.cross(width,forward));bend=np.mean([unit(self.source[c[1]][:3,3]-self.source[c[0]][:3,3])-unit(self.source[c[0]][:3,3]-self.source[self.parents[c[0]]][:3,3])for c in self.main],axis=0)
  if np.dot(normal,bend)<0:normal=-normal
  def palm_joint(name):return not any(d in name for d in ('thumb','index','medium','middle','ring','pinky'))or(metacarpal_palm and name.endswith('_01_jnt')and any(d in name for d in ('index','medium','ring','pinky')))
  palette=skin(self.arm,self.source);vertices=[v for vs,_ in self.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if palm_joint(self.names[j]))>=190];points=skin_positions(vertices,palette);distance=(points-wrist)@forward;span=np.linalg.norm(bases.mean(axis=0)-wrist);cohort=(distance>span*.2)&(distance<span*.9)
  if cohort.sum()<10:raise ValueError('Native palm skin cohort missing')
  face=cohort&(((points-wrist)@normal)>=np.percentile(((points-wrist)@normal)[cohort],65));points=points[face];self.targets=[];self.target_evidence=[]
  for digit,base,end,chain in zip(('index','medium','ring','pinky'),bases,self.ends,self.main):
   desired=base-forward*span*.4;verts=[v for vs,_ in self.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if digit in self.names[j]or(digit=='medium'and'middle'in self.names[j]))>=190];pad=skin_positions(verts,palette);radius=float(np.median(np.sort(np.linalg.norm(pad-self.source[end][:3,3],axis=1))[:10]));axis=self.source[chain[0]][:3,:3]@self.axes[chain[0]];plane_offset=np.dot(self.source[end][:3,3]-base,axis);target_points=points+normal*(radius+.0015);plane_error=np.abs((target_points-base)@axis-plane_offset);score=np.linalg.norm(points-desired,axis=1)+(plane_error*20 if plane_palm else 0);chosen=np.argmin(score);point=points[chosen];target=target_points[chosen];self.targets.append(target);self.target_evidence.append(dict(digit=digit,source_palm_surface=point.tolist(),target=target.tolist(),source_tip_skin_radius_native_model_units=radius,target_distance_from_source_digit_plane_native_model_units=float(plane_error[chosen])))
  self.targets=np.array(self.targets);self.old_area=np.linalg.norm(np.cross(self.before[:,1]-self.before[:,0],self.before[:,2]-self.before[:,0]),axis=1)
 def pose(self,p):
  p=np.asarray(p)
  if len(p)!=12 or not np.isfinite(p).all()or np.any(p<0)or np.any(p>self.limits):raise ValueError('12 bounded finite independent curl parameters required')
  local=[x.copy()for x in self.native]
  for k,j in enumerate(sum(self.main,[])):local[j][:3,:3]=self.native[j][:3,:3]@Rotation.from_rotvec(self.axes[j]*p[k]).as_matrix()
  for j in self.finger_helpers:
   name=self.names[j];digit=next(d for d in ('index','middle','ring','pinky')if d in name);d=('index','middle','ring','pinky').index(digit);segment=2 if'_2_'in name else 1 if'_1_'in name else 0;child=self.main[d][segment]
   if self.parents[j]!=self.parents[child]:raise ValueError('Native helper/main sibling changed')
   delta=local[child][:3,:3]@self.native[child][:3,:3].T;rotation=Rotation.from_matrix(delta).as_rotvec();part=Rotation.from_rotvec(rotation*.5).as_matrix();local[j][:3,:3]=part@self.native[j][:3,:3];pivot=self.native[child][:3,3];offset=self.native[j][:3,3]-pivot;theta=np.linalg.norm(rotation)
   if theta>1e-8 and self.volume_gain:
    axis=rotation/theta;parallel=axis*np.dot(offset,axis);radial=offset-parallel;factor=1+self.volume_gain*(1/max(.5,np.cos(theta*.5))-1);offset=parallel+radial*factor
   local[j][:3,3]=pivot+part@offset
  return fk(local,self.parents),local
 def quality(self,p):
  pose,local=self.pose(p);error=np.linalg.norm(np.array([pose[j][:3,3]for j in self.ends])-self.targets,axis=1);deficits=[];minimum=[]
  for amount in (.25,.5,.75,1.):
   stage=fk([interpolate_local(x,y,amount)for x,y in zip(self.native,local)],self.parents);after=np.concatenate([skin_positions(v,skin(self.arm,stage))[t]for v,t in self.surfaces]);area=np.linalg.norm(np.cross(after[:,1]-after[:,0],after[:,2]-after[:,0]),axis=1);ratio=area[self.old_area>1e-12]/self.old_area[self.old_area>1e-12];deficits.extend(np.maximum(0.,.15-ratio));minimum.append(float(ratio.min()))
  return float(error.sum()*10000+sum(deficits)*self.area_weight),error,minimum

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--source',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--digit-plane',action='store_true');a.add_argument('--metacarpal-palm',action='store_true');a.add_argument('--plane-palm',action='store_true');a.add_argument('--area-weight',type=float,default=3000.);a.add_argument('--maximum-pip',type=float,default=1.6);a.add_argument('--maximum-dip',type=float,default=.7);a.add_argument('--volume-gain',type=float,default=0.);args=a.parse_args()
 if not np.isfinite(args.area_weight)or not 1000<=args.area_weight<=100000:raise ValueError('Bounded positive area penalty required')
 if args.output.exists():raise ValueError('Use new output')
 report=json.loads(args.source.read_text());qualification=occupied_source_evidence(args.source,args.source.parent/'run-provenance.json');arm=verified_arm(report,'left');root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';m=IndependentLeftClosure(arm,root/'left-arm.bmd6model',args.digit_plane,args.metacarpal_palm,args.plane_palm,args.area_weight,args.maximum_pip,args.maximum_dip,args.volume_gain);args.output.mkdir(parents=True)
 def residual(p):
  pose,_=m.pose(p);return np.r_[(np.array([pose[j][:3,3]for j in m.ends])-m.targets).ravel(),p*.0005]
 p=least_squares(residual,np.tile([.9,.9,.4],4),bounds=(np.zeros(12),m.limits),max_nfev=100).x;best=m.quality(p);search=[]
 for step in (.15,.075):
  for k in range(12):
   seed=p.copy()
   for delta in (-step,step):
    trial=seed.copy();trial[k]+=delta
    try:result=m.quality(trial)
    except ValueError:continue
    if result[0]<best[0]:p=trial;best=result
  search.append(dict(step=step,target_errors_native_model_units=best[1].tolist(),sampled_minimum_area_ratios=best[2]));print(json.dumps(search[-1]),flush=True)
 pose,local=m.pose(p);stages=[]
 for amount in (0.,.25,.5,.75,1.):
  stage=fk([interpolate_local(x,y,amount)for x,y in zip(m.native,local)],m.parents);after=np.concatenate([skin_positions(v,skin(arm,stage))[t]for v,t in m.surfaces]);stages.append(dict(amount=amount,contacts=evaluate(m.before,after,m.ids),strain=strain(m.before,after)))
 render_close(arm,pose,root/'left-arm.bmd6model',args.output/'left-independent-curl.png','Independent left Empty source: sampled-area curl constraint; authored')
 result=dict(source=str(args.source.resolve()),source_qualification=qualification,left_native_source_decl='Empty',right_source_decl='Sword (occupied reference only)',right_rotations_used=False,same_digit_PIP_plane=args.digit_plane,metacarpal_root_skin_counts_as_palm=args.metacarpal_palm,targets_choose_same_digit_plane=args.plane_palm,area_penalty_weight=args.area_weight,authored_helper_volume_gain=args.volume_gain,helper_compensation_is_analytic_proxy=True,offline_search_limits=m.limits.tolist(),limits_are_anatomically_certified=False,parameters=p.tolist(),source_plane_axes={m.names[j]:v.tolist()for j,v in m.axes.items()},skin_targets=m.target_evidence,search=search,sampled_area_constraint_passed=min(best[2])>=.1,authored_transform_matrices=[x.tolist()for x in pose],stages=stages,production_activation=False,anatomical_acceptance=False,native_Empty_closed_captured=False)
 (args.output/'authored-independent-left.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(crossings=[s['contacts']['introduced_triangle_crossings']for s in stages],collapse=[s['strain']['triangles_below_one_tenth_source_area']for s in stages],minimum_area=best[2])))
if __name__=='__main__':main()
