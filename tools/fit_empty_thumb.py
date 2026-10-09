"""Offline native-rig thumb refit to the actual authored index curl and skin clearance."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
from refine_empty_web_closure import WebClosure,assess
from author_empty_contact_closure import locals_of
from author_native_hand_candidate import fk,skin
from export_native_hand_poses import verified_arm
from render_body_rig import skin_positions
from render_authored_hand_close import render_close

def unit(x):
 n=np.linalg.norm(x)
 if n<1e-8:raise ValueError('Degenerate anatomical thumb axis')
 return x/n

class ThumbClosure(WebClosure):
 partial_palm=True
 root_limit=1.2
 def posed_fingers(self,p,index_release=False):return WebClosure.pose(self,p,index_release)
 def target(self,p):
  closed=self.posed_fingers(p[:39]);bases=np.array([self.source[c[0]][:3,3]for c in self.main]);normal=unit(np.cross(bases.mean(axis=0)-self.source[self.wrist][:3,3],bases[0]-bases[-1]))
  bends=[]
  for c in self.main:bends.append(np.dot(unit(self.source[c[2]][:3,3]-self.source[c[1]][:3,3])-unit(self.source[c[1]][:3,3]-self.source[c[0]][:3,3]),normal))
  normal*=1 if np.mean(bends)>0 else -1
  return closed[self.main[0][1]][:3,3]-normal*p[44]
 def flex_axis(self,j,child,target):
  direction=unit(self.source[child][:3,3]-self.source[j][:3,3]);toward=unit(target-self.source[j][:3,3])
  return self.source[j][:3,:3].T@unit(np.cross(direction,toward))
 def pose(self,p,index_release=False):
  p=np.asarray(p)
  if len(p)!=45 or not np.isfinite(p).all()or np.any(np.abs(p[39:42])>self.root_limit)or np.any(p[42:44]<0)or np.any(p[42:44]>1.4)or not .012<=p[44]<=.04:raise ValueError('45 bounded finite thumb parameters required')
  local=locals_of(self.posed_fingers(p[:39],index_release),self.parents);target=self.target(p);thumb=[self.names.index(self.arm['side']+'_thumb_0'+str(i)+'_jnt')for i in (1,2,3)];end=self.names.index(self.arm['side']+'_thumb_end_jnt')
  for j in self.thumb:local[j]=self.native[j].copy()
  local[thumb[0]][:3,:3]=self.native[thumb[0]][:3,:3]@Rotation.from_rotvec(p[39:42]).as_matrix()
  for i,(j,child)in enumerate(zip(thumb[1:],thumb[2:]+[end])):
   axis=self.flex_axis(j,child,target)
   local[j][:3,:3]=self.native[j][:3,:3]@Rotation.from_rotvec(axis*p[42+i]).as_matrix()
  for j in self.thumb:
   name=self.names[j]
   if 'helper_thumb_'not in name:continue
   child=thumb[2]if'helper_thumb_2_'in name else thumb[1]if'helper_thumb_1_'in name or'flexmuscle'in name or'skinpalm'in name else thumb[0]
   if self.parents[j]!=self.parents[child]:raise ValueError('Thumb helper/main hierarchy changed')
   factor=0. if'_top_'in name or'1stdorsal'in name or'skinpalm'in name or'flexmuscle'in name else .2 if'_2_'in name else .16 if'tendon'in name else .1
   delta=local[child][:3,:3]@self.native[child][:3,:3].T;part=Rotation.from_rotvec(Rotation.from_matrix(delta).as_rotvec()*factor).as_matrix();local[j][:3,:3]=part@self.native[j][:3,:3]
   if'_bot_'in name:
    pivot=self.native[child][:3,3];local[j][:3,3]=pivot+part@(self.native[j][:3,3]-pivot)
   if self.partial_palm and ('skinpalm'in name or'flexmuscle'in name):
    current=fk(local,self.parents);oldroot=current[self.wrist]@self.native[thumb[0]];newroot=current[thumb[0]]
    rootdelta=newroot[:3,:3]@oldroot[:3,:3].T;portion=Rotation.from_rotvec(Rotation.from_matrix(rootdelta).as_rotvec()*.3).as_matrix()
    neutral=oldroot@self.native[j];worldposition=oldroot[:3,3]+portion@(neutral[:3,3]-oldroot[:3,3])
    local[j][:3,3]=(np.linalg.inv(current[self.parents[j]])@np.r_[worldposition,1.])[:3]
  return fk(local,self.parents)

class NativePlaneThumbClosure(ThumbClosure):
 def flex_axis(self,j,child,target):
  proximal=unit(self.source[j][:3,3]-self.source[self.parents[j]][:3,3]);distal=unit(self.source[child][:3,3]-self.source[j][:3,3])
  if np.linalg.norm(np.cross(proximal,distal))<.003:raise ValueError('Native thumb bend plane insufficiently determined')
  return self.source[j][:3,:3].T@unit(np.cross(proximal,distal))

class AnchoredThumbClosure(ThumbClosure):
 def pose(self,p,index_release=False):
  posed=super().pose(p,index_release);local=locals_of(posed,self.parents)
  for j in self.thumb:
   if 'skinpalm'in self.names[j]or'flexmuscle'in self.names[j]:
    anchor=posed[self.wrist]@np.linalg.inv(self.source[self.wrist])@self.source[j]
    local[j]=np.linalg.inv(posed[self.parents[j]])@anchor
  return fk(local,self.parents)

class LegacyThumbClosure(ThumbClosure):
 partial_palm=False

def fit(model,p):
 target=model.target(p);vertices=[]
 for v,_ in model.surfaces:
  vertices.extend([x for x in v if sum(w for w,j in zip(x[1],x[2])if'thumb'in model.names[j])>=190])
 fingers=WebClosure.pose(model,p[:39]);chain=model.main[0]+[model.names.index(model.arm['side']+'_index_end_jnt')];start=np.array([fingers[j][:3,3]for j in chain[:-1]]);ends=np.array([fingers[j][:3,3]for j in chain[1:]]);segment=ends-start
 end=model.names.index(model.arm['side']+'_thumb_end_jnt')
 index_vertices=[v for vs,_ in model.surfaces for v in vs if sum(w for w,j in zip(v[1],v[2])if 'index'in model.names[j])>=190]
 index_points=skin_positions(index_vertices,skin(model.arm,fingers));index_offset=index_points[:,None,:]-start;index_amount=np.clip(np.einsum('vki,ki->vk',index_offset,segment)/np.einsum('ki,ki->k',segment,segment),0.,1.)
 index_radius=np.linalg.norm(index_offset-index_amount[:,:,None]*segment,axis=2).min(axis=1);capsule_margin=float(np.percentile(index_radius,99))+.001
 root=model.names.index(model.arm['side']+'_thumb_01_jnt');second=model.names.index(model.arm['side']+'_thumb_02_jnt');root_axis=model.source[root][:3,:3].T@unit(model.source[second][:3,3]-model.source[root][:3,3])
 def residual(q):
  trial=p.copy();trial[39:44]=q;pose=model.pose(trial);points=skin_positions(vertices,skin(model.arm,pose));offset=points[:,None,:]-start;amount=np.clip(np.einsum('vki,ki->vk',offset,segment)/np.einsum('ki,ki->k',segment,segment),0.,1.);distance=np.linalg.norm(offset-amount[:,:,None]*segment,axis=2).min(axis=1)
  return np.r_[pose[end][:3,3]-target,q*.0015,np.dot(q[:3],root_axis)*.025,np.maximum(0.,capsule_margin-distance)*.9]
 p[39:44]=least_squares(residual,np.array([0.,0.,0.,.3,.3]),bounds=([-1.2]*3+[0.,0.],[1.2]*3+[1.4,1.4]),max_nfev=100).x
 return p,dict(measured_index_radius_p99_native_model_units=float(np.percentile(index_radius,99)),capsule_clearance_native_model_units=capsule_margin,root_axial_twist_radians=float(np.dot(p[39:42],root_axis)))

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--palm-anchor',action='store_true');parser.add_argument('--native-bend-planes',action='store_true');args=parser.parse_args()
 if args.palm_anchor and args.native_bend_planes:raise ValueError('Choose one private thumb model')
 if args.output.exists():raise ValueError('Use new output')
 source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);arms=[]
 for item,old in zip(source['arms'],baseline['arms']):
  arm=verified_arm(report,item['side']);model=(NativePlaneThumbClosure if args.native_bend_planes else AnchoredThumbClosure if args.palm_anchor else ThumbClosure)(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));previous=item['parameters'];previous=previous if len(previous)==39 else previous+[0.]*4+[previous[12],previous[13]]*4 if len(previous)==15 else previous;previous=previous+[0.]*(39-len(previous));best=None;variants=[]
  for clearance in (.024,.032,.04):
   p,fit_evidence=fit(model,np.array(previous+[0.,0.,0.,.3,.3,clearance]));score,closed,released,pose,partial=assess(model,p);end=model.names.index(item['side']+'_thumb_end_jnt');error=float(np.linalg.norm(pose[end][:3,3]-model.target(p)));entry=dict(clearance=clearance,fit_evidence=fit_evidence,target_error_native_model_units=error,closed=closed,index_released=released,partial_crossings=partial,parameters=p.tolist());variants.append(entry)
   render_close(arm,pose,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-clearance-'+str(clearance)+'.png'),'Thumb refit '+str(clearance))
   if best is None or score<best[0]:best=(score,p,entry,pose)
   print(json.dumps(dict(side=item['side'],clearance=clearance,closed=closed['introduced_triangle_crossings'],released=released['introduced_triangle_crossings'],partial=partial,error=error)),flush=True)
  _,p,entry,pose=best;entry=dict(entry);entry.update(side=item['side'],variants=variants,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in model.pose(p,True)]);arms.append(entry)
 source.update(authoring_model='ThumbClosure45_native_planes'if args.native_bend_planes else'ThumbClosure45_palm_anchor'if args.palm_anchor else'ThumbClosure45_palm_partial',arms=arms,production_activation=False,source_per_pose_carry_certified=False)
 (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
